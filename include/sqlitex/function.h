#pragma once

#include "sqlitex_export.h"

#include "sqlitex/value.h"

#include "sqlitex/detail/sqlite3_fwd.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sqlitex {

// ============================================================================
// FunctionContext — wraps sqlite3_context* for setting function results
// ============================================================================

class SQLITEX_EXPORT FunctionContext {
    public:
    explicit FunctionContext(sqlite3_context* ctx) noexcept : ctx_(ctx) {
    }

    /// Set the result to NULL.
    void set_result() noexcept;

    /// Set the result to an integer value.
    void set_result(int32_t val);
    void set_result(int64_t val);

    /// Set the result to a floating-point value.
    void set_result(double val);

    /// Set the result to a text value (copied by SQLite).
    void set_result(const char* val);
    void set_result(std::string_view val);

    /// Set the result to a BLOB value (copied by SQLite).
    void set_result(const void* blob, int size);
    void set_result(const std::vector<uint8_t>& blob);

    /// Set the result from a Value.
    void set_result(const Value& val);

    /// Set the result to an error message (SQLITE_ERROR).
    void set_error(const char* msg);

    /// Report that the function result is a sub-type.
    /// https://sqlite.org/c3ref/result_subtype.html
    void set_subtype(unsigned int subtype);

    /// Raw access to the underlying sqlite3_context*.
    sqlite3_context* handle() noexcept {
        return ctx_;
    }
    const sqlite3_context* handle() const noexcept {
        return ctx_;
    }

    private:
    sqlite3_context* ctx_;
};

// ============================================================================
// ScalarFunction — simple scalar SQL function
// ============================================================================

/// Callback type for scalar (non-aggregate) functions.
/// Receives the result context and a span of argument values.
using ScalarFunction =
    std::function<void(FunctionContext& ctx, const std::vector<Value>& args)>;

// ============================================================================
// AggregateFunction — user-defined aggregate (base class)
// ============================================================================

/// Base class for aggregate SQL functions.
/// Override step() and final().  One instance is created per GROUP BY group.
class SQLITEX_EXPORT AggregateFunction {
    public:
    virtual ~AggregateFunction() = default;

    /// Process a single row in the group.
    virtual void step(FunctionContext& ctx, const std::vector<Value>& args) = 0;

    /// Produce the final result for the group.
    virtual void final(FunctionContext& ctx) = 0;
};

// ============================================================================
// WindowFunction — user-defined window function
// ============================================================================

/// Base class for window SQL functions.
/// Extends AggregateFunction with value() and inverse() for window frames.
class SQLITEX_EXPORT WindowFunction : public AggregateFunction {
    public:
    /// Default final() calls value().  Override if cleanup is needed.
    void final(FunctionContext& ctx) override;

    /// Return the current value of the aggregate.
    virtual void value(FunctionContext& ctx) = 0;

    /// Remove a row from the aggregate (for sliding window frames).
    virtual void inverse(FunctionContext& ctx,
                         const std::vector<Value>& args) = 0;
};

// ============================================================================
// Internal helpers (in function.cpp)
// ============================================================================

namespace detail {

/// Convert sqlite3_value** array to a vector of Value objects.
std::vector<Value> convert_args(int argc, sqlite3_value** argv);

/// Template for xDestroy callback that deletes a CallbackContext<Func>.
template <typename Func>
void destroy_func_context(void* p) noexcept {
    delete static_cast<Func*>(p);
}

} // namespace detail

} // namespace sqlitex
