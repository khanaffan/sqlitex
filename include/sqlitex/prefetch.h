#pragma once

#include "sqlitex_export.h"

#include "sqlitex/error.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#ifdef SQLITEX_HAS_BCVFS
struct sqlite3_prefetch;
#endif

namespace sqlitex {

// ============================================================================
// PrefetchStatus
// ============================================================================

enum class PrefetchStatus {
    Running,
    Done,
    Error,
};

// ============================================================================
// PrefetchHandle — RAII wrapper for sqlite3_prefetch*
// ============================================================================

class SQLITEX_EXPORT PrefetchHandle {
    public:
    PrefetchHandle() noexcept = default;

#ifdef SQLITEX_HAS_BCVFS
    explicit PrefetchHandle(sqlite3_prefetch* handle) noexcept;
#endif

    ~PrefetchHandle();

    // Move-only
    PrefetchHandle(PrefetchHandle&& other) noexcept;
    PrefetchHandle& operator=(PrefetchHandle&& other) noexcept;
    PrefetchHandle(const PrefetchHandle&) = delete;
    PrefetchHandle& operator=(const PrefetchHandle&) = delete;

    explicit operator bool() const noexcept {
        return handle_ != nullptr;
    }

    /// Run one prefetch iteration, blocking for up to `timeout`.
    /// Does not throw when prefetch completes (SQLITE_DONE).
    void run(int max_requests, std::chrono::milliseconds timeout);

    /// Run one prefetch iteration. Returns true when all blocks have been
    /// fetched (SQLITE_DONE), false when more work remains (SQLITE_OK).
    bool step(int max_requests, std::chrono::milliseconds timeout);

    /// Get current status.
    PrefetchStatus status();

    /// Last status/error code from sqlite3_bcvfs_prefetch_errcode().
    /// Returns SQLITE_OK while running, SQLITE_DONE when finished, or an
    /// error code on failure.
    int error_code() const noexcept;

    /// Error message (valid when status == Error).
    std::string error_message() const;

    /// Number of outstanding prefetch HTTP requests (PFS_NOUTSTANDING).
    int64_t outstanding_requests() const;

    /// Number of clients waiting on on-demand requests (PFS_NDEMAND).
    int64_t on_demand_blocks() const;

    private:
#ifdef SQLITEX_HAS_BCVFS
    sqlite3_prefetch* handle_ = nullptr;
#else
    void* handle_ = nullptr;
#endif
};

} // namespace sqlitex
