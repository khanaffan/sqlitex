#pragma once

#include <sqlite3.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cloudsqlite {

class Database;
class Value;

// ============================================================================
// Statement — RAII wrapper for sqlite3_stmt*
// ============================================================================

class Statement {
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

    explicit operator bool() const noexcept { return stmt_ != nullptr; }

    sqlite3_stmt* handle() noexcept { return stmt_; }
    const sqlite3_stmt* handle() const noexcept { return stmt_; }

    // === Parameter Binding (returns *this for chaining) ===

    Statement& bind(int index, std::nullptr_t);
    Statement& bind(int index, int32_t value);
    Statement& bind(int index, int64_t value);
    Statement& bind(int index, double value);
    Statement& bind(int index, std::string_view value);
    Statement& bind(int index, const void* blob, int size);
    Statement& bind(int index, const std::vector<uint8_t>& blob);
    Statement& bind(int index, const Value& value);

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
    int column_type(int index) const noexcept;
    std::string_view column_name(int index) const;
    std::string_view column_decltype(int index) const;

    Value column_value(int index) const;

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

    std::string_view sql() const noexcept;
    std::string_view expanded_sql() const;

    // === Database access ===

    Database& database() noexcept { return *db_; }
    const Database& database() const noexcept { return *db_; }

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

} // namespace cloudsqlite
