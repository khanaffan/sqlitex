#pragma once

#include "sqlitex_export.h"

#include "sqlitex/detail/sqlite3_fwd.h"
#include "sqlitex/error.h"

#include <chrono>
#include <functional>
#include <string_view>

namespace sqlitex {

class Database;

// ============================================================================
// Backup — RAII wrapper for SQLite Online Backup API (sqlite3_backup*)
// ============================================================================

class SQLITEX_EXPORT Backup {
    public:
    Backup() noexcept = default;

    /// Begin an online backup from `source` to `dest`.
    /// Both Database handles must remain open for the lifetime of this Backup.
    Backup(Database& dest, Database& source,
           std::string_view dest_name = "main",
           std::string_view source_name = "main");

    ~Backup();

    // Move-only
    Backup(Backup&& other) noexcept;
    Backup& operator=(Backup&& other) noexcept;
    Backup(const Backup&) = delete;
    Backup& operator=(const Backup&) = delete;

    explicit operator bool() const noexcept {
        return backup_ != nullptr;
    }

    sqlite3_backup* handle() noexcept {
        return backup_;
    }
    const sqlite3_backup* handle() const noexcept {
        return backup_;
    }

    /// Copy up to `pages` pages (-1 copies all remaining pages).
    /// Returns true if the backup has finished (SQLITE_DONE), false if more
    /// pages remain (SQLITE_OK). Throws on errors (including SQLITE_BUSY /
    /// SQLITE_LOCKED; use step_raw() or run() to retry transient locks).
    bool step(int pages = -1);

    /// Raw step: returns SQLITE_OK, SQLITE_DONE, SQLITE_BUSY, or SQLITE_LOCKED.
    /// Throws on fatal errors.
    int step_raw(int pages = -1);

    /// Finish the backup and release resources. Throws if an error occurred
    /// during the backup. Calling finish() explicitly lets you observe errors
    /// that would otherwise be swallowed by the destructor.
    void finish();

    /// Number of pages still to be backed up (updated after the first step()).
    int remaining() const noexcept;

    /// Total number of pages in the source database (updated after step()).
    int pagecount() const noexcept;

    /// Run the entire backup to completion, retrying SQLITE_BUSY /
    /// SQLITE_LOCKED with `sleep_on_busy`, optionally reporting progress after
    /// each chunk, and calling finish().
    void run(int pages_per_step = -1,
             std::function<void(int remaining, int pagecount)> progress = {},
             std::chrono::milliseconds sleep_on_busy =
                 std::chrono::milliseconds(50));

    private:
    sqlite3_backup* backup_ = nullptr;
    Database* dest_ = nullptr;
};

} // namespace sqlitex
