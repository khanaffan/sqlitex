#include <cloudsqlite/cloudsqlite.h>

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <string>

namespace cloudsqlite {
namespace {

// ============================================================================
// RAII temp file helper
// ============================================================================

class TempFile {
public:
    TempFile() {
        path_ = "/tmp/cloudsqlite_test_" + std::to_string(counter_++) + ".db";
    }
    ~TempFile() {
        std::remove(path_.c_str());
    }
    const std::string& path() const { return path_; }
private:
    std::string path_;
    static inline int counter_ = 0;
};

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
        Database db(tmp.path(),
                    static_cast<int>(OpenFlag::ReadWrite) |
                        static_cast<int>(OpenFlag::Create));
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
    EXPECT_FALSE(db1);  // NOLINT(bugprone-use-after-move)
}

TEST(DatabaseTest, MoveAssignment) {
    Database db1(":memory:");
    Database db2(":memory:");
    db2 = std::move(db1);
    EXPECT_TRUE(db2);
    EXPECT_FALSE(db1);  // NOLINT(bugprone-use-after-move)
}

TEST(DatabaseTest, CantOpen) {
    EXPECT_THROW(Database db("/nonexistent/path/db.sqlite",
                              static_cast<int>(OpenFlag::ReadOnly)),
                 CantOpenError);
}

// ============================================================================
// Execute
// ============================================================================

TEST(DatabaseTest, ExecuteCreateTable) {
    Database db(":memory:");
    EXPECT_NO_THROW(db.execute("CREATE TABLE t (id INTEGER PRIMARY KEY, val TEXT)"));
}

TEST(DatabaseTest, ExecuteInsertAndQuery) {
    Database db(":memory:");
    db.execute("CREATE TABLE t (id INTEGER PRIMARY KEY, val TEXT)");
    db.execute("INSERT INTO t VALUES (1, 'hello')");

    auto rows = db.query("SELECT * FROM t");
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0][0].as_int64(), 1);
    EXPECT_EQ(rows[0][1].as_text(), "hello");
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

TEST(DatabaseTest, TransactionRollback) {
    Database db(":memory:");
    db.execute("CREATE TABLE t (id INTEGER PRIMARY KEY)");

    {
        auto txn = Database::Transaction(db);
        db.execute("INSERT INTO t VALUES (1)");
        // txn destroyed without commit → rollback
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
    EXPECT_NO_THROW(db.set_busy_timeout(1000));
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

}  // anonymous namespace
}  // namespace cloudsqlite
