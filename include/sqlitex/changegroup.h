#pragma once

#include "sqlitex_export.h"

#include "sqlitex/changeset.h"

#include <sqlite3.h>
#include "sqlitex/detail/session_api.h"

#include <memory>

#ifdef SQLITEX_HAS_SESSION

namespace sqlitex {

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

    /// Add a changeset to the group.
    void add(const Changeset& cs);

    /// Add a patchset to the group.
    void add(const Patchset& ps);

    /// Output the merged changeset.
    [[nodiscard]] Changeset output();

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
