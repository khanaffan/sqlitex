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

    /// Set the result to a specific SQLite error code
    /// (sqlite3_result_error_code).
    void set_error_code(int code) noexcept;

    /// Report out-of-memory error (sqlite3_result_error_nomem).
    void set_error_nomem() noexcept;

    /// Report string/blob too big error (sqlite3_result_error_toobig).
    void set_error_toobig() noexcept;

    /// Set the result to a zero-filled BLOB (sqlite3_result_zeroblob / 64).
    void set_result_zeroblob(int size) noexcept;
    void set_result_zeroblob64(uint64_t size);

    /// Set the result to a static/long-lived text or blob without copying
    /// (SQLITE_STATIC). Caller guarantees lifetime exceeds SQLite's use.
    void set_result_static(std::string_view val) noexcept;
    void set_result_static(const void* blob, int size) noexcept;

    /// Set a 64-bit sized BLOB or text result with optional custom destructor.
    /// If destructor is nullptr, SQLite makes a transient copy.
    void set_result_blob64(const void* blob, uint64_t size,
                           void (*destructor)(void*) = nullptr);
    void set_result_text64(const char* text, uint64_t size,
                           unsigned char encoding = SQLITE_UTF8,
                           void (*destructor)(void*) = nullptr);

    /// Set an opaque pointer result (sqlite3_result_pointer).
    /// `type_name` must be a static string literal.
    void set_result_pointer(void* ptr, const char* type_name,
                            void (*destructor)(void*) = nullptr) noexcept;

    /// Set the result to a copy of an existing sqlite3_value*
    /// (sqlite3_result_value).
    void set_result_value(const ValueRef& val) noexcept;
    void set_result_value(const ProtectedValue& val) noexcept;

    /// Report that the function result is a sub-type.
    /// https://sqlite.org/c3ref/result_subtype.html
    void set_subtype(unsigned int subtype);

    /// Retrieve cached auxiliary metadata for argument `arg_index`
    /// (sqlite3_get_auxdata).
    void* get_auxdata(int arg_index) const noexcept;

    /// Cache auxiliary metadata for argument `arg_index` (sqlite3_set_auxdata).
    void set_auxdata(int arg_index, void* data,
                     void (*destructor)(void*) = nullptr) noexcept;

    /// Return the owning sqlite3* database handle (sqlite3_context_db_handle).
    sqlite3* db_handle() const noexcept;

    /// Inside a virtual table xColumn, returns true if the column value will
    /// not change during an UPDATE (sqlite3_vtab_nochange).
    bool vtab_nochange() const noexcept;

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
/// Receives the result context and a vector of owned argument values.
using ScalarFunction =
    std::function<void(FunctionContext& ctx, const std::vector<Value>& args)>;

/// Callback type for raw scalar functions receiving non-owning ValueRef
/// arguments (enables subtype, pointer-passing, nochange, etc.).
using RawScalarFunction = std::function<void(
    FunctionContext& ctx, const std::vector<ValueRef>& args)>;

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
