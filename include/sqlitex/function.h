#pragma once

#include "sqlitex_export.h"

#include "sqlitex/concepts.h"
#include "sqlitex/detail/sqlite3_fwd.h"
#include "sqlitex/error.h"
#include "sqlitex/flags.h"
#include "sqlitex/value.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sqlitex {

// ============================================================================
// FunctionFlag — bitmask for custom SQL function registration
// ============================================================================

enum class FunctionFlag : int {
    Utf8 = SQLITE_UTF8,
    Utf16Le = SQLITE_UTF16LE,
    Utf16Be = SQLITE_UTF16BE,
    Utf16 = SQLITE_UTF16,
    Deterministic = SQLITE_DETERMINISTIC,
    DirectOnly = SQLITE_DIRECTONLY,
    Subtype = SQLITE_SUBTYPE,
    Innocuous = SQLITE_INNOCUOUS,
    ResultSubtype = SQLITE_RESULT_SUBTYPE,
    SelfOrder1 = SQLITE_SELFORDER1,
};

template <>
struct EnableBitmaskOperators<FunctionFlag> : std::true_type {};

using FunctionFlags = Flags<FunctionFlag>;

// ============================================================================
// FunctionContext — wraps sqlite3_context* for setting function results
// ============================================================================

class SQLITEX_EXPORT FunctionContext {
    public:
    explicit FunctionContext(sqlite3_context* ctx) noexcept : ctx_(ctx) {
    }

    /// Set the result to NULL.
    void set_result() noexcept;
    void set_result(std::nullptr_t) noexcept {
        set_result();
    }
    void set_result(std::nullopt_t) noexcept {
        set_result();
    }

    /// Set the result to a boolean (0 or 1).
    void set_result(bool val) {
        set_result(static_cast<int32_t>(val ? 1 : 0));
    }

    /// Set the result to an integer value.
    void set_result(int32_t val);
    void set_result(int64_t val);

    template <SqlInteger T>
        requires(!std::same_as<T, int32_t> && !std::same_as<T, int64_t>)
    void set_result(T val) {
        set_result(static_cast<int64_t>(val));
    }

    /// Set the result to a floating-point value.
    void set_result(double val);

    template <SqlFloat T>
        requires(!std::same_as<T, double>)
    void set_result(T val) {
        set_result(static_cast<double>(val));
    }

    /// Set the result to a text value (copied by SQLite). nullptr sets NULL.
    void set_result(const char* val);
    void set_result(std::string_view val);

    /// Set the result to a BLOB value (copied by SQLite).
    void set_result(std::span<const std::byte> blob);

    template <ConstByteRange R>
        requires(
            !std::same_as<std::remove_cvref_t<R>, std::span<const std::byte>>)
    void set_result(const R& blob) {
        set_result(to_const_bytes(blob));
    }

    /// Set the result from a Value.
    void set_result(const Value& val);

    /// Set the result from an optional value (nullopt sets SQL NULL).
    template <Bindable T>
    void set_result(const std::optional<T>& val) {
        if (!val.has_value()) {
            set_result();
        } else {
            set_result(*val);
        }
    }

    /// Set the result to an error message (SQLITE_ERROR).
    void set_error(std::string_view msg);

    /// Set the result to a specific SQLite error code
    /// (sqlite3_result_error_code).
    void set_error_code(ErrorCode code) noexcept {
        set_error_code(static_cast<int>(code));
    }
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
    void set_result_static(std::span<const std::byte> blob) noexcept;

    template <ConstByteRange R>
        requires(
            !std::same_as<std::remove_cvref_t<R>, std::span<const std::byte>>)
    void set_result_static(const R& blob) noexcept {
        set_result_static(to_const_bytes(blob));
    }

    /// Set a 64-bit sized BLOB or text result with optional custom destructor.
    /// If destructor is nullptr, SQLite makes a transient copy.
    void set_result_blob64(std::span<const std::byte> blob,
                           void (*destructor)(void*) = nullptr);
    void set_result_text64(std::string_view text,
                           unsigned char encoding = SQLITE_UTF8,
                           void (*destructor)(void*) = nullptr);

    /// Set an opaque pointer result (sqlite3_result_pointer).
    /// `type_name` must be a static string literal.
    void set_result_pointer(void* ptr, const char* type_name,
                            void (*destructor)(void*) = nullptr) noexcept;

    /// Type-safe owning pointer result; SQLite owns and destroys via
    /// delete_typed<T>.
    template <typename T>
    void set_result_pointer(std::unique_ptr<T> ptr,
                            const char* type_name) noexcept {
        set_result_pointer(ptr.release(), type_name, &detail::delete_typed<T>);
    }

    /// Set the result to a copy of an existing sqlite3_value*
    /// (sqlite3_result_value).
    void set_result_value(const ValueRef& val) noexcept;
    void set_result_value(const ProtectedValue& val) noexcept;

    /// Report that the function result is a sub-type.
    /// https://sqlite.org/c3ref/result_subtype.html
    void set_subtype(unsigned int subtype);

    /// Retrieve cached auxiliary metadata for argument `arg_index`
    /// (sqlite3_get_auxdata).
    [[nodiscard]] void* get_auxdata(int arg_index) const noexcept;

    /// Type-safe auxiliary metadata retrieval.
    template <typename T>
    [[nodiscard]] T* get_auxdata_as(int arg_index) const noexcept {
        return static_cast<T*>(get_auxdata(arg_index));
    }

    /// Cache auxiliary metadata for argument `arg_index` (sqlite3_set_auxdata).
    void set_auxdata(int arg_index, void* data,
                     void (*destructor)(void*) = nullptr) noexcept;

    /// Type-safe owning auxiliary metadata cache.
    template <typename T>
    void set_auxdata(int arg_index, std::unique_ptr<T> data) noexcept {
        set_auxdata(arg_index, data.release(), &detail::delete_typed<T>);
    }

    /// Return the owning sqlite3* database handle (sqlite3_context_db_handle).
    [[nodiscard]] sqlite3* db_handle() const noexcept;

    /// Inside a virtual table xColumn, returns true if the column value will
    /// not change during an UPDATE (sqlite3_vtab_nochange).
    [[nodiscard]] bool vtab_nochange() const noexcept;

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
/// Receives the result context and a non-owning span of argument values.
using ScalarFunction =
    std::function<void(FunctionContext& ctx, std::span<const Value> args)>;

/// Callback type for raw scalar functions receiving non-owning ValueRef
/// arguments (enables subtype, pointer-passing, nochange, etc.).
using RawScalarFunction =
    std::function<void(FunctionContext& ctx, std::span<const ValueRef> args)>;

// ============================================================================
// AggregateFunction — user-defined aggregate (base class)
// ============================================================================

/// Base class for aggregate SQL functions.
/// Override step() and final(). One instance is created per GROUP BY group.
class SQLITEX_EXPORT AggregateFunction {
    public:
    virtual ~AggregateFunction() = default;

    /// Process a single row in the group.
    virtual void step(FunctionContext& ctx, std::span<const Value> args) = 0;

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
    /// Default final() calls value(). Override if cleanup is needed.
    void final(FunctionContext& ctx) override;

    /// Return the current value of the aggregate.
    virtual void value(FunctionContext& ctx) = 0;

    /// Remove a row from the aggregate (for sliding window frames).
    virtual void inverse(FunctionContext& ctx, std::span<const Value> args) = 0;
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
