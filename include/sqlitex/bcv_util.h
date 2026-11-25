#pragma once

#include "sqlitex_export.h"

#include "sqlitex/error.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

struct sqlite3_bcv;

namespace sqlitex {

// ============================================================================
// BcvConfigOp — configuration keys for sqlite3_bcv_config()
// ============================================================================

enum class BcvConfigOp : int {
    Verbose = 1,     // SQLITE_BCVCONFIG_VERBOSE
    Progress = 2,    // SQLITE_BCVCONFIG_PROGRESS
    Log = 3,         // SQLITE_BCVCONFIG_LOG
    NRequest = 4,    // SQLITE_BCVCONFIG_NREQUEST
    LogLevel = 5,    // SQLITE_BCVCONFIG_LOGLEVEL
    TestNoKv = 6,    // SQLITE_BCVCONFIG_TESTNOKV
    HttpTimeout = 7, // SQLITE_BCVCONFIG_HTTPTIMEOUT
    FindOrphans = 8, // SQLITE_BCVCONFIG_FINDORPHANS
};

/// Progress callback invoked during upload/download/cleanup.
/// Return 0 (SQLITE_OK) to continue, SQLITE_DONE (101) during cleanup to
/// finish early and publish the manifest, or any other non-zero code to abort.
using BcvProgressCallback = std::function<int(int64_t done, int64_t total)>;

/// Log callback invoked for HTTP(S) request/response events.
using BcvLogCallback = std::function<void(std::string_view message)>;

// ============================================================================
// BcvClient — RAII wrapper for sqlite3_bcv* (bcvutil.h)
// ============================================================================

class SQLITEX_EXPORT BcvClient {
    public:
    /// Open a cloud storage client handle.
    /// @param module    Module name (e.g. "azure", "google", or custom module;
    ///                  may include URI parameters like "azure?sas=1")
    /// @param user      Cloud storage user / account name
    /// @param auth      Authentication credential (key, SAS, etc.)
    /// @param container Cloud storage container / bucket name
    BcvClient(std::string_view module, std::string_view user,
              std::string_view auth, std::string_view container);

    ~BcvClient();

    // Move-only
    BcvClient(BcvClient&& other) noexcept;
    BcvClient& operator=(BcvClient&& other) noexcept;
    BcvClient(const BcvClient&) = delete;
    BcvClient& operator=(const BcvClient&) = delete;

    explicit operator bool() const noexcept {
#ifdef SQLITEX_HAS_BCVFS
        return handle_ != nullptr;
#else
        return false;
#endif
    }

    sqlite3_bcv* handle() noexcept {
#ifdef SQLITEX_HAS_BCVFS
        return handle_;
#else
        return nullptr;
#endif
    }

    // === Configuration (sqlite3_bcv_config) ===

    BcvClient& set_verbose(bool enabled);
    BcvClient& set_max_requests(int n);
    BcvClient& set_log_level(int level);
    BcvClient& set_http_timeout(std::chrono::seconds timeout);
    BcvClient& set_find_orphans(bool enabled);
    BcvClient& set_test_no_kv(bool enabled);
    BcvClient& set_progress_callback(BcvProgressCallback cb);
    BcvClient& set_log_callback(BcvLogCallback cb);

    // === Cloud Container & Database Operations ===

    /// Create a new container and manifest file.
    /// @param name_size  Optional block name size (0 = default)
    /// @param block_size Optional block size in bytes (0 = default)
    void create(int name_size = 0, int block_size = 0);

    /// Delete the entire container / bucket.
    void destroy();

    /// Upload a local SQLite database file to cloud storage.
    void upload(std::string_view local_path, std::string_view remote_name);

    /// Download a cloud database to a local file path.
    void download(std::string_view remote_name, std::string_view local_path);

    /// Copy a database within the cloud storage container.
    void copy(std::string_view from_name, std::string_view to_name);

    /// Delete a single database from cloud storage.
    void remove(std::string_view remote_name);

    /// Delete unused / orphaned blocks older than `age_seconds` (<= 0 deletes
    /// all unused blocks).
    void cleanup(int age_seconds = 0);

    // === Diagnostics ===

    /// Error code from the most recent operation (SQLite code < 256 or HTTP
    /// status >= 400).
    int errcode() const noexcept;

    /// Error message from the most recent operation (empty if none).
    std::string errmsg() const;

    private:
#ifdef SQLITEX_HAS_BCVFS
    sqlite3_bcv* handle_ = nullptr;
#endif
    std::unique_ptr<BcvProgressCallback> progress_cb_{};
    std::unique_ptr<BcvLogCallback> log_cb_{};
};

} // namespace sqlitex
