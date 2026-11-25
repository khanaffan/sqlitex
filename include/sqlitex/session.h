#pragma once

#include "sqlitex_export.h"

#include "sqlitex/error.h"
#include "sqlitex/changeset.h"

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

enum class SessionObjectConfig : int {
    Size = SQLITE_SESSION_OBJCONFIG_SIZE,
    RowId = SQLITE_SESSION_OBJCONFIG_ROWID,
};

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

    [[nodiscard]] explicit operator bool() const noexcept {
        return session_ != nullptr;
    }
    [[nodiscard]] sqlite3_session* handle() noexcept {
        return session_;
    }

    // === Control ===

    /// Enable or disable change tracking.
    /// @return Previous enable state (true = was enabled, false = was
    /// disabled).
    bool enable(bool enabled = true);

    /// Enable or disable recording of indirect changes (trigger-fired).
    /// @return Previous indirect state (true = was enabled, false = was
    /// disabled).
    bool set_indirect(bool indirect = true);

    // === Table Attachment ===

    /// Attach a table to track. Pass empty to attach all tables.
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
    [[nodiscard]] Changeset diff(std::string_view table_name,
                                 std::string_view from_db = "main");

    // === Streaming Extraction ===

    /// Stream the accumulated changeset via an output callback.
    void changeset_stream(StreamOutputFunc output);

    /// Stream the accumulated patchset via an output callback.
    void patchset_stream(StreamOutputFunc output);

    /// Convenience: stream the changeset to a std::ostream.
    void changeset_to_stream(std::ostream& os);

    /// Convenience: stream the patchset to a std::ostream.
    void patchset_to_stream(std::ostream& os);

    // === Introspection ===

    [[nodiscard]] bool is_empty() const noexcept;
    [[nodiscard]] int64_t memory_used() const noexcept;

    /// Upper bound on changeset size in bytes (requires
    /// SessionObjectConfig::Size enabled before attaching tables).
    [[nodiscard]] int64_t changeset_size() const noexcept;

    // === Object Config ===

    /// Type-safe session object configuration (must be called before attaching
    /// any table). Returns the resulting boolean state.
    bool set_object_config(SessionObjectConfig op, bool enable);

    /// Query a session object configuration option.
    [[nodiscard]] bool get_object_config(SessionObjectConfig op) const;

    /// Raw escape hatch for sqlite3session_object_config().
    void set_object_config(int op, void* arg);

    // === Global Session Config ===

    /// Raw wrapper for sqlite3session_config().
    static void config(int op, void* arg);

    /// Set the global streaming chunk size (bytes) and return the active value.
    static int set_stream_chunk_size(int bytes);

    /// Query the current global streaming chunk size (bytes).
    [[nodiscard]] static int stream_chunk_size();

    private:
    sqlite3_session* session_ = nullptr;
    Database* db_ = nullptr;

    // Kept alive for the C callback shim
    std::unique_ptr<SessionTableFilter> filter_cb_{};
};

} // namespace sqlitex

#endif // SQLITEX_HAS_SESSION
