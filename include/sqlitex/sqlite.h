#pragma once

#include "sqlitex_export.h"

#include "sqlitex/detail/sqlite3_fwd.h"
#include "sqlitex/error.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sqlitex {

/// SQLite threading modes (matches SQLITE_THREADSAFE compile-time values and
/// sqlite3_config() threading verbs).
enum class ThreadingMode : int {
    SingleThread = 0,
    Serialized = 1,
    MultiThread = 2,
};

// ============================================================================
// Library Version, Compile Options & Error Strings
// ============================================================================

/// Return the SQLite version string (e.g. "3.54.1").
[[nodiscard]] SQLITEX_EXPORT std::string_view libversion() noexcept;

/// Return the SQLite version number (e.g. 3054001 for 3.54.1).
[[nodiscard]] SQLITEX_EXPORT int libversion_number() noexcept;

/// Return the SQLite source check-in identifier string.
[[nodiscard]] SQLITEX_EXPORT std::string_view sourceid() noexcept;

/// Return the SQLite thread-safety compile-time mode.
[[nodiscard]] SQLITEX_EXPORT ThreadingMode threadsafe() noexcept;

/// Check whether SQLite was compiled with a given option (e.g. "ENABLE_FTS5").
[[nodiscard]] SQLITEX_EXPORT bool compileoption_used(
    std::string_view option_name) noexcept;

/// Return the Nth compile option string, or std::nullopt if out of range.
[[nodiscard]] SQLITEX_EXPORT std::optional<std::string_view> compileoption_get(
    int index) noexcept;

/// Return all compile options compiled into SQLite.
[[nodiscard]] SQLITEX_EXPORT std::vector<std::string> compile_options();

/// Return the English description of an SQLite result code.
[[nodiscard]] SQLITEX_EXPORT std::string_view errstr(int sqlite_rc) noexcept;

// ============================================================================
// Library Lifecycle & Global Configuration
// ============================================================================

/// Initialize the SQLite library. Throws on error.
SQLITEX_EXPORT void initialize();

/// Shut down the SQLite library and release global resources. Throws on error.
SQLITEX_EXPORT void shutdown();

/// Enable or disable shared-cache mode globally. Throws on error.
SQLITEX_EXPORT void enable_shared_cache(bool enabled);

/// Emit a message to SQLite's global error log callback (sqlite3_log).
SQLITEX_EXPORT void log(int errcode, std::string_view message) noexcept;

/// Log callback type for config_log().
using LogCallback = std::function<void(int errcode, std::string_view message)>;

/// Configure SQLite global options (sqlite3_config).
/// Most sqlite3_config options must be called before initialize() (or after
/// shutdown()). Throws on error.
SQLITEX_EXPORT void config_threading_mode(ThreadingMode mode);
SQLITEX_EXPORT void config_memstatus(bool enabled);
SQLITEX_EXPORT void config_lookaside(int slot_size, int slot_count);
SQLITEX_EXPORT void config_uri(bool enabled);
SQLITEX_EXPORT void config_covering_index_scan(bool enabled);
SQLITEX_EXPORT void config_mmap_size(int64_t default_size, int64_t max_size);
SQLITEX_EXPORT void config_pmasz(unsigned int min_size);
SQLITEX_EXPORT void config_stmtjrnl_spill(int threshold_bytes);
SQLITEX_EXPORT void config_small_malloc(bool enabled);
SQLITEX_EXPORT void config_sorterref_size(int threshold_bytes);
SQLITEX_EXPORT void config_memdb_maxsize(int64_t max_bytes);
SQLITEX_EXPORT int config_rowid_in_view(int value = -1);
SQLITEX_EXPORT int config_pcache_hdrsz();

/// Register or clear the global SQLite error log callback (SQLITE_CONFIG_LOG).
/// Pass an empty LogCallback{} to unregister.
SQLITEX_EXPORT void config_log(LogCallback callback);

// ============================================================================
// Global Memory & Runtime Status
// ============================================================================

/// Current number of bytes of memory currently outstanding
/// (sqlite3_memory_used).
SQLITEX_EXPORT int64_t memory_used() noexcept;

/// Maximum number of bytes outstanding since last reset.
SQLITEX_EXPORT int64_t memory_highwater(bool reset = false) noexcept;

/// Query or set the soft heap limit in bytes. Pass negative to query without
/// modifying; returns the previous limit.
SQLITEX_EXPORT int64_t soft_heap_limit(int64_t limit = -1) noexcept;

/// Query or set the hard heap limit in bytes. Pass negative to query without
/// modifying; returns the previous limit.
SQLITEX_EXPORT int64_t hard_heap_limit(int64_t limit = -1) noexcept;

/// Attempt to free at least `bytes` of heap memory across all connections.
/// Returns the number of bytes actually freed.
SQLITEX_EXPORT int release_memory(int bytes) noexcept;

/// Global runtime status parameters (sqlite3_status64).
enum class StatusOp : int {
    MemoryUsed = SQLITE_STATUS_MEMORY_USED,
    PagecacheUsed = SQLITE_STATUS_PAGECACHE_USED,
    PagecacheOverflow = SQLITE_STATUS_PAGECACHE_OVERFLOW,
    MallocSize = SQLITE_STATUS_MALLOC_SIZE,
    ParserStack = SQLITE_STATUS_PARSER_STACK,
    PagecacheSize = SQLITE_STATUS_PAGECACHE_SIZE,
    MallocCount = SQLITE_STATUS_MALLOC_COUNT,
};

struct StatusResult {
    int64_t current = 0;
    int64_t highwater = 0;
};

/// Query a global SQLite status counter (sqlite3_status64).
SQLITEX_EXPORT StatusResult status(StatusOp op, bool reset = false);

// ============================================================================
// SQL Completeness & Keyword Introspection
// ============================================================================

/// Return true if `sql` appears to be a complete SQL statement
/// (sqlite3_complete).
SQLITEX_EXPORT bool complete(std::string_view sql);

/// Inspect whether `sql` is complete, empty, or incomplete
/// (sqlite3_incomplete). Returns SQLITE_OK (0) when complete, SQLITE_EMPTY (16)
/// when whitespace or comments only, or a non-zero diagnostic code describing
/// what is needed.
SQLITEX_EXPORT int64_t incomplete(std::string_view sql);

/// Total number of SQL keywords understood by SQLite.
SQLITEX_EXPORT int keyword_count() noexcept;

/// Return the Nth SQL keyword, or std::nullopt if out of range.
SQLITEX_EXPORT std::optional<std::string_view> keyword_name(int index) noexcept;

/// Return true if `word` is an SQLite SQL keyword.
SQLITEX_EXPORT bool keyword_check(std::string_view word) noexcept;

// ============================================================================
// URI & Filename Helpers
// ============================================================================

/// RAII wrapper for a SQLite URI filename created with
/// sqlite3_create_filename() and freed with sqlite3_free_filename().
class SQLITEX_EXPORT UriFilename {
    public:
    UriFilename() noexcept = default;

    /// Create a SQLite filename object with optional URI query parameters.
    UriFilename(std::string_view database, std::string_view journal = {},
                std::string_view wal = {},
                std::span<const std::pair<std::string_view, std::string_view>>
                    params = {});

    UriFilename(
        std::string_view database, std::string_view journal,
        std::string_view wal,
        std::initializer_list<std::pair<std::string_view, std::string_view>>
            params)
        : UriFilename(
              database, journal, wal,
              std::span<const std::pair<std::string_view, std::string_view>>(
                  params.begin(), params.size())) {
    }

    ~UriFilename();

    UriFilename(UriFilename&& other) noexcept;
    UriFilename& operator=(UriFilename&& other) noexcept;
    UriFilename(const UriFilename&) = delete;
    UriFilename& operator=(const UriFilename&) = delete;

    explicit operator bool() const noexcept {
        return filename_ != nullptr;
    }

    sqlite3_filename c_str() const noexcept {
        return filename_;
    }

    std::string_view database() const noexcept;
    std::string_view journal() const noexcept;
    std::string_view wal() const noexcept;

    std::optional<std::string_view> parameter(std::string_view name) const;
    bool boolean_parameter(std::string_view name, bool default_val) const;
    int64_t int64_parameter(std::string_view name, int64_t default_val) const;
    std::optional<std::string_view> key(int index) const noexcept;

    private:
    sqlite3_filename filename_ = nullptr;
};

/// Non-owning URI / filename query helpers operating on an existing
/// sqlite3_filename (for example from Database::db_filename() or a VFS).
SQLITEX_EXPORT std::optional<std::string_view> uri_parameter(
    sqlite3_filename filename, std::string_view param);
SQLITEX_EXPORT bool uri_boolean(sqlite3_filename filename,
                                std::string_view param, bool default_val);
SQLITEX_EXPORT int64_t uri_int64(sqlite3_filename filename,
                                 std::string_view param, int64_t default_val);
SQLITEX_EXPORT std::optional<std::string_view> uri_key(
    sqlite3_filename filename, int index) noexcept;

SQLITEX_EXPORT std::string_view filename_database(
    sqlite3_filename filename) noexcept;
SQLITEX_EXPORT std::string_view filename_journal(
    sqlite3_filename filename) noexcept;
SQLITEX_EXPORT std::string_view filename_wal(
    sqlite3_filename filename) noexcept;

/// Return the sqlite3_file* associated with a database filename (from
/// Database::db_filename() or inside a VFS).
SQLITEX_EXPORT sqlite3_file* database_file_object(
    sqlite3_filename filename) noexcept;

// ============================================================================
// VFS Registry
// ============================================================================

/// Find a registered VFS by name (empty string returns the default VFS).
SQLITEX_EXPORT sqlite3_vfs* vfs_find(std::string_view name = {});

/// Register a VFS with SQLite. Throws on error.
SQLITEX_EXPORT void vfs_register(sqlite3_vfs* vfs, bool make_default = false);

/// Unregister a VFS from SQLite. Throws on error.
SQLITEX_EXPORT void vfs_unregister(sqlite3_vfs* vfs);

// ============================================================================
// Global Auto-Extension Registry
// ============================================================================

/// Entry point signature required by sqlite3_auto_extension().
using AutoExtensionEntryPoint = void (*)();

/// Register an automatic extension loaded for every new database connection.
SQLITEX_EXPORT void auto_extension(AutoExtensionEntryPoint entry_point);

/// Cancel a previously registered automatic extension.
/// Returns true if the extension was found and removed.
SQLITEX_EXPORT bool cancel_auto_extension(
    AutoExtensionEntryPoint entry_point) noexcept;

/// Remove all registered automatic extensions.
SQLITEX_EXPORT void reset_auto_extension() noexcept;

} // namespace sqlitex
