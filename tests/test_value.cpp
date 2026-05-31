#include <cloudsqlite/cloudsqlite.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace cloudsqlite {
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
    EXPECT_EQ(v.blob_size(), 3);
    EXPECT_EQ(v.get_blob()[0], 0x01);
}

TEST(ValueTest, RawBlobConstruction) {
    const uint8_t data[] = {0xAA, 0xBB};
    Value v(static_cast<const void*>(data), 2);
    EXPECT_TRUE(v.is_blob());
    EXPECT_EQ(v.blob_size(), 2);
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

TEST(ValueTest, AsText) {
    Value v(std::string("hello"));
    EXPECT_EQ(v.as_text(), "hello");
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
    EXPECT_EQ(v.blob_size(), 0);
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

}  // anonymous namespace
}  // namespace cloudsqlite
