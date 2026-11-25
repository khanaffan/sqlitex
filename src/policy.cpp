#include "sqlitex/policy.h"

#include "policy_internal.h"
#include "sqlitex/database.h"
#include "sqlitex/error.h"

#include <sqlite3.h>

#include <algorithm>
#include <cassert>
#include <exception>
#include <string>
#include <utility>

namespace sqlitex {

#define SQLITEX_CHECK_ACTION(name, code)                              \
    static_assert(static_cast<int>(AuthorizerAction::name) == (code), \
                  "AuthorizerAction::" #name " does not match " #code)
SQLITEX_CHECK_ACTION(CreateIndex, SQLITE_CREATE_INDEX);
SQLITEX_CHECK_ACTION(CreateTable, SQLITE_CREATE_TABLE);
SQLITEX_CHECK_ACTION(CreateTempIndex, SQLITE_CREATE_TEMP_INDEX);
SQLITEX_CHECK_ACTION(CreateTempTable, SQLITE_CREATE_TEMP_TABLE);
SQLITEX_CHECK_ACTION(CreateTempTrigger, SQLITE_CREATE_TEMP_TRIGGER);
SQLITEX_CHECK_ACTION(CreateTempView, SQLITE_CREATE_TEMP_VIEW);
SQLITEX_CHECK_ACTION(CreateTrigger, SQLITE_CREATE_TRIGGER);
SQLITEX_CHECK_ACTION(CreateView, SQLITE_CREATE_VIEW);
SQLITEX_CHECK_ACTION(Delete, SQLITE_DELETE);
SQLITEX_CHECK_ACTION(DropIndex, SQLITE_DROP_INDEX);
SQLITEX_CHECK_ACTION(DropTable, SQLITE_DROP_TABLE);
SQLITEX_CHECK_ACTION(DropTempIndex, SQLITE_DROP_TEMP_INDEX);
SQLITEX_CHECK_ACTION(DropTempTable, SQLITE_DROP_TEMP_TABLE);
SQLITEX_CHECK_ACTION(DropTempTrigger, SQLITE_DROP_TEMP_TRIGGER);
SQLITEX_CHECK_ACTION(DropTempView, SQLITE_DROP_TEMP_VIEW);
SQLITEX_CHECK_ACTION(DropTrigger, SQLITE_DROP_TRIGGER);
SQLITEX_CHECK_ACTION(DropView, SQLITE_DROP_VIEW);
SQLITEX_CHECK_ACTION(Insert, SQLITE_INSERT);
SQLITEX_CHECK_ACTION(Pragma, SQLITE_PRAGMA);
SQLITEX_CHECK_ACTION(Read, SQLITE_READ);
SQLITEX_CHECK_ACTION(Select, SQLITE_SELECT);
SQLITEX_CHECK_ACTION(Transaction, SQLITE_TRANSACTION);
SQLITEX_CHECK_ACTION(Update, SQLITE_UPDATE);
SQLITEX_CHECK_ACTION(Attach, SQLITE_ATTACH);
SQLITEX_CHECK_ACTION(Detach, SQLITE_DETACH);
SQLITEX_CHECK_ACTION(AlterTable, SQLITE_ALTER_TABLE);
SQLITEX_CHECK_ACTION(Reindex, SQLITE_REINDEX);
SQLITEX_CHECK_ACTION(Analyze, SQLITE_ANALYZE);
SQLITEX_CHECK_ACTION(CreateVtable, SQLITE_CREATE_VTABLE);
SQLITEX_CHECK_ACTION(DropVtable, SQLITE_DROP_VTABLE);
SQLITEX_CHECK_ACTION(Function, SQLITE_FUNCTION);
SQLITEX_CHECK_ACTION(Savepoint, SQLITE_SAVEPOINT);
SQLITEX_CHECK_ACTION(Recursive, SQLITE_RECURSIVE);
#undef SQLITEX_CHECK_ACTION

namespace {

using Action = AuthorizerAction;
using Result = AuthorizerResult;

char ascii_lower(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool iequals(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (ascii_lower(a[i]) != ascii_lower(b[i])) {
            return false;
        }
    }
    return true;
}

bool iequals(const std::optional<std::string_view>& a,
             std::string_view b) noexcept {
    return a.has_value() && iequals(*a, b);
}

/// Map the sqlite_schema aliases to the names the authorizer reports.
std::string canonical_table(std::string_view name) {
    if (iequals(name, "sqlite_schema")) {
        return "sqlite_master";
    }
    if (iequals(name, "sqlite_temp_schema")) {
        return "sqlite_temp_master";
    }
    return std::string(name);
}

bool table_matches(const std::optional<std::string_view>& reported,
                   const std::string& rule) {
    return reported.has_value() &&
           iequals(std::string_view(canonical_table(*reported)),
                   std::string_view(rule));
}

/// A NULL request schema matches any rule (fail closed); an empty rule
/// schema matches any request.
bool schema_matches(const std::optional<std::string_view>& reported,
                    const std::string& rule) {
    return rule.empty() || !reported.has_value() ||
           iequals(*reported, std::string_view(rule));
}

bool is_starting(const AuthorizerRequest& r) {
    return iequals(r.arg1, "BEGIN");
}

/// Transaction/savepoint requests that end something already open.
bool is_ending(const AuthorizerRequest& r) {
    return (r.action == Action::Transaction || r.action == Action::Savepoint) &&
           !is_starting(r);
}

bool is_redactable(Action a) {
    return a == Action::Read || a == Action::Function || a == Action::Select ||
           a == Action::Recursive;
}

const char* action_name(Action a) {
    static const char* const names[] = {"Copy",
                                        "CreateIndex",
                                        "CreateTable",
                                        "CreateTempIndex",
                                        "CreateTempTable",
                                        "CreateTempTrigger",
                                        "CreateTempView",
                                        "CreateTrigger",
                                        "CreateView",
                                        "Delete",
                                        "DropIndex",
                                        "DropTable",
                                        "DropTempIndex",
                                        "DropTempTable",
                                        "DropTempTrigger",
                                        "DropTempView",
                                        "DropTrigger",
                                        "DropView",
                                        "Insert",
                                        "Pragma",
                                        "Read",
                                        "Select",
                                        "Transaction",
                                        "Update",
                                        "Attach",
                                        "Detach",
                                        "AlterTable",
                                        "Reindex",
                                        "Analyze",
                                        "CreateVtable",
                                        "DropVtable",
                                        "Function",
                                        "Savepoint",
                                        "Recursive"};
    auto i = static_cast<int>(a);
    if (i < 0 || i >= static_cast<int>(sizeof(names) / sizeof(names[0]))) {
        return "Unknown";
    }
    return names[i];
}

std::string describe(const AuthorizerRequest& r) {
    std::string out = action_name(r.action);
    out += '(';
    out += std::to_string(static_cast<int>(r.action));
    out += ')';
    auto field = [&out](const char* label,
                        const std::optional<std::string_view>& v) {
        if (v) {
            out += ' ';
            out += label;
            out += "=\"";
            out.append(v->data(), v->size());
            out += '"';
        }
    };
    field("arg1", r.arg1);
    field("arg2", r.arg2);
    field("database", r.database);
    field("source", r.source);
    return out;
}

std::optional<std::string_view> opt(const char* s) {
    return s ? std::optional<std::string_view>(s) : std::nullopt;
}

/// The argument naming the table for actions that carry one.
std::optional<std::string_view> table_arg(const AuthorizerRequest& r) {
    switch (r.action) {
    case Action::CreateTable:
    case Action::CreateTempTable:
    case Action::CreateView:
    case Action::CreateTempView:
    case Action::DropTable:
    case Action::DropTempTable:
    case Action::DropView:
    case Action::DropTempView:
    case Action::Insert:
    case Action::Update:
    case Action::Delete:
    case Action::Read:
    case Action::Analyze:
    case Action::CreateVtable:
    case Action::DropVtable:
        return r.arg1;
    case Action::CreateIndex:
    case Action::CreateTempIndex:
    case Action::DropIndex:
    case Action::DropTempIndex:
    case Action::CreateTrigger:
    case Action::CreateTempTrigger:
    case Action::DropTrigger:
    case Action::DropTempTrigger:
    case Action::AlterTable:
        return r.arg2;
    default:
        return std::nullopt;
    }
}

std::optional<std::string_view> schema_arg(const AuthorizerRequest& r) {
    // ALTER TABLE reports the schema in arg1.
    return r.action == Action::AlterTable ? r.arg1 : r.database;
}

bool is_read_only_pragma(const std::optional<std::string_view>& name) {
    static const char* const allowed[] = {
        "table_info",  "table_xinfo",      "index_list",  "index_info",
        "index_xinfo", "foreign_key_list", "data_version"};
    return std::any_of(std::begin(allowed), std::end(allowed),
                       [&](const char* p) { return iequals(name, p); });
}

[[noreturn]] void misuse(const std::string& msg) {
    throw MisuseError(ErrorCode::Misuse, msg);
}

// ----------------------------------------------------------------------------
// Thread-local sink stack
// ----------------------------------------------------------------------------

thread_local detail::AuthSink* tls_top_sink = nullptr;

} // anonymous namespace

// ============================================================================
// detail::PolicyAccess — evaluation shared by the shim and BlobIO
// ============================================================================

namespace detail {

struct PolicyAccess {
    static ScopedPolicy* top(Database& db) noexcept {
        return db.policy_;
    }
    static void set_top(Database& db, ScopedPolicy* p) noexcept {
        db.policy_ = p;
    }
    static sqlite3* top_handle(const ScopedPolicy& p) noexcept {
        return p.db_->handle();
    }

    /// Built-ins, then callbacks.  First Deny wins, otherwise any Ignore.
    static Result evaluate(const ScopedPolicy& p, const AuthorizerRequest& r) {
        for (const auto& rule : p.builtins_) {
            if (rule(r) == Result::Deny) {
                return Result::Deny;
            }
        }
        Result combined = Result::Allow;
        for (const auto& cb : p.callbacks_) {
            Result res = cb(r);
            switch (res) {
            case Result::Allow:
                break;
            case Result::Ignore:
                combined = Result::Ignore;
                break;
            case Result::Deny:
                return Result::Deny;
            default:
                misuse(
                    "authorizer callback returned an invalid "
                    "AuthorizerResult (" +
                    std::to_string(static_cast<int>(res)) + ")");
            }
        }
        return combined;
    }

    /// evaluate() plus Ignore normalization for SQL compiled by SQLite.
    static Result decide(const ScopedPolicy& p, sqlite3* db,
                         const AuthorizerRequest& r) {
        Result res = evaluate(p, r);
        if (res == Result::Ignore &&
            !(p.read_only_ && is_redactable(r.action) &&
              !AuthSink::ignore_forbidden(db))) {
            res = Result::Deny;
        }
        return res;
    }
};

// ----------------------------------------------------------------------------
// AuthSink
// ----------------------------------------------------------------------------

AuthSink::AuthSink(sqlite3* db, bool forbid_ignore) noexcept
    : db_(db), forbid_ignore_(forbid_ignore), prev_(tls_top_sink) {
    tls_top_sink = this;
}

AuthSink::~AuthSink() {
    assert(tls_top_sink == this);
    tls_top_sink = prev_;
}

AuthSink* AuthSink::find(sqlite3* db) noexcept {
    for (AuthSink* s = tls_top_sink; s != nullptr; s = s->prev_) {
        if (s->db_ == db) {
            return s;
        }
    }
    return nullptr;
}

bool AuthSink::ignore_forbidden(sqlite3* db) noexcept {
    for (AuthSink* s = tls_top_sink; s != nullptr; s = s->prev_) {
        if (s->db_ == db && s->forbid_ignore_) {
            return true;
        }
    }
    return false;
}

void AuthSink::capture(std::exception_ptr exc) noexcept {
    if (!exception_) {
        exception_ = std::move(exc);
    }
}

void AuthSink::record_denial(const AuthorizerRequest& request) noexcept {
    if (denied_) {
        return;
    }
    denied_ = true;
    try {
        denial_ = describe(request);
    } catch (...) {
        denial_.clear(); // allocation failure: keep the flag, drop the text
    }
}

void AuthSink::check(int rc, const char* message) {
    if (exception_) {
        std::exception_ptr exc = std::exchange(exception_, nullptr);
        std::rethrow_exception(exc);
    }
    int const primary = rc & 0xFF;
    if (denied_ && (primary == SQLITE_AUTH || primary == SQLITE_ERROR)) {
        std::string msg =
            message ? message : (db_ ? sqlite3_errmsg(db_) : "not authorized");
        msg += " [denied by policy: ";
        msg += denial_;
        msg += ']';
        throw AuthError(SQLITE_AUTH, std::move(msg));
    }
}

// ----------------------------------------------------------------------------
// BlobIO check
// ----------------------------------------------------------------------------

void check_blob_access(Database& db, std::string_view schema,
                       std::string_view table, std::string_view column,
                       bool writable) {
    ScopedPolicy* p = PolicyAccess::top(db);
    if (p == nullptr) {
        return;
    }
    auto check_one = [&](Action action) {
        AuthorizerRequest r{action, table, column, schema, std::nullopt};
        if (PolicyAccess::evaluate(*p, r) != Result::Allow) {
            throw AuthError(SQLITE_AUTH,
                            "blob " + std::string(writable ? "write" : "read") +
                                " access to " + std::string(table) + "." +
                                std::string(column) +
                                " is prohibited by policy [denied by policy: " +
                                describe(r) + "]");
        }
    };
    check_one(Action::Read);
    if (writable) {
        check_one(Action::Update);
    }
}

} // namespace detail

// ============================================================================
// C shim
// ============================================================================

namespace {

int authorizer_shim(void* user, int code, const char* a1, const char* a2,
                    const char* db_name, const char* source) noexcept {
    auto* policy = static_cast<ScopedPolicy*>(user);
    AuthorizerRequest req{static_cast<Action>(code), opt(a1), opt(a2),
                          opt(db_name), opt(source)};
    sqlite3* handle = nullptr;
    detail::AuthSink* sink = nullptr;
    try {
        handle = detail::PolicyAccess::top_handle(*policy);
        sink = detail::AuthSink::find(handle);
        switch (detail::PolicyAccess::decide(*policy, handle, req)) {
        case Result::Allow:
            return SQLITE_OK;
        case Result::Ignore:
            return SQLITE_IGNORE;
        default:
            break;
        }
    } catch (...) {
        // Without a sink (raw handle() use) the exception is dropped.
        if (sink != nullptr) {
            sink->capture(std::current_exception());
        }
    }
    if (sink != nullptr) {
        sink->record_denial(req);
    }
    return SQLITE_DENY;
}

} // anonymous namespace

// ============================================================================
// ScopedPolicy
// ============================================================================

ScopedPolicy::ScopedPolicy(Database& db) : db_(&db) {
    if (!db) {
        misuse("ScopedPolicy requires an open database");
    }
    prev_ = detail::PolicyAccess::top(db);
    int rc = sqlite3_set_authorizer(db.handle(), authorizer_shim, this);
    throw_on_error(rc, db.handle());
    detail::PolicyAccess::set_top(db, this);
}

ScopedPolicy::ScopedPolicy(Database& db, AuthorizerCallback callback)
    : ScopedPolicy(db) {
    add(std::move(callback));
}

ScopedPolicy::~ScopedPolicy() {
    assert(detail::PolicyAccess::top(*db_) == this &&
           "ScopedPolicy guards must be destroyed in LIFO order");
    assert(static_cast<bool>(*db_) &&
           "Database closed while a ScopedPolicy is alive");
    sqlite3* h = db_->handle();

    for (auto it = saved_.rbegin(); it != saved_.rend(); ++it) {
        if (it->is_limit) {
            sqlite3_limit(h, it->id, it->value);
        } else if (sqlite3_db_config(h, it->id, it->value, nullptr) !=
                   SQLITE_OK) {
            std::terminate();
        }
    }

    detail::PolicyAccess::set_top(*db_, prev_);
    int rc = prev_ != nullptr
                 ? sqlite3_set_authorizer(h, authorizer_shim, prev_)
                 : sqlite3_set_authorizer(h, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        std::terminate();
    }
}

void ScopedPolicy::refresh() {
    // Re-registering expires prepared statements so they are re-authorized
    // against the updated policy at their next step.
    if (detail::PolicyAccess::top(*db_) == this) {
        int rc = sqlite3_set_authorizer(db_->handle(), authorizer_shim, this);
        throw_on_error(rc, db_->handle());
    }
}

ScopedPolicy& ScopedPolicy::add(AuthorizerCallback callback) {
    if (!callback) {
        misuse("ScopedPolicy::add requires a non-empty callback");
    }
    callbacks_.push_back(std::move(callback));
    try {
        refresh();
    } catch (...) {
        callbacks_.pop_back();
        throw;
    }
    return *this;
}

void ScopedPolicy::add_builtin(AuthorizerCallback rule) {
    builtins_.push_back(std::move(rule));
    try {
        refresh();
    } catch (...) {
        builtins_.pop_back();
        throw;
    }
}

ScopedPolicy& ScopedPolicy::deny(AuthorizerAction action) {
    if (action == Action::Transaction || action == Action::Savepoint) {
        add_builtin([action](const AuthorizerRequest& r) {
            return (r.action == action && is_starting(r)) ? Result::Deny
                                                          : Result::Allow;
        });
    } else {
        add_builtin([action](const AuthorizerRequest& r) {
            return r.action == action ? Result::Deny : Result::Allow;
        });
    }
    return *this;
}

ScopedPolicy& ScopedPolicy::deny_table(std::string_view name,
                                       std::string_view schema) {
    add_builtin([name = canonical_table(name),
                 schema = std::string(schema)](const AuthorizerRequest& r) {
        if (!table_matches(table_arg(r), name)) {
            return Result::Allow;
        }
        // A TEMP trigger reports "temp" even when it targets another schema.
        bool const any_schema = r.action == Action::CreateTempTrigger ||
                                r.action == Action::DropTempTrigger;
        return (any_schema || schema_matches(schema_arg(r), schema))
                   ? Result::Deny
                   : Result::Allow;
    });
    return *this;
}

ScopedPolicy& ScopedPolicy::deny_column(std::string_view table,
                                        std::string_view column,
                                        std::string_view schema) {
    add_builtin([table = canonical_table(table), column = std::string(column),
                 schema = std::string(schema)](const AuthorizerRequest& r) {
        bool hit = false;
        if (r.action == Action::Read || r.action == Action::Update) {
            hit = table_matches(r.arg1, table) && iequals(r.arg2, column) &&
                  schema_matches(r.database, schema);
        } else if (r.action == Action::AlterTable) {
            // DROP COLUMN and ALTER COLUMN ... NOT NULL report the column in
            // the database slot.
            hit = table_matches(r.arg2, table) && iequals(r.database, column) &&
                  schema_matches(r.arg1, schema);
        }
        return hit ? Result::Deny : Result::Allow;
    });
    return *this;
}

ScopedPolicy& ScopedPolicy::deny_function(std::string_view name) {
    add_builtin([name = std::string(name)](const AuthorizerRequest& r) {
        return (r.action == Action::Function && iequals(r.arg2, name))
                   ? Result::Deny
                   : Result::Allow;
    });
    return *this;
}

ScopedPolicy& ScopedPolicy::deny_pragma(std::string_view name) {
    add_builtin([name = std::string(name)](const AuthorizerRequest& r) {
        return (r.action == Action::Pragma && iequals(r.arg1, name))
                   ? Result::Deny
                   : Result::Allow;
    });
    return *this;
}

ScopedPolicy& ScopedPolicy::read_only() {
    add_builtin([](const AuthorizerRequest& r) {
        switch (r.action) {
        case Action::Read:
        case Action::Select:
        case Action::Function:
        case Action::Recursive:
        case Action::Transaction:
        case Action::Savepoint:
            return Result::Allow;
        case Action::Pragma:
            return is_read_only_pragma(r.arg1) ? Result::Allow : Result::Deny;
        default:
            return Result::Deny;
        }
    });
    read_only_ = true;
    return *this;
}

ScopedPolicy& ScopedPolicy::allow_only(
    std::span<const AuthorizerAction> actions) {
    std::vector<AuthorizerAction> allowed(actions.begin(), actions.end());
    add_builtin([allowed = std::move(allowed)](const AuthorizerRequest& r) {
        if (is_ending(r) || std::find(allowed.begin(), allowed.end(),
                                      r.action) != allowed.end()) {
            return Result::Allow;
        }
        return Result::Deny;
    });
    return *this;
}

// ============================================================================
// Hardening
// ============================================================================

void ScopedPolicy::require_active() const {
    if (detail::PolicyAccess::top(*db_) != this) {
        misuse(
            "hardening settings can only be changed on the innermost "
            "active ScopedPolicy");
    }
}

ScopedPolicy& ScopedPolicy::set_db_config(int op, bool on) {
    require_active();
    sqlite3* h = db_->handle();
    int current = 0;
    throw_on_error(sqlite3_db_config(h, op, -1, &current), h);

    bool const first = std::none_of(
        saved_.begin(), saved_.end(),
        [op](const SavedSetting& s) { return !s.is_limit && s.id == op; });
    if (first) {
        saved_.reserve(saved_.size() + 1); // push_back below cannot throw
    }

    int const wanted = on ? 1 : 0;
    int applied = -1;
    int rc = sqlite3_db_config(h, op, wanted, &applied);
    if (rc != SQLITE_OK || applied != wanted) {
        sqlite3_db_config(h, op, current, nullptr);
        throw Error(rc != SQLITE_OK ? rc : SQLITE_ERROR,
                    "sqlite3_db_config(" + std::to_string(op) +
                        ") could not be applied");
    }
    if (first) {
        saved_.push_back({false, op, current});
    }
    return *this;
}

ScopedPolicy& ScopedPolicy::defensive(bool on) {
    return set_db_config(SQLITE_DBCONFIG_DEFENSIVE, on);
}

ScopedPolicy& ScopedPolicy::trusted_schema(bool on) {
    return set_db_config(SQLITE_DBCONFIG_TRUSTED_SCHEMA, on);
}

ScopedPolicy& ScopedPolicy::triggers_enabled(bool on) {
    return set_db_config(SQLITE_DBCONFIG_ENABLE_TRIGGER, on);
}

ScopedPolicy& ScopedPolicy::views_enabled(bool on) {
    return set_db_config(SQLITE_DBCONFIG_ENABLE_VIEW, on);
}

ScopedPolicy& ScopedPolicy::attach_create(bool on) {
    return set_db_config(SQLITE_DBCONFIG_ENABLE_ATTACH_CREATE, on);
}

ScopedPolicy& ScopedPolicy::attach_write(bool on) {
    return set_db_config(SQLITE_DBCONFIG_ENABLE_ATTACH_WRITE, on);
}

ScopedPolicy& ScopedPolicy::max_attached(int n) {
    if (n < 0) {
        misuse("max_attached requires a non-negative limit");
    }
    require_active();
    sqlite3* h = db_->handle();
    bool const first =
        std::none_of(saved_.begin(), saved_.end(), [](const SavedSetting& s) {
            return s.is_limit && s.id == SQLITE_LIMIT_ATTACHED;
        });
    if (first) {
        saved_.reserve(saved_.size() + 1);
    }
    int const previous = sqlite3_limit(h, SQLITE_LIMIT_ATTACHED, n);
    if (first) {
        saved_.push_back({true, SQLITE_LIMIT_ATTACHED, previous});
    }
    return *this;
}

} // namespace sqlitex
