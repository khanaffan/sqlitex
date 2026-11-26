#include <sqlitex/sqlitex.h>

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace sqlitex {
namespace {

class StatementCacheTest : public ::testing::Test {
    protected:
    void SetUp() override {
        db_.open(":memory:",
                 OpenFlag::ReadWrite | OpenFlag::Create | OpenFlag::FullMutex);
        db_.execute(
            "CREATE TABLE users ("
            "  id INTEGER PRIMARY KEY,"
            "  name TEXT NOT NULL,"
            "  ssn TEXT"
            ");"
            "INSERT INTO users VALUES (1, 'Alice', '111-11-1111');"
            "INSERT INTO users VALUES (2, 'Bob', '222-22-2222');"
            "INSERT INTO users VALUES (3, 'Carol', NULL);");
    }

    Database db_;
};

// ============================================================================
// Basic caching, handle reuse, and statistics
// ============================================================================

TEST_F(StatementCacheTest, HitMissAndHandleReuse) {
    StatementCache cache(db_);
    EXPECT_TRUE(cache);
    EXPECT_EQ(cache.capacity(), StatementCache::kDefaultCapacity);
    EXPECT_TRUE(cache.empty());
    EXPECT_EQ(cache.size(), 0U);

    constexpr std::string_view kSql = "SELECT name FROM users WHERE id = ?";
    sqlite3_stmt* first_handle = nullptr;

    {
        auto stmt = cache.get(kSql);
        ASSERT_TRUE(stmt);
        EXPECT_FALSE(stmt.from_cache());
        EXPECT_TRUE(stmt.is_cacheable());
        first_handle = stmt->handle();

        stmt->bind(1, 1);
        ASSERT_TRUE(stmt->step());
        EXPECT_EQ(stmt->column_text(0), "Alice");
        EXPECT_FALSE(stmt->step());

        // Checked out -> not idle in cache yet.
        EXPECT_EQ(cache.size(), 0U);
        EXPECT_FALSE(cache.contains(kSql));
    }

    EXPECT_EQ(cache.size(), 1U);
    EXPECT_TRUE(cache.contains(kSql));

    {
        auto stmt = cache.get(kSql);
        ASSERT_TRUE(stmt);
        EXPECT_TRUE(stmt.from_cache());
        EXPECT_EQ(stmt->handle(), first_handle);

        stmt->bind(1, 2);
        ASSERT_TRUE(stmt->step());
        EXPECT_EQ(stmt->column_text(0), "Bob");
    }

    auto st = cache.stats();
    EXPECT_EQ(st.misses, 1U);
    EXPECT_EQ(st.hits, 1U);
    EXPECT_EQ(st.evictions, 0U);
    EXPECT_EQ(st.bypasses, 0U);

    cache.reset_stats();
    st = cache.stats();
    EXPECT_EQ(st.misses, 0U);
    EXPECT_EQ(st.hits, 0U);
}

// ============================================================================
// Automatic reset, binding cleanup, early-break lock release, error recovery
// ============================================================================

TEST_F(StatementCacheTest, ClearsBindingsOnReturn) {
    StatementCache cache(db_);
    constexpr std::string_view kSql = "SELECT ?1, ?2";

    {
        auto stmt = cache.get(kSql);
        stmt->bind(1, 42);
        stmt->bind(2, "leaked");
        ASSERT_TRUE(stmt->step());
        EXPECT_EQ(stmt->column_int64(0), 42);
        EXPECT_EQ(stmt->column_text(1), "leaked");
    }

    {
        auto stmt = cache.get(kSql);
        EXPECT_TRUE(stmt.from_cache());
        // Bind only ?1; ?2 must have been cleared to NULL.
        stmt->bind(1, 7);
        ASSERT_TRUE(stmt->step());
        EXPECT_EQ(stmt->column_int64(0), 7);
        EXPECT_TRUE(stmt->column_is_null(1));
    }
}

TEST_F(StatementCacheTest, EarlyBreakResetsAndReleasesReadLock) {
    db_.execute(
        "CREATE TABLE temp_drop (id INTEGER PRIMARY KEY);"
        "INSERT INTO temp_drop VALUES (1), (2), (3);");

    StatementCache cache(db_);
    int64_t first = 0;
    {
        auto stmt = cache.get("SELECT id FROM temp_drop ORDER BY id");
        for (auto id : stmt->rows<int64_t>()) {
            first = id;
            break; // Statement is still mid-scan (busy) here.
        }
        EXPECT_TRUE(stmt->is_busy());
    }
    EXPECT_EQ(first, 1);

    // If ~CachedStatement had not reset the statement, DROP TABLE would fail
    // with SQLITE_LOCKED.
    EXPECT_NO_THROW(db_.execute("DROP TABLE temp_drop"));
}

TEST_F(StatementCacheTest, RecoversCleanlyAfterStepError) {
    StatementCache cache(db_);
    constexpr std::string_view kInsert =
        "INSERT INTO users (id, name, ssn) VALUES (?, ?, ?)";

    // Trigger a UNIQUE/PRIMARY KEY constraint failure.
    EXPECT_THROW(
        {
            auto stmt = cache.get(kInsert);
            stmt->bind_all(1, "Duplicate", nullptr);
            (void)stmt->step();
        },
        ConstraintError);

    // Statement must still be cleanly reset and returned to the cache.
    EXPECT_EQ(cache.size(), 1U);

    {
        auto stmt = cache.get(kInsert);
        EXPECT_TRUE(stmt.from_cache());
        stmt->bind_all(10, "Dave", "000-00-0000");
        EXPECT_FALSE(stmt->step());
    }

    EXPECT_EQ(
        db_.query_value<std::string>("SELECT name FROM users WHERE id = 10"),
        "Dave");
}

// ============================================================================
// LRU eviction and capacity controls
// ============================================================================

TEST_F(StatementCacheTest, LruEvictionOrderAndResize) {
    StatementCache cache(db_, 2);
    EXPECT_EQ(cache.capacity(), 2U);

    {
        auto a = cache.get("SELECT 1");
        EXPECT_FALSE(a.from_cache());
    }
    {
        auto b = cache.get("SELECT 2");
        EXPECT_FALSE(b.from_cache());
    }
    EXPECT_EQ(cache.size(), 2U);

    // Touch "SELECT 1" so "SELECT 2" becomes least recently used.
    {
        auto a = cache.get("SELECT 1");
        EXPECT_TRUE(a.from_cache());
    }

    // Inserting "SELECT 3" must evict "SELECT 2".
    {
        auto c = cache.get("SELECT 3");
        EXPECT_FALSE(c.from_cache());
    }
    EXPECT_EQ(cache.size(), 2U);
    EXPECT_TRUE(cache.contains("SELECT 1"));
    EXPECT_FALSE(cache.contains("SELECT 2"));
    EXPECT_TRUE(cache.contains("SELECT 3"));
    EXPECT_EQ(cache.stats().evictions, 1U);

    // Shrinking capacity to 1 must evict "SELECT 1" (older than "SELECT 3").
    cache.set_capacity(1);
    EXPECT_EQ(cache.size(), 1U);
    EXPECT_FALSE(cache.contains("SELECT 1"));
    EXPECT_TRUE(cache.contains("SELECT 3"));
    EXPECT_EQ(cache.stats().evictions, 2U);

    // Setting capacity to 0 disables caching and clears remaining entries.
    cache.set_capacity(0);
    EXPECT_EQ(cache.size(), 0U);
    {
        auto s = cache.get("SELECT 3");
        EXPECT_FALSE(s.from_cache());
        EXPECT_FALSE(s.is_cacheable());
    }
    EXPECT_EQ(cache.size(), 0U);
    EXPECT_EQ(cache.stats().bypasses, 1U);
}

// ============================================================================
// PrepareFlags key normalization & separation, erase()
// ============================================================================

TEST_F(StatementCacheTest, KeyedBySqlAndNormalizedPrepareFlags) {
    StatementCache cache(db_);
    constexpr std::string_view kSql = "SELECT 1";

    {
        auto s = cache.get(kSql);
        EXPECT_FALSE(s.from_cache());
    }
    // Persistent is automatically implied, so passing it explicitly hits the
    // same entry.
    EXPECT_TRUE(cache.contains(kSql));
    EXPECT_TRUE(cache.contains(kSql, PrepareFlag::Persistent));
    {
        auto s = cache.get(kSql, PrepareFlag::Persistent);
        EXPECT_TRUE(s.from_cache());
    }

    // Different flag combination creates a distinct cache entry.
    EXPECT_FALSE(cache.contains(kSql, PrepareFlag::NoVtab));
    {
        auto s = cache.get(kSql, PrepareFlag::NoVtab);
        EXPECT_FALSE(s.from_cache());
    }
    EXPECT_EQ(cache.size(), 2U);
    EXPECT_TRUE(cache.contains(kSql, PrepareFlag::NoVtab));

    EXPECT_EQ(cache.erase(kSql, PrepareFlag::NoVtab), 1U);
    EXPECT_EQ(cache.erase(kSql, PrepareFlag::NoVtab), 0U);
    EXPECT_FALSE(cache.contains(kSql, PrepareFlag::NoVtab));
    EXPECT_TRUE(cache.contains(kSql));
    EXPECT_EQ(cache.size(), 1U);
}

// ============================================================================
// Re-entrant / nested simultaneous checkouts of the same SQL
// ============================================================================

TEST_F(StatementCacheTest, NestedReentrantCheckoutSameSql) {
    db_.execute(
        "CREATE TABLE nodes (id INTEGER PRIMARY KEY, parent_id INTEGER);"
        "INSERT INTO nodes VALUES (1, 0), (2, 1), (3, 1), (4, 2);");

    StatementCache cache(db_, 8);
    constexpr std::string_view kSql =
        "SELECT id FROM nodes WHERE parent_id = ? ORDER BY id";

    std::vector<int64_t> visited;
    auto walk = [&](auto& self, int64_t parent) -> void {
        auto stmt = cache.get(kSql);
        stmt->bind(1, parent);
        for (auto id : stmt->rows<int64_t>()) {
            visited.push_back(id);
            self(self, id);
        }
    };

    walk(walk, 0);
    EXPECT_EQ(visited, (std::vector<int64_t>{1, 2, 4, 3}));
    // Peak recursion depth is 4 (parent 0 -> 1 -> 2 -> 4 -> leaf).
    EXPECT_EQ(cache.size(), 4U);
    EXPECT_GE(cache.stats().hits, 1U);

    // Second full traversal should be 100% cache hits.
    cache.reset_stats();
    visited.clear();
    walk(walk, 0);
    EXPECT_EQ(visited, (std::vector<int64_t>{1, 2, 4, 3}));
    EXPECT_EQ(cache.stats().misses, 0U);
    EXPECT_EQ(cache.stats().hits, 5U);
}

// ============================================================================
// discard(), release(), clear() generation invalidation, move & lifetime safety
// ============================================================================

TEST_F(StatementCacheTest, DiscardAndRelease) {
    StatementCache cache(db_);

    {
        auto s = cache.get("SELECT 1");
        s.discard();
        EXPECT_FALSE(s.is_cacheable());
    }
    EXPECT_EQ(cache.size(), 0U);

    Statement detached;
    {
        auto s = cache.get("SELECT 42");
        detached = s.release();
        EXPECT_FALSE(s);
    }
    EXPECT_EQ(cache.size(), 0U);
    ASSERT_TRUE(detached);
    ASSERT_TRUE(detached.step());
    EXPECT_EQ(detached.column_int64(0), 42);
}

TEST_F(StatementCacheTest, ClearInvalidatesInFlightLeases) {
    StatementCache cache(db_);

    {
        auto s = cache.get("SELECT 1");
        EXPECT_TRUE(s.is_cacheable());
        cache.clear();
        // `s` is from before clear(), so its destructor must NOT re-insert it.
    }
    EXPECT_EQ(cache.size(), 0U);
}

TEST_F(StatementCacheTest, CacheDestroyedBeforeCheckedOutLease) {
    CachedStatement lease;
    {
        StatementCache cache(db_);
        lease = cache.get("SELECT name FROM users WHERE id = 1");
    }
    // Cache is gone, but lease must still work and finalize cleanly.
    ASSERT_TRUE(lease);
    ASSERT_TRUE(lease->step());
    EXPECT_EQ(lease->column_text(0), "Alice");
}

TEST_F(StatementCacheTest, CachedStatementMoveAssignmentReturnsPrevious) {
    StatementCache cache(db_);

    auto s1 = cache.get("SELECT 1");
    auto s2 = cache.get("SELECT 2");
    EXPECT_EQ(cache.size(), 0U);

    // Overwriting s1 must return "SELECT 1" to the cache.
    s1 = std::move(s2);
    EXPECT_FALSE(s2); // NOLINT(bugprone-use-after-move)
    EXPECT_EQ(cache.size(), 1U);
    EXPECT_TRUE(cache.contains("SELECT 1"));
    EXPECT_FALSE(cache.contains("SELECT 2"));
}

// ============================================================================
// Multi-statement rejection, trailing comments, empty SQL, try_get, misuse
// ============================================================================

TEST_F(StatementCacheTest, RejectsMultiStatementAndHandlesTrivia) {
    StatementCache cache(db_);

    EXPECT_THROW((void)cache.get("SELECT 1; SELECT 2"), MisuseError);
    auto exp = cache.try_get("SELECT 1; SELECT 2");
    ASSERT_FALSE(exp.has_value());
    EXPECT_EQ(exp.error().error_code(), ErrorCode::Misuse);
    EXPECT_EQ(cache.size(), 0U);

    // Trailing semicolon, whitespace, line/block comments are fine.
    {
        auto s = cache.get("SELECT 7; -- trailing comment\n /* ok */ ");
        ASSERT_TRUE(s);
        ASSERT_TRUE(s->step());
        EXPECT_EQ(s->column_int64(0), 7);
    }
    EXPECT_EQ(cache.size(), 1U);

    // Empty / comment-only SQL yields an empty handle and is not cached.
    {
        auto empty = cache.get("   -- nothing here");
        EXPECT_FALSE(empty);
        EXPECT_FALSE(empty.is_cacheable());
    }
    EXPECT_EQ(cache.size(), 1U);

    // Syntax error in try_get returns unexpected without throwing.
    auto bad = cache.try_get("SELECT * FROM nonexistent_table_xyz");
    ASSERT_FALSE(bad.has_value());
    EXPECT_EQ(bad.error().error_code(), ErrorCode::Error);
}

TEST_F(StatementCacheTest, UnboundCacheAndClosedDatabaseErrors) {
    StatementCache unbound;
    EXPECT_FALSE(unbound);
    EXPECT_EQ(unbound.size(), 0U);
    EXPECT_EQ(unbound.capacity(), 0U);
    EXPECT_THROW((void)unbound.get("SELECT 1"), MisuseError);
    auto exp1 = unbound.try_get("SELECT 1");
    ASSERT_FALSE(exp1.has_value());
    EXPECT_EQ(exp1.error().error_code(), ErrorCode::Misuse);

    Database closed_db;
    EXPECT_THROW((void)closed_db.prepare_cached("SELECT 1"), MisuseError);
    auto exp2 = closed_db.try_prepare_cached("SELECT 1");
    ASSERT_FALSE(exp2.has_value());
    EXPECT_EQ(exp2.error().error_code(), ErrorCode::Misuse);
}

// ============================================================================
// Convenience helpers: execute, query, query_value
// ============================================================================

TEST_F(StatementCacheTest, ConvenienceExecuteQueryAndQueryValue) {
    StatementCache cache(db_);

    cache.execute("INSERT INTO users (id, name, ssn) VALUES (?, ?, ?)", 4,
                  "Dan", nullptr);
    cache.execute("INSERT INTO users (id, name, ssn) VALUES (?, ?, ?)", 5,
                  "Eve", "555-55-5555");
    EXPECT_EQ(cache.stats().hits, 1U);

    auto rows =
        cache.query("SELECT id, name FROM users WHERE id >= ? ORDER BY id", 4);
    ASSERT_EQ(rows.size(), 2U);
    EXPECT_EQ(rows[0][0].get_int(), 4);
    EXPECT_EQ(rows[0][1].get_text(), "Dan");
    EXPECT_EQ(rows[1][0].get_int(), 5);
    EXPECT_EQ(rows[1][1].get_text(), "Eve");

    auto name = cache.query_value<std::string>(
        "SELECT name FROM users WHERE id = ?", 5);
    ASSERT_TRUE(name.has_value());
    EXPECT_EQ(*name, "Eve");

    // SQL NULL -> nullopt for scalar/owned types, preserved for Value.
    auto null_ssn =
        cache.query_value<std::string>("SELECT ssn FROM users WHERE id = ?", 3);
    EXPECT_FALSE(null_ssn.has_value());

    auto null_val =
        cache.query_value<Value>("SELECT ssn FROM users WHERE id = ?", 3);
    ASSERT_TRUE(null_val.has_value());
    EXPECT_TRUE(null_val->is_null());

    // Zero-arg query_value also works and hits cache on repeat.
    auto count1 = cache.query_value<int64_t>("SELECT COUNT(*) FROM users");
    auto count2 = cache.query_value<int64_t>("SELECT COUNT(*) FROM users");
    EXPECT_EQ(count1, 5);
    EXPECT_EQ(count2, 5);
}

// ============================================================================
// ScopedPolicy automatic bypass & redaction isolation
// ============================================================================

TEST_F(StatementCacheTest, ScopedPolicyBypassesCacheAndPreventsLeak) {
    constexpr std::string_view kSelectSsn =
        "SELECT name, ssn FROM users WHERE id = ?";

    // 1. Warm BOTH the built-in Database cache and a standalone cache.
    StatementCache standalone(db_);
    {
        auto s1 = db_.prepare_cached(kSelectSsn);
        s1->bind(1, 1);
        ASSERT_TRUE(s1->step());
        EXPECT_EQ(s1->column_text(1), "111-11-1111");

        auto s2 = standalone.get(kSelectSsn);
        s2->bind(1, 1);
        ASSERT_TRUE(s2->step());
        EXPECT_EQ(s2->column_text(1), "111-11-1111");
    }
    EXPECT_EQ(db_.statement_cache().size(), 1U);
    EXPECT_EQ(standalone.size(), 1U);
    EXPECT_FALSE(db_.has_active_policy());

    // 2. Under a denying ScopedPolicy, cached statements MUST be bypassed so
    //    the authorizer denies access.
    {
        ScopedPolicy policy(db_);
        policy.deny_column("users", "ssn");
        EXPECT_TRUE(db_.has_active_policy());

        EXPECT_THROW((void)db_.prepare_cached(kSelectSsn), AuthError);
        EXPECT_THROW((void)standalone.get(kSelectSsn), AuthError);

        auto exp = db_.try_prepare_cached(kSelectSsn);
        ASSERT_FALSE(exp.has_value());
        EXPECT_EQ(exp.error().error_code(), ErrorCode::Auth);
    }

    // 3. Under a redacting (Ignore) read_only ScopedPolicy, redacted bytecode
    //    must execute AND must never be stored in the cache.
    constexpr std::string_view kRedactedOnly =
        "SELECT ssn FROM users WHERE id = ?";
    {
        ScopedPolicy policy(db_);
        policy.read_only().add([](const AuthorizerRequest& req) {
            if (req.action == AuthorizerAction::Read && req.arg2 == "ssn") {
                return AuthorizerResult::Ignore;
            }
            return AuthorizerResult::Allow;
        });

        auto s = db_.prepare_cached(kRedactedOnly);
        EXPECT_FALSE(s.from_cache());
        EXPECT_FALSE(s.is_cacheable());
        s->bind(1, 1);
        ASSERT_TRUE(s->step());
        EXPECT_TRUE(s->column_is_null(0));
    }
    EXPECT_FALSE(db_.statement_cache().contains(kRedactedOnly));
    EXPECT_GE(db_.statement_cache().stats().bypasses, 3U);

    // 4. After ScopedPolicy exits:
    //    - Pre-policy entry is still cached and returns unredacted data.
    //    - kRedactedOnly prepares fresh and returns real unredacted ssn.
    {
        auto s = db_.prepare_cached(kSelectSsn);
        EXPECT_TRUE(s.from_cache());
        s->bind(1, 1);
        ASSERT_TRUE(s->step());
        EXPECT_EQ(s->column_text(1), "111-11-1111");
    }
    {
        auto s = db_.prepare_cached(kRedactedOnly);
        EXPECT_FALSE(s.from_cache());
        s->bind(1, 1);
        ASSERT_TRUE(s->step());
        EXPECT_EQ(s->column_text(0), "111-11-1111");
    }
}

// ============================================================================
// Database built-in cache integration & lifecycle (move, close, reopen)
// ============================================================================

TEST_F(StatementCacheTest, DatabaseMoveRebindsCachedStatements) {
    Database db1(":memory:");
    db1.execute("CREATE TABLE t (x INTEGER); INSERT INTO t VALUES (99);");
    db1.set_statement_cache_capacity(16);
    {
        auto s = db1.prepare_cached("SELECT x FROM t");
        ASSERT_TRUE(s->step());
        EXPECT_EQ(s->column_int64(0), 99);
    }
    EXPECT_EQ(db1.statement_cache().size(), 1U);

    Database db2(std::move(db1));
    EXPECT_EQ(db2.statement_cache_capacity(), 16U);
    EXPECT_EQ(db2.statement_cache().size(), 1U);
    {
        auto s = db2.prepare_cached("SELECT x FROM t");
        EXPECT_TRUE(s.from_cache());
        EXPECT_EQ(&s->database(), &db2);
        ASSERT_TRUE(s->step());
        EXPECT_EQ(s->column_int64(0), 99);
    }
}

TEST_F(StatementCacheTest, DatabaseCloseClearsAndReopenPreservesCapacity) {
    Database db;
    db.set_statement_cache_capacity(12);
    db.open(":memory:");
    EXPECT_EQ(db.statement_cache_capacity(), 12U);

    {
        auto s = db.prepare_cached("SELECT 1");
        ASSERT_TRUE(s->step());
    }
    EXPECT_EQ(db.statement_cache().size(), 1U);

    db.close();
    EXPECT_EQ(db.statement_cache().size(), 0U);
    EXPECT_EQ(db.statement_cache_capacity(), 12U);

    db.open(":memory:");
    EXPECT_EQ(db.statement_cache_capacity(), 12U);
    EXPECT_EQ(db.statement_cache().size(), 0U);

    {
        auto s = db.prepare_cached("SELECT 1");
        EXPECT_FALSE(s.from_cache());
        ASSERT_TRUE(s->step());
        EXPECT_EQ(s->column_int64(0), 1);
    }
    EXPECT_EQ(db.statement_cache().size(), 1U);
}

TEST_F(StatementCacheTest, TryOpenRebindsBuiltInStatementCache) {
    auto exp_db = Database::try_open(":memory:");
    ASSERT_TRUE(exp_db.has_value());
    {
        auto s = exp_db->prepare_cached("SELECT 123");
        ASSERT_TRUE(s);
        EXPECT_EQ(&s->database(), &(*exp_db));
        ASSERT_TRUE(s->step());
        EXPECT_EQ(s->column_int64(0), 123);
    }
    EXPECT_EQ(exp_db->statement_cache().size(), 1U);
}

// ============================================================================
// Multi-threaded concurrency stress test
// ============================================================================

TEST_F(StatementCacheTest, ConcurrentMultiThreadedAccess) {
    // Prepare a small set of queries executed across 8 threads concurrently.
    db_.set_statement_cache_capacity(16);

    constexpr int kThreads = 8;
    constexpr int kIterations = 100;
    std::atomic<int> failures{0};

    std::vector<std::thread> workers;
    workers.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([&, t]() {
            try {
                for (int i = 0; i < kIterations; ++i) {
                    int const id = (i % 2) + 1;
                    if (i % 25 == 0 && t == 0) {
                        db_.clear_statement_cache();
                    }
                    if (i % 17 == 0 && t == 1) {
                        db_.set_statement_cache_capacity(
                            static_cast<std::size_t>(8 + (i % 16)));
                    }

                    auto stmt = db_.prepare_cached(
                        "SELECT name FROM users WHERE id = ?");
                    stmt->bind(1, id);
                    if (!stmt->step()) {
                        ++failures;
                        continue;
                    }
                    std::string_view const expected =
                        (id == 1) ? "Alice" : "Bob";
                    if (stmt->column_text(0) != expected) {
                        ++failures;
                    }

                    // Also exercise convenience helper concurrently.
                    auto val = db_.statement_cache().query_value<int64_t>(
                        "SELECT ? + ?", t, i);
                    if (!val.has_value() || *val != t + i) {
                        ++failures;
                    }
                }
            } catch (...) {
                ++failures;
            }
        });
    }

    for (auto& w : workers) {
        w.join();
    }

    EXPECT_EQ(failures.load(), 0);
    auto st = db_.statement_cache().stats();
    EXPECT_GT(st.hits, 0U);
    EXPECT_LE(db_.statement_cache().size(), db_.statement_cache_capacity());
}

} // namespace
} // namespace sqlitex
