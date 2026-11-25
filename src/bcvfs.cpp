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

static_assert(static_cast<int>(BcvfsConfig::CacheSize) == SQLITE_BCV_CACHESIZE);
static_assert(static_cast<int>(BcvfsConfig::NRequest) == SQLITE_BCV_NREQUEST);
static_assert(static_cast<int>(BcvfsConfig::HttpTimeout) ==
              SQLITE_BCV_HTTPTIMEOUT);
static_assert(static_cast<int>(BcvfsConfig::HttpLog) == SQLITE_BCV_CURLVERBOSE);
static_assert(static_cast<int>(BcvfsConfig::HttpLogTimeout) ==
              SQLITE_BCV_HTTPLOG_TIMEOUT);
static_assert(static_cast<int>(BcvfsConfig::HttpLogMaxEntries) ==
              SQLITE_BCV_HTTPLOG_NENTRY);
static_assert(static_cast<int>(BcvfsLogFlag::Http) == SQLITE_BCV_LOG_HTTP);
static_assert(static_cast<int>(BcvfsLogFlag::Upload) == SQLITE_BCV_LOG_UPLOAD);
static_assert(static_cast<int>(BcvfsLogFlag::Cleanup) ==
              SQLITE_BCV_LOG_CLEANUP);
static_assert(static_cast<int>(BcvfsLogFlag::Event) == SQLITE_BCV_LOG_EVENT);
static_assert(static_cast<int>(BcvfsLogFlag::HttpRetry) ==
              SQLITE_BCV_LOG_HTTPRETRY);
static_assert(static_cast<int>(BcvfsAttachFlag::Secure) ==
              SQLITE_BCV_ATTACH_SECURE);
static_assert(static_cast<int>(BcvfsAttachFlag::IfNotExists) ==
              SQLITE_BCV_ATTACH_IFNOT);

int auth_shim(void* ctx, const char* storage, const char* account,
              const char* container, char** auth_token) noexcept {
    auto* cb = static_cast<BcvfsAuthCallback*>(ctx);
    try {
        std::string token;
        bool const ok = (*cb)(storage ? storage : "", account ? account : "",
                              container ? container : "", token);
        if (ok && !token.empty()) {
            *auth_token = sqlite3_mprintf("%s", token.c_str());
        }
        return ok ? SQLITE_OK : SQLITE_DENY;
    } catch (...) {
        return SQLITE_DENY;
    }
}

void log_shim(void* ctx, int mask, const char* msg) noexcept {
    auto* cb = static_cast<BcvfsMaskedLogCallback*>(ctx);
    if (cb && *cb && msg) {
        try {
            (*cb)(BcvfsLogFlags::from_raw(mask), std::string_view(msg));
        } catch (...) {
        }
    }
}

int busy_shim(void* ctx, int retry_count) noexcept {
    auto* cb = static_cast<BcvfsBusyCallback*>(ctx);
    if (!cb || !*cb) {
        return -1; // default cancel
    }
    try {
        auto delay = (*cb)(retry_count + 1); // SQLite counts from 0
        if (!delay) {
            return -1;
        }
        return static_cast<int>(delay->count());
    } catch (...) {
        return -1;
    }
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

Bcvfs& Bcvfs::config(BcvfsConfig op, int64_t val) {
    int rc = sqlite3_bcvfs_config(bcvfs_, static_cast<int>(op),
                                  static_cast<sqlite3_int64>(val));
    if (rc != SQLITE_OK) {
        throw_on_error(rc, "sqlite3_bcvfs_config failed");
    }
    return *this;
}

Bcvfs& Bcvfs::set_cache_size(int64_t bytes) {
    return config(BcvfsConfig::CacheSize, bytes);
}

Bcvfs& Bcvfs::set_max_requests(int n) {
    return config(BcvfsConfig::NRequest, n);
}

Bcvfs& Bcvfs::set_http_timeout(std::chrono::seconds timeout) {
    return config(BcvfsConfig::HttpTimeout, timeout.count());
}

Bcvfs& Bcvfs::set_http_logging(bool enabled) {
    return config(BcvfsConfig::HttpLog, enabled ? 1 : 0);
}

Bcvfs& Bcvfs::set_http_log_timeout(std::chrono::seconds timeout) {
    return config(BcvfsConfig::HttpLogTimeout, timeout.count());
}

Bcvfs& Bcvfs::set_http_log_max_entries(int max_entries) {
    return config(BcvfsConfig::HttpLogMaxEntries, max_entries);
}

// ============================================================================
// Callbacks
// ============================================================================

void Bcvfs::set_auth_callback(BcvfsAuthCallback cb) {
    if (!cb) {
        sqlite3_bcvfs_auth_callback(bcvfs_, nullptr, nullptr);
        auth_cb_.reset();
        return;
    }
    auth_cb_ = std::make_unique<BcvfsAuthCallback>(std::move(cb));
    sqlite3_bcvfs_auth_callback(bcvfs_, static_cast<void*>(auth_cb_.get()),
                                auth_shim);
}

void Bcvfs::set_log_callback(BcvfsLogCallback cb, BcvfsLogFlags mask) {
    if (!cb) {
        set_masked_log_callback({}, BcvfsLogFlag::None);
        return;
    }
    set_masked_log_callback(
        [fn = std::move(cb)](BcvfsLogFlags /*mask*/, std::string_view msg) {
            fn(msg);
        },
        mask);
}

void Bcvfs::set_masked_log_callback(BcvfsMaskedLogCallback cb,
                                    BcvfsLogFlags mask) {
    if (!cb) {
        sqlite3_bcvfs_log_callback(bcvfs_, nullptr, 0, nullptr);
        log_cb_.reset();
        return;
    }
    log_cb_ = std::make_unique<BcvfsMaskedLogCallback>(std::move(cb));
    sqlite3_bcvfs_log_callback(bcvfs_, static_cast<void*>(log_cb_.get()),
                               mask.raw(), log_shim);
}

// ============================================================================
// Container Management
// ============================================================================

void Bcvfs::attach(std::string_view storage, std::string_view account,
                   std::string_view container, std::string_view alias,
                   BcvfsAttachFlags flags) {
    std::string stor(storage);
    std::string acct(account);
    std::string cont(container);
    std::string al(alias);
    char* err = nullptr;

    int rc = sqlite3_bcvfs_attach(bcvfs_, stor.c_str(), acct.c_str(),
                                  cont.c_str(), al.c_str(), flags.raw(), &err);
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
    if (rc != SQLITE_OK) {
        std::string msg(err ? err : "bcvfs_upload failed");
        if (err)
            sqlite3_free(err);
        throw Error(rc < 256 ? map_error_code(rc) : ErrorCode::Error,
                    std::move(msg));
    }

    return rc;
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

    return true;
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

bool Bcvfs::is_daemon() const noexcept {
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
    (void)step(max_requests, timeout);
}

bool PrefetchHandle::step(int max_requests, std::chrono::milliseconds timeout) {
    if (!handle_) {
        return true;
    }
    int rc = sqlite3_bcvfs_prefetch_run(handle_, max_requests,
                                        static_cast<int>(timeout.count()));
    if (rc == SQLITE_DONE) {
        return true;
    }
    if (rc == SQLITE_OK) {
        return false;
    }
    const char* msg = sqlite3_bcvfs_prefetch_errmsg(handle_);
    throw Error(
        rc < 256 ? map_error_code(rc) : ErrorCode::Error,
        msg ? std::string(msg) : std::string("bcvfs_prefetch_run failed"));
}

PrefetchStatus PrefetchHandle::status() const {
    if (!handle_) {
        return PrefetchStatus::Error;
    }
    int err = sqlite3_bcvfs_prefetch_errcode(handle_);
    if (err == SQLITE_DONE) {
        return PrefetchStatus::Done;
    }
    if (err != SQLITE_OK) {
        return PrefetchStatus::Error;
    }
    sqlite3_int64 outstanding = 0;
    int rc = sqlite3_bcvfs_prefetch_status(
        handle_, SQLITE_BCVFS_PFS_NOUTSTANDING, &outstanding);
    if (rc != SQLITE_OK) {
        return PrefetchStatus::Error;
    }
    return PrefetchStatus::Running;
}

int PrefetchHandle::error_code() const noexcept {
    if (!handle_) {
        return SQLITE_MISUSE;
    }
    return sqlite3_bcvfs_prefetch_errcode(handle_);
}

std::string PrefetchHandle::error_message() const {
    if (!handle_) {
        return "invalid prefetch handle";
    }
    const char* msg = sqlite3_bcvfs_prefetch_errmsg(handle_);
    return msg ? std::string(msg) : std::string{};
}

int64_t PrefetchHandle::outstanding_requests() const {
    if (!handle_) {
        return 0;
    }
    sqlite3_int64 val = 0;
    int rc = sqlite3_bcvfs_prefetch_status(handle_,
                                           SQLITE_BCVFS_PFS_NOUTSTANDING, &val);
    throw_on_error(rc, "sqlite3_bcvfs_prefetch_status(NOUTSTANDING) failed");
    return val;
}

int64_t PrefetchHandle::on_demand_blocks() const {
    if (!handle_) {
        return 0;
    }
    sqlite3_int64 val = 0;
    int rc =
        sqlite3_bcvfs_prefetch_status(handle_, SQLITE_BCVFS_PFS_NDEMAND, &val);
    throw_on_error(rc, "sqlite3_bcvfs_prefetch_status(NDEMAND) failed");
    return val;
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

Bcvfs::~Bcvfs() = default;

Bcvfs& Bcvfs::config(BcvfsConfig /*unused*/, int64_t /*unused*/) {
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
Bcvfs& Bcvfs::set_http_log_timeout(std::chrono::seconds /*unused*/) {
    return *this;
}
Bcvfs& Bcvfs::set_http_log_max_entries(int /*unused*/) {
    return *this;
}

void Bcvfs::set_auth_callback(BcvfsAuthCallback /*unused*/) {
}
void Bcvfs::set_log_callback(BcvfsLogCallback /*unused*/,
                             BcvfsLogFlags /*unused*/) {
}
void Bcvfs::set_masked_log_callback(BcvfsMaskedLogCallback /*unused*/,
                                    BcvfsLogFlags /*unused*/) {
}

void Bcvfs::attach(std::string_view /*unused*/, std::string_view /*unused*/,
                   std::string_view /*unused*/, std::string_view /*unused*/,
                   BcvfsAttachFlags /*unused*/) {
}
void Bcvfs::detach(std::string_view /*unused*/) {
}
int Bcvfs::upload(std::string_view /*unused*/, BcvfsBusyCallback /*unused*/) {
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
bool Bcvfs::is_daemon() const noexcept {
    return false;
}
void Bcvfs::register_vtab(Database& /*unused*/) {
}

PrefetchHandle Bcvfs::prefetch(std::string_view /*unused*/,
                               std::string_view /*unused*/) {
    throw Error(ErrorCode::Error, "bcvfs not available");
}

// PrefetchHandle stubs

PrefetchHandle::~PrefetchHandle() = default;
PrefetchHandle::PrefetchHandle(PrefetchHandle&&) noexcept = default;
PrefetchHandle& PrefetchHandle::operator=(PrefetchHandle&&) noexcept = default;
void PrefetchHandle::run(int /*unused*/, std::chrono::milliseconds /*unused*/) {
}
bool PrefetchHandle::step(int /*unused*/,
                          std::chrono::milliseconds /*unused*/) {
    return true;
}
PrefetchStatus PrefetchHandle::status() const {
    return PrefetchStatus::Error;
}
int PrefetchHandle::error_code() const noexcept {
    return SQLITE_ERROR;
}
std::string PrefetchHandle::error_message() const {
    return "bcvfs support not compiled in";
}
int64_t PrefetchHandle::outstanding_requests() const {
    return 0;
}
int64_t PrefetchHandle::on_demand_blocks() const {
    return 0;
}

} // namespace sqlitex

#endif // SQLITEX_HAS_BCVFS
