#pragma once

#include "cloudsqlite/error.h"
#include "cloudsqlite/changeset.h"

#include <sqlite3.h>
#include "cloudsqlite/detail/session_api.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace cloudsqlite {

class Database;
class Changeset;
class Patchset;

// ============================================================================
// Session — RAII wrapper for sqlite3_session*
// ============================================================================

/// Table filter callbacks: return true to track changes for a table.
using SessionTableFilter = std::function<bool(std::string_view table_name)>;

class Session {
public:
    /// Create a session attached to a database.
    /// @param db      Database connection
    /// @param db_name Database name (typically "main")
    Session(Database& db, std::string_view db_name = "main");

    ~Session();

    // Move-only
    Session(Session&& other) noexcept;
    Session& operator=(Session&& other) noexcept;
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    explicit operator bool() const noexcept { return session_ != nullptr; }
    sqlite3_session* handle() noexcept { return session_; }

    // === Control ===

    /// Enable or disable change tracking.
    /// @return Previous enable state (0 = disabled, 1 = enabled).
    int enable(bool enabled = true);

    /// Enable or disable recording of indirect changes (trigger-fired).
    /// @return Previous indirect state.
    int set_indirect(bool indirect = true);

    // === Table Attachment ===

    /// Attach a table to track.  Pass empty/null to attach all tables.
    void attach(std::string_view table_name = {});

    /// Attach all tables for change tracking.
    void attach_all();

    /// Set a table filter callback. The callback is invoked the first time
    /// a table is written; return true to track changes for that table.
    void set_table_filter(SessionTableFilter filter);

    // === Changeset / Patchset Extraction ===

    /// Extract the accumulated changeset.
    [[nodiscard]] Changeset changeset();

    /// Extract the accumulated patchset.
    [[nodiscard]] Patchset patchset();

    /// Compute a diff of a single table against another attached database
    /// on the same connection.
    /// @param table_name  Table to diff
    /// @param from_db     Name of the attached database with the reference schema
    [[nodiscard]] Changeset diff(std::string_view table_name,
                                 std::string_view from_db = "main");

    // === Introspection ===

    bool is_empty() const noexcept;
    int64_t memory_used() const noexcept;

    // === Object Config ===

    /// Configure session behavior.
    /// op: SQLITE_SESSION_OBJCONFIG_SIZE or SQLITE_SESSION_OBJCONFIG_ROWID
    void set_object_config(int op, void* arg);

private:
    sqlite3_session* session_ = nullptr;
    Database* db_ = nullptr;

    // Kept alive for the C callback shim
    std::unique_ptr<SessionTableFilter> filter_cb_;
};

} // namespace cloudsqlite
