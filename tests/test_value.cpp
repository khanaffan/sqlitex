#include <sqlitex/sqlitex.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <vector>

namespace sqlitex {
namespace {

// ============================================================================
// Construction
// ============================================================================

TEST(ValueTest, DefaultConstruction) {
    Value v;
    EXPECT_TRUE(v.is_null());
    EXPECT_EQ(v.type(), Value::Type::Null);
}

TEST(ValueTest, NullConstruction) {
    Value v(nullptr);
    EXPECT_TRUE(v.is_null());
}

TEST(ValueTest, IntegerConstruction) {
    Value v(42);
    EXPECT_TRUE(v.is_integer());
    EXPECT_EQ(v.get_int(), 42);
}

TEST(ValueTest, Int64Construction) {
    Value v(static_cast<int64_t>(9223372036854775807LL));
    EXPECT_TRUE(v.is_integer());
    EXPECT_EQ(v.get_int(), 9223372036854775807LL);
}

TEST(ValueTest, DoubleConstruction) {
    Value v(3.14159);
    EXPECT_TRUE(v.is_float());
    EXPECT_DOUBLE_EQ(v.get_double(), 3.14159);
}

TEST(ValueTest, StringConstruction) {
    Value v(std::string("hello"));
    EXPECT_TRUE(v.is_text());
    EXPECT_EQ(v.get_text(), "hello");
}

TEST(ValueTest, StringViewConstruction) {
    Value v(std::string_view("world"));
    EXPECT_TRUE(v.is_text());
    EXPECT_EQ(v.get_text(), "world");
}

TEST(ValueTest, CStringConstruction) {
    Value v("cstring");
    EXPECT_TRUE(v.is_text());
    EXPECT_EQ(v.get_text(), "cstring");
}

TEST(ValueTest, BlobConstruction) {
    std::vector<uint8_t> data = {0x01, 0x02, 0x03};
    Value v(data);
    EXPECT_TRUE(v.is_blob());
    EXPECT_EQ(v.as_bytes().size(), 3u);
    EXPECT_EQ(v.get_blob()[0], std::byte{0x01});
}

TEST(ValueTest, SpanBlobConstruction) {
    const std::array<uint8_t, 2> data = {0xAA, 0xBB};
    Value v(std::as_bytes(std::span(data)));
    EXPECT_TRUE(v.is_blob());
    EXPECT_EQ(v.as_bytes().size(), 2u);
    EXPECT_EQ(v.as_bytes()[0], std::byte{0xAA});
    EXPECT_EQ(v.as_bytes()[1], std::byte{0xBB});
}

// ============================================================================
// Type inspection
// ============================================================================

TEST(ValueTest, TypeEnum) {
    EXPECT_EQ(Value(42).type(), Value::Type::Integer);
    EXPECT_EQ(Value(3.14).type(), Value::Type::Float);
    EXPECT_EQ(Value(std::string("text")).type(), Value::Type::Text);
    EXPECT_EQ(Value(nullptr).type(), Value::Type::Null);
}

// ============================================================================
// Accessors
// ============================================================================

TEST(ValueTest, ToString) {
    Value v(std::string("hello"));
    EXPECT_EQ(v.to_string(), "hello");
}

TEST(ValueTest, AsInt64) {
    Value v(100);
    EXPECT_EQ(v.as_int64(), 100);
}

TEST(ValueTest, AsDouble) {
    Value v(2.718);
    EXPECT_DOUBLE_EQ(v.as_double(), 2.718);
}

TEST(ValueTest, IntPromotesToDouble) {
    Value v(42);
    EXPECT_DOUBLE_EQ(v.get_double(), 42.0);
}

// ============================================================================
// Equality
// ============================================================================

TEST(ValueTest, EqualityNull) {
    EXPECT_EQ(Value(nullptr), Value(nullptr));
    EXPECT_EQ(Value(), Value(nullptr));
}

TEST(ValueTest, EqualityInteger) {
    EXPECT_EQ(Value(42), Value(42));
    EXPECT_NE(Value(42), Value(43));
}

TEST(ValueTest, EqualityText) {
    EXPECT_EQ(Value(std::string("a")), Value(std::string("a")));
    EXPECT_NE(Value(std::string("a")), Value(std::string("b")));
}

TEST(ValueTest, EqualityCrossType) {
    EXPECT_NE(Value(42), Value(3.14));
    EXPECT_NE(Value(nullptr), Value(42));
}

// ============================================================================
// Edge cases
// ============================================================================

TEST(ValueTest, EmptyBlob) {
    Value v(std::vector<uint8_t>{});
    EXPECT_TRUE(v.is_blob());
    EXPECT_EQ(v.as_bytes().size(), 0u);
}

TEST(ValueTest, EmptyString) {
    Value v(std::string(""));
    EXPECT_TRUE(v.is_text());
    EXPECT_EQ(v.get_text(), "");
}

TEST(ValueTest, NegativeInt64) {
    Value v(static_cast<int64_t>(-1));
    EXPECT_EQ(v.get_int(), -1);
}

// ============================================================================
// Cross-type conversions
// ============================================================================

TEST(ValueTest, AsInt64FromDouble) {
    Value v(3.14);
    EXPECT_EQ(v.as_int64(), 3); // truncates toward zero
}

TEST(ValueTest, AsDoubleFromInt) {
    Value v(42);
    EXPECT_DOUBLE_EQ(v.as_double(), 42.0);
}

TEST(ValueTest, ToStringFromInteger) {
    Value v(42);
    EXPECT_EQ(v.to_string(), "42");
}

TEST(ValueTest, ToStringFromDouble) {
    Value v(3.140000);
    auto text = v.to_string();
    EXPECT_FALSE(text.empty());
}

TEST(ValueTest, ToStringFromDoubleThenIntOwnedStrings) {
    // to_string() returns owned std::string so both values remain valid
    Value v_int(999);
    Value v_dbl(1.5);
    std::string t1 = v_int.to_string();
    std::string t2 = v_dbl.to_string();
    EXPECT_EQ(t1, "999");
    EXPECT_FALSE(t2.empty());
}

// ============================================================================
// Equality for all types
// ============================================================================

TEST(ValueTest, EqualityFloat) {
    EXPECT_EQ(Value(3.14), Value(3.14));
    EXPECT_NE(Value(3.14), Value(2.718));
}

TEST(ValueTest, EqualityBlob) {
    std::vector<uint8_t> b1 = {0x01, 0x02, 0x03};
    std::vector<uint8_t> b2 = {0x01, 0x02, 0x03};
    std::vector<uint8_t> b3 = {0x04, 0x05};
    EXPECT_EQ(Value(b1), Value(b2));
    EXPECT_NE(Value(b1), Value(b3));
}

TEST(ValueTest, OperatorNotEqual) {
    EXPECT_TRUE(Value(1) != Value(2));
    EXPECT_FALSE(Value(1) != Value(1));
}

// ============================================================================
// Accessor edge cases
// ============================================================================

TEST(ValueTest, AsBytes) {
    std::vector<uint8_t> data = {0xAA, 0xBB, 0xCC};
    Value v(data);
    auto bytes = v.as_bytes();
    ASSERT_EQ(bytes.size(), 3u);
    EXPECT_EQ(bytes[0], std::byte{0xAA});
    EXPECT_EQ(bytes[1], std::byte{0xBB});
    EXPECT_EQ(bytes[2], std::byte{0xCC});
}

TEST(ValueTest, GetBlob) {
    std::vector<uint8_t> data = {0xDE, 0xAD};
    Value v(data);
    const auto& blob = v.get_blob();
    EXPECT_EQ(blob.size(), 2u);
    EXPECT_EQ(blob[0], std::byte{0xDE});
    EXPECT_EQ(blob[1], std::byte{0xAD});
}

// ============================================================================
// from_sqlite3_value — exercised through actual SQL queries
// ============================================================================

TEST(ValueTest, FromSqlite3ValueNull) {
    Database db(":memory:");
    db.execute("CREATE TABLE t (x)");
    db.execute("INSERT INTO t VALUES (NULL)");
    auto rows = db.query("SELECT x FROM t");
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_TRUE(rows[0][0].is_null());
}

TEST(ValueTest, FromSqlite3ValueInteger) {
    Database db(":memory:");
    auto rows = db.query("SELECT 42");
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_TRUE(rows[0][0].is_integer());
    EXPECT_EQ(rows[0][0].as_int64(), 42);
}

TEST(ValueTest, FromSqlite3ValueFloat) {
    Database db(":memory:");
    auto rows = db.query("SELECT 3.14");
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_TRUE(rows[0][0].is_float());
    EXPECT_DOUBLE_EQ(rows[0][0].as_double(), 3.14);
}

TEST(ValueTest, FromSqlite3ValueText) {
    Database db(":memory:");
    auto rows = db.query("SELECT 'hello'");
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_TRUE(rows[0][0].is_text());
    EXPECT_EQ(rows[0][0].get_text(), "hello");
}

TEST(ValueTest, FromSqlite3ValueBlob) {
    Database db(":memory:");
    auto rows = db.query("SELECT X'CAFE'");
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_TRUE(rows[0][0].is_blob());
    EXPECT_EQ(rows[0][0].as_bytes().size(), 2u);
}

} // anonymous namespace
} // namespace sqlitex
