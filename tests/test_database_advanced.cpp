#include <sqlitex/sqlitex.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

namespace sqlitex {
namespace {

// ============================================================================
// Update, Commit, Rollback, Preupdate Hooks
// ============================================================================

TEST(DatabaseAdvancedTest, UpdateCommitRollbackHooks) {
    Database db(":memory:");
    db.execute("CREATE TABLE items(id INTEGER PRIMARY KEY, v INT);");

    std::vector<std::pair<int, int64_t>> updates;
    db.set_update_hook([&](int op, std::string_view schema,
                           std::string_view table, int64_t rowid) {
        EXPECT_EQ(schema, "main");
        EXPECT_EQ(table, "items");
        updates.emplace_back(op, rowid);
    });

    int commits = 0;
    int rollbacks = 0;
    bool allow_commit = true;

    db.set_commit_hook([&]() -> bool {
        ++commits;
        return allow_commit;
    });
    db.set_rollback_hook([&]() { ++rollbacks; });

    db.execute("INSERT INTO items VALUES (1, 10);");
    EXPECT_EQ(commits, 1);
    EXPECT_EQ(rollbacks, 0);
    ASSERT_EQ(updates.size(), 1U);
    EXPECT_EQ(updates[0].first, SQLITE_INSERT);
    EXPECT_EQ(updates[0].second, 1);

    // Veto commit -> update_hook fires during INSERT before commit veto rolls
    // back
    allow_commit = false;
    EXPECT_THROW(db.execute("INSERT INTO items VALUES (2, 20);"), Error);
    EXPECT_EQ(commits, 2);
    EXPECT_EQ(rollbacks, 1);
    EXPECT_EQ(updates.size(), 2U);
    EXPECT_EQ(db.query_value<int64_t>("SELECT COUNT(*) FROM items;"), 1);

    // Clear hooks
    db.set_commit_hook({});
    db.set_rollback_hook({});
    db.set_update_hook({});
    db.execute("INSERT INTO items VALUES (3, 30);");
    EXPECT_EQ(commits, 2);
    EXPECT_EQ(updates.size(), 2U);
}

TEST(DatabaseAdvancedTest, PreupdateHook) {
    Database db(":memory:");
    db.execute("CREATE TABLE kv(k INTEGER PRIMARY KEY, v TEXT);");

    struct Event {
        int op = 0;
        int64_t old_id = 0;
        int64_t new_id = 0;
        int cols = 0;
        int depth = 0;
        int blobwrite = -1;
        std::string old_v;
        std::string new_v;
    };
    std::vector<Event> events;

    db.set_preupdate_hook([&](const PreupdateContext& ctx) {
        Event ev;
        ev.op = ctx.op();
        ev.old_id = ctx.old_rowid();
        ev.new_id = ctx.new_rowid();
        ev.cols = ctx.count();
        ev.depth = ctx.depth();
        ev.blobwrite = ctx.blobwrite();
        if (ev.op == SQLITE_UPDATE || ev.op == SQLITE_DELETE) {
            ev.old_v = ctx.old_value(1).as_text();
        }
        if (ev.op == SQLITE_INSERT || ev.op == SQLITE_UPDATE) {
            ev.new_v = ctx.new_value(1).as_text();
        }
        events.push_back(std::move(ev));
    });

    db.execute("INSERT INTO kv VALUES (1, 'alpha');");
    db.execute("UPDATE kv SET v = 'beta' WHERE k = 1;");
    db.execute("DELETE FROM kv WHERE k = 1;");
    db.set_preupdate_hook({});

    ASSERT_EQ(events.size(), 3U);
    EXPECT_EQ(events[0].op, SQLITE_INSERT);
    EXPECT_EQ(events[0].new_id, 1);
    EXPECT_EQ(events[0].cols, 2);
    EXPECT_EQ(events[0].depth, 0);
    EXPECT_EQ(events[0].blobwrite, -1);
    EXPECT_EQ(events[0].new_v, "alpha");

    EXPECT_EQ(events[1].op, SQLITE_UPDATE);
    EXPECT_EQ(events[1].old_v, "alpha");
    EXPECT_EQ(events[1].new_v, "beta");

    EXPECT_EQ(events[2].op, SQLITE_DELETE);
    EXPECT_EQ(events[2].old_v, "beta");
}

TEST(DatabaseAdvancedTest, TraceHookAndProgressHandler) {
    Database db(":memory:");
    db.execute("CREATE TABLE nums(n INT);");

    int stmt_traces = 0;
    int profile_traces = 0;
    int row_traces = 0;
    db.set_trace_hook(TraceFlag::Stmt | TraceFlag::Profile | TraceFlag::Row,
                      [&](const TraceEvent& ev) {
                          if (ev.type == SQLITE_TRACE_STMT) {
                              ++stmt_traces;
                          } else if (ev.type == SQLITE_TRACE_PROFILE) {
                              ++profile_traces;
                              EXPECT_GE(ev.duration_ns, 0);
                          } else if (ev.type == SQLITE_TRACE_ROW) {
                              ++row_traces;
                          }
                      });

    db.execute("INSERT INTO nums VALUES (1), (2), (3);");
    auto rows = db.query("SELECT n FROM nums;");
    EXPECT_EQ(rows.size(), 3U);
    EXPECT_GE(stmt_traces, 2);
    EXPECT_GE(profile_traces, 2);
    EXPECT_EQ(row_traces, 3);
    db.set_trace_hook(0, {});

    // Progress handler interrupts a large recursive CTE
    int ticks = 0;
    db.set_progress_handler(10, [&]() -> bool { return ++ticks >= 3; });
    EXPECT_THROW(
        db.query("WITH RECURSIVE c(x) AS (VALUES(1) UNION ALL SELECT x+1 FROM "
                 "c WHERE x < 1000000) SELECT SUM(x) FROM c;"),
        Error);
    EXPECT_GE(ticks, 3);
    db.set_progress_handler(0, {});
}

// ============================================================================
// Savepoints (nested + exception rollback)
// ============================================================================

TEST(DatabaseAdvancedTest, SavepointsNestedAndRollback) {
    Database db(":memory:");
    db.execute("CREATE TABLE t(v INT);");

    {
        Database::Savepoint sp1(db, "outer");
        db.execute("INSERT INTO t VALUES (1);");

        {
            Database::Savepoint sp2(db, "inner");
            db.execute("INSERT INTO t VALUES (2);");
            sp2.rollback();
        }
        EXPECT_EQ(db.query_value<int64_t>("SELECT COUNT(*) FROM t;"), 1);

        try {
            Database::Savepoint sp3(db, "throwing");
            db.execute("INSERT INTO t VALUES (3);");
            throw std::runtime_error("boom");
        } catch (const std::runtime_error&) {
        }
        EXPECT_EQ(db.query_value<int64_t>("SELECT COUNT(*) FROM t;"), 1);

        db.execute("INSERT INTO t VALUES (4);");
        sp1.release();
    }

    EXPECT_EQ(db.query_value<int64_t>("SELECT SUM(v) FROM t;"), 5);
}

// ============================================================================
// Custom Collations & Collation-Needed Hook
// ============================================================================

TEST(DatabaseAdvancedTest, CollationsAndCollationNeeded) {
    Database db(":memory:");

    // Reverse ASCII collation
    db.create_collation("REV", [](std::string_view a, std::string_view b) {
        if (a < b) {
            return 1;
        }
        if (a > b) {
            return -1;
        }
        return 0;
    });

    db.execute("CREATE TABLE words(w TEXT);");
    db.execute("INSERT INTO words VALUES ('apple'), ('banana'), ('cherry');");

    auto rows = db.query("SELECT w FROM words ORDER BY w COLLATE REV;");
    ASSERT_EQ(rows.size(), 3U);
    EXPECT_EQ(rows[0][0].as_text(), "cherry");
    EXPECT_EQ(rows[2][0].as_text(), "apple");

    // On-demand collation registration via set_collation_needed
    int needed_calls = 0;
    db.set_collation_needed(
        [&](Database& d, int /*rep*/, std::string_view name) {
            ++needed_calls;
            if (name == "LEN_ORDER") {
                d.create_collation(
                    name, [](std::string_view a, std::string_view b) -> int {
                        if (a.size() != b.size()) {
                            return a.size() < b.size() ? -1 : 1;
                        }
                        return a.compare(b);
                    });
            }
        });

    auto len_rows =
        db.query("SELECT w FROM words ORDER BY w COLLATE LEN_ORDER;");
    EXPECT_EQ(needed_calls, 1);
    ASSERT_EQ(len_rows.size(), 3U);
    EXPECT_EQ(len_rows[0][0].as_text(), "apple"); // len 5

    db.set_collation_needed({});
    db.drop_collation("REV");
}

// ============================================================================
// DbConfig, Limits, Status, Introspection, ClientData, FileControl
// ============================================================================

TEST(DatabaseAdvancedTest, ConfigLimitsStatusAndIntrospection) {
    Database db(":memory:");
    db.execute(
        "CREATE TABLE t(id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "name TEXT NOT NULL COLLATE NOCASE);");

    // DbConfig boolean toggle
    bool prev_fk = db.db_config_get(DbConfig::EnableFKey);
    EXPECT_EQ(db.db_config(DbConfig::EnableFKey, prev_fk ? 0 : 1), !prev_fk);
    EXPECT_EQ(db.db_config_get(DbConfig::EnableFKey), !prev_fk);

    // FP digits (2-arg varargs config!)
    int prev_fp = db.fp_digits();
    int updated_fp = db.set_fp_digits(6);
    EXPECT_EQ(updated_fp, 6);
    EXPECT_EQ(db.fp_digits(), 6);
    db.set_fp_digits(prev_fp > 0 ? prev_fp : 15);

    // MainDbName (Database owns string storage!)
    db.set_main_db_name("primary_schema");
    EXPECT_EQ(db.db_name(0), std::optional<std::string_view>("primary_schema"));
    auto names = db.db_names();
    ASSERT_FALSE(names.empty());
    EXPECT_EQ(names[0], "primary_schema");
    db.set_main_db_name("main");

    // Limits
    int prev_max_sql = db.limit(LimitCategory::SqlLength, -1);
    EXPECT_GT(prev_max_sql, 0);
    int old_val = db.limit(LimitCategory::SqlLength, 50000);
    EXPECT_EQ(old_val, prev_max_sql);
    EXPECT_EQ(db.limit(LimitCategory::SqlLength, prev_max_sql), 50000);

    // Status
    auto st = db.db_status(DbStatusOp::CacheUsed);
    EXPECT_GE(st.current, 0);

    // Rowid / changes / txn state / readonly / extended errcode / error_offset
    db.execute("INSERT INTO t(name) VALUES ('one'), ('two');");
    EXPECT_EQ(db.changes(), 2);
    EXPECT_EQ(db.changes64(), 2);
    EXPECT_EQ(db.total_changes(), 2);
    EXPECT_EQ(db.total_changes64(), 2);
    db.set_last_insert_rowid(42);
    EXPECT_EQ(db.last_insert_rowid(), 42);

    EXPECT_TRUE(db.is_autocommit());
    EXPECT_EQ(db.txn_state("main"), TxnState::None);
    EXPECT_EQ(db.db_readonly("main"), 0);
    EXPECT_FALSE(db.is_readonly("main"));
    EXPECT_EQ(db.db_readonly("no_such_db"), -1);

    db.begin_transaction(TransactionMode::Immediate);
    EXPECT_FALSE(db.is_autocommit());
    EXPECT_EQ(db.txn_state("main"), TxnState::Write);
    db.commit();

    EXPECT_FALSE(db.is_interrupted());
    db.set_extended_result_codes(true);

    // error_offset on syntax error
    EXPECT_THROW(db.execute("SELECT * FORM t;"), Error);
    EXPECT_GE(db.error_offset(), 0);

    // Table column metadata
    EXPECT_TRUE(db.table_exists("t"));
    EXPECT_FALSE(db.table_exists("does_not_exist"));
    auto meta_id = db.table_column_metadata("t", "id");
    EXPECT_EQ(meta_id.data_type, "INTEGER");
    EXPECT_TRUE(meta_id.primary_key);
    EXPECT_TRUE(meta_id.auto_increment);
    auto meta_name = db.table_column_metadata("t", "name");
    EXPECT_EQ(meta_name.data_type, "TEXT");
    EXPECT_EQ(meta_name.collation_sequence, "NOCASE");
    EXPECT_TRUE(meta_name.not_null);

    // Active statements iteration
    {
        auto s1 = db.prepare("SELECT 1;");
        auto s2 = db.prepare("SELECT 2;");
        auto active = db.active_statements();
        EXPECT_EQ(active.size(), 2U);
        EXPECT_NE(db.next_stmt(nullptr), nullptr);
    }

    // Memory / cache maintenance & setlk_timeout / busy_handler / autovacuum
    EXPECT_NO_THROW(db.db_release_memory());
    EXPECT_NO_THROW(db.db_cacheflush());
    EXPECT_NO_THROW(db.setlk_timeout(100, 0));
    EXPECT_NO_THROW(db.set_busy_handler([](int) { return false; }));
    EXPECT_NO_THROW(db.set_busy_handler({}));
    EXPECT_NO_THROW(
        db.set_autovacuum_pages([](std::string_view, unsigned int, unsigned int,
                                   unsigned int) { return 0U; }));
    EXPECT_NO_THROW(db.set_autovacuum_pages({}));
    EXPECT_NO_THROW(db.enable_load_extension(false));
    EXPECT_NO_THROW(db.unlock_notify({}));

    // file_control (SQLITE_FCNTL_PERSIST_WAL = 10 -> query with -1)
    int persist = -1;
    int fc_rc = db.file_control("main", 10, &persist);
    EXPECT_TRUE(fc_rc == SQLITE_OK || fc_rc == SQLITE_NOTFOUND);
}

TEST(DatabaseAdvancedTest, ClientDataLifetime) {
    static std::atomic<int> destroyed{0};
    destroyed.store(0);
    {
        Database db(":memory:");
        int* payload = new int(12345);
        db.set_clientdata("test.key", payload, [](void* p) {
            delete static_cast<int*>(p);
            destroyed.fetch_add(1);
        });
        auto* got = static_cast<int*>(db.get_clientdata("test.key"));
        ASSERT_NE(got, nullptr);
        EXPECT_EQ(*got, 12345);
        EXPECT_EQ(db.get_clientdata("missing.key"), nullptr);
    }
    // Closing the database must invoke the clientdata destructor!
    EXPECT_EQ(destroyed.load(), 1);
}

} // namespace
} // namespace sqlitex
