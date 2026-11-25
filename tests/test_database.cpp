#include <sqlitex/sqlitex.h>

#include "test_helpers.h"

#include <gtest/gtest.h>

#include <fstream>
#include <string>

namespace sqlitex {
namespace {

using test::TempFile;

// ============================================================================
// Construction / open
// ============================================================================

TEST(DatabaseTest, OpenInMemory) {
    Database db(":memory:");
    EXPECT_TRUE(db);
    EXPECT_TRUE(db.is_autocommit());
}

TEST(DatabaseTest, OpenFile) {
    TempFile tmp;
    {
        Database db(tmp.path(), OpenFlag::ReadWrite | OpenFlag::Create);
        EXPECT_TRUE(db);
    }
    // File should exist after close
    std::ifstream f(tmp.path(), std::ios::binary);
    EXPECT_TRUE(f.good());
}

TEST(DatabaseTest, DefaultConstruction) {
    Database db;
    EXPECT_FALSE(db);
}

TEST(DatabaseTest, MoveConstruction) {
    Database db1(":memory:");
    EXPECT_TRUE(db1);

    Database db2(std::move(db1));
    EXPECT_TRUE(db2);
    EXPECT_FALSE(db1); // NOLINT(bugprone-use-after-move)
}

TEST(DatabaseTest, MoveAssignment) {
    Database db1(":memory:");
    Database db2(":memory:");
    db2 = std::move(db1);
    EXPECT_TRUE(db2);
    EXPECT_FALSE(db1); // NOLINT(bugprone-use-after-move)
}

TEST(DatabaseTest, CantOpen) {
    EXPECT_THROW(Database db("/nonexistent/path/db.sqlite", OpenFlag::ReadOnly),
                 CantOpenError);
}

// ============================================================================
// Execute
// ============================================================================

TEST(DatabaseTest, ExecuteCreateTable) {
    Database db(":memory:");
    EXPECT_NO_THROW(
        db.execute("CREATE TABLE t (id INTEGER PRIMARY KEY, val TEXT)"));
}

TEST(DatabaseTest, ExecuteInsertAndQuery) {
    Database db(":memory:");
    db.execute("CREATE TABLE t (id INTEGER PRIMARY KEY, val TEXT)");
    db.execute("INSERT INTO t VALUES (1, 'hello')");

    auto rows = db.query("SELECT * FROM t");
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0][0].as_int64(), 1);
    EXPECT_EQ(rows[0][1].get_text(), "hello");
}

TEST(DatabaseTest, ExecuteBadSql) {
    Database db(":memory:");
    EXPECT_THROW(db.execute("INVALID SQL STATEMENT"), RuntimeError);
}

// ============================================================================
// Prepare
// ============================================================================

TEST(DatabaseTest, PrepareStatement) {
    Database db(":memory:");
    db.execute("CREATE TABLE t (id INTEGER PRIMARY KEY, val TEXT)");
    db.execute("INSERT INTO t VALUES (1, 'a')");
    db.execute("INSERT INTO t VALUES (2, 'b')");

    auto stmt = db.prepare("SELECT * FROM t WHERE id = ?");
    stmt.bind(1, 1);
    EXPECT_TRUE(stmt.step());
    EXPECT_EQ(stmt.column_int64(0), 1);
    EXPECT_EQ(stmt.column_text(1), "a");
}

// ============================================================================
// Query convenience
// ============================================================================

TEST(DatabaseTest, QueryReturnsRows) {
    Database db(":memory:");
    db.execute("CREATE TABLE t (id INTEGER PRIMARY KEY)");
    db.execute("INSERT INTO t VALUES (1)");
    db.execute("INSERT INTO t VALUES (2)");
    db.execute("INSERT INTO t VALUES (3)");

    auto rows = db.query("SELECT * FROM t ORDER BY id");
    EXPECT_EQ(rows.size(), 3u);
}

TEST(DatabaseTest, QueryEmpty) {
    Database db(":memory:");
    db.execute("CREATE TABLE t (id INTEGER PRIMARY KEY)");
    auto rows = db.query("SELECT * FROM t");
    EXPECT_TRUE(rows.empty());
}

TEST(DatabaseTest, QueryValue) {
    Database db(":memory:");
    db.execute("CREATE TABLE t (id INTEGER PRIMARY KEY, val TEXT)");
    db.execute("INSERT INTO t VALUES (1, 'hello')");

    auto count = db.query_value<int64_t>("SELECT COUNT(*) FROM t");
    ASSERT_TRUE(count.has_value());
    EXPECT_EQ(*count, 1);

    auto text = db.query_value<std::string>("SELECT val FROM t WHERE id = 1");
    ASSERT_TRUE(text.has_value());
    EXPECT_EQ(*text, "hello");
}

TEST(DatabaseTest, QueryValueNull) {
    Database db(":memory:");
    db.execute("CREATE TABLE t (id INTEGER PRIMARY KEY)");
    auto val = db.query_value<int64_t>("SELECT id FROM t WHERE id = 999");
    EXPECT_FALSE(val.has_value());
}

// ============================================================================
// Transactions
// ============================================================================

TEST(DatabaseTest, TransactionCommit) {
    Database db(":memory:");
    db.execute("CREATE TABLE t (id INTEGER PRIMARY KEY)");

    {
        auto txn = Database::Transaction(db);
        db.execute("INSERT INTO t VALUES (1)");
        db.execute("INSERT INTO t VALUES (2)");
        txn.commit();
    }

    auto count = db.query_value<int64_t>("SELECT COUNT(*) FROM t");
    EXPECT_EQ(*count, 2);
}

TEST(DatabaseTest, TransactionRollbackOnDrop) {
    Database db(":memory:");
    db.execute("CREATE TABLE t (id INTEGER PRIMARY KEY)");

    {
        auto txn = db.transaction();
        db.execute("INSERT INTO t VALUES (1)");
        // txn destroyed without explicit commit -> rolls back safely
    }

    auto count = db.query_value<int64_t>("SELECT COUNT(*) FROM t");
    EXPECT_EQ(*count, 0);
}

TEST(DatabaseTest, TransactionRollbackOnException) {
    Database db(":memory:");
    db.execute("CREATE TABLE t (id INTEGER PRIMARY KEY)");

    try {
        auto txn = Database::Transaction(db);
        db.execute("INSERT INTO t VALUES (1)");
        throw std::runtime_error("boom");
    } catch (const std::runtime_error&) {
        // exception in flight → destructor rolls back
    }

    auto count = db.query_value<int64_t>("SELECT COUNT(*) FROM t");
    EXPECT_EQ(*count, 0);
}

TEST(DatabaseTest, TransactionMove) {
    Database db(":memory:");
    db.execute("CREATE TABLE t (id INTEGER PRIMARY KEY)");

    {
        auto txn1 = Database::Transaction(db);
        db.execute("INSERT INTO t VALUES (1)");

        auto txn2 = std::move(txn1);
        db.execute("INSERT INTO t VALUES (2)");
        txn2.commit();
    }

    auto count = db.query_value<int64_t>("SELECT COUNT(*) FROM t");
    EXPECT_EQ(*count, 2);
}

// ============================================================================
// Utility
// ============================================================================

TEST(DatabaseTest, LastInsertRowid) {
    Database db(":memory:");
    db.execute("CREATE TABLE t (id INTEGER PRIMARY KEY, val TEXT)");
    db.execute("INSERT INTO t VALUES (NULL, 'a')");
    EXPECT_EQ(db.last_insert_rowid(), 1);
    db.execute("INSERT INTO t VALUES (NULL, 'b')");
    EXPECT_EQ(db.last_insert_rowid(), 2);
}

TEST(DatabaseTest, Changes) {
    Database db(":memory:");
    db.execute("CREATE TABLE t (id INTEGER PRIMARY KEY)");
    db.execute("INSERT INTO t VALUES (1), (2), (3)");
    EXPECT_EQ(db.changes(), 3);
}

TEST(DatabaseTest, ErrorCodeAndMessage) {
    Database db(":memory:");
    // Trigger an error
    try {
        db.execute("INVALID SQL");
    } catch (const Error&) {
        // After catching, we can still query the error state
        EXPECT_NE(db.errcode(), 0);
        EXPECT_NE(db.errmsg().size(), 0u);
    }
}

TEST(DatabaseTest, BusyTimeout) {
    Database db(":memory:");
    EXPECT_NO_THROW(db.set_busy_timeout(std::chrono::milliseconds{1000}));
}

// ============================================================================
// exec() with RowCallback
// ============================================================================

TEST(DatabaseTest, ExecCallbackCollectsRows) {
    Database db(":memory:");
    db.execute("CREATE TABLE t (id INTEGER, val TEXT)");
    db.execute("INSERT INTO t VALUES (1, 'alpha')");
    db.execute("INSERT INTO t VALUES (2, 'beta')");

    std::vector<std::pair<std::string, std::string>> rows;
    db.exec("SELECT id, val FROM t ORDER BY id",
            [&](std::span<const std::optional<std::string_view>> values,
                std::span<const std::string_view> names) -> bool {
                EXPECT_EQ(values.size(), 2u);
                EXPECT_EQ(names.size(), 2u);
                rows.emplace_back(std::string(values[0].value_or("")),
                                  std::string(values[1].value_or("")));
                return true; // continue
            });

    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].first, "1");
    EXPECT_EQ(rows[0].second, "alpha");
    EXPECT_EQ(rows[1].first, "2");
    EXPECT_EQ(rows[1].second, "beta");
}

TEST(DatabaseTest, ExecCallbackReturnFalseStopsEarly) {
    Database db(":memory:");
    db.execute("CREATE TABLE t (id INTEGER)");
    db.execute("INSERT INTO t VALUES (1)");
    db.execute("INSERT INTO t VALUES (2)");
    db.execute("INSERT INTO t VALUES (3)");

    int count = 0;
    // Returning false on the first row should stop iteration without throwing.
    EXPECT_NO_THROW(
        db.exec("SELECT id FROM t ORDER BY id",
                [&](std::span<const std::optional<std::string_view>>,
                    std::span<const std::string_view>) -> bool {
                    ++count;
                    return false; // stop after first row
                }));
    EXPECT_EQ(count, 1);
}

TEST(DatabaseTest, ExecCallbackExceptionPropagates) {
    Database db(":memory:");
    db.execute("CREATE TABLE t (id INTEGER)");
    db.execute("INSERT INTO t VALUES (1)");

    EXPECT_THROW(db.exec("SELECT id FROM t",
                         [](std::span<const std::optional<std::string_view>>,
                            std::span<const std::string_view>) -> bool {
                             throw std::runtime_error("callback error");
                         }),
                 std::runtime_error);
}

// ============================================================================
// Close
// ============================================================================

TEST(DatabaseTest, CloseAndReopen) {
    TempFile tmp;
    {
        Database db(tmp.path());
        db.execute("CREATE TABLE t (id INTEGER PRIMARY KEY)");
        db.execute("INSERT INTO t VALUES (42)");
    }
    {
        Database db(tmp.path());
        auto val = db.query_value<int64_t>("SELECT id FROM t");
        EXPECT_EQ(*val, 42);
    }
}

// ============================================================================
// total_changes
// ============================================================================

TEST(DatabaseTest, TotalChanges) {
    Database db(":memory:");
    db.execute("CREATE TABLE t (id INTEGER PRIMARY KEY)");
    db.execute("INSERT INTO t VALUES (1), (2), (3)");
    // total_changes is cumulative across all operations on the connection
    EXPECT_GE(db.total_changes(), 3);
}

// ============================================================================
// db_filename
// ============================================================================

TEST(DatabaseTest, DbFilename) {
    TempFile tmp;
    Database db(tmp.path());
    auto name = db.db_filename("main");
    // On macOS, /tmp is a symlink to /private/tmp, so SQLite may resolve
    // the path differently. Verify it's non-empty and contains the filename.
    EXPECT_FALSE(name.empty());
    EXPECT_NE(name.find("sqlitex_test_"), std::string_view::npos);
}

TEST(DatabaseTest, DbFilenameMemory) {
    Database db(":memory:");
    auto name = db.db_filename("main");
    // sqlite3_db_filename returns empty string for :memory: databases
    EXPECT_TRUE(name.empty());
}

// ============================================================================
// query_value<double>
// ============================================================================

TEST(DatabaseTest, QueryValueDouble) {
    Database db(":memory:");
    db.execute("CREATE TABLE t (val REAL)");
    db.execute("INSERT INTO t VALUES (3.14)");

    auto val = db.query_value<double>("SELECT val FROM t");
    ASSERT_TRUE(val.has_value());
    EXPECT_DOUBLE_EQ(*val, 3.14);
}

TEST(DatabaseTest, QueryValueDoubleEmpty) {
    Database db(":memory:");
    db.execute("CREATE TABLE t (val REAL)");
    auto val = db.query_value<double>("SELECT val FROM t WHERE 1 = 0");
    EXPECT_FALSE(val.has_value());
}

// ============================================================================
// exec() with non-std::exception callback
// ============================================================================

TEST(DatabaseTest, ExecCallbackThrowsNonStdException) {
    Database db(":memory:");
    db.execute("CREATE TABLE t (id INTEGER)");
    db.execute("INSERT INTO t VALUES (1)");

    class CustomException {};
    EXPECT_THROW(db.exec("SELECT id FROM t",
                         [](std::span<const std::optional<std::string_view>>,
                            std::span<const std::string_view>) -> bool {
                             throw CustomException();
                         }),
                 CustomException);
}

// ============================================================================
// Open with VFS (validates the VFS parameter path)
// ============================================================================

TEST(DatabaseTest, OpenWithVfs) {
    // Open with default (nullptr) VFS — should succeed
    Database db(":memory:", OpenFlag::ReadWrite | OpenFlag::Create, "");
    EXPECT_TRUE(db);
    db.execute("CREATE TABLE t (x INTEGER)");
    auto rows = db.query("SELECT COUNT(*) FROM t");
    EXPECT_EQ(rows[0][0].as_int64(), 0);
}

} // anonymous namespace
} // namespace sqlitex
