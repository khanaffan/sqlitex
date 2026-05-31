#pragma once

#include "cloudsqlite/changeset.h"

#include <sqlite3.h>
#include "cloudsqlite/detail/session_api.h"

#include <memory>

namespace cloudsqlite {

// ============================================================================
// Changegroup — RAII wrapper for sqlite3_changegroup*
// ============================================================================

class Changegroup {
public:
    Changegroup();
    ~Changegroup();

    // Move-only
    Changegroup(Changegroup&& other) noexcept;
    Changegroup& operator=(Changegroup&& other) noexcept;
    Changegroup(const Changegroup&) = delete;
    Changegroup& operator=(const Changegroup&) = delete;

    explicit operator bool() const noexcept { return group_ != nullptr; }
    sqlite3_changegroup* handle() noexcept { return group_; }

    /// Add a changeset to the group.
    void add(const Changeset& cs);

    /// Add a patchset to the group.
    void add(const Patchset& ps);

    /// Output the merged changeset.
    [[nodiscard]] Changeset output();

private:
    sqlite3_changegroup* group_ = nullptr;
};

} // namespace cloudsqlite
