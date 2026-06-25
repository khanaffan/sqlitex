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

    /// Run the prefetch operation, blocking for up to `timeout`.
    void run(int max_requests, std::chrono::milliseconds timeout);

    /// Get current status.
    PrefetchStatus status();

    /// Error message (valid when status == Error).
    std::string error_message() const;

    private:
#ifdef SQLITEX_HAS_BCVFS
    sqlite3_prefetch* handle_ = nullptr;
#else
    void* handle_ = nullptr;
#endif
};

} // namespace sqlitex
