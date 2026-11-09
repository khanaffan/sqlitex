#include "sqlitex/session.h"
#include "sqlitex/changeset.h"
#include "sqlitex/database.h"
#include "sqlitex/error.h"

#include "policy_internal.h"

#include <sqlite3.h>

#include <cstring>
#include <string>
#include <utility>

#ifdef SQLITEX_HAS_SESSION

namespace sqlitex {

// ============================================================================
// C callback shims
// ============================================================================

namespace {

int table_filter_shim(void* ctx, const char* tbl_name) {
    auto& fn = *static_cast<SessionTableFilter*>(ctx);
    return fn(std::string_view(tbl_name ? tbl_name : "")) ? 1 : 0;
}

} // anonymous namespace

// ============================================================================
// Construction / Destruction
// ============================================================================

Session::Session(Database& db, std::string_view db_name) : db_(&db) {
    std::string name(db_name);
    sqlite3_session* s = nullptr;

    int rc = sqlite3session_create(db.handle(), name.c_str(), &s);
    throw_on_error(rc, db.handle());

    session_ = s;
}

Session::~Session() {
    if (session_ != nullptr) {
        sqlite3session_delete(session_);
    }
}

Session::Session(Session&& other) noexcept
    : session_(std::exchange(other.session_, nullptr)),
      db_(std::exchange(other.db_, nullptr)),
      filter_cb_(std::move(other.filter_cb_)) {
    if (filter_cb_ && session_) {
        sqlite3session_table_filter(session_, table_filter_shim,
                                    static_cast<void*>(filter_cb_.get()));
    }
}

Session& Session::operator=(Session&& other) noexcept {
    if (this != &other) {
        if (session_ != nullptr) {
            sqlite3session_delete(session_);
        }
        session_ = std::exchange(other.session_, nullptr);
        db_ = std::exchange(other.db_, nullptr);
        filter_cb_ = std::move(other.filter_cb_);
        if (filter_cb_ && session_) {
            sqlite3session_table_filter(session_, table_filter_shim,
                                        static_cast<void*>(filter_cb_.get()));
        }
    }
    return *this;
}

// ============================================================================
// Control
// ============================================================================

bool Session::enable(bool enabled) {
    // Query the current state without modifying it (negative argument), then
    // apply the requested state. sqlite3session_enable() returns the state
    // *after* the call, so a positive argument cannot report the prior state.
    int const prev = sqlite3session_enable(session_, -1);
    sqlite3session_enable(session_, enabled ? 1 : 0);
    return prev != 0;
}

bool Session::set_indirect(bool indirect) {
    int const prev = sqlite3session_indirect(session_, -1);
    sqlite3session_indirect(session_, indirect ? 1 : 0);
    return prev != 0;
}

// ============================================================================
// Table Attachment
// ============================================================================

void Session::attach(std::string_view table_name) {
    std::string tbl(table_name);
    const char* tbl_ptr = tbl.empty() ? nullptr : tbl.c_str();
    detail::AuthSink sink(db_->handle(), true);
    int const rc = sqlite3session_attach(session_, tbl_ptr);
    sink.check(rc);
    throw_on_error(rc, db_->handle());
}

void Session::attach_all() {
    detail::AuthSink sink(db_->handle(), true);
    int const rc = sqlite3session_attach(session_, nullptr);
    sink.check(rc);
    throw_on_error(rc, db_->handle());
}

void Session::set_table_filter(SessionTableFilter filter) {
    filter_cb_ = std::make_unique<SessionTableFilter>(std::move(filter));
    sqlite3session_table_filter(session_, table_filter_shim,
                                static_cast<void*>(filter_cb_.get()));
}

// ============================================================================
// Changeset / Patchset Extraction
// ============================================================================

Changeset Session::changeset() {
    void* data = nullptr;
    int n = 0;

    detail::AuthSink sink(db_->handle(), true);
    int const rc = sqlite3session_changeset(session_, &n, &data);
    sink.check(rc);
    throw_on_error(rc, db_->handle());

    return {data, n};
}

Patchset Session::patchset() {
    void* data = nullptr;
    int n = 0;

    detail::AuthSink sink(db_->handle(), true);
    int const rc = sqlite3session_patchset(session_, &n, &data);
    sink.check(rc);
    throw_on_error(rc, db_->handle());

    return {data, n};
}

Changeset Session::diff(std::string_view table_name, std::string_view from_db) {
    std::string tbl(table_name);
    std::string from(from_db);
    char* err_msg = nullptr;

    // sqlite3session_diff APPENDS the diff to the session's existing change
    // set rather than replacing it.  A subsequent call to changeset() will
    // therefore return all previously accumulated changes PLUS the diff
    // computed here.  If you want only the diff, call this on a freshly
    // created session that has no other changes attached.
    detail::AuthSink sink(db_->handle(), true);
    int rc = sqlite3session_diff(session_, from.c_str(), tbl.c_str(), &err_msg);
    if (rc != SQLITE_OK) {
        std::string msg(err_msg ? err_msg : "diff failed");
        if (err_msg != nullptr) {
            sqlite3_free(err_msg);
        }
        sink.check(rc, msg.c_str());
        throw Error(map_error_code(rc), std::move(msg));
    }

    // After diff, extract the changeset
    return changeset();
}

// ============================================================================
// Streaming Extraction
// ============================================================================

void Session::changeset_stream(StreamOutputFunc output) {
    void* ctx = static_cast<void*>(&output);
    detail::AuthSink sink(db_->handle(), true);
    int const rc = sqlite3session_changeset_strm(
        session_, detail::stream_output_shim, ctx);
    sink.check(rc);
    throw_on_error(rc, db_->handle());
}

void Session::patchset_stream(StreamOutputFunc output) {
    void* ctx = static_cast<void*>(&output);
    detail::AuthSink sink(db_->handle(), true);
    int const rc = sqlite3session_patchset_strm(
        session_, detail::stream_output_shim, ctx);
    sink.check(rc);
    throw_on_error(rc, db_->handle());
}

void Session::changeset_to_stream(std::ostream& os) {
    changeset_stream(make_stream_output(os));
}

void Session::patchset_to_stream(std::ostream& os) {
    patchset_stream(make_stream_output(os));
}

// ============================================================================
// Introspection
// ============================================================================

bool Session::is_empty() const noexcept {
    return sqlite3session_isempty(session_) != 0;
}

int64_t Session::memory_used() const noexcept {
    return sqlite3session_memory_used(session_);
}

void Session::set_object_config(int op, void* arg) {
    throw_on_error(sqlite3session_object_config(session_, op, arg),
                   db_->handle());
}

} // namespace sqlitex

#endif // SQLITEX_HAS_SESSION
