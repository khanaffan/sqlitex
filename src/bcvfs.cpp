#include "sqlitex/bcvfs.h"
#include "sqlitex/database.h"
#include "sqlitex/prefetch.h"

#include <sqlite3.h>

#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

// ============================================================================
// When bcvfs is available
// ============================================================================

#ifdef SQLITEX_HAS_BCVFS

#include "blockcachevfs.h"

namespace sqlitex {

// ============================================================================
// C callback shims
// ============================================================================

namespace {

int auth_shim(void* ctx, const char* storage, const char* account,
              const char* container, char** auth_token) {
    auto* cb = static_cast<BcvfsAuthCallback*>(ctx);
    std::string token;
    bool ok = (*cb)(storage ? storage : "", account ? account : "",
                    container ? container : "", token);
    if (ok && !token.empty()) {
        *auth_token = sqlite3_mprintf("%s", token.c_str());
    }
    return ok ? SQLITE_OK : SQLITE_DENY;
}

void log_shim(void* ctx, int /*mask*/, const char* msg) {
    auto* cb = static_cast<BcvfsLogCallback*>(ctx);
    if (msg)
        (*cb)(std::string_view(msg));
}

int busy_shim(void* ctx, int retry_count) {
    auto* cb = static_cast<BcvfsBusyCallback*>(ctx);
    if (!cb || !*cb)
        return -1;                       // default cancel
    auto delay = (*cb)(retry_count + 1); // SQLite counts from 0
    if (!delay)
        return -1;
    return static_cast<int>(delay->count());
}

} // anonymous namespace

// ============================================================================
// Construction / Destruction
// ============================================================================

Bcvfs::Bcvfs(std::string_view cache_dir, std::string_view vfs_name)
    : vfs_name_(vfs_name) {
    std::string dir(cache_dir);
    std::string name(vfs_name);
    char* err = nullptr;

    int rc = sqlite3_bcvfs_create(dir.c_str(), name.c_str(), &bcvfs_, &err);
    if (rc != SQLITE_OK) {
        std::string msg(err ? err : "bcvfs_create failed");
        if (err)
            sqlite3_free(err);
        throw Error(ErrorCode::Error, std::move(msg));
    }
}

Bcvfs::~Bcvfs() {
    if (bcvfs_) {
        sqlite3_bcvfs_destroy(bcvfs_);
    }
}

Bcvfs::Bcvfs(Bcvfs&& other) noexcept
    : bcvfs_(std::exchange(other.bcvfs_, nullptr)),
      vfs_name_(std::move(other.vfs_name_)),
      auth_cb_(std::move(other.auth_cb_)),
      log_cb_(std::move(other.log_cb_)) {
}

Bcvfs& Bcvfs::operator=(Bcvfs&& other) noexcept {
    if (this != &other) {
        if (bcvfs_)
            sqlite3_bcvfs_destroy(bcvfs_);
        bcvfs_ = std::exchange(other.bcvfs_, nullptr);
        vfs_name_ = std::move(other.vfs_name_);
        auth_cb_ = std::move(other.auth_cb_);
        log_cb_ = std::move(other.log_cb_);
    }
    return *this;
}

// ============================================================================
// Configuration
// ============================================================================

Bcvfs& Bcvfs::set_cache_size(int64_t bytes) {
    sqlite3_bcvfs_config(bcvfs_, static_cast<int>(BcvfsConfig::CacheSize),
                         bytes);
    return *this;
}

Bcvfs& Bcvfs::set_max_requests(int n) {
    sqlite3_bcvfs_config(bcvfs_, static_cast<int>(BcvfsConfig::NRequest), n);
    return *this;
}

Bcvfs& Bcvfs::set_http_timeout(std::chrono::seconds timeout) {
    sqlite3_bcvfs_config(bcvfs_, static_cast<int>(BcvfsConfig::HttpTimeout),
                         static_cast<sqlite3_int64>(timeout.count()));
    return *this;
}

Bcvfs& Bcvfs::set_http_logging(bool enabled) {
    sqlite3_bcvfs_config(bcvfs_, static_cast<int>(BcvfsConfig::HttpLog),
                         enabled ? 1 : 0);
    return *this;
}

// ============================================================================
// Callbacks
// ============================================================================

void Bcvfs::set_auth_callback(BcvfsAuthCallback cb) {
    auth_cb_ = std::make_unique<BcvfsAuthCallback>(std::move(cb));
    sqlite3_bcvfs_auth_callback(bcvfs_, static_cast<void*>(auth_cb_.get()),
                                auth_shim);
}

void Bcvfs::set_log_callback(BcvfsLogCallback cb) {
    log_cb_ = std::make_unique<BcvfsLogCallback>(std::move(cb));
    sqlite3_bcvfs_log_callback(bcvfs_, static_cast<void*>(log_cb_.get()), 0xFF,
                               log_shim);
}

// ============================================================================
// Container Management
// ============================================================================

void Bcvfs::attach(std::string_view storage, std::string_view account,
                   std::string_view container, std::string_view alias) {
    std::string stor(storage);
    std::string acct(account);
    std::string cont(container);
    std::string al(alias);
    char* err = nullptr;

    int rc = sqlite3_bcvfs_attach(bcvfs_, stor.c_str(), acct.c_str(),
                                  cont.c_str(), al.c_str(), 0, &err);
    if (rc != SQLITE_OK) {
        std::string msg(err ? err : "bcvfs_attach failed");
        if (err)
            sqlite3_free(err);
        throw Error(map_error_code(rc), std::move(msg));
    }
}

void Bcvfs::detach(std::string_view alias) {
    std::string al(alias);
    char* err = nullptr;

    int rc = sqlite3_bcvfs_detach(bcvfs_, al.c_str(), &err);
    if (rc != SQLITE_OK) {
        std::string msg(err ? err : "bcvfs_detach failed");
        if (err)
            sqlite3_free(err);
        throw Error(map_error_code(rc), std::move(msg));
    }
}

// ============================================================================
// Cloud Operations
// ============================================================================

int Bcvfs::upload(std::string_view container, BcvfsBusyCallback busy_cb) {
    std::string cont(container);
    char* err = nullptr;

    // busy_ptr is a local unique_ptr — safe because sqlite3_bcvfs_upload
    // invokes the busy callback synchronously during the call.
    auto busy_ptr =
        busy_cb ? std::make_unique<BcvfsBusyCallback>(std::move(busy_cb))
                : nullptr;

    void* ctx = busy_ptr ? static_cast<void*>(busy_ptr.get()) : nullptr;
    int (*busy_fn)(void*, int) = busy_ptr ? busy_shim : nullptr;

    int rc = sqlite3_bcvfs_upload(bcvfs_, cont.c_str(), busy_fn, ctx, &err);
    if (rc < 0) {
        std::string msg(err ? err : "bcvfs_upload failed");
        if (err)
            sqlite3_free(err);
        throw Error(map_error_code(rc), std::move(msg));
    }

    return rc; // Number of databases uploaded
}

bool Bcvfs::poll(std::string_view container) {
    std::string cont(container);
    char* err = nullptr;

    int rc = sqlite3_bcvfs_poll(bcvfs_, cont.c_str(), &err);
    if (rc != SQLITE_OK) {
        std::string msg(err ? err : "bcvfs_poll failed");
        if (err)
            sqlite3_free(err);
        throw Error(map_error_code(rc), std::move(msg));
    }

    return rc == SQLITE_OK;
}

void Bcvfs::copy(std::string_view container, std::string_view from_db,
                 std::string_view to_db) {
    std::string cont(container);
    std::string from(from_db);
    std::string to(to_db);
    char* err = nullptr;

    int rc = sqlite3_bcvfs_copy(bcvfs_, cont.c_str(), from.c_str(), to.c_str(),
                                &err);
    if (rc != SQLITE_OK) {
        std::string msg(err ? err : "bcvfs_copy failed");
        if (err)
            sqlite3_free(err);
        throw Error(map_error_code(rc), std::move(msg));
    }
}

void Bcvfs::remove(std::string_view container, std::string_view db_name) {
    std::string cont(container);
    std::string db(db_name);
    char* err = nullptr;

    int rc = sqlite3_bcvfs_delete(bcvfs_, cont.c_str(), db.c_str(), &err);
    if (rc != SQLITE_OK) {
        std::string msg(err ? err : "bcvfs_delete failed");
        if (err)
            sqlite3_free(err);
        throw Error(map_error_code(rc), std::move(msg));
    }
}

void Bcvfs::revert(std::string_view container) {
    std::string cont(container);
    char* err = nullptr;

    int rc = sqlite3_bcvfs_revert(bcvfs_, cont.c_str(), &err);
    if (rc != SQLITE_OK) {
        std::string msg(err ? err : "bcvfs_revert failed");
        if (err)
            sqlite3_free(err);
        throw Error(map_error_code(rc), std::move(msg));
    }
}

bool Bcvfs::is_daemon() noexcept {
    return sqlite3_bcvfs_isdaemon(const_cast<sqlite3_bcvfs*>(bcvfs_)) != 0;
}

void Bcvfs::register_vtab(Database& db) {
    throw_on_error(sqlite3_bcvfs_register_vtab(db.handle()), db.handle());
}

PrefetchHandle Bcvfs::prefetch(std::string_view container,
                               std::string_view db_name) {
    std::string cont(container);
    std::string db(db_name);
    sqlite3_prefetch* p = nullptr;

    int rc = sqlite3_bcvfs_prefetch_new(bcvfs_, cont.c_str(), db.c_str(), &p);
    throw_on_error(rc, "bcvfs_prefetch_new failed");

    return PrefetchHandle(p);
}

// ============================================================================
// PrefetchHandle
// ============================================================================

PrefetchHandle::PrefetchHandle(sqlite3_prefetch* handle) noexcept
    : handle_(handle) {
}

PrefetchHandle::~PrefetchHandle() {
    if (handle_) {
        sqlite3_bcvfs_prefetch_destroy(handle_);
    }
}

PrefetchHandle::PrefetchHandle(PrefetchHandle&& other) noexcept
    : handle_(std::exchange(other.handle_, nullptr)) {
}

PrefetchHandle& PrefetchHandle::operator=(PrefetchHandle&& other) noexcept {
    if (this != &other) {
        if (handle_)
            sqlite3_bcvfs_prefetch_destroy(handle_);
        handle_ = std::exchange(other.handle_, nullptr);
    }
    return *this;
}

void PrefetchHandle::run(int max_requests, std::chrono::milliseconds timeout) {
    if (!handle_) {
        return;
    }
    int rc = sqlite3_bcvfs_prefetch_run(handle_, max_requests,
                                        static_cast<int>(timeout.count()));
    throw_on_error(rc, "bcvfs_prefetch_run failed");
}

PrefetchStatus PrefetchHandle::status() {
    if (!handle_) {
        return PrefetchStatus::Error;
    }
    sqlite3_int64 running = 0;
    sqlite3_bcvfs_prefetch_status(handle_, 0, &running);
    if (running)
        return PrefetchStatus::Running;

    sqlite3_int64 errors = 0;
    sqlite3_bcvfs_prefetch_status(handle_, 2, &errors);
    if (errors > 0)
        return PrefetchStatus::Error;

    return PrefetchStatus::Done;
}

std::string PrefetchHandle::error_message() const {
    const char* msg = sqlite3_bcvfs_prefetch_errmsg(handle_);
    return msg ? std::string(msg) : std::string{};
}

} // namespace sqlitex

#else // !SQLITEX_HAS_BCVFS

// ============================================================================
// Stub implementations when bcvfs is not available
// ============================================================================

namespace sqlitex {

Bcvfs::Bcvfs(std::string_view /*cache_dir*/, std::string_view vfs_name)
    : vfs_name_(vfs_name) {
    throw Error(ErrorCode::Error,
                "bcvfs support not compiled in. Rebuild with "
                "blockcachevfs.h and blockcachevfs.c from the SQLite "
                "source tree (ext/bcvfs/).");
}

Bcvfs::Bcvfs(Bcvfs&& other) noexcept
    : vfs_name_(std::move(other.vfs_name_)),
      auth_cb_(std::move(other.auth_cb_)),
      log_cb_(std::move(other.log_cb_)) {
}

Bcvfs& Bcvfs::operator=(Bcvfs&& other) noexcept {
    if (this != &other) {
        vfs_name_ = std::move(other.vfs_name_);
        auth_cb_ = std::move(other.auth_cb_);
        log_cb_ = std::move(other.log_cb_);
    }
    return *this;
}

Bcvfs& Bcvfs::set_cache_size(int64_t /*unused*/) {
    return *this;
}
Bcvfs& Bcvfs::set_max_requests(int /*unused*/) {
    return *this;
}
Bcvfs& Bcvfs::set_http_timeout(std::chrono::seconds /*unused*/) {
    return *this;
}
Bcvfs& Bcvfs::set_http_logging(bool /*unused*/) {
    return *this;
}

void Bcvfs::set_auth_callback(BcvfsAuthCallback /*unused*/) {
}
void Bcvfs::set_log_callback(BcvfsLogCallback /*unused*/) {
}

void Bcvfs::attach(std::string_view /*unused*/, std::string_view /*unused*/,
                   std::string_view /*unused*/, std::string_view /*unused*/) {
}
void Bcvfs::detach(std::string_view /*unused*/) {
}
int Bcvfs::upload(std::string_view /*unused*/,
                         BcvfsBusyCallback /*unused*/) {
    return 0;
}
bool Bcvfs::poll(std::string_view /*unused*/) {
    return false;
}
void Bcvfs::copy(std::string_view /*unused*/, std::string_view /*unused*/,
                 std::string_view /*unused*/) {
}
void Bcvfs::remove(std::string_view /*unused*/, std::string_view /*unused*/) {
}
void Bcvfs::revert(std::string_view /*unused*/) {
}
void Bcvfs::register_vtab(Database& /*unused*/) {
}

PrefetchHandle Bcvfs::prefetch(std::string_view /*unused*/,
                                      std::string_view /*unused*/) {
    throw Error(ErrorCode::Error, "bcvfs not available");
}

// PrefetchHandle stubs

PrefetchHandle::PrefetchHandle(PrefetchHandle&&) noexcept = default;
PrefetchHandle& PrefetchHandle::operator=(PrefetchHandle&&) noexcept = default;
void PrefetchHandle::run(int /*unused*/, std::chrono::milliseconds /*unused*/) {
}
PrefetchStatus PrefetchHandle::status() {
    return PrefetchStatus::Error;
}

} // namespace sqlitex

#endif // SQLITEX_HAS_BCVFS
