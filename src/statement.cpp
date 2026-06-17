#include "sqlitex/statement.h"
#include "sqlitex/database.h"
#include "sqlitex/error.h"
#include "sqlitex/value.h"

#include <sqlite3.h>

#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

namespace sqlitex {

// ============================================================================
// Construction / Destruction
// ============================================================================

Statement::Statement(sqlite3_stmt* stmt, Database* db) noexcept
    : stmt_(stmt), db_(db) {
}

Statement::~Statement() {
    if (stmt_ != nullptr) {
        sqlite3_finalize(stmt_);
    }
}

Statement::Statement(Statement&& other) noexcept
    : stmt_(std::exchange(other.stmt_, nullptr)),
      db_(std::exchange(other.db_, nullptr)) {
}

Statement& Statement::operator=(Statement&& other) noexcept {
    if (this != &other) {
        if (stmt_ != nullptr) {
            sqlite3_finalize(stmt_);
        }
        stmt_ = std::exchange(other.stmt_, nullptr);
        db_ = std::exchange(other.db_, nullptr);
    }
    return *this;
}

// ============================================================================
// Binding
// ============================================================================

Statement& Statement::bind(int index, std::nullptr_t /*unused*/) {
    throw_on_error(sqlite3_bind_null(stmt_, index), db_->handle());
    return *this;
}

Statement& Statement::bind(int index, int32_t value) {
    throw_on_error(sqlite3_bind_int(stmt_, index, value), db_->handle());
    return *this;
}

Statement& Statement::bind(int index, int64_t value) {
    throw_on_error(sqlite3_bind_int64(stmt_, index, value), db_->handle());
    return *this;
}

Statement& Statement::bind(int index, double value) {
    throw_on_error(sqlite3_bind_double(stmt_, index, value), db_->handle());
    return *this;
}

Statement& Statement::bind(int index, std::string_view value) {
    // SQLITE_TRANSIENT tells SQLite to make its own copy
    throw_on_error(
        sqlite3_bind_text(stmt_, index, value.data(),
                          static_cast<int>(value.size()), SQLITE_TRANSIENT),
        db_->handle());
    return *this;
}

Statement& Statement::bind(int index, const void* blob, int size) {
    throw_on_error(
        sqlite3_bind_blob(stmt_, index, blob, size, SQLITE_TRANSIENT),
        db_->handle());
    return *this;
}

Statement& Statement::bind(int index, const std::vector<uint8_t>& blob) {
    return bind(index, blob.data(), static_cast<int>(blob.size()));
}

Statement& Statement::bind(int index, const Value& value) {
    switch (value.type()) {
    case Value::Type::Null:
        return bind(index, nullptr);
    case Value::Type::Integer:
        return bind(index, value.get_int());
    case Value::Type::Float:
        return bind(index, value.get_double());
    case Value::Type::Text: {
        auto sv = value.get_text();
        return bind(index, sv);
    }
    case Value::Type::Blob:
        return bind(index, value.blob_data(), value.blob_size());
    }
    return *this;
}

// ============================================================================
// Execution
// ============================================================================

bool Statement::step() {
    int const rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW) {
        return true;
    }
    if (rc == SQLITE_DONE) {
        return false;
    }
    throw_on_error(rc, db_->handle());
    return false; // unreachable
}

bool Statement::execute() {
    return step();
}

void Statement::reset() {
    throw_on_error(sqlite3_reset(stmt_), db_->handle());
}

void Statement::clear_bindings() {
    throw_on_error(sqlite3_clear_bindings(stmt_), db_->handle());
}

// ============================================================================
// Column Access
// ============================================================================

int Statement::column_count() const noexcept {
    return sqlite3_column_count(stmt_);
}

int Statement::column_type(int index) const noexcept {
    return sqlite3_column_type(stmt_, index);
}

std::string_view Statement::column_name(int index) const noexcept {
    const char* name = sqlite3_column_name(stmt_, index);
    return name ? std::string_view(name) : std::string_view{};
}

std::string_view Statement::column_decltype(int index) const noexcept {
    const char* dt = sqlite3_column_decltype(stmt_, index);
    return dt ? std::string_view(dt) : std::string_view{};
}

Value Statement::column_value(int index) const {
    switch (sqlite3_column_type(stmt_, index)) {
    case SQLITE_INTEGER:
        return Value(static_cast<int64_t>(sqlite3_column_int64(stmt_, index)));
    case SQLITE_FLOAT:
        return Value(sqlite3_column_double(stmt_, index));
    case SQLITE_TEXT:
        return Value(std::string(
            reinterpret_cast<const char*>(sqlite3_column_text(stmt_, index)),
            static_cast<std::size_t>(sqlite3_column_bytes(stmt_, index))));
    case SQLITE_BLOB: {
        const auto* data =
            static_cast<const uint8_t*>(sqlite3_column_blob(stmt_, index));
        int const size = sqlite3_column_bytes(stmt_, index);
        if (!data || size <= 0)
            return Value(std::vector<uint8_t>{});
        return Value(std::vector<uint8_t>(data, data + size));
    }
    case SQLITE_NULL:
    default:
        return Value(nullptr);
    }
}

int32_t Statement::column_int(int index) const {
    return sqlite3_column_int(stmt_, index);
}

int64_t Statement::column_int64(int index) const {
    return sqlite3_column_int64(stmt_, index);
}

double Statement::column_double(int index) const {
    return sqlite3_column_double(stmt_, index);
}

std::string_view Statement::column_text(int index) const {
    const auto* text =
        reinterpret_cast<const char*>(sqlite3_column_text(stmt_, index));
    if (text == nullptr) {
        return {};
    }
    return std::string_view(
        text, static_cast<std::size_t>(sqlite3_column_bytes(stmt_, index)));
}

const void* Statement::column_blob(int index) const {
    return sqlite3_column_blob(stmt_, index);
}

int Statement::column_bytes(int index) const noexcept {
    return sqlite3_column_bytes(stmt_, index);
}

bool Statement::column_is_null(int index) const noexcept {
    return sqlite3_column_type(stmt_, index) == SQLITE_NULL;
}

std::optional<int64_t> Statement::column_optional_int64(int index) const {
    if (column_is_null(index))
        return std::nullopt;
    return column_int64(index);
}

std::optional<double> Statement::column_optional_double(int index) const {
    if (column_is_null(index))
        return std::nullopt;
    return column_double(index);
}

std::optional<std::string_view> Statement::column_optional_text(
    int index) const {
    if (column_is_null(index))
        return std::nullopt;
    return column_text(index);
}

// ============================================================================
// Parameter Metadata
// ============================================================================

int Statement::bind_parameter_count() const noexcept {
    return sqlite3_bind_parameter_count(stmt_);
}

int Statement::bind_parameter_index(std::string_view name) const {
    // sqlite3_bind_parameter_index expects a string that may or may not
    // include the leading ':', '@', or '$'.
    std::string n(name);
    int idx = sqlite3_bind_parameter_index(stmt_, n.c_str());
    if (idx == 0 &&
        (n.empty() || (n[0] != ':' && n[0] != '@' && n[0] != '$'))) {
        // Try with colon prefix
        n = ":" + n;
        idx = sqlite3_bind_parameter_index(stmt_, n.c_str());
    }
    if (idx == 0) {
        throw NotFoundError(
            ErrorCode::NotFound,
            std::string("Unknown bind parameter: ") + std::string(name));
    }
    return idx;
}

std::string_view Statement::bind_parameter_name(int index) const {
    const char* name = sqlite3_bind_parameter_name(stmt_, index);
    return name ? std::string_view(name) : std::string_view{};
}

// ============================================================================
// Statement Metadata
// ============================================================================

bool Statement::is_busy() const noexcept {
    return sqlite3_stmt_busy(stmt_) != 0;
}

bool Statement::is_readonly() const noexcept {
    return sqlite3_stmt_readonly(stmt_) != 0;
}

bool Statement::is_explain() const noexcept {
    return sqlite3_stmt_isexplain(stmt_) != 0;
}

std::string_view Statement::sql() const noexcept {
    const char* s = sqlite3_sql(stmt_);
    return s ? std::string_view(s) : std::string_view{};
}

std::string Statement::expanded_sql() const {
    char* s = sqlite3_expanded_sql(stmt_);
    if (s == nullptr) {
        return {};
    }
    std::string result(s);
    sqlite3_free(s);
    return result;
}

} // namespace sqlitex
