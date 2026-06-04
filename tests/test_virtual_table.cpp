#include <sqlitex/sqlitex.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace sqlitex {
namespace {

// ============================================================================
// Helpers
// ============================================================================

/// A simple read-only virtual table that stores rows in a vector.
class SimpleVTab : public VirtualTable {
    public:
    struct Row {
        int64_t rowid;
        std::vector<Value> values;
    };

    explicit SimpleVTab(std::vector<Row> rows,
                        std::vector<std::string> col_names, std::string schema)
        : rows_(std::move(rows)),
          col_names_(std::move(col_names)),
          schema_(std::move(schema)) {
    }

    int create(sqlite3* db, int, const char* const*) override {
        int rc = sqlite3_declare_vtab(db, schema_.c_str());
        if (rc != SQLITE_OK)
            return rc;
        return SQLITE_OK;
    }

    int best_index(IndexInfo info) override {
        // Simple full-scan plan for everything
        info.set_idx_num(0);
        info.set_estimated_cost(static_cast<double>(rows_.size()));
        info.set_estimated_rows(static_cast<int64_t>(rows_.size()));

        // Check for equality constraints on each column
        for (int i = 0; i < info.num_constraints(); ++i) {
            auto c = info.constraint(i);
            if (c.usable && c.op == IndexInfo::Eq) {
                info.set_argv_index(i, i);
                info.set_estimated_cost(1.0);
                info.set_estimated_rows(1);
            }
        }
        return SQLITE_OK;
    }

    int open(std::unique_ptr<Cursor>& cursor) override;

    private:
    friend class SimpleCursor;
    std::vector<Row> rows_;
    std::vector<std::string> col_names_;
    std::string schema_;
};

class SimpleCursor : public Cursor {
    public:
    explicit SimpleCursor(const SimpleVTab& vtab) : vtab_(vtab) {
    }

    int filter(int idx_num, const char*,
               const std::vector<Value>& argv) override {
        (void)idx_num;
        pos_ = 0;

        // Apply simple equality filter if argv set
        if (!argv.empty()) {
            filtered_.clear();
            for (const auto& row : vtab_.rows_) {
                bool match = true;
                // Simple: just check first arg against first column
                if (!argv.empty() && argv[0].type() != Value::Type::Null) {
                    if (row.values.size() > 0 && row.values[0] != argv[0]) {
                        match = false;
                    }
                }
                if (match)
                    filtered_.push_back(row);
            }
        } else {
            filtered_ = vtab_.rows_;
        }
        return SQLITE_OK;
    }

    int next() override {
        pos_++;
        return SQLITE_OK;
    }

    bool eof() override {
        return pos_ >= static_cast<int>(filtered_.size());
    }

    Value column(int i) override {
        if (i < static_cast<int>(filtered_[pos_].values.size())) {
            return filtered_[pos_].values[i];
        }
        return Value();
    }

    int64_t rowid() override {
        return filtered_[pos_].rowid;
    }

    private:
    const SimpleVTab& vtab_;
    std::vector<SimpleVTab::Row> filtered_;
    int pos_ = 0;
};

int SimpleVTab::open(std::unique_ptr<Cursor>& cursor) {
    cursor = std::make_unique<SimpleCursor>(*this);
    return SQLITE_OK;
}

// ============================================================================
// A read-write virtual table backed by a std::vector (like SimpleVTab)
// ============================================================================

class RwVTab : public VirtualTable {
    public:
    struct Row {
        int64_t rowid;
        std::vector<Value> values;
    };

    int create(sqlite3* db, int, const char* const*) override {
        return sqlite3_declare_vtab(db,
                                    "CREATE TABLE x (id INTEGER, name TEXT)");
    }

    int best_index(IndexInfo info) override {
        info.set_idx_num(0);
        info.set_estimated_cost(10.0);
        info.set_estimated_rows(static_cast<int64_t>(rows_.size()));
        return SQLITE_OK;
    }

    int open(std::unique_ptr<Cursor>& cursor) override;

    // Read-write support
    //
    // SQLite xUpdate argument convention (observed on SQLite 3.45+):
     //   argc = N + 2  where N = number of declared columns
     //   DELETE: argc==1, argv[0]=rowid
     //   INSERT: argv[0]=NULL (new rowid placeholder),
     //           argv[1]=NULL (old rowid, NULL for INSERT),
     //           argv[2..N+1]=column values
     //   UPDATE: argv[0]=old rowid,
     //           argv[1]=new rowid (same as old unless rowid changes),
     //           argv[2..N+1]=column values
     //
    int update(int argc, const std::vector<Value>& argv,
               int64_t* p_rowid) override {
        // DELETE
        if (argc == 1 && !argv[0].is_null()) {
            int64_t rowid = argv[0].as_int64();
            rows_.erase(std::remove_if(
                            rows_.begin(), rows_.end(),
                            [rowid](const Row& r) { return r.rowid == rowid; }),
                        rows_.end());
            return SQLITE_OK;
        }

        // INSERT or UPDATE: column values start at argv[2]
        // argv[0] = old rowid (NULL for INSERT)
        // argv[1] = new rowid (NULL for INSERT)
        // argv[2..] = column values
        int first_col = 2;

        if (argv[0].is_null()) {
            // INSERT
            Row row;
            row.rowid = next_rowid_++;
            for (int i = first_col; i < argc; ++i) {
                row.values.push_back(argv[i]);
            }
            rows_.push_back(std::move(row));
            *p_rowid = row.rowid;
            return SQLITE_OK;
        } else {
            // UPDATE
            int64_t old_rowid = argv[0].as_int64();
            for (auto& r : rows_) {
                if (r.rowid == old_rowid) {
                    r.values.clear();
                    for (int i = first_col; i < argc; ++i) {
                        r.values.push_back(argv[i]);
                    }
                    break;
                }
            }
            return SQLITE_OK;
        }
    }

    int begin_txn() override {
        return SQLITE_OK;
    }
    int sync() override {
        return SQLITE_OK;
    }
    int commit_txn() override {
        return SQLITE_OK;
    }
    int rollback_txn() override {
        return SQLITE_OK;
    }

    private:
    friend class RwCursor;
    std::vector<Row> rows_;
    int64_t next_rowid_ = 1;
};

class RwCursor : public Cursor {
    public:
    explicit RwCursor(const RwVTab& vtab) : vtab_(vtab) {
    }

    int filter(int, const char*, const std::vector<Value>&) override {
        filtered_ = vtab_.rows_;
        pos_ = 0;
        return SQLITE_OK;
    }
    int next() override {
        pos_++;
        return SQLITE_OK;
    }
    bool eof() override {
        return pos_ >= static_cast<int>(filtered_.size());
    }

    Value column(int i) override {
        if (i < static_cast<int>(filtered_[pos_].values.size())) {
            return filtered_[pos_].values[i];
        }
        return Value();
    }

    int64_t rowid() override {
        return filtered_[pos_].rowid;
    }

    private:
    const RwVTab& vtab_;
    std::vector<RwVTab::Row> filtered_;
    int pos_ = 0;
};

int RwVTab::open(std::unique_ptr<Cursor>& cursor) {
    cursor = std::make_unique<RwCursor>(*this);
    return SQLITE_OK;
}

// ============================================================================
// Tests: Read-Only Virtual Table
// ============================================================================

TEST(VirtualTableTest, ReadOnlyFullScan) {
    Database db(":memory:");

    std::vector<SimpleVTab::Row> rows = {
        {1, {Value(1), Value(std::string("alice"))}},
        {2, {Value(2), Value(std::string("bob"))}},
    };

    auto reg = db.create_module("simple", [r = std::move(rows)]() mutable {
        return std::make_unique<SimpleVTab>(
            r, std::vector<std::string>{"id", "name"},
            "CREATE TABLE simple (id INTEGER, name TEXT)");
    });
    EXPECT_TRUE(reg);

    db.execute("CREATE VIRTUAL TABLE t USING simple");

    auto result = db.query("SELECT * FROM t ORDER BY id");
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0][0].as_int64(), 1);
    EXPECT_EQ(result[0][1].as_text(), "alice");
    EXPECT_EQ(result[1][0].as_int64(), 2);
    EXPECT_EQ(result[1][1].as_text(), "bob");
}

TEST(VirtualTableTest, ReadOnlyWithConstraint) {
    Database db(":memory:");

    std::vector<SimpleVTab::Row> rows = {
        {1, {Value(1), Value(std::string("alice"))}},
        {2, {Value(2), Value(std::string("bob"))}},
        {3, {Value(3), Value(std::string("charlie"))}},
    };

    [[maybe_unused]] auto reg =
        db.create_module("simple", [r = std::move(rows)]() mutable {
            return std::make_unique<SimpleVTab>(
                r, std::vector<std::string>{"id", "name"},
                "CREATE TABLE simple (id INTEGER, name TEXT)");
        });
    db.execute("CREATE VIRTUAL TABLE t USING simple");

    auto result = db.query("SELECT * FROM t WHERE id = 2");
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0][1].as_text(), "bob");
}

TEST(VirtualTableTest, ReadOnlyEmptyTable) {
    Database db(":memory:");

    [[maybe_unused]] auto reg = db.create_module("simple", []() {
        return std::make_unique<SimpleVTab>(
            std::vector<SimpleVTab::Row>{},
            std::vector<std::string>{"id", "name"},
            "CREATE TABLE simple (id INTEGER, name TEXT)");
    });
    db.execute("CREATE VIRTUAL TABLE t USING simple");

    auto result = db.query("SELECT * FROM t");
    EXPECT_TRUE(result.empty());
}

TEST(VirtualTableTest, ReadOnlyBestIndexPlans) {
    Database db(":memory:");

    std::vector<SimpleVTab::Row> rows = {
        {1, {Value(1), Value(std::string("x"))}},
        {2, {Value(2), Value(std::string("y"))}},
    };

    [[maybe_unused]] auto reg =
        db.create_module("simple", [r = std::move(rows)]() mutable {
            return std::make_unique<SimpleVTab>(
                r, std::vector<std::string>{"id", "name"},
                "CREATE TABLE simple (id INTEGER, name TEXT)");
        });
    db.execute("CREATE VIRTUAL TABLE t USING simple");

    // Just verify queries work — best_index is exercised internally
    auto count = db.query_value<int64_t>("SELECT COUNT(*) FROM t");
    ASSERT_TRUE(count.has_value());
    EXPECT_EQ(*count, 2);
}

TEST(VirtualTableTest, ReadOnlyColumnAccess) {
    Database db(":memory:");

    std::vector<SimpleVTab::Row> rows = {
        {10, {Value(100), Value(std::string("test"))}},
    };

    [[maybe_unused]] auto reg =
        db.create_module("cols", [r = std::move(rows)]() mutable {
            return std::make_unique<SimpleVTab>(
                r, std::vector<std::string>{"a", "b"},
                "CREATE TABLE cols (a INTEGER, b TEXT)");
        });
    db.execute("CREATE VIRTUAL TABLE t USING cols");

    auto result = db.query("SELECT a, b FROM t");
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0][0].as_int64(), 100);
    EXPECT_EQ(result[0][1].as_text(), "test");
}

// ============================================================================
// Tests: Read-Write Virtual Table
// ============================================================================

TEST(VirtualTableTest, InsertAndQuery) {
    Database db(":memory:");

    [[maybe_unused]] auto reg =
        db.create_module("rwtab", []() { return std::make_unique<RwVTab>(); });
    db.execute("CREATE VIRTUAL TABLE t USING rwtab");

    // Test that INSERT works
    db.execute("INSERT INTO t (id, name) VALUES (1, 'hello')");

    // Use query_value to verify basic row count
    auto count = db.query_value<int64_t>("SELECT COUNT(*) FROM t");
    ASSERT_TRUE(count.has_value());
    EXPECT_EQ(*count, 1);

    // Check raw values with type info
    auto result = db.query("SELECT id, name FROM t");
    ASSERT_EQ(result.size(), 1u);
    ASSERT_EQ(result[0].size(), 2u);
    // Debug: check types
    EXPECT_TRUE(result[0][0].is_integer())
        << "col 0 type=" << static_cast<int>(result[0][0].type());
    EXPECT_TRUE(result[0][1].is_text())
        << "col 1 type=" << static_cast<int>(result[0][1].type());
    if (result[0][0].is_integer())
        EXPECT_EQ(result[0][0].as_int64(), 1);
    if (result[0][1].is_text())
        EXPECT_EQ(result[0][1].as_text(), "hello");

    // Second insert
    db.execute("INSERT INTO t (id, name) VALUES (2, 'world')");
    result = db.query("SELECT id, name FROM t ORDER BY id");
    ASSERT_EQ(result.size(), 2u);
    ASSERT_EQ(result[0].size(), 2u);
    EXPECT_TRUE(result[0][0].is_integer());
    if (result[0][0].is_integer())
        EXPECT_EQ(result[0][0].as_int64(), 1);
    EXPECT_TRUE(result[0][1].is_text());
    if (result[0][1].is_text())
        EXPECT_EQ(result[0][1].as_text(), "hello");
}

TEST(VirtualTableTest, UpdateRow) {
    Database db(":memory:");

    [[maybe_unused]] auto reg =
        db.create_module("rwtab", []() { return std::make_unique<RwVTab>(); });
    db.execute("CREATE VIRTUAL TABLE t USING rwtab");

    db.execute("INSERT INTO t (id, name) VALUES (1, 'old')");
    db.execute("UPDATE t SET name = 'new' WHERE id = 1");

    auto result = db.query("SELECT id, name FROM t");
    ASSERT_GE(result.size(), 1u);
    // Find row with id=1
    bool found = false;
    for (auto& row : result) {
        if (row[0].as_int64() == 1) {
            EXPECT_EQ(row[1].as_text(), "new");
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST(VirtualTableTest, DeleteRow) {
    Database db(":memory:");

    [[maybe_unused]] auto reg =
        db.create_module("rwtab", []() { return std::make_unique<RwVTab>(); });
    db.execute("CREATE VIRTUAL TABLE t USING rwtab");

    db.execute("INSERT INTO t (id, name) VALUES (1, 'a')");
    db.execute("INSERT INTO t (id, name) VALUES (2, 'b')");
    db.execute("DELETE FROM t WHERE id = 1");

    auto result = db.query("SELECT id FROM t");
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0][0].as_int64(), 2);
}

TEST(VirtualTableTest, ReadOnlyRejectsWrite) {
    Database db(":memory:");

    [[maybe_unused]] auto reg = db.create_module("ro", []() {
        return std::make_unique<SimpleVTab>(
            std::vector<SimpleVTab::Row>{},
            std::vector<std::string>{"id", "name"},
            "CREATE TABLE ro (id INTEGER, name TEXT)");
    });
    db.execute("CREATE VIRTUAL TABLE t USING ro");

    // The default update() returns SQLITE_READONLY
    EXPECT_THROW(db.execute("INSERT INTO t (id, name) VALUES (1, 'x')"), Error);
}

} // namespace
} // namespace sqlitex
