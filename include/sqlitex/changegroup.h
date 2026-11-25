#pragma once

#include "sqlitex_export.h"

#include "sqlitex/changeset.h"
#include "sqlitex/changeset_iterator.h"

#include "sqlitex/detail/session_api.h"

#include <cstdint>
#include <memory>
#include <string_view>

#ifdef SQLITEX_HAS_SESSION

namespace sqlitex {

class Database;

// ============================================================================
// Changegroup — RAII wrapper for sqlite3_changegroup*
// ============================================================================

class SQLITEX_EXPORT Changegroup {
    public:
    Changegroup();
    ~Changegroup();

    // Move-only
    Changegroup(Changegroup&& other) noexcept;
    Changegroup& operator=(Changegroup&& other) noexcept;
    Changegroup(const Changegroup&) = delete;
    Changegroup& operator=(const Changegroup&) = delete;

    explicit operator bool() const noexcept {
        return group_ != nullptr;
    }
    sqlite3_changegroup* handle() noexcept {
        return group_;
    }

    // === Configuration & Schema ===

    /// Raw wrapper for sqlite3changegroup_config().
    void config(int op, void* arg);

    /// Configure whether output is a patchset (true) or changeset (false).
    /// Returns the effective mode (true = patchset, false = changeset).
    bool set_patchset_mode(bool patchset);

    /// Query whether this changegroup produces a patchset.
    bool is_patchset_mode();

    /// Configure schema metadata from a database (required for
    /// sqlite3changegroup_change_* and optional validation).
    void set_schema(Database& db, std::string_view db_name = "main");

    // === Adding Changes ===

    /// Add a changeset to the group.
    void add(const Changeset& cs);

    /// Add a patchset to the group.
    void add(const Patchset& ps);

    /// Add the single current change from a ChangesetIterator.
    void add_change(const ChangesetIterator& iter);

    // === Programmatic Change Construction (SQLite 3.54+) ===

    /// Begin constructing a single change for a table.
    void change_begin(ChangeOperation op, std::string_view table,
                      bool indirect = false);

    /// Set a 64-bit integer value on the change under construction.
    void change_int64(bool is_new, int col, int64_t val);

    /// Set a double value on the change under construction.
    void change_double(bool is_new, int col, double val);

    /// Set a NULL value on the change under construction.
    void change_null(bool is_new, int col);

    /// Set a UTF-8 text value on the change under construction.
    void change_text(bool is_new, int col, std::string_view val);

    /// Set a BLOB value on the change under construction.
    void change_blob(bool is_new, int col, const void* data, int size);

    /// Finish (commit or discard) the change under construction.
    void change_finish(bool discard = false);

    // === Output ===

    /// Output the merged changeset.
    [[nodiscard]] Changeset output();

    /// Output the merged patchset (for patchset-mode changegroups).
    [[nodiscard]] Patchset output_patchset();

    // === Streaming Operations ===

    /// Add a changeset from a streaming input callback.
    void add_stream(StreamInputFunc input);

    /// Output the merged changeset via a streaming output callback.
    void output_stream(StreamOutputFunc output);

    private:
    sqlite3_changegroup* group_ = nullptr;
};

} // namespace sqlitex

#endif // SQLITEX_HAS_SESSION
