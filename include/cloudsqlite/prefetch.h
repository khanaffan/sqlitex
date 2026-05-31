#pragma once

#include "cloudsqlite/error.h"

#include <sqlite3.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#ifdef CLOUDSQLITE_HAS_BCVFS
struct sqlite3_bcvfs_prefetch;
#endif

namespace cloudsqlite {

// ============================================================================
// PrefetchStatus
// ============================================================================

enum class PrefetchStatus {
    Running,
    Done,
    Error,
};

// ============================================================================
// PrefetchHandle — RAII wrapper for sqlite3_bcvfs_prefetch*
// ============================================================================

class PrefetchHandle {
public:
    PrefetchHandle() noexcept = default;

#ifdef CLOUDSQLITE_HAS_BCVFS
    explicit PrefetchHandle(sqlite3_bcvfs_prefetch* handle) noexcept;
#endif

    ~PrefetchHandle();

    // Move-only
    PrefetchHandle(PrefetchHandle&& other) noexcept;
    PrefetchHandle& operator=(PrefetchHandle&& other) noexcept;
    PrefetchHandle(const PrefetchHandle&) = delete;
    PrefetchHandle& operator=(const PrefetchHandle&) = delete;

    explicit operator bool() const noexcept { return handle_ != nullptr; }

    /// Run the prefetch operation, blocking for up to `timeout`.
    void run(int max_requests, std::chrono::milliseconds timeout);

    /// Get current status.
    PrefetchStatus status() const;

    /// Error message (valid when status == Error).
    std::string error_message() const;

private:
#ifdef CLOUDSQLITE_HAS_BCVFS
    sqlite3_bcvfs_prefetch* handle_ = nullptr;
#else
    void* handle_ = nullptr;
#endif
};

} // namespace cloudsqlite
