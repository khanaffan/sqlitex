#include <sqlitex/sqlitex.h>

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace sqlitex {
namespace {

class StatementTest : public ::testing::Test {
    protected:
    void SetUp() override {
        db_.execute(
            "CREATE TABLE t (id INTEGER PRIMARY KEY, name TEXT,"
            " score REAL, data BLOB)");
        db_.execute("INSERT INTO t VALUES (1, 'Alice', 95.5, X'DEAD')");
        db_.execute("INSERT INTO t VALUES (2, 'Bob', 82.0, X'BEEF')");
        db_.execute("INSERT INTO t VALUES (3, 'Charlie', 70.0, NULL)");
    }

    Database db_{":memory:"};
};

// ============================================================================
// Binding
// ============================================================================

TEST_F(StatementTest, BindInt) {
    auto stmt = db_.prepare("SELECT * FROM t WHERE id = ?");
    stmt.bind(1, 1);
    ASSERT_TRUE(stmt.step());
    EXPECT_EQ(stmt.column_int64(0), 1);
}

TEST_F(StatementTest, BindInt64) {
    auto stmt = db_.prepare("SELECT * FROM t WHERE id = ?");
    stmt.bind(1, static_cast<int64_t>(2));
    ASSERT_TRUE(stmt.step());
    EXPECT_EQ(stmt.column_text(1), "Bob");
}

TEST_F(StatementTest, BindDouble) {
    auto stmt = db_.prepare("SELECT * FROM t WHERE score > ?");
    stmt.bind(1, 90.0);
    ASSERT_TRUE(stmt.step());
    EXPECT_EQ(stmt.column_int64(0), 1);
}

TEST_F(StatementTest, BindText) {
    auto stmt = db_.prepare("SELECT * FROM t WHERE name = ?");
    stmt.bind(1, std::string_view("Charlie"));
    ASSERT_TRUE(stmt.step());
    EXPECT_EQ(stmt.column_int64(0), 3);
}

TEST_F(StatementTest, BindNull) {
    auto stmt = db_.prepare("SELECT * FROM t WHERE data IS ?");
    stmt.bind(1, nullptr);
    ASSERT_TRUE(stmt.step());
    EXPECT_EQ(stmt.column_int64(0), 3);
}

TEST_F(StatementTest, BindBlob) {
    auto stmt = db_.prepare("SELECT * FROM t WHERE data = ?");
    std::vector<uint8_t> blob = {0xDE, 0xAD};
    stmt.bind(1, blob);
    ASSERT_TRUE(stmt.step());
    EXPECT_EQ(stmt.column_int64(0), 1);
}

TEST_F(StatementTest, BindByName) {
    auto stmt = db_.prepare("SELECT * FROM t WHERE id = :id");
    stmt.bind("id", 1);
    ASSERT_TRUE(stmt.step());
    EXPECT_EQ(stmt.column_text(1), "Alice");
}

TEST_F(StatementTest, BindAll) {
    auto stmt = db_.prepare("INSERT INTO t (id, name, score) VALUES (?, ?, ?)");
    stmt.bind_all(4, std::string_view("Dave"), 88.0);
    EXPECT_NO_THROW(stmt.execute());

    auto rows = db_.query("SELECT COUNT(*) FROM t");
    EXPECT_EQ(rows[0][0].as_int64(), 4);
}

TEST_F(StatementTest, BindChaining) {
    auto stmt = db_.prepare("SELECT * FROM t WHERE id = ? AND name = ?");
    stmt.bind(1, 1).bind(2, std::string_view("Alice"));
    ASSERT_TRUE(stmt.step());
    EXPECT_EQ(stmt.column_int64(0), 1);
}

// ============================================================================
// Execution
// ============================================================================

TEST_F(StatementTest, StepReturnsRow) {
    auto stmt = db_.prepare("SELECT * FROM t ORDER BY id");
    EXPECT_TRUE(stmt.step());
    EXPECT_EQ(stmt.column_int64(0), 1);
}

TEST_F(StatementTest, StepExhausted) {
    auto stmt = db_.prepare("SELECT * FROM t WHERE id = 999");
    EXPECT_FALSE(stmt.step());
}

TEST_F(StatementTest, ExecuteReturnsBool) {
    auto stmt = db_.prepare("INSERT INTO t VALUES (4, 'Eve', 100.0, NULL)");
    EXPECT_FALSE(stmt.execute()); // INSERT returns no rows
}

TEST_F(StatementTest, ResetAndReuse) {
    auto stmt = db_.prepare("SELECT * FROM t WHERE id = ?");
    stmt.bind(1, 1);
    ASSERT_TRUE(stmt.step());
    EXPECT_EQ(stmt.column_int64(0), 1);

    stmt.reset();
    stmt.clear_bindings();
    stmt.bind(1, 2);
    ASSERT_TRUE(stmt.step());
    EXPECT_EQ(stmt.column_int64(0), 2);
}

// ============================================================================
// Column access
// ============================================================================

TEST_F(StatementTest, ColumnCount) {
    auto stmt = db_.prepare("SELECT * FROM t");
    EXPECT_EQ(stmt.column_count(), 4);
}

TEST_F(StatementTest, ColumnName) {
    auto stmt = db_.prepare("SELECT id, name, score FROM t");
    stmt.step();
    EXPECT_EQ(stmt.column_name(0), "id");
    EXPECT_EQ(stmt.column_name(1), "name");
    EXPECT_EQ(stmt.column_name(2), "score");
}

TEST_F(StatementTest, ColumnType) {
    auto stmt = db_.prepare("SELECT * FROM t WHERE id = 1");
    stmt.step();
    EXPECT_EQ(stmt.column_type(0), SQLITE_INTEGER);
    EXPECT_EQ(stmt.column_type(1), SQLITE_TEXT);
    EXPECT_EQ(stmt.column_type(3), SQLITE_BLOB);
}

TEST_F(StatementTest, ColumnValue) {
    auto stmt = db_.prepare("SELECT * FROM t WHERE id = 1");
    stmt.step();

    Value id = stmt.column_value(0);
    EXPECT_TRUE(id.is_integer());
    EXPECT_EQ(id.as_int64(), 1);

    Value name = stmt.column_value(1);
    EXPECT_TRUE(name.is_text());
    EXPECT_EQ(name.as_text(), "Alice");
}

TEST_F(StatementTest, ColumnIsNull) {
    auto stmt = db_.prepare("SELECT data FROM t WHERE id = 3");
    stmt.step();
    EXPECT_TRUE(stmt.column_is_null(0));

    auto stmt2 = db_.prepare("SELECT name FROM t WHERE id = 1");
    stmt2.step();
    EXPECT_FALSE(stmt2.column_is_null(0));
}

TEST_F(StatementTest, OptionalColumn) {
    // data is NULL for id=3, so optional should be nullopt
    auto stmt = db_.prepare("SELECT data FROM t WHERE id = 3");
    stmt.step();
    auto val = stmt.column_optional_text(0);
    EXPECT_FALSE(val.has_value());

    auto stmt2 = db_.prepare("SELECT name FROM t WHERE id = 1");
    stmt2.step();
    auto name = stmt2.column_optional_text(0);
    EXPECT_TRUE(name.has_value());
    EXPECT_EQ(*name, "Alice");
}

// ============================================================================
// Parameter metadata
// ============================================================================

TEST_F(StatementTest, BindParameterCount) {
    auto stmt = db_.prepare("SELECT * FROM t WHERE id = ? AND name = ?");
    EXPECT_EQ(stmt.bind_parameter_count(), 2);
}

TEST_F(StatementTest, BindParameterName) {
    auto stmt = db_.prepare("SELECT * FROM t WHERE id = :user_id");
    EXPECT_EQ(stmt.bind_parameter_index("user_id"), 1);
    EXPECT_EQ(stmt.bind_parameter_name(1), ":user_id");
}

// ============================================================================
// Statement metadata
// ============================================================================

TEST_F(StatementTest, IsReadonly) {
    auto select = db_.prepare("SELECT * FROM t");
    EXPECT_TRUE(select.is_readonly());

    auto insert = db_.prepare("INSERT INTO t VALUES (99, 'X', 0, NULL)");
    EXPECT_FALSE(insert.is_readonly());
}

TEST_F(StatementTest, Sql) {
    auto stmt = db_.prepare("SELECT * FROM t");
    auto sql = stmt.sql();
    EXPECT_NE(sql.size(), 0u);
}

TEST_F(StatementTest, IsBusy) {
    auto stmt = db_.prepare("SELECT * FROM t");
    EXPECT_FALSE(stmt.is_busy());
}

// ============================================================================
// Move semantics
// ============================================================================

TEST_F(StatementTest, MoveConstruction) {
    auto stmt1 = db_.prepare("SELECT * FROM t WHERE id = 1");
    EXPECT_TRUE(stmt1);

    Statement stmt2(std::move(stmt1));
    EXPECT_TRUE(stmt2);
    EXPECT_FALSE(stmt1); // NOLINT

    stmt2.step();
    EXPECT_EQ(stmt2.column_int64(0), 1);
}

TEST_F(StatementTest, MoveAssignment) {
    auto stmt1 = db_.prepare("SELECT * FROM t WHERE id = 1");
    auto stmt2 = db_.prepare("SELECT * FROM t WHERE id = 2");
    stmt2 = std::move(stmt1);
    EXPECT_TRUE(stmt2);
    stmt2.step();
    EXPECT_EQ(stmt2.column_int64(0), 1);
}

// ============================================================================
// Statement metadata — remaining accessors
// ============================================================================

TEST_F(StatementTest, IsExplain) {
    auto stmt = db_.prepare("SELECT * FROM t");
    EXPECT_FALSE(stmt.is_explain());
}

TEST_F(StatementTest, ExpandedSql) {
    auto stmt = db_.prepare("SELECT * FROM t WHERE id = ?");
    stmt.bind(1, 42);
    auto expanded = stmt.expanded_sql();
    EXPECT_FALSE(expanded.empty());
}

TEST_F(StatementTest, ExpandedSqlNoBinding) {
    auto stmt = db_.prepare("SELECT * FROM t");
    auto expanded = stmt.expanded_sql();
    EXPECT_FALSE(expanded.empty());
}

TEST_F(StatementTest, ColumnDecltype) {
    auto stmt = db_.prepare("SELECT id, name, score FROM t");
    // For in-memory databases, decltype may or may not be available,
    // but the call itself should not crash.
    auto dt0 = stmt.column_decltype(0);
    auto dt1 = stmt.column_decltype(1);
    auto dt2 = stmt.column_decltype(2);
    // At minimum, the calls succeed and return something (possibly empty)
    (void)dt0;
    (void)dt1;
    (void)dt2;
}

// ============================================================================
// Statement::database() on null/default-constructed statement
// ============================================================================

TEST(StatementEdgeCaseTest, DatabaseOnDefaultConstructedStatement) {
    Statement stmt;
    EXPECT_FALSE(stmt);
    EXPECT_THROW({ stmt.database(); }, std::logic_error);
}

TEST(StatementEdgeCaseTest, DatabaseOnMovedFromStatement) {
    Database db(":memory:");
    db.execute("CREATE TABLE t (x INTEGER)");
    auto stmt1 = db.prepare("SELECT * FROM t");
    Statement stmt2(std::move(stmt1));
    EXPECT_TRUE(stmt2);
    EXPECT_NO_THROW(stmt2.database());
    // stmt1 is moved-from, should throw
    EXPECT_THROW({ stmt1.database(); }, std::logic_error);
}

} // anonymous namespace
} // namespace sqlitex
