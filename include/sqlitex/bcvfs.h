#pragma once

#include "sqlitex_export.h"

#include "sqlitex/concepts.h"
#include "sqlitex/error.h"
#include "sqlitex/flags.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

// Forward declarations for bcvfs types (from blockcachevfs.h, when available)
struct sqlite3_bcvfs;
struct sqlite3_prefetch;

namespace sqlitex {

class Database;
class PrefetchHandle;

// ============================================================================
// BcvfsConfig — configuration keys
// ============================================================================

enum class BcvfsConfig : int {
    CacheSize = 1,         // SQLITE_BCV_CACHESIZE: Max cache size in bytes
    NRequest = 2,          // SQLITE_BCV_NREQUEST: Max concurrent HTTP requests
    HttpTimeout = 3,       // SQLITE_BCV_HTTPTIMEOUT: HTTP timeout in seconds
    HttpLog = 4,           // SQLITE_BCV_CURLVERBOSE: Enable CURL verbose log
    HttpLogTimeout = 5,    // SQLITE_BCV_HTTPLOG_TIMEOUT: HTTP log timeout (s)
    HttpLogMaxEntries = 6, // SQLITE_BCV_HTTPLOG_NENTRY: Max HTTP log entries
};

// ============================================================================
// BcvfsLogFlag — bitmask values for log callback
// ============================================================================

enum class BcvfsLogFlag : int {
    None = 0,
    Http = 0x0001,
    Upload = 0x0002,
    Cleanup = 0x0004,
    Event = 0x0008,
    HttpRetry = 0x0010,
    All = 0x001F,
};

template <>
struct EnableBitmaskOperators<BcvfsLogFlag> : std::true_type {};

using BcvfsLogFlags = Flags<BcvfsLogFlag>;

// ============================================================================
// BcvfsAttachFlag — bitmask values for Bcvfs::attach()
// ============================================================================

enum class BcvfsAttachFlag : int {
    None = 0,
    Secure = 0x0001,      // SQLITE_BCV_ATTACH_SECURE
    IfNotExists = 0x0002, // SQLITE_BCV_ATTACH_IFNOT
};

template <>
struct EnableBitmaskOperators<BcvfsAttachFlag> : std::true_type {};

using BcvfsAttachFlags = Flags<BcvfsAttachFlag>;

// ============================================================================
// BcvfsStorage — cloud storage backend type
// ============================================================================

enum class BcvfsStorage {
    Azure,
    Google,
};

// ============================================================================
// Callback types
// ============================================================================

/// Auth callback: receives storage, account, container; sets auth token.
/// Return true to allow, false to deny.
using BcvfsAuthCallback =
    std::function<bool(std::string_view storage, std::string_view account,
                       std::string_view container, std::string& auth_token)>;

/// Log callback for bcvfs diagnostic messages.
using BcvfsLogCallback = std::function<void(std::string_view message)>;

/// Log callback that also receives the SQLITE_BCV_LOG_* event mask.
using BcvfsMaskedLogCallback =
    std::function<void(BcvfsLogFlags mask, std::string_view message)>;

/// Busy callback for upload operations.
/// Return a duration to wait before retrying, or nullopt to cancel.
using BcvfsBusyCallback =
    std::function<std::optional<std::chrono::milliseconds>(int retry_count)>;

// ============================================================================
// Bcvfs — RAII wrapper for sqlite3_bcvfs*
// ============================================================================

class SQLITEX_EXPORT Bcvfs {
    public:
    /// Create a bcvfs instance.
    /// @param cache_dir Local directory for the block cache
    /// @param vfs_name  Name to register with SQLite (default: "bcvfs")
    explicit Bcvfs(std::string_view cache_dir,
                   std::string_view vfs_name = "bcvfs");

    template <FilesystemPath Path>
    explicit Bcvfs(const Path& cache_dir, std::string_view vfs_name = "bcvfs")
        : Bcvfs(detail::path_to_utf8(cache_dir), vfs_name) {
    }

    ~Bcvfs();

    // Move-only
    Bcvfs(Bcvfs&& other) noexcept;
    Bcvfs& operator=(Bcvfs&& other) noexcept;
    Bcvfs(const Bcvfs&) = delete;
    Bcvfs& operator=(const Bcvfs&) = delete;

    explicit operator bool() const noexcept {
#ifdef SQLITEX_HAS_BCVFS
        return bcvfs_ != nullptr;
#else
        return false;
#endif
    }

    /// The VFS name to use in Database::open() calls.
    [[nodiscard]] const std::string& vfs_name() const noexcept {
        return vfs_name_;
    }

    [[nodiscard]] sqlite3_bcvfs* handle() noexcept {
#ifdef SQLITEX_HAS_BCVFS
        return bcvfs_;
#else
        return nullptr;
#endif
    }

    // === Configuration ===

    /// Set a SQLITE_BCV_* configuration option.
    Bcvfs& config(BcvfsConfig op, int64_t val);

    /// Set a configuration value.
    Bcvfs& set_cache_size(int64_t bytes);
    Bcvfs& set_max_requests(int n);
    Bcvfs& set_http_timeout(std::chrono::seconds timeout);
    Bcvfs& set_http_logging(bool enabled);
    Bcvfs& set_http_log_timeout(std::chrono::seconds timeout);
    Bcvfs& set_http_log_max_entries(int max_entries);

    // === Callbacks ===

    void set_auth_callback(BcvfsAuthCallback cb);
    void set_log_callback(BcvfsLogCallback cb,
                          BcvfsLogFlags mask = BcvfsLogFlag::All);
    void set_masked_log_callback(BcvfsMaskedLogCallback cb,
                                 BcvfsLogFlags mask = BcvfsLogFlag::All);

    // === Container Management ===

    /// Attach a cloud storage container.
    /// @param storage   "azure", "google", or custom module name
    /// @param account   Storage account name
    /// @param container Container name
    /// @param alias     Local alias (the path prefix used in Database::open())
    /// @param flags     Bitmask of BcvfsAttachFlag values (default: None)
    void attach(std::string_view storage, std::string_view account,
                std::string_view container, std::string_view alias,
                BcvfsAttachFlags flags = BcvfsAttachFlag::None);

    /// Detach a container.
    void detach(std::string_view alias);

    // === Cloud Operations ===

    /// Upload locally modified databases to cloud storage.
    /// Returns SQLITE_OK (0) on success; throws Error on failure.
    int upload(std::string_view container, BcvfsBusyCallback busy_cb = {});

    /// Poll for changes from cloud storage.
    bool poll(std::string_view container);

    /// Copy a database within the container (local operation).
    void copy(std::string_view container, std::string_view from_db,
              std::string_view to_db);

    /// Mark a database for deletion within the container (local operation).
    void remove(std::string_view container, std::string_view db_name);

    /// Revert local changes since last upload.
    void revert(std::string_view container);

    /// Check if this VFS is running in daemon mode.
    [[nodiscard]] bool is_daemon() const noexcept;

    /// Register introspection virtual tables on a database connection.
    void register_vtab(Database& db);

    // === Prefetch ===

    /// Create a prefetch handle.
    [[nodiscard]] PrefetchHandle prefetch(std::string_view container,
                                          std::string_view db_name);

    private:
#ifdef SQLITEX_HAS_BCVFS
    sqlite3_bcvfs* bcvfs_ = nullptr;
#endif
    std::string vfs_name_{};

    // Callback storage (kept alive for C shim lifetime)
    std::unique_ptr<BcvfsAuthCallback> auth_cb_{};
    std::unique_ptr<BcvfsMaskedLogCallback> log_cb_{};
};

} // namespace sqlitex
