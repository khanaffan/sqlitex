#pragma once

#include "sqlitex_export.h"

#include "sqlitex/detail/sqlite3_fwd.h"

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace sqlitex {

class Database;
class Value;
class ValueRef;
class ProtectedValue;

// ============================================================================
// Statement enums & scan-status descriptors
// ============================================================================

enum class StmtStatusOp : int {
    FullscanStep = SQLITE_STMTSTATUS_FULLSCAN_STEP,
    Sort = SQLITE_STMTSTATUS_SORT,
    AutoIndex = SQLITE_STMTSTATUS_AUTOINDEX,
    VmStep = SQLITE_STMTSTATUS_VM_STEP,
    Reprepare = SQLITE_STMTSTATUS_REPREPARE,
    Run = SQLITE_STMTSTATUS_RUN,
    FilterMiss = SQLITE_STMTSTATUS_FILTER_MISS,
    FilterHit = SQLITE_STMTSTATUS_FILTER_HIT,
    MemUsed = SQLITE_STMTSTATUS_MEMUSED,
};

enum class ExplainMode : int {
    Normal = 0,
    Explain = 1,
    QueryPlan = 2,
};

enum class CArrayType : int {
    Int32 = SQLITE_CARRAY_INT32,
    Int64 = SQLITE_CARRAY_INT64,
    Double = SQLITE_CARRAY_DOUBLE,
    Text = SQLITE_CARRAY_TEXT,
    Blob = SQLITE_CARRAY_BLOB,
};

struct ScanStatusEntry {
    int64_t nloop = 0;
    int64_t nvisit = 0;
    double est = 0.0;
    std::string name;
    std::string explain;
    int select_id = 0;
    int parent_id = 0;
    int64_t ncycle = 0;
};

// ============================================================================
// Statement — RAII wrapper for sqlite3_stmt*
// ============================================================================

class SQLITEX_EXPORT Statement {
    public:
    Statement() noexcept = default;

    /// Construct from a prepared statement.
    /// Prefer Database::prepare() over direct construction.
    Statement(sqlite3_stmt* stmt, Database* db) noexcept;

    ~Statement();

    // Move-only
    Statement(Statement&& other) noexcept;
    Statement& operator=(Statement&& other) noexcept;
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    explicit operator bool() const noexcept {
        return stmt_ != nullptr;
    }

    sqlite3_stmt* handle() noexcept {
        return stmt_;
    }
    const sqlite3_stmt* handle() const noexcept {
        return stmt_;
    }

    // === Parameter Binding (returns *this for chaining) ===

    Statement& bind(int index, std::nullptr_t);
    Statement& bind(int index, int32_t value);
    Statement& bind(int index, int64_t value);
    Statement& bind(int index, double value);
    Statement& bind(int index, std::string_view value);
    Statement& bind(int index, const void* blob, int size);
    Statement& bind(int index, const std::vector<uint8_t>& blob);
    Statement& bind(int index, const Value& value);

    /// Bind a zero-filled BLOB (sqlite3_bind_zeroblob / 64).
    Statement& bind_zeroblob(int index, int size);
    Statement& bind_zeroblob64(int index, uint64_t size);

    /// Bind static text or blob without copying (SQLITE_STATIC).
    /// Caller guarantees lifetime exceeds statement execution.
    Statement& bind_static(int index, std::string_view value);
    Statement& bind_static(int index, const void* blob, int size);

    /// Bind 64-bit sized BLOB or text with optional custom destructor.
    /// If destructor is nullptr, SQLite makes a transient copy.
    Statement& bind_blob64(int index, const void* blob, uint64_t size,
                           void (*destructor)(void*) = nullptr);
    Statement& bind_text64(int index, const char* text, uint64_t size,
                           unsigned char encoding = SQLITE_UTF8,
                           void (*destructor)(void*) = nullptr);

    /// Bind an opaque pointer (sqlite3_bind_pointer).
    /// `type_name` must be a static string literal.
    Statement& bind_pointer(int index, void* ptr, const char* type_name,
                            void (*destructor)(void*) = nullptr);

    /// Bind a sqlite3_value* via ValueRef or ProtectedValue
    /// (sqlite3_bind_value).
    Statement& bind_value(int index, const ValueRef& val);
    Statement& bind_value(int index, const ProtectedValue& val);

    /// Bind an array to a carray() table-valued function parameter
    /// (sqlite3_carray_bind / sqlite3_carray_bind_v2).
    Statement& bind_carray(int index, void* data, int count, CArrayType type,
                           void (*destructor)(void*) = nullptr);
    Statement& bind_carray_v2(int index, void* data, int count, CArrayType type,
                              void (*destructor)(void*), void* del_arg);
    Statement& bind_carray(int index, const std::vector<int32_t>& values);
    Statement& bind_carray(int index, const std::vector<int64_t>& values);
    Statement& bind_carray(int index, const std::vector<double>& values);
    Statement& bind_carray(int index, const std::vector<std::string>& values);

    /// Bind by named parameter. Returns *this for chaining.
    Statement& bind(std::string_view name, std::nullptr_t v) {
        int idx = bind_parameter_index(name);
        return bind(idx, v);
    }
    Statement& bind(std::string_view name, int32_t v) {
        int idx = bind_parameter_index(name);
        return bind(idx, v);
    }
    Statement& bind(std::string_view name, int64_t v) {
        int idx = bind_parameter_index(name);
        return bind(idx, v);
    }
    Statement& bind(std::string_view name, double v) {
        int idx = bind_parameter_index(name);
        return bind(idx, v);
    }
    Statement& bind(std::string_view name, std::string_view v) {
        int idx = bind_parameter_index(name);
        return bind(idx, v);
    }
    Statement& bind(std::string_view name, const Value& v) {
        int idx = bind_parameter_index(name);
        return bind(idx, v);
    }

    /// Bind all positional parameters from a parameter pack.
    /// Values are bound starting at index 1.
    template <typename... Args>
    Statement& bind_all(Args&&... args);

    // === Execution ===

    /// Advance to the next row.
    /// Returns true if a row is available, false when done.
    /// Throws on error.
    bool step();

    /// Convenience: call step() once, discarding any row.
    /// Returns true if rows remain.
    bool execute();

    /// Reset the statement so it can be re-executed.
    void reset();

    /// Clear all parameter bindings.
    void clear_bindings();

    // === Column Access ===

    int column_count() const noexcept;
    int data_count() const noexcept;
    int column_type(int index) const noexcept;
    std::string_view column_name(int index) const noexcept;
    std::string_view column_decltype(int index) const noexcept;
    std::string_view column_database_name(int index) const noexcept;
    std::string_view column_table_name(int index) const noexcept;
    std::string_view column_origin_name(int index) const noexcept;

    Value column_value(int index) const;
    ValueRef column_raw_value(int index) const noexcept;

    int32_t column_int(int index) const;
    int64_t column_int64(int index) const;
    double column_double(int index) const;
    std::string_view column_text(int index) const;
    const void* column_blob(int index) const;
    int column_bytes(int index) const noexcept;

    bool column_is_null(int index) const noexcept;

    /// Null-checked column access: returns nullopt for SQL NULL.
    std::optional<int64_t> column_optional_int64(int index) const;
    std::optional<double> column_optional_double(int index) const;
    std::optional<std::string_view> column_optional_text(int index) const;

    // === Parameter Metadata ===

    int bind_parameter_count() const noexcept;
    int bind_parameter_index(std::string_view name) const;

    std::string_view bind_parameter_name(int index) const;

    // === Statement Metadata ===

    bool is_busy() const noexcept;
    bool is_readonly() const noexcept;
    bool is_explain() const noexcept;
    ExplainMode explain_mode() const noexcept;
    void set_explain(ExplainMode mode);

    std::string_view sql() const noexcept;
    std::string expanded_sql() const;
    std::string_view normalized_sql() const noexcept;

    /// Query a prepared-statement status counter (sqlite3_stmt_status).
    int status(StmtStatusOp op, bool reset = false) noexcept;

    /// Collect query-loop execution statistics (sqlite3_stmt_scanstatus_v2).
    std::vector<ScanStatusEntry> scanstatus(int flags = 0) const;

    /// Reset scanstatus counters to zero (sqlite3_stmt_scanstatus_reset).
    void scanstatus_reset() noexcept;

    // === Database access ===

    /// Return the owning Database.
    /// \pre The statement must have been constructed via Database::prepare()
    ///      (i.e. must not be default-constructed).
    /// \throws std::logic_error if the statement is not associated with a
    ///         database (default-constructed or moved-from).
    Database& database() {
        if (db_ == nullptr) {
            throw std::logic_error(
                "Statement::database() called on a null statement");
        }
        return *db_;
    }
    const Database& database() const {
        if (db_ == nullptr) {
            throw std::logic_error(
                "Statement::database() called on a null statement");
        }
        return *db_;
    }

    private:
    sqlite3_stmt* stmt_ = nullptr;
    Database* db_ = nullptr;

    // Used by Database::prepare()
    friend class Database;
};

// ============================================================================
// Template implementation
// ============================================================================

template <typename... Args>
Statement& Statement::bind_all(Args&&... args) {
    int index = 1;
    // Fold expression over the comma operator to bind each arg in order.
    (bind(index++, std::forward<Args>(args)), ...);
    return *this;
}

} // namespace sqlitex
