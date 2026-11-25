#include "sqlitex/statement.h"
#include "sqlitex/database.h"
#include "sqlitex/error.h"
#include "sqlitex/value.h"

#include "policy_internal.h"

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

Statement& Statement::bind_zeroblob(int index, int size) {
    throw_on_error(sqlite3_bind_zeroblob(stmt_, index, size), db_->handle());
    return *this;
}

Statement& Statement::bind_zeroblob64(int index, uint64_t size) {
    throw_on_error(sqlite3_bind_zeroblob64(stmt_, index,
                                           static_cast<sqlite3_uint64>(size)),
                   db_->handle());
    return *this;
}

Statement& Statement::bind_static(int index, std::string_view value) {
    throw_on_error(
        sqlite3_bind_text(stmt_, index, value.data(),
                          static_cast<int>(value.size()), SQLITE_STATIC),
        db_->handle());
    return *this;
}

Statement& Statement::bind_static(int index, const void* blob, int size) {
    throw_on_error(sqlite3_bind_blob(stmt_, index, blob, size, SQLITE_STATIC),
                   db_->handle());
    return *this;
}

Statement& Statement::bind_blob64(int index, const void* blob, uint64_t size,
                                  void (*destructor)(void*)) {
    throw_on_error(sqlite3_bind_blob64(
                       stmt_, index, blob, static_cast<sqlite3_uint64>(size),
                       destructor ? destructor : SQLITE_TRANSIENT),
                   db_->handle());
    return *this;
}

Statement& Statement::bind_text64(int index, const char* text, uint64_t size,
                                  unsigned char encoding,
                                  void (*destructor)(void*)) {
    throw_on_error(sqlite3_bind_text64(
                       stmt_, index, text, static_cast<sqlite3_uint64>(size),
                       destructor ? destructor : SQLITE_TRANSIENT, encoding),
                   db_->handle());
    return *this;
}

Statement& Statement::bind_pointer(int index, void* ptr, const char* type_name,
                                   void (*destructor)(void*)) {
    throw_on_error(
        sqlite3_bind_pointer(stmt_, index, ptr, type_name, destructor),
        db_->handle());
    return *this;
}

Statement& Statement::bind_value(int index, const ValueRef& val) {
    throw_on_error(sqlite3_bind_value(stmt_, index, val.handle()),
                   db_->handle());
    return *this;
}

Statement& Statement::bind_value(int index, const ProtectedValue& val) {
    throw_on_error(sqlite3_bind_value(stmt_, index, val.handle()),
                   db_->handle());
    return *this;
}

Statement& Statement::bind_carray(int index, void* data, int count,
                                  CArrayType type, void (*destructor)(void*)) {
    throw_on_error(sqlite3_carray_bind(stmt_, index, data, count,
                                       static_cast<int>(type), destructor),
                   db_->handle());
    return *this;
}

Statement& Statement::bind_carray_v2(int index, void* data, int count,
                                     CArrayType type, void (*destructor)(void*),
                                     void* del_arg) {
    throw_on_error(
        sqlite3_carray_bind_v2(stmt_, index, data, count,
                               static_cast<int>(type), destructor, del_arg),
        db_->handle());
    return *this;
}

Statement& Statement::bind_carray(int index,
                                  const std::vector<int32_t>& values) {
    if (values.empty()) {
        return bind_carray(index, nullptr, 0, CArrayType::Int32, SQLITE_STATIC);
    }
    return bind_carray(index, const_cast<int32_t*>(values.data()),
                       static_cast<int>(values.size()), CArrayType::Int32,
                       SQLITE_TRANSIENT);
}

Statement& Statement::bind_carray(int index,
                                  const std::vector<int64_t>& values) {
    if (values.empty()) {
        return bind_carray(index, nullptr, 0, CArrayType::Int64, SQLITE_STATIC);
    }
    return bind_carray(index, const_cast<int64_t*>(values.data()),
                       static_cast<int>(values.size()), CArrayType::Int64,
                       SQLITE_TRANSIENT);
}

Statement& Statement::bind_carray(int index,
                                  const std::vector<double>& values) {
    if (values.empty()) {
        return bind_carray(index, nullptr, 0, CArrayType::Double,
                           SQLITE_STATIC);
    }
    return bind_carray(index, const_cast<double*>(values.data()),
                       static_cast<int>(values.size()), CArrayType::Double,
                       SQLITE_TRANSIENT);
}

Statement& Statement::bind_carray(int index,
                                  const std::vector<std::string>& values) {
    if (values.empty()) {
        return bind_carray(index, nullptr, 0, CArrayType::Text, SQLITE_STATIC);
    }
    std::vector<const char*> ptrs;
    ptrs.reserve(values.size());
    for (const auto& s : values) {
        ptrs.push_back(s.c_str());
    }
    return bind_carray(index, ptrs.data(), static_cast<int>(ptrs.size()),
                       CArrayType::Text, SQLITE_TRANSIENT);
}

// ============================================================================
// Execution
// ============================================================================

bool Statement::step() {
    // step may reprepare or compile SQL internally, which is authorized.
    detail::AuthSink sink(sqlite3_db_handle(stmt_));
    int const rc = sqlite3_step(stmt_);
    sink.check(rc);
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

int Statement::data_count() const noexcept {
    return sqlite3_data_count(stmt_);
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

std::string_view Statement::column_database_name(int index) const noexcept {
    const char* name = sqlite3_column_database_name(stmt_, index);
    return name ? std::string_view(name) : std::string_view{};
}

std::string_view Statement::column_table_name(int index) const noexcept {
    const char* name = sqlite3_column_table_name(stmt_, index);
    return name ? std::string_view(name) : std::string_view{};
}

std::string_view Statement::column_origin_name(int index) const noexcept {
    const char* name = sqlite3_column_origin_name(stmt_, index);
    return name ? std::string_view(name) : std::string_view{};
}

ValueRef Statement::column_raw_value(int index) const noexcept {
    return ValueRef(sqlite3_column_value(stmt_, index));
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

ExplainMode Statement::explain_mode() const noexcept {
    int m = sqlite3_stmt_isexplain(stmt_);
    if (m == 1) {
        return ExplainMode::Explain;
    }
    if (m == 2) {
        return ExplainMode::QueryPlan;
    }
    return ExplainMode::Normal;
}

void Statement::set_explain(ExplainMode mode) {
    int rc = sqlite3_stmt_explain(stmt_, static_cast<int>(mode));
    throw_on_error(rc, db_ ? db_->handle() : nullptr);
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

std::string_view Statement::normalized_sql() const noexcept {
    const char* s = sqlite3_normalized_sql(stmt_);
    return s ? std::string_view(s) : std::string_view{};
}

int Statement::status(StmtStatusOp op, bool reset) noexcept {
    return sqlite3_stmt_status(stmt_, static_cast<int>(op), reset ? 1 : 0);
}

std::vector<ScanStatusEntry> Statement::scanstatus(int flags) const {
    std::vector<ScanStatusEntry> entries;
    for (int idx = 0;; ++idx) {
        sqlite3_int64 nloop = 0;
        if (sqlite3_stmt_scanstatus_v2(stmt_, idx, SQLITE_SCANSTAT_NLOOP, flags,
                                       &nloop) != 0) {
            break;
        }
        ScanStatusEntry e;
        e.nloop = static_cast<int64_t>(nloop);

        sqlite3_int64 nvisit = 0;
        if (sqlite3_stmt_scanstatus_v2(stmt_, idx, SQLITE_SCANSTAT_NVISIT,
                                       flags, &nvisit) == 0) {
            e.nvisit = static_cast<int64_t>(nvisit);
        }
        double est = 0.0;
        if (sqlite3_stmt_scanstatus_v2(stmt_, idx, SQLITE_SCANSTAT_EST, flags,
                                       &est) == 0) {
            e.est = est;
        }
        const char* name = nullptr;
        if (sqlite3_stmt_scanstatus_v2(stmt_, idx, SQLITE_SCANSTAT_NAME, flags,
                                       &name) == 0 &&
            name != nullptr) {
            e.name = name;
        }
        const char* explain = nullptr;
        if (sqlite3_stmt_scanstatus_v2(stmt_, idx, SQLITE_SCANSTAT_EXPLAIN,
                                       flags, &explain) == 0 &&
            explain != nullptr) {
            e.explain = explain;
        }
        int select_id = 0;
        if (sqlite3_stmt_scanstatus_v2(stmt_, idx, SQLITE_SCANSTAT_SELECTID,
                                       flags, &select_id) == 0) {
            e.select_id = select_id;
        }
        int parent_id = 0;
        if (sqlite3_stmt_scanstatus_v2(stmt_, idx, SQLITE_SCANSTAT_PARENTID,
                                       flags, &parent_id) == 0) {
            e.parent_id = parent_id;
        }
        sqlite3_int64 ncycle = 0;
        if (sqlite3_stmt_scanstatus_v2(stmt_, idx, SQLITE_SCANSTAT_NCYCLE,
                                       flags, &ncycle) == 0) {
            e.ncycle = static_cast<int64_t>(ncycle);
        }

        entries.push_back(std::move(e));
    }
    return entries;
}

void Statement::scanstatus_reset() noexcept {
    sqlite3_stmt_scanstatus_reset(stmt_);
}

} // namespace sqlitex
