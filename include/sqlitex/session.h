#pragma once

#include "sqlitex_export.h"

#include "sqlitex/error.h"
#include "sqlitex/changeset.h"

#include <sqlite3.h>
#include "sqlitex/detail/session_api.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>

#ifdef SQLITEX_HAS_SESSION

namespace sqlitex {

class Database;
class Changeset;
class Patchset;

// ============================================================================
// Session — RAII wrapper for sqlite3_session*
// ============================================================================

/// Table filter callbacks: return true to track changes for a table.
using SessionTableFilter = std::function<bool(std::string_view table_name)>;

class SQLITEX_EXPORT Session {
    public:
    /// Create a session attached to a database.
    /// @param db      Database connection
    /// @param db_name Database name (typically "main")
    explicit Session(Database& db, std::string_view db_name = "main");

    ~Session();

    // Move-only
    Session(Session&& other) noexcept;
    Session& operator=(Session&& other) noexcept;
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    explicit operator bool() const noexcept {
        return session_ != nullptr;
    }
    sqlite3_session* handle() noexcept {
        return session_;
    }

    // === Control ===

    /// Enable or disable change tracking.
    /// @return Previous enable state (true = was enabled, false = was disabled).
    bool enable(bool enabled = true);

    /// Enable or disable recording of indirect changes (trigger-fired).
    /// @return Previous indirect state (true = was enabled, false = was disabled).
    bool set_indirect(bool indirect = true);

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
    /// on the same connection, and return the accumulated changeset.
    ///
    /// \param table_name  Table to diff.
    /// \param from_db     Name of the attached database with the reference
    ///                    schema (typically an ATTACH-ed database).
    ///
    /// \warning sqlite3session_diff **appends** the diff to this session's
    /// existing change set rather than replacing it.  The returned Changeset
    /// will contain all previously recorded changes **plus** the diff.
    /// To obtain only the diff, call this on a freshly created session with
    /// no other changes attached to it.
    [[nodiscard]] Changeset diff(std::string_view table_name,
                                 std::string_view from_db = "main");

    // === Streaming Extraction ===

    /// Stream the accumulated changeset via an output callback.
    /// The callback is invoked synchronously for each chunk of data.
    void changeset_stream(StreamOutputFunc output);

    /// Stream the accumulated patchset via an output callback.
    void patchset_stream(StreamOutputFunc output);

    /// Convenience: stream the changeset to a std::ostream.
    void changeset_to_stream(std::ostream& os);

    /// Convenience: stream the patchset to a std::ostream.
    void patchset_to_stream(std::ostream& os);

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
    std::unique_ptr<SessionTableFilter> filter_cb_{};
};

} // namespace sqlitex

#endif // SQLITEX_HAS_SESSION
