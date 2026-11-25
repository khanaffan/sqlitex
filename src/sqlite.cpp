#include "sqlitex/sqlite.h"

#include <sqlite3.h>

#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace sqlitex {

// ============================================================================
// Library Version, Compile Options & Error Strings
// ============================================================================

std::string_view libversion() noexcept {
    const char* v = sqlite3_libversion();
    return v ? std::string_view(v) : std::string_view{};
}

int libversion_number() noexcept {
    return sqlite3_libversion_number();
}

std::string_view sourceid() noexcept {
    const char* s = sqlite3_sourceid();
    return s ? std::string_view(s) : std::string_view{};
}

ThreadingMode threadsafe() noexcept {
    switch (sqlite3_threadsafe()) {
    case 0:
        return ThreadingMode::SingleThread;
    case 2:
        return ThreadingMode::MultiThread;
    case 1:
    default:
        return ThreadingMode::Serialized;
    }
}

bool compileoption_used(std::string_view option_name) noexcept {
    std::string opt(option_name);
    return sqlite3_compileoption_used(opt.c_str()) != 0;
}

std::optional<std::string_view> compileoption_get(int index) noexcept {
    const char* opt = sqlite3_compileoption_get(index);
    if (opt == nullptr) {
        return std::nullopt;
    }
    return std::string_view(opt);
}

std::vector<std::string> compile_options() {
    std::vector<std::string> result;
    for (int i = 0;; ++i) {
        const char* opt = sqlite3_compileoption_get(i);
        if (opt == nullptr) {
            break;
        }
        result.emplace_back(opt);
    }
    return result;
}

std::string_view errstr(int sqlite_rc) noexcept {
    const char* s = sqlite3_errstr(sqlite_rc);
    return s ? std::string_view(s) : std::string_view{};
}

// ============================================================================
// Library Lifecycle & Global Configuration
// ============================================================================

void initialize() {
    throw_on_error(sqlite3_initialize(), "sqlite3_initialize failed");
}

void shutdown() {
    throw_on_error(sqlite3_shutdown(), "sqlite3_shutdown failed");
}

void enable_shared_cache(bool enabled) {
    throw_on_error(sqlite3_enable_shared_cache(enabled ? 1 : 0),
                   "sqlite3_enable_shared_cache failed");
}

void log(int errcode, std::string_view message) noexcept {
    std::string msg(message);
    sqlite3_log(errcode, "%s", msg.c_str());
}

void config_threading_mode(ThreadingMode mode) {
    int op = SQLITE_CONFIG_SERIALIZED;
    switch (mode) {
    case ThreadingMode::SingleThread:
        op = SQLITE_CONFIG_SINGLETHREAD;
        break;
    case ThreadingMode::MultiThread:
        op = SQLITE_CONFIG_MULTITHREAD;
        break;
    case ThreadingMode::Serialized:
        op = SQLITE_CONFIG_SERIALIZED;
        break;
    }
    throw_on_error(sqlite3_config(op), "sqlite3_config(threading mode) failed");
}

void config_memstatus(bool enabled) {
    throw_on_error(sqlite3_config(SQLITE_CONFIG_MEMSTATUS, enabled ? 1 : 0),
                   "sqlite3_config(MEMSTATUS) failed");
}

void config_lookaside(int slot_size, int slot_count) {
    throw_on_error(
        sqlite3_config(SQLITE_CONFIG_LOOKASIDE, slot_size, slot_count),
        "sqlite3_config(LOOKASIDE) failed");
}

void config_uri(bool enabled) {
    throw_on_error(sqlite3_config(SQLITE_CONFIG_URI, enabled ? 1 : 0),
                   "sqlite3_config(URI) failed");
}

void config_covering_index_scan(bool enabled) {
    throw_on_error(
        sqlite3_config(SQLITE_CONFIG_COVERING_INDEX_SCAN, enabled ? 1 : 0),
        "sqlite3_config(COVERING_INDEX_SCAN) failed");
}

void config_mmap_size(int64_t default_size, int64_t max_size) {
    throw_on_error(sqlite3_config(SQLITE_CONFIG_MMAP_SIZE,
                                  static_cast<sqlite3_int64>(default_size),
                                  static_cast<sqlite3_int64>(max_size)),
                   "sqlite3_config(MMAP_SIZE) failed");
}

void config_pmasz(unsigned int min_size) {
    throw_on_error(sqlite3_config(SQLITE_CONFIG_PMASZ, min_size),
                   "sqlite3_config(PMASZ) failed");
}

void config_stmtjrnl_spill(int threshold_bytes) {
    throw_on_error(
        sqlite3_config(SQLITE_CONFIG_STMTJRNL_SPILL, threshold_bytes),
        "sqlite3_config(STMTJRNL_SPILL) failed");
}

void config_small_malloc(bool enabled) {
    throw_on_error(sqlite3_config(SQLITE_CONFIG_SMALL_MALLOC, enabled ? 1 : 0),
                   "sqlite3_config(SMALL_MALLOC) failed");
}

void config_sorterref_size(int threshold_bytes) {
    throw_on_error(
        sqlite3_config(SQLITE_CONFIG_SORTERREF_SIZE, threshold_bytes),
        "sqlite3_config(SORTERREF_SIZE) failed");
}

void config_memdb_maxsize(int64_t max_bytes) {
    throw_on_error(sqlite3_config(SQLITE_CONFIG_MEMDB_MAXSIZE,
                                  static_cast<sqlite3_int64>(max_bytes)),
                   "sqlite3_config(MEMDB_MAXSIZE) failed");
}

int config_rowid_in_view(int value) {
    int inout = value;
    throw_on_error(sqlite3_config(SQLITE_CONFIG_ROWID_IN_VIEW, &inout),
                   "sqlite3_config(ROWID_IN_VIEW) failed");
    return inout;
}

int config_pcache_hdrsz() {
    int sz = 0;
    throw_on_error(sqlite3_config(SQLITE_CONFIG_PCACHE_HDRSZ, &sz),
                   "sqlite3_config(PCACHE_HDRSZ) failed");
    return sz;
}

namespace {

std::mutex g_log_mutex;
LogCallback g_log_callback;

void global_log_shim(void* /*ctx*/, int errcode, const char* msg) noexcept {
    LogCallback cb;
    {
        std::lock_guard<std::mutex> lock(g_log_mutex);
        cb = g_log_callback;
    }
    if (cb) {
        try {
            cb(errcode, msg ? std::string_view(msg) : std::string_view{});
        } catch (...) {
            // Never throw across the C boundary.
        }
    }
}

} // anonymous namespace

void config_log(LogCallback callback) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (callback) {
        int rc = sqlite3_config(SQLITE_CONFIG_LOG, &global_log_shim, nullptr);
        throw_on_error(rc, "sqlite3_config(LOG) failed");
        g_log_callback = std::move(callback);
    } else {
        int rc = sqlite3_config(SQLITE_CONFIG_LOG, nullptr, nullptr);
        throw_on_error(rc, "sqlite3_config(LOG) clear failed");
        g_log_callback = nullptr;
    }
}

// ============================================================================
// Global Memory & Runtime Status
// ============================================================================

int64_t memory_used() noexcept {
    return sqlite3_memory_used();
}

int64_t memory_highwater(bool reset) noexcept {
    return sqlite3_memory_highwater(reset ? 1 : 0);
}

int64_t soft_heap_limit(int64_t limit) noexcept {
    return sqlite3_soft_heap_limit64(static_cast<sqlite3_int64>(limit));
}

int64_t hard_heap_limit(int64_t limit) noexcept {
    return sqlite3_hard_heap_limit64(static_cast<sqlite3_int64>(limit));
}

int release_memory(int bytes) noexcept {
    return sqlite3_release_memory(bytes);
}

StatusResult status(StatusOp op, bool reset) {
    sqlite3_int64 current = 0;
    sqlite3_int64 highwater = 0;
    int rc = sqlite3_status64(static_cast<int>(op), &current, &highwater,
                              reset ? 1 : 0);
    throw_on_error(rc, "sqlite3_status64 failed");
    return {current, highwater};
}

// ============================================================================
// SQL Completeness & Keyword Introspection
// ============================================================================

bool complete(std::string_view sql) {
    std::string s(sql);
    int rc = sqlite3_complete(s.c_str());
    if (rc == SQLITE_NOMEM) {
        throw std::bad_alloc();
    }
    return rc != 0;
}

int64_t incomplete(std::string_view sql) {
    std::string s(sql);
    return sqlite3_incomplete(s.c_str());
}

int keyword_count() noexcept {
    return sqlite3_keyword_count();
}

std::optional<std::string_view> keyword_name(int index) noexcept {
    const char* name = nullptr;
    int len = 0;
    int rc = sqlite3_keyword_name(index, &name, &len);
    if (rc != SQLITE_OK || name == nullptr) {
        return std::nullopt;
    }
    return std::string_view(name, static_cast<std::size_t>(len));
}

bool keyword_check(std::string_view word) noexcept {
    return sqlite3_keyword_check(word.data(), static_cast<int>(word.size())) !=
           0;
}

// ============================================================================
// URI & Filename Helpers
// ============================================================================

UriFilename::UriFilename(
    std::string_view database, std::string_view journal, std::string_view wal,
    std::span<const std::pair<std::string_view, std::string_view>> params) {
    std::string db_str(database);
    std::string jr_str(journal);
    std::string wal_str(wal);

    std::vector<std::string> param_storage;
    param_storage.reserve(params.size() * 2);
    for (const auto& [k, v] : params) {
        param_storage.emplace_back(k);
        param_storage.emplace_back(v);
    }

    std::vector<const char*> param_ptrs;
    param_ptrs.reserve(param_storage.size());
    for (const auto& s : param_storage) {
        param_ptrs.push_back(s.c_str());
    }

    filename_ = sqlite3_create_filename(
        db_str.c_str(), jr_str.c_str(), wal_str.c_str(),
        static_cast<int>(params.size()),
        param_ptrs.empty() ? nullptr : param_ptrs.data());
    if (filename_ == nullptr) {
        throw std::bad_alloc();
    }
}

UriFilename::~UriFilename() {
    if (filename_ != nullptr) {
        sqlite3_free_filename(filename_);
    }
}

UriFilename::UriFilename(UriFilename&& other) noexcept
    : filename_(std::exchange(other.filename_, nullptr)) {
}

UriFilename& UriFilename::operator=(UriFilename&& other) noexcept {
    if (this != &other) {
        if (filename_ != nullptr) {
            sqlite3_free_filename(filename_);
        }
        filename_ = std::exchange(other.filename_, nullptr);
    }
    return *this;
}

std::string_view UriFilename::database() const noexcept {
    return filename_database(filename_);
}

std::string_view UriFilename::journal() const noexcept {
    return filename_journal(filename_);
}

std::string_view UriFilename::wal() const noexcept {
    return filename_wal(filename_);
}

std::optional<std::string_view> UriFilename::parameter(
    std::string_view name) const {
    return uri_parameter(filename_, name);
}

bool UriFilename::boolean_parameter(std::string_view name,
                                    bool default_val) const {
    return uri_boolean(filename_, name, default_val);
}

int64_t UriFilename::int64_parameter(std::string_view name,
                                     int64_t default_val) const {
    return uri_int64(filename_, name, default_val);
}

std::optional<std::string_view> UriFilename::key(int index) const noexcept {
    return uri_key(filename_, index);
}

std::optional<std::string_view> uri_parameter(sqlite3_filename filename,
                                              std::string_view param) {
    if (filename == nullptr) {
        return std::nullopt;
    }
    std::string p(param);
    const char* v = sqlite3_uri_parameter(filename, p.c_str());
    if (v == nullptr) {
        return std::nullopt;
    }
    return std::string_view(v);
}

bool uri_boolean(sqlite3_filename filename, std::string_view param,
                 bool default_val) {
    if (filename == nullptr) {
        return default_val;
    }
    std::string p(param);
    return sqlite3_uri_boolean(filename, p.c_str(), default_val ? 1 : 0) != 0;
}

int64_t uri_int64(sqlite3_filename filename, std::string_view param,
                  int64_t default_val) {
    if (filename == nullptr) {
        return default_val;
    }
    std::string p(param);
    return sqlite3_uri_int64(filename, p.c_str(),
                             static_cast<sqlite3_int64>(default_val));
}

std::optional<std::string_view> uri_key(sqlite3_filename filename,
                                        int index) noexcept {
    if (filename == nullptr) {
        return std::nullopt;
    }
    const char* k = sqlite3_uri_key(filename, index);
    if (k == nullptr) {
        return std::nullopt;
    }
    return std::string_view(k);
}

std::string_view filename_database(sqlite3_filename filename) noexcept {
    if (filename == nullptr) {
        return {};
    }
    const char* s = sqlite3_filename_database(filename);
    return s ? std::string_view(s) : std::string_view{};
}

std::string_view filename_journal(sqlite3_filename filename) noexcept {
    if (filename == nullptr) {
        return {};
    }
    const char* s = sqlite3_filename_journal(filename);
    return s ? std::string_view(s) : std::string_view{};
}

std::string_view filename_wal(sqlite3_filename filename) noexcept {
    if (filename == nullptr) {
        return {};
    }
    const char* s = sqlite3_filename_wal(filename);
    return s ? std::string_view(s) : std::string_view{};
}

sqlite3_file* database_file_object(sqlite3_filename filename) noexcept {
    if (filename == nullptr) {
        return nullptr;
    }
    return sqlite3_database_file_object(filename);
}

// ============================================================================
// VFS Registry
// ============================================================================

sqlite3_vfs* vfs_find(std::string_view name) {
    if (name.empty()) {
        return sqlite3_vfs_find(nullptr);
    }
    std::string n(name);
    return sqlite3_vfs_find(n.c_str());
}

void vfs_register(sqlite3_vfs* vfs, bool make_default) {
    throw_on_error(sqlite3_vfs_register(vfs, make_default ? 1 : 0),
                   "sqlite3_vfs_register failed");
}

void vfs_unregister(sqlite3_vfs* vfs) {
    throw_on_error(sqlite3_vfs_unregister(vfs),
                   "sqlite3_vfs_unregister failed");
}

// ============================================================================
// Global Auto-Extension Registry
// ============================================================================

void auto_extension(AutoExtensionEntryPoint entry_point) {
    throw_on_error(sqlite3_auto_extension(entry_point),
                   "sqlite3_auto_extension failed");
}

bool cancel_auto_extension(AutoExtensionEntryPoint entry_point) noexcept {
    return sqlite3_cancel_auto_extension(entry_point) != 0;
}

void reset_auto_extension() noexcept {
    sqlite3_reset_auto_extension();
}

} // namespace sqlitex
