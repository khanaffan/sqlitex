#include <sqlitex/sqlitex.h>

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace sqlitex {
namespace {

// ============================================================================
// Statement: carray, zeroblob, pointer, metadata, normalized_sql, explain,
//            stmt_status, scanstatus, prepare_v3
// ============================================================================

TEST(StatementAdvancedTest, CArrayBindAllTypes) {
    Database db(":memory:");

    // Int32
    {
        std::vector<int32_t> vals{10, 20, 30};
        auto stmt = db.prepare("SELECT SUM(value) FROM carray(?1);");
        stmt.bind_carray(1, vals);
        ASSERT_TRUE(stmt.step());
        EXPECT_EQ(stmt.column_int64(0), 60);
    }

    // Int64
    {
        std::vector<int64_t> vals{10000000000LL, 20000000000LL};
        auto stmt = db.prepare("SELECT SUM(value) FROM carray(?1);");
        stmt.bind_carray(1, vals);
        ASSERT_TRUE(stmt.step());
        EXPECT_EQ(stmt.column_int64(0), 30000000000LL);
    }

    // Double
    {
        std::vector<double> vals{1.5, 2.5, 3.0};
        auto stmt = db.prepare("SELECT SUM(value) FROM carray(?1);");
        stmt.bind_carray(1, vals);
        ASSERT_TRUE(stmt.step());
        EXPECT_DOUBLE_EQ(stmt.column_double(0), 7.0);
    }

    // Text
    {
        std::vector<std::string> vals{"alpha", "beta", "gamma"};
        auto stmt =
            db.prepare("SELECT group_concat(value, ',') FROM carray(?1);");
        stmt.bind_carray(1, vals);
        ASSERT_TRUE(stmt.step());
        EXPECT_EQ(stmt.column_text(0), "alpha,beta,gamma");
    }
}

TEST(StatementAdvancedTest, ZeroblobStaticBind64AndClearBindings) {
    Database db(":memory:");

    auto stmt = db.prepare("SELECT length(?1), ?2, length(?3);");
    static const char kStaticText[] = "static-val";
    const uint8_t blob_bytes[] = {1, 2, 3, 4, 5};
    stmt.bind_zeroblob(1, 16)
        .bind_static(2, std::string_view(kStaticText))
        .bind_blob64(3, std::as_bytes(std::span{blob_bytes}));
    ASSERT_TRUE(stmt.step());
    EXPECT_EQ(stmt.data_count(), 3);
    EXPECT_EQ(stmt.column_int(0), 16);
    EXPECT_EQ(stmt.column_text(1), "static-val");
    EXPECT_EQ(stmt.column_int(2), 5);

    stmt.reset();
    stmt.clear_bindings();
    ASSERT_TRUE(stmt.step());
    EXPECT_TRUE(stmt.column_is_null(0));
    EXPECT_TRUE(stmt.column_is_null(1));
    EXPECT_TRUE(stmt.column_is_null(2));
}

TEST(StatementAdvancedTest, ColumnOriginMetadataAndNormalizedSql) {
    Database db(":memory:");
    db.execute("CREATE TABLE users(uid INTEGER PRIMARY KEY, uname TEXT);");
    db.execute("INSERT INTO users VALUES (1, 'Alice');");

    std::string_view tail;
    auto stmt = db.prepare_v3(
        "SELECT uid, uname AS alias_name FROM users WHERE uid = ?1; SELECT 2;",
        PrepareFlag::Persistent, &tail);
    EXPECT_EQ(tail, " SELECT 2;");

    EXPECT_TRUE(stmt.is_readonly());
    EXPECT_FALSE(stmt.is_busy());
    EXPECT_FALSE(stmt.is_explain());
    EXPECT_EQ(stmt.explain_mode(), ExplainMode::Normal);

    stmt.bind(1, 1);
    EXPECT_NE(stmt.expanded_sql().find("uid = 1"), std::string::npos);
    EXPECT_FALSE(stmt.normalized_sql().empty());

    ASSERT_TRUE(stmt.step());
    EXPECT_TRUE(stmt.is_busy());
    EXPECT_EQ(stmt.column_database_name(1), "main");
    EXPECT_EQ(stmt.column_table_name(1), "users");
    EXPECT_EQ(stmt.column_origin_name(1), "uname");
    EXPECT_EQ(stmt.column_name(1), "alias_name");
    EXPECT_EQ(stmt.column_decltype(1), "TEXT");
    EXPECT_EQ(stmt.column_bytes(1), 5);

    // ValueRef + ProtectedValue via column_raw_value
    ValueRef raw = stmt.column_raw_value(1);
    ASSERT_TRUE(raw);
    EXPECT_EQ(raw.type(), Value::Type::Text);
    EXPECT_EQ(raw.as_text(), "Alice");
    ProtectedValue dup = raw.dup();
    ASSERT_TRUE(dup);
    EXPECT_EQ(dup.to_value().get_text(), "Alice");

    // Step to completion & check status / scanstatus
    EXPECT_FALSE(stmt.step());
    EXPECT_FALSE(stmt.is_busy());
    EXPECT_GE(stmt.status(StmtStatusOp::VmStep), 1);
    auto scans = stmt.scanstatus();
    EXPECT_FALSE(scans.empty());
    EXPECT_NO_THROW(stmt.scanstatus_reset());

    // Explain mode toggling
    stmt.reset();
    stmt.set_explain(ExplainMode::QueryPlan);
    EXPECT_TRUE(stmt.is_explain());
    EXPECT_EQ(stmt.explain_mode(), ExplainMode::QueryPlan);
    stmt.set_explain(ExplainMode::Normal);
    EXPECT_FALSE(stmt.is_explain());
}

// ============================================================================
// RawScalarFunction, Pointer Passing, Subtype, Auxdata, ValueRef
// ============================================================================

TEST(FunctionAdvancedTest, PointerPassingSubtypeAndAuxdata) {
    Database db(":memory:");

    static std::atomic<int> ptr_destroyed{0};
    ptr_destroyed.store(0);

    // Function reading a bound opaque pointer
    db.create_raw_function(
        "read_ptr", 1,
        [](FunctionContext& ctx, std::span<const ValueRef> args) {
            ASSERT_EQ(args.size(), 1U);
            EXPECT_TRUE(args[0].from_bind());
            auto* p = args[0].pointer_as<int>("my_int_ptr");
            if (p == nullptr) {
                ctx.set_result();
            } else {
                ctx.set_result(static_cast<int64_t>(*p));
            }
        });

    // Wrong type returns NULL
    db.create_raw_function(
        "read_wrong_ptr", 1,
        [](FunctionContext& ctx, std::span<const ValueRef> args) {
            auto* p = args[0].pointer("other_type");
            ctx.set_result(p == nullptr ? 0 : 1);
        });

    {
        auto stmt = db.prepare("SELECT read_ptr(?1), read_wrong_ptr(?1);");
        int* payload = new int(777);
        stmt.bind_pointer(1, payload, "my_int_ptr", [](void* p) {
            delete static_cast<int*>(p);
            ptr_destroyed.fetch_add(1);
        });
        ASSERT_TRUE(stmt.step());
        EXPECT_EQ(stmt.column_int64(0), 777);
        EXPECT_EQ(stmt.column_int(1), 0);
    }
    EXPECT_EQ(ptr_destroyed.load(), 1);

    // Subtype propagation (FunctionFlag::ResultSubtype)
    db.create_raw_function(
        "tag_sub", 1,
        [](FunctionContext& ctx, std::span<const ValueRef> args) {
            ctx.set_result_value(args[0]);
            ctx.set_subtype('J');
        },
        FunctionFlag::Utf8 | FunctionFlag::ResultSubtype);

    db.create_raw_function(
        "get_sub", 1, [](FunctionContext& ctx, std::span<const ValueRef> args) {
            ctx.set_result(static_cast<int64_t>(args[0].subtype()));
        });

    EXPECT_EQ(db.query_value<int64_t>("SELECT get_sub(tag_sub('hi'));"),
              static_cast<int64_t>('J'));

    // Auxdata caching across rows
    static std::atomic<int> aux_computes{0};
    static std::atomic<int> aux_destroyed{0};
    aux_computes.store(0);
    aux_destroyed.store(0);

    db.create_raw_function(
        "aux_mul", 2, [](FunctionContext& ctx, std::span<const ValueRef> args) {
            auto* factor = ctx.get_auxdata_as<int>(0);
            if (factor == nullptr) {
                factor = new int(args[0].as_int() * 10);
                aux_computes.fetch_add(1);
                ctx.set_auxdata(0, factor, [](void* p) {
                    delete static_cast<int*>(p);
                    aux_destroyed.fetch_add(1);
                });
            }
            ctx.set_result(static_cast<int64_t>(*factor + args[1].as_int()));
        });

    db.execute("CREATE TABLE r(x INT); INSERT INTO r VALUES (1),(2),(3),(4);");
    auto res = db.query("SELECT aux_mul(5, x) FROM r;");
    EXPECT_EQ(res.size(), 4U);
    EXPECT_EQ(res[0][0].as_int64(), 51);
    EXPECT_EQ(res[3][0].as_int64(), 54);
    EXPECT_EQ(aux_computes.load(), 1);
    EXPECT_EQ(aux_destroyed.load(), 1);

    // Error code helper & zeroblob result
    db.create_raw_function("fail_code", 0,
                           [](FunctionContext& ctx, std::span<const ValueRef>) {
                               ctx.set_error("custom fail");
                               ctx.set_error_code(ErrorCode::Constraint);
                           });
    EXPECT_THROW((void)db.query("SELECT fail_code();"), Error);

    db.create_raw_function(
        "make_zblob", 1,
        [](FunctionContext& ctx, std::span<const ValueRef> args) {
            ctx.set_result_zeroblob(args[0].as_int());
        });
    EXPECT_EQ(db.query_value<int64_t>("SELECT length(make_zblob(32));"), 32);
}

// ============================================================================
// VirtualTable: VtabInIterator, IndexInfo::{collation, distinct, set_in,
//               rhs_value, empty set_idx_str}, VirtualTable::{declare_vtab,
//               vtab_config}
// ============================================================================

class InListVTab final : public VirtualTable {
    public:
    class InCursor final : public Cursor {
        public:
        explicit InCursor(const std::vector<int64_t>& all_data)
            : all_(all_data) {
        }

        int filter_raw(int idx_num, const char* /*idx_str*/,
                       std::span<const ValueRef> argv) override {
            rows_.clear();
            pos_ = 0;
            if (idx_num == 1 && !argv.empty()) {
                auto vals = VtabInIterator::collect(argv[0]);
                for (const auto& v : vals) {
                    int64_t target = v.as_int64();
                    for (int64_t d : all_) {
                        if (d == target) {
                            rows_.push_back(d);
                        }
                    }
                }
            } else {
                rows_ = all_;
            }
            return SQLITE_OK;
        }

        int filter(int /*idx_num*/, const char* /*idx_str*/,
                   std::span<const Value> /*argv*/) override {
            return SQLITE_OK;
        }

        int next() override {
            ++pos_;
            return SQLITE_OK;
        }

        bool eof() override {
            return pos_ >= rows_.size();
        }

        Value column(int /*i*/) override {
            return Value(rows_[pos_]);
        }

        int64_t rowid() override {
            return static_cast<int64_t>(pos_ + 1);
        }

        private:
        const std::vector<int64_t>& all_;
        std::vector<int64_t> rows_;
        std::size_t pos_ = 0;
    };

    int create(sqlite3* db,
               std::span<const std::string_view> /*argv*/) override {
        vtab_config(db, VtabConfig::Innocuous);
        return declare_vtab(db, "CREATE TABLE x(val INTEGER)");
    }

    int best_index(IndexInfo info) override {
        // Test empty set_idx_str edge case + distinct() + collation()
        info.set_idx_str(std::string_view{});
        (void)info.distinct();

        for (int i = 0; i < info.num_constraints(); ++i) {
            auto c = info.constraint(i);
            if (c.usable && c.column == 0 && c.op == IndexInfo::Op::Eq) {
                (void)info.collation(i);
                (void)info.rhs_value(i);
                if (info.set_in(i, 1)) {
                    info.set_argv_index(i, 0);
                    info.set_omit(i, true);
                    info.set_idx_num(1);
                    info.set_estimated_cost(10.0);
                    return SQLITE_OK;
                }
            }
        }
        info.set_idx_num(0);
        info.set_estimated_cost(1000.0);
        return SQLITE_OK;
    }

    int open(std::unique_ptr<Cursor>& cursor) override {
        cursor = std::make_unique<InCursor>(data_);
        return SQLITE_OK;
    }

    private:
    std::vector<int64_t> data_{10, 20, 30, 40, 50};
};

TEST(VirtualTableAdvancedTest, InIteratorAndHelpers) {
    Database db(":memory:");
    auto reg = db.create_module(
        "in_test", []() { return std::make_unique<InListVTab>(); });

    db.execute("CREATE VIRTUAL TABLE vt USING in_test;");

    auto rows = db.query(
        "SELECT val FROM vt WHERE val IN (20, 40, 99) "
        "ORDER BY val;");
    ASSERT_EQ(rows.size(), 2U);
    EXPECT_EQ(rows[0][0].as_int64(), 20);
    EXPECT_EQ(rows[1][0].as_int64(), 40);
}

} // namespace
} // namespace sqlitex
