#include <sqlitex/sqlitex.h>

#include "test_helpers.h"

#include <gtest/gtest.h>

#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <vector>

namespace sqlitex {
namespace {

// ============================================================================
// Flags<E> bitmask safety and operations
// ============================================================================

template <typename A, typename B>
concept CanBitwiseOr = requires(A a, B b) { a | b; };

TEST(Cpp23FlagsTest, OperationsAndCrossEnumSafety) {
    // Distinct flag enums cannot be mixed or constructed from raw int.
    static_assert(!std::is_convertible_v<int, OpenFlags>);
    static_assert(!std::is_convertible_v<PrepareFlag, OpenFlags>);
    static_assert(!CanBitwiseOr<OpenFlag, PrepareFlag>);
    static_assert(CanBitwiseOr<OpenFlag, OpenFlag>);
    static_assert(CanBitwiseOr<OpenFlag, OpenFlags>);

    OpenFlags flags = OpenFlag::ReadWrite | OpenFlag::Create;
    EXPECT_FALSE(flags.empty());
    EXPECT_TRUE(static_cast<bool>(flags));
    EXPECT_TRUE(flags.contains(OpenFlag::ReadWrite));
    EXPECT_TRUE(flags.contains(OpenFlag::Create));
    EXPECT_FALSE(flags.contains(OpenFlag::ReadOnly));
    EXPECT_TRUE(flags.any(OpenFlag::ReadOnly | OpenFlag::Create));
    EXPECT_FALSE(flags.any(OpenFlag::ReadOnly | OpenFlag::Uri));

    flags |= OpenFlag::Uri;
    EXPECT_TRUE(flags.contains(OpenFlag::Uri));

    flags ^= OpenFlag::Create;
    EXPECT_FALSE(flags.contains(OpenFlag::Create));

    flags &= OpenFlag::ReadWrite;
    EXPECT_EQ(flags, OpenFlags(OpenFlag::ReadWrite));
    EXPECT_EQ(OpenFlags::from_raw(flags.raw()), flags);

    OpenFlags empty_flags;
    EXPECT_TRUE(empty_flags.empty());
    EXPECT_FALSE(static_cast<bool>(empty_flags));
}

// ============================================================================
// C++20/23 concept constraints
// ============================================================================

TEST(Cpp23ConceptsTest, CompileTimeConceptContracts) {
    // SqlInteger rejects bool and character types to avoid silent misuse.
    static_assert(SqlInteger<int16_t>);
    static_assert(SqlInteger<int32_t>);
    static_assert(SqlInteger<int64_t>);
    static_assert(SqlInteger<uint32_t>);
    static_assert(!SqlInteger<bool>);
    static_assert(!SqlInteger<char>);
    static_assert(!SqlInteger<signed char>);
    static_assert(!SqlInteger<unsigned char>);
    static_assert(!SqlInteger<double>);

    // ConstByteRange accepts byte/uint8_t contiguous ranges, rejects strings.
    static_assert(ConstByteRange<std::vector<std::byte>>);
    static_assert(ConstByteRange<std::vector<uint8_t>>);
    static_assert(ConstByteRange<std::span<const std::byte>>);
    static_assert(!ConstByteRange<std::string>);
    static_assert(!ConstByteRange<std::string_view>);

    // OwnedColumnValue prevents dangling views from Database::query_value.
    static_assert(OwnedColumnValue<int64_t>);
    static_assert(OwnedColumnValue<std::string>);
    static_assert(OwnedColumnValue<std::vector<std::byte>>);
    static_assert(OwnedColumnValue<std::optional<std::string>>);
    static_assert(!OwnedColumnValue<std::string_view>);
    static_assert(!OwnedColumnValue<std::span<const std::byte>>);
    static_assert(!OwnedColumnValue<std::optional<std::string_view>>);

    // ColumnValue allows views during live Statement iteration.
    static_assert(ColumnValue<std::string_view>);
    static_assert(ColumnValue<std::span<const std::byte>>);
    static_assert(ColumnValue<std::optional<std::string_view>>);

    // Bindable accepts SQL scalar/text/byte/optional types, rejects raw ptrs.
    static_assert(Bindable<std::nullptr_t>);
    static_assert(Bindable<std::nullopt_t>);
    static_assert(Bindable<bool>);
    static_assert(Bindable<int64_t>);
    static_assert(Bindable<double>);
    static_assert(Bindable<std::string_view>);
    static_assert(Bindable<std::vector<std::byte>>);
    static_assert(Bindable<std::optional<std::string>>);
    static_assert(!Bindable<char>);
    static_assert(!Bindable<void*>);

    SUCCEED();
}

// ============================================================================
// Ranges: Statement::rows() & Statement::rows<Ts...>()
// ============================================================================

TEST(Cpp23RangesTest, RowRangeAndTypedRowRangeWithViews) {
    static_assert(std::ranges::input_range<RowRange>);
    static_assert(std::ranges::view<RowRange>);
    static_assert(
        std::ranges::input_range<TypedRowRange<int64_t, std::string>>);
    static_assert(std::ranges::view<TypedRowRange<int64_t, std::string>>);

    Database db(":memory:");
    db.execute(
        "CREATE TABLE items (id INTEGER PRIMARY KEY, name TEXT, score REAL)");
    db.execute("INSERT INTO items VALUES (1, 'alpha', 1.5)");
    db.execute("INSERT INTO items VALUES (2, 'beta', NULL)");
    db.execute("INSERT INTO items VALUES (3, 'gamma', 3.5)");

    // Untyped RowView range
    auto stmt1 = db.prepare("SELECT id, name FROM items ORDER BY id");
    std::vector<std::pair<int64_t, std::string>> seen;
    for (RowView row : stmt1.rows()) {
        seen.emplace_back(row.get<int64_t>(0),
                          std::string(row.get<std::string_view>(1)));
    }
    ASSERT_EQ(seen.size(), 3u);
    EXPECT_EQ(seen[0].second, "alpha");
    EXPECT_EQ(seen[2].second, "gamma");

    // Typed multi-column range with std::views::filter + transform + ranges::to
    auto stmt2 = db.prepare("SELECT id, name, score FROM items ORDER BY id");
    auto scored_names =
        stmt2.rows<int64_t, std::string, std::optional<double>>() |
        std::views::filter([](const auto& tup) {
            return std::get<2>(tup).has_value() && *std::get<2>(tup) > 2.0;
        }) |
        std::views::transform(
            [](const auto& tup) { return std::get<1>(tup); }) |
        std::ranges::to<std::vector>();
    ASSERT_EQ(scored_names.size(), 1u);
    EXPECT_EQ(scored_names[0], "gamma");

    // Single-column typed range yields T directly (not std::tuple<T>)
    auto stmt3 = db.prepare("SELECT name FROM items ORDER BY id");
    auto all_names = stmt3.rows<std::string>() | std::ranges::to<std::vector>();
    ASSERT_EQ(all_names.size(), 3u);
    EXPECT_EQ(all_names[1], "beta");
}

// ============================================================================
// std::expected non-throwing APIs & honest NULL handling in query_value
// ============================================================================

TEST(Cpp23ExpectedTest, TryOpenTryPrepareTryStepAndQueryValueNull) {
    // Failure path: nonexistent parent directory returns CantOpen without throw
    auto bad_db =
        Database::try_open("/nonexistent_dir_sqlitex_cpp23/db.sqlite");
    ASSERT_FALSE(bad_db.has_value());
    EXPECT_EQ(bad_db.error().error_code(), ErrorCode::CantOpen);

    // Success path
    auto maybe_db = Database::try_open(":memory:");
    ASSERT_TRUE(maybe_db.has_value());
    Database db = std::move(*maybe_db);

    db.execute("CREATE TABLE t (id INTEGER PRIMARY KEY, val INTEGER)");
    db.execute("INSERT INTO t VALUES (1, NULL)");

    // Invalid SQL via try_prepare
    auto bad_stmt = db.try_prepare("SELECT * FROM no_such_table");
    ASSERT_FALSE(bad_stmt.has_value());
    EXPECT_EQ(bad_stmt.error().error_code(), ErrorCode::Error);

    // Constraint violation via try_step (no exception thrown)
    auto ins = db.try_prepare("INSERT INTO t VALUES (1, 99)");
    ASSERT_TRUE(ins.has_value());
    auto step_res = ins->try_step();
    ASSERT_FALSE(step_res.has_value());
    EXPECT_EQ(step_res.error().error_code(), ErrorCode::Constraint);

    // Normal iteration via try_step
    auto sel = db.try_prepare("SELECT id FROM t");
    ASSERT_TRUE(sel.has_value());
    auto r1 = sel->try_step();
    ASSERT_TRUE(r1.has_value());
    EXPECT_TRUE(*r1);
    auto r2 = sel->try_step();
    ASSERT_TRUE(r2.has_value());
    EXPECT_FALSE(*r2);

    // query_value returns nullopt on SQL NULL instead of fabricating 0
    auto null_int = db.query_value<int64_t>("SELECT val FROM t WHERE id = 1");
    EXPECT_FALSE(null_int.has_value());

    // query_value<Value> distinguishes a NULL column from no matching row
    auto null_val = db.query_value<Value>("SELECT val FROM t WHERE id = 1");
    ASSERT_TRUE(null_val.has_value());
    EXPECT_TRUE(null_val->is_null());

    auto missing_row =
        db.query_value<Value>("SELECT val FROM t WHERE id = 999");
    EXPECT_FALSE(missing_row.has_value());
}

// ============================================================================
// RAII Transaction & Savepoint rollback-on-drop + std::filesystem::path
// ============================================================================

TEST(Cpp23RaiiTest, TransactionSavepointRollbackOnDropAndFilesystemPath) {
    test::TempFile tmp_file;
    std::filesystem::path db_path(tmp_file.path());

    {
        Database db(db_path);
        db.set_busy_timeout(std::chrono::milliseconds{250});
        db.execute("CREATE TABLE log (id INTEGER PRIMARY KEY, msg TEXT)");

        // Uncommitted transaction rolls back on scope exit
        {
            Database::Transaction tx(db);
            db.execute("INSERT INTO log VALUES (1, 'dropped')");
        }
        EXPECT_EQ(db.query_value<int64_t>("SELECT COUNT(*) FROM log"), 0);

        // Committed transaction with an uncommitted nested savepoint
        {
            Database::Transaction tx(db);
            db.execute("INSERT INTO log VALUES (2, 'kept')");
            {
                Database::Savepoint sp(db, "inner");
                db.execute("INSERT INTO log VALUES (3, 'sp_dropped')");
            }
            {
                Database::Savepoint sp(db, "inner_ok");
                db.execute("INSERT INTO log VALUES (4, 'sp_released')");
                sp.release();
            }
            tx.commit();
        }
    }

    // Reopen via Database::try_open(std::filesystem::path)
    auto reopened = Database::try_open(db_path, OpenFlag::ReadOnly);
    ASSERT_TRUE(reopened.has_value());
    auto ids =
        reopened->prepare("SELECT id FROM log ORDER BY id").rows<int64_t>() |
        std::ranges::to<std::vector>();
    ASSERT_EQ(ids.size(), 2u);
    EXPECT_EQ(ids[0], 2);
    EXPECT_EQ(ids[1], 4);
}

// ============================================================================
// Typed unique_ptr ownership helpers across SQLite boundary
// ============================================================================

struct TrackedPayload {
    int value = 0;
    int* destroy_count = nullptr;

    TrackedPayload(int v, int* counter) : value(v), destroy_count(counter) {
    }
    ~TrackedPayload() {
        if (destroy_count != nullptr) {
            ++(*destroy_count);
        }
    }
};

TEST(Cpp23OwnershipTest, BindPointerAuxDataClientDataFts5AndRTree) {
    int bind_destroyed = 0;
    int aux_destroyed = 0;
    int client_destroyed = 0;
    int fts5_destroyed = 0;
    int rtree_destroyed = 0;

    {
        Database db(":memory:");

        // 1. Database::set_clientdata(std::unique_ptr<T>)
        db.set_clientdata(
            "cfg", std::make_unique<TrackedPayload>(77, &client_destroyed));
        auto* cfg = db.get_clientdata_as<TrackedPayload>("cfg");
        ASSERT_NE(cfg, nullptr);
        EXPECT_EQ(cfg->value, 77);
        EXPECT_EQ(client_destroyed, 0);

        // 2. Statement::bind_pointer(std::unique_ptr<T>) + ValueRef::pointer_as
        db.create_raw_function(
            "read_ptr", 1,
            [](FunctionContext& ctx, std::span<const ValueRef> args) {
                auto* p = args[0].pointer_as<TrackedPayload>("tracked");
                ASSERT_NE(p, nullptr);
                ctx.set_result(static_cast<int64_t>(p->value));
            });

        {
            auto stmt = db.prepare("SELECT read_ptr(?)");
            stmt.bind_pointer(
                1, std::make_unique<TrackedPayload>(123, &bind_destroyed),
                "tracked");
            ASSERT_TRUE(stmt.step());
            EXPECT_EQ(stmt.column_int64(0), 123);
        }
        EXPECT_EQ(bind_destroyed, 1);

        // 3. FunctionContext::set_auxdata(std::unique_ptr<T>)
        db.create_raw_function(
            "cached_aux", 1,
            [&](FunctionContext& ctx, std::span<const ValueRef>) {
                auto* cached = ctx.get_auxdata_as<TrackedPayload>(0);
                if (cached == nullptr) {
                    auto ptr =
                        std::make_unique<TrackedPayload>(42, &aux_destroyed);
                    cached = ptr.get();
                    ctx.set_auxdata(0, std::move(ptr));
                }
                ctx.set_result(static_cast<int64_t>(cached->value));
            });
        {
            auto stmt = db.prepare("SELECT cached_aux('const_arg')");
            ASSERT_TRUE(stmt.step());
            EXPECT_EQ(stmt.column_int64(0), 42);
        }
        EXPECT_EQ(aux_destroyed, 1);

        // 4. Fts5ExtensionContext::set_auxdata / get_auxdata_as /
        // take_auxdata_as
        db.execute("CREATE VIRTUAL TABLE docs USING fts5(body)");
        db.execute("INSERT INTO docs VALUES ('hello world')");
        db.execute("INSERT INTO docs VALUES ('hello cpp23')");

        auto fts = Fts5Api::from_database(db);
        fts.create_function("aux_check", [&](const Fts5ExtensionContext& f5,
                                             FunctionContext& sql_ctx,
                                             std::span<const ValueRef>) {
            auto* p = f5.get_auxdata_as<TrackedPayload>();
            if (p == nullptr) {
                f5.set_auxdata(
                    std::make_unique<TrackedPayload>(1, &fts5_destroyed));
                p = f5.get_auxdata_as<TrackedPayload>();
            } else {
                auto taken = f5.take_auxdata_as<TrackedPayload>();
                ASSERT_NE(taken, nullptr);
                taken->value += 10;
                sql_ctx.set_result(static_cast<int64_t>(taken->value));
                return;
            }
            ASSERT_NE(p, nullptr);
            sql_ctx.set_result(static_cast<int64_t>(p->value));
        });

        auto fts_vals =
            db.prepare(
                  "SELECT aux_check(docs) FROM docs WHERE docs MATCH 'hello' "
                  "ORDER BY rowid")
                .rows<int64_t>() |
            std::ranges::to<std::vector>();
        ASSERT_EQ(fts_vals.size(), 2u);
        EXPECT_EQ(fts_vals[0], 1);
        EXPECT_EQ(fts_vals[1], 11);
        // Transferred via take_auxdata_as -> destroyed once, never double-freed
        EXPECT_EQ(fts5_destroyed, 1);

        // 5. RTreeQueryInfo::set_user_data(std::unique_ptr<T>)
        db.execute(
            "CREATE VIRTUAL TABLE boxes USING rtree(id, minX, maxX, minY, "
            "maxY)");
        db.execute("INSERT INTO boxes VALUES (1, 0, 10, 0, 10)");

        register_rtree_query_callback(db, "tracked_q", [&](RTreeQueryInfo& qi) {
            if (qi.user_data_as<TrackedPayload>() == nullptr) {
                qi.set_user_data(
                    std::make_unique<TrackedPayload>(5, &rtree_destroyed));
            }
            EXPECT_EQ(qi.user_data_as<TrackedPayload>()->value, 5);
            qi.set_within(RTreeWithin::FullyWithin);
            qi.set_score(1.0);
        });

        auto box_cnt = db.query_value<int64_t>(
            "SELECT COUNT(*) FROM boxes WHERE id MATCH tracked_q(1.0)");
        EXPECT_EQ(box_cnt, 1);
        EXPECT_EQ(rtree_destroyed, 1);
    }

    // Client data destroyed cleanly on Database destruction
    EXPECT_EQ(client_destroyed, 1);
}

// ============================================================================
// Snapshot three-way comparison (<=>)
// ============================================================================

TEST(Cpp23SnapshotTest, ThreeWayComparison) {
    test::TempFile db_file;
    Database db(db_file.path());
    db.execute("PRAGMA journal_mode=WAL");
    db.execute("CREATE TABLE t (id INTEGER PRIMARY KEY)");
    db.execute("INSERT INTO t VALUES (1)");

    db.execute("BEGIN");
    (void)db.query_value<int64_t>("SELECT COUNT(*) FROM t");
    Snapshot s1 = Snapshot::get(db);
    db.execute("COMMIT");

    db.execute("INSERT INTO t VALUES (2)");

    db.execute("BEGIN");
    (void)db.query_value<int64_t>("SELECT COUNT(*) FROM t");
    Snapshot s2 = Snapshot::get(db);
    db.execute("COMMIT");

    EXPECT_TRUE(s1 < s2);
    EXPECT_TRUE(s2 > s1);
    EXPECT_TRUE(s1 == s1);
    EXPECT_TRUE(s1 != s2);
    EXPECT_EQ(s1 <=> s2, std::strong_ordering::less);
}

// ============================================================================
// std::format specializations
// ============================================================================

TEST(Cpp23FormatTest, FormatEnumsAndValue) {
    EXPECT_EQ(std::format("{}", ErrorCode::Constraint), "Constraint");
    EXPECT_EQ(std::format("{}", Value::Type::Blob), "BLOB");
    EXPECT_EQ(std::format("{}", ChangeOperation::Update), "UPDATE");
    EXPECT_EQ(std::format("{}", TxnState::Write), "Write");
    EXPECT_EQ(std::format("{}", ThreadingMode::Serialized), "Serialized");
    EXPECT_EQ(std::format("{}", BackupStepResult::Done), "Done");

    EXPECT_EQ(std::format("{}", Value(nullptr)), "NULL");
    EXPECT_EQ(std::format("{}", Value(int64_t{42})), "42");
    EXPECT_EQ(std::format("{}", Value("hello")), "hello");
    std::vector<std::byte> blob(3, std::byte{0xAB});
    EXPECT_EQ(std::format("{}", Value(blob)), "<blob 3 bytes>");
}

// ============================================================================
// Session / Changeset / Changegroup C++23 features
// ============================================================================

#ifdef SQLITEX_HAS_SESSION

TEST(Cpp23SessionTest, ChangesetIteratorRangeForAndChangegroupBuilder) {
    static_assert(std::ranges::input_range<ChangesetIterator>);

    Database db(":memory:");
    db.execute("CREATE TABLE items (id INTEGER PRIMARY KEY, name TEXT)");

    Session session(db, "main");
    session.enable(true);
    session.attach_all();
    db.execute("INSERT INTO items VALUES (1, 'one')");
    db.execute("INSERT INTO items VALUES (2, 'two')");

    Changeset cs = session.changeset();
    std::vector<int64_t> ids;
    for (const auto& ch : cs.changes()) {
        EXPECT_EQ(ch.op(), ChangeOperation::Insert);
        auto id = ch.new_int(0);
        ASSERT_TRUE(id.has_value());
        ids.push_back(*id);
    }
    ASSERT_EQ(ids.size(), 2u);
    EXPECT_EQ(ids[0], 1);
    EXPECT_EQ(ids[1], 2);

    // Changegroup::ChangeBuilder RAII commit vs auto-discard on drop
    Changegroup cg;
    cg.set_schema(db, "main");
    {
        auto b = cg.begin_change(ChangeOperation::Insert, "items");
        b.set(ChangeRow::New, 0, 10).set(ChangeRow::New, 1, "committed");
        b.commit();
    }
    {
        auto b = cg.begin_change(ChangeOperation::Insert, "items");
        b.set(ChangeRow::New, 0, 99).set(ChangeRow::New, 1, "auto_discarded");
        // No commit() -> destructor discards cleanly
    }

    Changeset built = cg.output();
    built.apply(db, [](ConflictType, const ChangesetIterator&) {
        return ChangesetAction::Abort;
    });

    EXPECT_EQ(db.query_value<std::string>("SELECT name FROM items WHERE id=10"),
              "committed");
    EXPECT_FALSE(
        db.query_value<std::string>("SELECT name FROM items WHERE id=99")
            .has_value());
}

#endif // SQLITEX_HAS_SESSION

// ============================================================================
// Bcvfs std::filesystem::path overload
// ============================================================================

#ifdef SQLITEX_HAS_BCVFS

TEST(Cpp23BcvfsTest, CreateWithFilesystemPath) {
    test::TempFile base;
    std::filesystem::path cache_dir = base.path() + "_bcvfs_dir";
    std::filesystem::create_directories(cache_dir);
    struct DirCleanup {
        std::filesystem::path p;
        ~DirCleanup() {
            std::error_code ec;
            std::filesystem::remove_all(p, ec);
        }
    } cleanup{cache_dir};

    Bcvfs vfs(cache_dir, "cpp23_path_vfs");
    ASSERT_TRUE(static_cast<bool>(vfs));
    EXPECT_EQ(vfs.vfs_name(), "cpp23_path_vfs");
}

#endif // SQLITEX_HAS_BCVFS

} // namespace
} // namespace sqlitex
