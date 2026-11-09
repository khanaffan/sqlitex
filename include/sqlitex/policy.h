#pragma once

#include "sqlitex_export.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sqlitex {

class Database;

namespace detail {
struct PolicyAccess;
} // namespace detail

// ============================================================================
// Authorizer request types
// ============================================================================

/// SQLite authorizer action codes (SQLITE_CREATE_INDEX ... SQLITE_RECURSIVE).
/// Values match SQLite's numeric codes.  Codes added by future SQLite
/// releases may be reported with values outside this list.
enum class AuthorizerAction : int {
    Copy = 0, ///< Legacy code; never emitted by current SQLite.
    CreateIndex = 1,
    CreateTable = 2,
    CreateTempIndex = 3,
    CreateTempTable = 4,
    CreateTempTrigger = 5,
    CreateTempView = 6,
    CreateTrigger = 7,
    CreateView = 8,
    Delete = 9,
    DropIndex = 10,
    DropTable = 11,
    DropTempIndex = 12,
    DropTempTable = 13,
    DropTempTrigger = 14,
    DropTempView = 15,
    DropTrigger = 16,
    DropView = 17,
    Insert = 18,
    Pragma = 19,
    Read = 20,
    Select = 21,
    Transaction = 22,
    Update = 23,
    Attach = 24,
    Detach = 25,
    AlterTable = 26,
    Reindex = 27,
    Analyze = 28,
    CreateVtable = 29,
    DropVtable = 30,
    Function = 31,
    Savepoint = 32,
    Recursive = 33,
};

/// Decision returned by an authorizer callback.
///
/// Ignore is only honored for Read, Function, Select, and Recursive requests
/// while the innermost policy includes read_only(); every other Ignore is
/// treated as Deny (see README "Scoped authorization policies").
enum class AuthorizerResult : int {
    Allow = 0,
    Ignore = 1,
    Deny = 2,
};

/// One authorization request.  The string views are valid only during the
/// callback.  A missing argument (SQLite passed NULL) is std::nullopt, which is
/// distinct from an empty string.
struct AuthorizerRequest {
    AuthorizerAction action = AuthorizerAction::Copy;
    std::optional<std::string_view> arg1{};
    std::optional<std::string_view> arg2{};
    /// Schema name ("main", "temp", attachment alias), if applicable.
    std::optional<std::string_view> database{};
    /// Innermost trigger or view responsible for the access, if any.
    std::optional<std::string_view> source{};
};

using AuthorizerCallback =
    std::function<AuthorizerResult(const AuthorizerRequest&)>;

// ============================================================================
// ScopedPolicy — RAII authorization scope
// ============================================================================

/// Installs an authorization policy on a connection for the lifetime of the
/// guard.  Constraints may be added while the guard is alive.  Destruction
/// restores the previous policy (or removes the authorizer) and any hardening
/// settings changed through this guard.
///
/// Nested guards on the same connection replace the outer policy while they
/// are alive; only the innermost guard is evaluated.  Hardening settings are
/// connection state, so inner guards inherit them.
///
/// Preconditions: the Database must be open and must outlive the guard, must
/// not be moved, closed or reopened while guarded, and guards must be
/// destroyed in reverse order of construction.  Do not call
/// sqlite3_set_authorizer() directly while a guard is alive.
class SQLITEX_EXPORT ScopedPolicy {
    public:
    explicit ScopedPolicy(Database& db);
    ScopedPolicy(Database& db, AuthorizerCallback callback);
    ~ScopedPolicy();

    ScopedPolicy(const ScopedPolicy&) = delete;
    ScopedPolicy& operator=(const ScopedPolicy&) = delete;
    ScopedPolicy(ScopedPolicy&&) = delete;
    ScopedPolicy& operator=(ScopedPolicy&&) = delete;

    // === Constraints ===

    /// Add a custom callback.  Callbacks run after built-in constraints, in
    /// insertion order; the first Deny wins, otherwise any Ignore wins.
    ScopedPolicy& add(AuthorizerCallback callback);

    /// Deny every request with this action.  For Transaction and Savepoint
    /// only starting requests ("BEGIN") are denied, so open transactions and
    /// savepoints can always be committed, released or rolled back.
    ScopedPolicy& deny(AuthorizerAction action);

    ScopedPolicy& deny_copy() {
        return deny(AuthorizerAction::Copy);
    }
    ScopedPolicy& deny_create_index() {
        return deny(AuthorizerAction::CreateIndex);
    }
    ScopedPolicy& deny_create_table() {
        return deny(AuthorizerAction::CreateTable);
    }
    ScopedPolicy& deny_create_temp_index() {
        return deny(AuthorizerAction::CreateTempIndex);
    }
    ScopedPolicy& deny_create_temp_table() {
        return deny(AuthorizerAction::CreateTempTable);
    }
    ScopedPolicy& deny_create_temp_trigger() {
        return deny(AuthorizerAction::CreateTempTrigger);
    }
    ScopedPolicy& deny_create_temp_view() {
        return deny(AuthorizerAction::CreateTempView);
    }
    ScopedPolicy& deny_create_trigger() {
        return deny(AuthorizerAction::CreateTrigger);
    }
    ScopedPolicy& deny_create_view() {
        return deny(AuthorizerAction::CreateView);
    }
    ScopedPolicy& deny_delete() {
        return deny(AuthorizerAction::Delete);
    }
    ScopedPolicy& deny_drop_index() {
        return deny(AuthorizerAction::DropIndex);
    }
    ScopedPolicy& deny_drop_table() {
        return deny(AuthorizerAction::DropTable);
    }
    ScopedPolicy& deny_drop_temp_index() {
        return deny(AuthorizerAction::DropTempIndex);
    }
    ScopedPolicy& deny_drop_temp_table() {
        return deny(AuthorizerAction::DropTempTable);
    }
    ScopedPolicy& deny_drop_temp_trigger() {
        return deny(AuthorizerAction::DropTempTrigger);
    }
    ScopedPolicy& deny_drop_temp_view() {
        return deny(AuthorizerAction::DropTempView);
    }
    ScopedPolicy& deny_drop_trigger() {
        return deny(AuthorizerAction::DropTrigger);
    }
    ScopedPolicy& deny_drop_view() {
        return deny(AuthorizerAction::DropView);
    }
    ScopedPolicy& deny_insert() {
        return deny(AuthorizerAction::Insert);
    }
    ScopedPolicy& deny_pragma() {
        return deny(AuthorizerAction::Pragma);
    }
    ScopedPolicy& deny_read() {
        return deny(AuthorizerAction::Read);
    }
    ScopedPolicy& deny_select() {
        return deny(AuthorizerAction::Select);
    }
    ScopedPolicy& deny_transaction() {
        return deny(AuthorizerAction::Transaction);
    }
    ScopedPolicy& deny_update() {
        return deny(AuthorizerAction::Update);
    }
    ScopedPolicy& deny_attach() {
        return deny(AuthorizerAction::Attach);
    }
    ScopedPolicy& deny_detach() {
        return deny(AuthorizerAction::Detach);
    }
    ScopedPolicy& deny_alter_table() {
        return deny(AuthorizerAction::AlterTable);
    }
    ScopedPolicy& deny_reindex() {
        return deny(AuthorizerAction::Reindex);
    }
    ScopedPolicy& deny_analyze() {
        return deny(AuthorizerAction::Analyze);
    }
    ScopedPolicy& deny_create_vtable() {
        return deny(AuthorizerAction::CreateVtable);
    }
    ScopedPolicy& deny_drop_vtable() {
        return deny(AuthorizerAction::DropVtable);
    }
    ScopedPolicy& deny_function() {
        return deny(AuthorizerAction::Function);
    }
    ScopedPolicy& deny_savepoint() {
        return deny(AuthorizerAction::Savepoint);
    }
    ScopedPolicy& deny_recursive() {
        return deny(AuthorizerAction::Recursive);
    }

    /// Deny every request naming table `name` (case-insensitive) in `schema`
    /// (empty = any schema).  sqlite_schema / sqlite_temp_schema are treated
    /// as sqlite_master / sqlite_temp_master.
    ScopedPolicy& deny_table(std::string_view name,
                             std::string_view schema = {});

    /// Deny reads and updates of one column (and ALTER TABLE forms that report
    /// that column).  Does not block INSERT or RENAME COLUMN.
    ScopedPolicy& deny_column(std::string_view table, std::string_view column,
                              std::string_view schema = {});

    /// Deny calls to the named SQL function (case-insensitive).
    ScopedPolicy& deny_function(std::string_view name);

    /// Deny the named PRAGMA (case-insensitive), including pragma_* functions.
    ScopedPolicy& deny_pragma(std::string_view name);

    /// Allow only queries, transaction control, savepoints and introspection
    /// PRAGMAs (table_info, table_xinfo, index_list, index_info, index_xinfo,
    /// foreign_key_list, data_version).  Enables Ignore-based read redaction.
    ScopedPolicy& read_only();

    /// Deny every action not in `actions`, including unknown future codes.
    /// Transaction and savepoint ending requests are always allowed.  Several
    /// allow_only() constraints intersect.
    ScopedPolicy& allow_only(std::vector<AuthorizerAction> actions);

    // === Connection hardening (active guard only; restored on destruction) ===

    ScopedPolicy& defensive(bool on = true);
    ScopedPolicy& trusted_schema(bool on);
    ScopedPolicy& triggers_enabled(bool on);
    ScopedPolicy& views_enabled(bool on);
    ScopedPolicy& attach_create(bool on);
    ScopedPolicy& attach_write(bool on);
    ScopedPolicy& max_attached(int n);

    private:
    friend struct detail::PolicyAccess;

    struct SavedSetting {
        bool is_limit;
        int id;
        int value;
    };

    void add_builtin(AuthorizerCallback rule);
    void refresh();
    void require_active() const;
    ScopedPolicy& set_db_config(int op, bool on);

    Database* db_ = nullptr;
    ScopedPolicy* prev_ = nullptr;
    std::vector<AuthorizerCallback> builtins_{};
    std::vector<AuthorizerCallback> callbacks_{};
    std::vector<SavedSetting> saved_{};
    bool read_only_ = false;
};

} // namespace sqlitex
