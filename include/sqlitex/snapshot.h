#pragma once

#include "sqlitex_export.h"

#include "sqlitex/detail/sqlite3_fwd.h"
#include "sqlitex/error.h"

#include <compare>
#include <string_view>

namespace sqlitex {

class Database;

// ============================================================================
// Snapshot — RAII wrapper for WAL database snapshots (sqlite3_snapshot*)
// ============================================================================

class SQLITEX_EXPORT Snapshot {
    public:
    Snapshot() noexcept = default;
    ~Snapshot();

    // Move-only
    Snapshot(Snapshot&& other) noexcept;
    Snapshot& operator=(Snapshot&& other) noexcept;
    Snapshot(const Snapshot&) = delete;
    Snapshot& operator=(const Snapshot&) = delete;

    explicit operator bool() const noexcept {
        return snapshot_ != nullptr;
    }

    sqlite3_snapshot* handle() noexcept {
        return snapshot_;
    }
    const sqlite3_snapshot* handle() const noexcept {
        return snapshot_;
    }

    /// Record a snapshot of the current WAL state for `schema`.
    /// Requires an active read transaction on a WAL-mode database.
    [[nodiscard]] static Snapshot get(Database& db,
                                      std::string_view schema = "main");

    /// Non-throwing variant of get().
    [[nodiscard]] static Expected<Snapshot> try_get(
        Database& db, std::string_view schema = "main");

    /// Start a read transaction on `db` at this historical snapshot.
    void open(Database& db, std::string_view schema = "main") const;

    /// Recover snapshots from a WAL file (sqlite3_snapshot_recover).
    static void recover(Database& db, std::string_view schema = "main");

    /// Compare the ages of two snapshots of the same database.
    /// Returns < 0 if a is older than b, 0 if identical, > 0 if a is newer.
    [[nodiscard]] static int compare(const Snapshot& a,
                                     const Snapshot& b) noexcept;

    bool operator==(const Snapshot& other) const noexcept {
        return compare(*this, other) == 0;
    }
    std::strong_ordering operator<=>(const Snapshot& other) const noexcept {
        return compare(*this, other) <=> 0;
    }

    private:
    explicit Snapshot(sqlite3_snapshot* snap) noexcept : snapshot_(snap) {
    }

    sqlite3_snapshot* snapshot_ = nullptr;
};

} // namespace sqlitex
