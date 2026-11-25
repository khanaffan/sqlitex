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
// Operations
// ============================================================================

void Changegroup::add(const Changeset& cs) {
    int const rc =
        sqlite3changegroup_add(group_, cs.size(), const_cast<void*>(cs.data()));
    throw_on_error(rc, "sqlite3changegroup_add failed");
}

void Changegroup::add(const Patchset& ps) {
    int const rc =
        sqlite3changegroup_add(group_, ps.size(), const_cast<void*>(ps.data()));
    throw_on_error(rc, "sqlite3changegroup_add failed");
}

void Changegroup::add_change(const ChangesetIterator& iter) {
    int const rc = sqlite3changegroup_add_change(group_, iter.handle());
    throw_on_error(rc, "sqlite3changegroup_add_change failed");
}

// ============================================================================
// Programmatic Change Construction
// ============================================================================

void Changegroup::change_begin(ChangeOperation op, std::string_view table,
                               bool indirect) {
    std::string tbl(table);
    char* err_msg = nullptr;
    int const rc = sqlite3changegroup_change_begin(
        group_, static_cast<int>(op), tbl.c_str(), indirect ? 1 : 0, &err_msg);
    throw_with_errmsg(rc, err_msg, "sqlite3changegroup_change_begin failed");
}

void Changegroup::change_int64(bool is_new, int col, int64_t val) {
    int const rc =
        sqlite3changegroup_change_int64(group_, is_new ? 1 : 0, col, val);
    throw_on_error(rc, "sqlite3changegroup_change_int64 failed");
}

void Changegroup::change_double(bool is_new, int col, double val) {
    int const rc =
        sqlite3changegroup_change_double(group_, is_new ? 1 : 0, col, val);
    throw_on_error(rc, "sqlite3changegroup_change_double failed");
}

void Changegroup::change_null(bool is_new, int col) {
    int const rc = sqlite3changegroup_change_null(group_, is_new ? 1 : 0, col);
    throw_on_error(rc, "sqlite3changegroup_change_null failed");
}

void Changegroup::change_text(bool is_new, int col, std::string_view val) {
    int const rc = sqlite3changegroup_change_text(group_, is_new ? 1 : 0, col,
                                                  val.empty() ? "" : val.data(),
                                                  static_cast<int>(val.size()));
    throw_on_error(rc, "sqlite3changegroup_change_text failed");
}

void Changegroup::change_blob(bool is_new, int col, const void* data,
                              int size) {
    static const uint8_t kEmpty = 0;
    const void* ptr =
        (data != nullptr) ? data : static_cast<const void*>(&kEmpty);
    int const rc =
        sqlite3changegroup_change_blob(group_, is_new ? 1 : 0, col, ptr, size);
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
