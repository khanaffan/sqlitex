#include <sqlitex/sqlitex.h>

#include "test_helpers.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace sqlitex {
namespace {

using test::TempFile;

// ============================================================================
// Scalar Functions
// ============================================================================

TEST(FunctionTest, ScalarDouble) {
    Database db(":memory:");
    db.create_function(
        "double", 1, [](FunctionContext& ctx, const std::vector<Value>& args) {
            ctx.set_result(args[0].as_int64() * 2);
        });

    auto stmt = db.prepare("SELECT double(21)");
    EXPECT_TRUE(stmt.step());
    EXPECT_EQ(stmt.column_int64(0), 42);
}

TEST(FunctionTest, ScalarMultipleArgs) {
    Database db(":memory:");
    db.create_function(
        "my_add", 2, [](FunctionContext& ctx, const std::vector<Value>& args) {
            ctx.set_result(args[0].as_int64() + args[1].as_int64());
        });

    auto val = db.query_value<int64_t>("SELECT my_add(10, 20)");
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 30);
}

TEST(FunctionTest, ScalarNullResult) {
    Database db(":memory:");
    db.create_function(
        "maybe_null", 1,
        [](FunctionContext& ctx, const std::vector<Value>& args) {
            if (args[0].as_int64() < 0) {
                ctx.set_result();
            } else {
                ctx.set_result(args[0].as_int64());
            }
        });

    // Use query() to check for NULL properly (query_value<int64_t>
    // can't distinguish NULL from 0).
    auto rows = db.query("SELECT maybe_null(-1)");
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_TRUE(rows[0][0].is_null());

    rows = db.query("SELECT maybe_null(42)");
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0][0].as_int64(), 42);
}

TEST(FunctionTest, ScalarTextResult) {
    Database db(":memory:");
    db.create_function(
        "greet", 1, [](FunctionContext& ctx, const std::vector<Value>& args) {
            ctx.set_result(std::string("Hello, ") +
                           std::string(args[0].as_text()));
        });

    auto val = db.query_value<std::string>("SELECT greet('World')");
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, "Hello, World");
}

TEST(FunctionTest, ScalarBlobResult) {
    Database db(":memory:");
    db.execute("CREATE TABLE t (data BLOB)");

    db.create_function("make_blob", 0,
                       [](FunctionContext& ctx, const std::vector<Value>&) {
                           const uint8_t data[] = {0x01, 0x02, 0x03};
                           ctx.set_result(data, 3);
                       });

    db.execute("INSERT INTO t VALUES (make_blob())");
    auto rows = db.query("SELECT data FROM t");
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_TRUE(rows[0][0].is_blob());
    EXPECT_EQ(rows[0][0].blob_size(), 3);
}

TEST(FunctionTest, ScalarError) {
    Database db(":memory:");
    db.create_function("must_throw", 0,
                       [](FunctionContext& ctx, const std::vector<Value>&) {
                           ctx.set_error("custom error message");
                       });

    EXPECT_THROW(db.execute("SELECT must_throw()"), RuntimeError);
}

TEST(FunctionTest, ScalarExceptionInCallback) {
    Database db(":memory:");
    db.create_function("cpp_throw", 0,
                       [](FunctionContext&, const std::vector<Value>&) {
                           throw std::runtime_error("C++ exception");
                       });

    EXPECT_THROW(db.execute("SELECT cpp_throw()"), RuntimeError);
}

TEST(FunctionTest, ScalarVariadicArgs) {
    Database db(":memory:");
    // -1 means variadic (any number of args)
    db.create_function(
        "count_args", -1,
        [](FunctionContext& ctx, const std::vector<Value>& args) {
            ctx.set_result(static_cast<int64_t>(args.size()));
        });

    EXPECT_EQ(*db.query_value<int64_t>("SELECT count_args()"), 0);
    EXPECT_EQ(*db.query_value<int64_t>("SELECT count_args(1)"), 1);
    EXPECT_EQ(*db.query_value<int64_t>("SELECT count_args(1,2,3,4,5)"), 5);
}

TEST(FunctionTest, ScalarDoubleValue) {
    Database db(":memory:");
    db.create_function(
        "mul_double", 2,
        [](FunctionContext& ctx, const std::vector<Value>& args) {
            ctx.set_result(args[0].as_double() * args[1].as_double());
        });

    auto val = db.query_value<double>("SELECT mul_double(2.5, 4.0)");
    ASSERT_TRUE(val.has_value());
    EXPECT_DOUBLE_EQ(*val, 10.0);
}

// ============================================================================
// Aggregate Functions (interface-based)
// ============================================================================

class SumAggregate : public AggregateFunction {
    int64_t total_ = 0;

    public:
    void step(FunctionContext&, const std::vector<Value>& args) override {
        total_ += args[0].as_int64();
    }
    void final(FunctionContext& ctx) override {
        ctx.set_result(total_);
    }
};

TEST(FunctionTest, AggregateSum) {
    Database db(":memory:");
    db.execute("CREATE TABLE t (val INTEGER)");
    db.execute("INSERT INTO t VALUES (1), (2), (3), (4), (5)");

    db.create_aggregate("mysum", 1,
                        []() { return std::make_unique<SumAggregate>(); });

    auto val = db.query_value<int64_t>("SELECT mysum(val) FROM t");
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 15);
}

TEST(FunctionTest, AggregateWithGroupBy) {
    Database db(":memory:");
    db.execute("CREATE TABLE t (grp TEXT, val INTEGER)");
    db.execute("INSERT INTO t VALUES ('a', 1), ('a', 2), ('b', 10), ('b', 20)");

    db.create_aggregate("mysum", 1,
                        []() { return std::make_unique<SumAggregate>(); });

    auto rows =
        db.query("SELECT grp, mysum(val) FROM t GROUP BY grp ORDER BY grp");
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0][0].as_text(), "a");
    EXPECT_EQ(rows[0][1].as_int64(), 3);
    EXPECT_EQ(rows[1][0].as_text(), "b");
    EXPECT_EQ(rows[1][1].as_int64(), 30);
}

TEST(FunctionTest, AggregateEmptySet) {
    Database db(":memory:");
    db.execute("CREATE TABLE t (val INTEGER)");

    db.create_aggregate("mysum", 1,
                        []() { return std::make_unique<SumAggregate>(); });

    auto val = db.query_value<int64_t>("SELECT mysum(val) FROM t");
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 0);
}

// ============================================================================
// Aggregate Functions (template convenience)
// ============================================================================

TEST(FunctionTest, AggregateTemplateSum) {
    Database db(":memory:");
    db.execute("CREATE TABLE t (val INTEGER)");
    db.execute("INSERT INTO t VALUES (10), (20), (30)");

    struct State {
        int64_t total = 0;
    };
    db.create_aggregate<State>(
        "tsum", 1,
        [](FunctionContext&, State& s, const std::vector<Value>& args) {
            s.total += args[0].as_int64();
        },
        [](FunctionContext& ctx, State& s) { ctx.set_result(s.total); });

    auto val = db.query_value<int64_t>("SELECT tsum(val) FROM t");
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 60);
}

TEST(FunctionTest, AggregateTemplateAverage) {
    Database db(":memory:");
    db.execute("CREATE TABLE t (val INTEGER)");
    db.execute("INSERT INTO t VALUES (10), (20), (30)");

    struct AvgState {
        int64_t total = 0;
        int count = 0;
    };
    db.create_aggregate<AvgState>(
        "myavg", 1,
        [](FunctionContext&, AvgState& s, const std::vector<Value>& args) {
            s.total += args[0].as_int64();
            s.count++;
        },
        [](FunctionContext& ctx, AvgState& s) {
            if (s.count > 0)
                ctx.set_result(static_cast<double>(s.total) / s.count);
            else
                ctx.set_result();
        });

    auto val = db.query_value<double>("SELECT myavg(val) FROM t");
    ASSERT_TRUE(val.has_value());
    EXPECT_DOUBLE_EQ(*val, 20.0);
}

// ============================================================================
// Window Functions
// ============================================================================

class WindowSum : public WindowFunction {
    int64_t total_ = 0;

    public:
    void step(FunctionContext&, const std::vector<Value>& args) override {
        total_ += args[0].as_int64();
    }
    void inverse(FunctionContext&, const std::vector<Value>& args) override {
        total_ -= args[0].as_int64();
    }
    void value(FunctionContext& ctx) override {
        ctx.set_result(total_);
    }
};

TEST(FunctionTest, WindowSumOverPartition) {
    Database db(":memory:");
    db.execute("CREATE TABLE t (grp TEXT, val INTEGER)");
    db.execute(
        "INSERT INTO t VALUES "
        "('a', 1), ('a', 2), ('b', 10), ('b', 20)");

    db.create_window_function("wsum", 1,
                              []() { return std::make_unique<WindowSum>(); });

    auto rows = db.query(
        "SELECT grp, val, wsum(val) OVER "
        "(PARTITION BY grp ORDER BY val) AS run_sum FROM t ORDER BY grp, val");
    ASSERT_EQ(rows.size(), 4u);
    // a, 1 → running sum = 1
    EXPECT_EQ(rows[0][0].as_text(), "a");
    EXPECT_EQ(rows[0][1].as_int64(), 1);
    EXPECT_EQ(rows[0][2].as_int64(), 1);
    // a, 2 → running sum = 3
    EXPECT_EQ(rows[1][0].as_text(), "a");
    EXPECT_EQ(rows[1][1].as_int64(), 2);
    EXPECT_EQ(rows[1][2].as_int64(), 3);
    // b, 10 → running sum = 10
    EXPECT_EQ(rows[2][0].as_text(), "b");
    EXPECT_EQ(rows[2][1].as_int64(), 10);
    EXPECT_EQ(rows[2][2].as_int64(), 10);
    // b, 20 → running sum = 30
    EXPECT_EQ(rows[3][0].as_text(), "b");
    EXPECT_EQ(rows[3][1].as_int64(), 20);
    EXPECT_EQ(rows[3][2].as_int64(), 30);
}

TEST(FunctionTest, WindowSumOverAll) {
    Database db(":memory:");
    db.execute("CREATE TABLE t (val INTEGER)");
    db.execute("INSERT INTO t VALUES (1), (2), (3)");

    db.create_window_function("wsum", 1,
                              []() { return std::make_unique<WindowSum>(); });

    auto rows = db.query(
        "SELECT val, wsum(val) OVER (ORDER BY val "
        "ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW) FROM t");
    ASSERT_EQ(rows.size(), 3u);
    EXPECT_EQ(rows[0][1].as_int64(), 1);
    EXPECT_EQ(rows[1][1].as_int64(), 3);
    EXPECT_EQ(rows[2][1].as_int64(), 6);
}

// ============================================================================
// Deterministic function flag
// ============================================================================

TEST(FunctionTest, ScalarDeterministic) {
    Database db(":memory:");
    db.create_function(
        "dbl", 1,
        [](FunctionContext& ctx, const std::vector<Value>& args) {
            ctx.set_result(args[0].as_int64() * 2);
        },
        SQLITE_UTF8 | SQLITE_DETERMINISTIC);

    auto val = db.query_value<int64_t>("SELECT dbl(5)");
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 10);
}

// ============================================================================
// FunctionContext::set_subtype
// ============================================================================

TEST(FunctionTest, SetSubtype) {
    Database db(":memory:");
    db.create_function(
        "json_val", 1,
        [](FunctionContext& ctx, const std::vector<Value>& args) {
            ctx.set_result(args[0].as_text());
            // Mark as JSON subtype (SQLITE_RESULT_SUBTYPE must be set
            // so sqlite3_result_subtype takes effect)
            ctx.set_subtype(74); // 'J' = JSON subtype marker
        },
        SQLITE_UTF8 | SQLITE_RESULT_SUBTYPE);

    // The function should execute without error
    auto val = db.query_value<std::string>("SELECT json_val('{\"a\":1}')");
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, "{\"a\":1}");
}

TEST(FunctionTest, SetSubtypeZero) {
    Database db(":memory:");
    db.create_function(
        "zero_sub", 0,
        [](FunctionContext& ctx, const std::vector<Value>&) {
            ctx.set_result(0);
            ctx.set_subtype(0);
        },
        SQLITE_UTF8 | SQLITE_RESULT_SUBTYPE);

    auto val = db.query_value<int64_t>("SELECT zero_sub()");
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 0);
}

} // namespace
} // namespace sqlitex
