#include "sqlitex/changegroup.h"
#include "sqlitex/database.h"
#include "sqlitex/error.h"

#include <sqlite3.h>

#include <string>
#include <utility>

#ifdef SQLITEX_HAS_SESSION

namespace sqlitex {

namespace {

void throw_with_errmsg(int rc, char* err_msg, const char* fallback) {
    if (rc == SQLITE_OK) {
        if (err_msg != nullptr) {
            sqlite3_free(err_msg);
        }
        return;
    }
    std::string msg = err_msg ? std::string(err_msg) : std::string(fallback);
    if (err_msg != nullptr) {
        sqlite3_free(err_msg);
    }
    throw Error(map_error_code(rc), std::move(msg));
}

} // anonymous namespace

// ============================================================================
// Construction / Destruction
// ============================================================================

Changegroup::Changegroup() {
    sqlite3_changegroup* g = nullptr;
    int const rc = sqlite3changegroup_new(&g);
    throw_on_error(rc, "sqlite3changegroup_new failed");
    group_ = g;
}

Changegroup::~Changegroup() {
    if (group_ != nullptr) {
        sqlite3changegroup_delete(group_);
    }
}

Changegroup::Changegroup(Changegroup&& other) noexcept
    : group_(std::exchange(other.group_, nullptr)) {
}

Changegroup& Changegroup::operator=(Changegroup&& other) noexcept {
    if (this != &other) {
        if (group_ != nullptr) {
            sqlite3changegroup_delete(group_);
        }
        group_ = std::exchange(other.group_, nullptr);
    }
    return *this;
}

// ============================================================================
// Configuration & Schema
// ============================================================================

void Changegroup::config(int op, void* arg) {
    throw_on_error(sqlite3changegroup_config(group_, op, arg),
                   "sqlite3changegroup_config failed");
}

bool Changegroup::set_patchset_mode(bool patchset) {
    int val = patchset ? 1 : 0;
    throw_on_error(sqlite3changegroup_config(
                       group_, SQLITE_CHANGEGROUP_CONFIG_PATCHSET, &val),
                   "sqlite3changegroup_config(PATCHSET) failed");
    return val != 0;
}

bool Changegroup::is_patchset_mode() {
    int val = -1;
    throw_on_error(sqlite3changegroup_config(
                       group_, SQLITE_CHANGEGROUP_CONFIG_PATCHSET, &val),
                   "sqlite3changegroup_config(PATCHSET) failed");
    return val != 0;
}

void Changegroup::set_schema(Database& db, std::string_view db_name) {
    std::string name(db_name);
    int const rc = sqlite3changegroup_schema(group_, db.handle(), name.c_str());
    throw_on_error(rc, db.handle());
}

// ============================================================================
// ChangeBuilder (RAII programmatic change construction)
// ============================================================================

Changegroup::ChangeBuilder::~ChangeBuilder() {
    if (group_ != nullptr) {
        try {
            group_->change_finish(true);
        } catch (...) {
        }
    }
}

Changegroup::ChangeBuilder::ChangeBuilder(ChangeBuilder&& other) noexcept
    : group_(std::exchange(other.group_, nullptr)) {
}

Changegroup::ChangeBuilder& Changegroup::ChangeBuilder::operator=(
    ChangeBuilder&& other) noexcept {
    if (this != &other) {
        if (group_ != nullptr) {
            try {
                group_->change_finish(true);
            } catch (...) {
            }
        }
        group_ = std::exchange(other.group_, nullptr);
    }
    return *this;
}

Changegroup::ChangeBuilder& Changegroup::ChangeBuilder::set_null(ChangeRow row,
                                                                 int col) {
    group_->change_null(row, col);
    return *this;
}

Changegroup::ChangeBuilder& Changegroup::ChangeBuilder::set_int(ChangeRow row,
                                                                int col,
                                                                int64_t val) {
    group_->change_int64(row, col, val);
    return *this;
}

Changegroup::ChangeBuilder& Changegroup::ChangeBuilder::set_double(
    ChangeRow row, int col, double val) {
    group_->change_double(row, col, val);
    return *this;
}

Changegroup::ChangeBuilder& Changegroup::ChangeBuilder::set_text(
    ChangeRow row, int col, std::string_view val) {
    group_->change_text(row, col, val);
    return *this;
}

Changegroup::ChangeBuilder& Changegroup::ChangeBuilder::set_blob(
    ChangeRow row, int col, std::span<const std::byte> data) {
    group_->change_blob(row, col, data);
    return *this;
}

Changegroup::ChangeBuilder& Changegroup::ChangeBuilder::set(ChangeRow row,
                                                            int col,
                                                            const Value& val) {
    switch (val.type()) {
    case Value::Type::Null:
        return set_null(row, col);
    case Value::Type::Integer:
        return set_int(row, col, val.get_int());
    case Value::Type::Float:
        return set_double(row, col, val.get_double());
    case Value::Type::Text:
        return set_text(row, col, val.get_text());
    case Value::Type::Blob:
        return set_blob(row, col, val.as_bytes());
    }
    return *this;
}

void Changegroup::ChangeBuilder::commit() {
    if (group_ != nullptr) {
        Changegroup* g = std::exchange(group_, nullptr);
        g->change_finish(false);
    }
}

void Changegroup::ChangeBuilder::discard() {
    if (group_ != nullptr) {
        Changegroup* g = std::exchange(group_, nullptr);
        g->change_finish(true);
    }
}

// ============================================================================
// Operations
// ============================================================================

void Changegroup::add(const Changeset& cs) {
    int const rc = sqlite3changegroup_add(group_, static_cast<int>(cs.size()),
                                          const_cast<void*>(cs.data()));
    throw_on_error(rc, "sqlite3changegroup_add failed");
}

void Changegroup::add(const Patchset& ps) {
    int const rc = sqlite3changegroup_add(group_, static_cast<int>(ps.size()),
                                          const_cast<void*>(ps.data()));
    throw_on_error(rc, "sqlite3changegroup_add failed");
}

void Changegroup::add_change(const ChangesetIterator& iter) {
    int const rc = sqlite3changegroup_add_change(group_, iter.handle());
    throw_on_error(rc, "sqlite3changegroup_add_change failed");
}

// ============================================================================
// Programmatic Change Construction
// ============================================================================

Changegroup::ChangeBuilder Changegroup::begin_change(ChangeOperation op,
                                                     std::string_view table,
                                                     bool indirect) {
    change_begin(op, table, indirect);
    return ChangeBuilder(this);
}

void Changegroup::change_begin(ChangeOperation op, std::string_view table,
                               bool indirect) {
    std::string tbl(table);
    char* err_msg = nullptr;
    int const rc = sqlite3changegroup_change_begin(
        group_, static_cast<int>(op), tbl.c_str(), indirect ? 1 : 0, &err_msg);
    throw_with_errmsg(rc, err_msg, "sqlite3changegroup_change_begin failed");
}

void Changegroup::change_int64(ChangeRow row, int col, int64_t val) {
    int const rc = sqlite3changegroup_change_int64(
        group_, static_cast<int>(row), col, val);
    throw_on_error(rc, "sqlite3changegroup_change_int64 failed");
}

void Changegroup::change_double(ChangeRow row, int col, double val) {
    int const rc = sqlite3changegroup_change_double(
        group_, static_cast<int>(row), col, val);
    throw_on_error(rc, "sqlite3changegroup_change_double failed");
}

void Changegroup::change_null(ChangeRow row, int col) {
    int const rc =
        sqlite3changegroup_change_null(group_, static_cast<int>(row), col);
    throw_on_error(rc, "sqlite3changegroup_change_null failed");
}

void Changegroup::change_text(ChangeRow row, int col, std::string_view val) {
    int const rc = sqlite3changegroup_change_text(
        group_, static_cast<int>(row), col, val.empty() ? "" : val.data(),
        static_cast<int>(val.size()));
    throw_on_error(rc, "sqlite3changegroup_change_text failed");
}

void Changegroup::change_blob(ChangeRow row, int col,
                              std::span<const std::byte> data) {
    static const uint8_t kEmpty = 0;
    const void* ptr = !data.empty() ? static_cast<const void*>(data.data())
                                    : static_cast<const void*>(&kEmpty);
    int const rc = sqlite3changegroup_change_blob(
        group_, static_cast<int>(row), col, ptr, static_cast<int>(data.size()));
    throw_on_error(rc, "sqlite3changegroup_change_blob failed");
}

void Changegroup::change_finish(bool discard) {
    char* err_msg = nullptr;
    int const rc =
        sqlite3changegroup_change_finish(group_, discard ? 1 : 0, &err_msg);
    throw_with_errmsg(rc, err_msg, "sqlite3changegroup_change_finish failed");
}

Changeset Changegroup::output() {
    void* data = nullptr;
    int n = 0;

    int const rc = sqlite3changegroup_output(group_, &n, &data);
    throw_on_error(rc, "sqlite3changegroup_output failed");

    return {data, n};
}

Patchset Changegroup::output_patchset() {
    void* data = nullptr;
    int n = 0;

    int const rc = sqlite3changegroup_output(group_, &n, &data);
    throw_on_error(rc, "sqlite3changegroup_output failed");

    return {data, n};
}

// ============================================================================
// Streaming Operations
// ============================================================================

void Changegroup::add_stream(StreamInputFunc input) {
    void* ctx = static_cast<void*>(&input);
    int const rc =
        sqlite3changegroup_add_strm(group_, detail::stream_input_shim, ctx);
    throw_on_error(rc, "sqlite3changegroup_add_strm failed");
}

void Changegroup::output_stream(StreamOutputFunc output) {
    void* ctx = static_cast<void*>(&output);
    int const rc =
        sqlite3changegroup_output_strm(group_, detail::stream_output_shim, ctx);
    throw_on_error(rc, "sqlite3changegroup_output_strm failed");
}

} // namespace sqlitex

#endif // SQLITEX_HAS_SESSION
