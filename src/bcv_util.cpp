#include "sqlitex/bcv_util.h"

#include <sqlite3.h>

#include <string>
#include <utility>

#ifdef SQLITEX_HAS_BCVFS

#include "bcvutil.h"

namespace sqlitex {

namespace {

int bcv_progress_shim(void* ctx, sqlite3_int64 done,
                      sqlite3_int64 total) noexcept {
    auto* cb = static_cast<BcvProgressCallback*>(ctx);
    if (!cb || !*cb) {
        return SQLITE_OK;
    }
    try {
        return (*cb)(static_cast<int64_t>(done), static_cast<int64_t>(total));
    } catch (...) {
        return SQLITE_ABORT;
    }
}

void bcv_log_shim(void* ctx, const char* msg) noexcept {
    auto* cb = static_cast<BcvLogCallback*>(ctx);
    if (!cb || !*cb || msg == nullptr) {
        return;
    }
    try {
        (*cb)(std::string_view(msg));
    } catch (...) {
    }
}

void throw_bcv_error(int rc, sqlite3_bcv* h, const char* fallback) {
    if (rc == SQLITE_OK) {
        return;
    }
    const char* raw_msg = h ? sqlite3_bcv_errmsg(h) : nullptr;
    std::string msg =
        (raw_msg != nullptr) ? std::string(raw_msg) : std::string(fallback);
    if (rc >= 400) {
        msg = "HTTP " + std::to_string(rc) + ": " + msg;
        throw Error(ErrorCode::Error, std::move(msg));
    }
    throw Error(map_error_code(rc), std::move(msg));
}

} // anonymous namespace

// ============================================================================
// Construction / Destruction
// ============================================================================

BcvClient::BcvClient(std::string_view module, std::string_view user,
                     std::string_view auth, std::string_view container) {
    std::string mod_s(module);
    std::string user_s(user);
    std::string auth_s(auth);
    std::string cont_s(container);

    sqlite3_bcv* h = nullptr;
    int const rc = sqlite3_bcv_open(mod_s.c_str(), user_s.c_str(),
                                    auth_s.c_str(), cont_s.c_str(), &h);
    if (rc != SQLITE_OK) {
        std::string msg = (h && sqlite3_bcv_errmsg(h))
                              ? std::string(sqlite3_bcv_errmsg(h))
                              : std::string("sqlite3_bcv_open failed");
        if (h != nullptr) {
            sqlite3_bcv_close(h);
        }
        throw Error(rc < 256 ? map_error_code(rc) : ErrorCode::Error,
                    std::move(msg));
    }
    handle_ = h;
}

BcvClient::~BcvClient() {
    if (handle_ != nullptr) {
        sqlite3_bcv_close(handle_);
    }
}

BcvClient::BcvClient(BcvClient&& other) noexcept
    : handle_(std::exchange(other.handle_, nullptr)),
      progress_cb_(std::move(other.progress_cb_)),
      log_cb_(std::move(other.log_cb_)) {
}

BcvClient& BcvClient::operator=(BcvClient&& other) noexcept {
    if (this != &other) {
        if (handle_ != nullptr) {
            sqlite3_bcv_close(handle_);
        }
        handle_ = std::exchange(other.handle_, nullptr);
        progress_cb_ = std::move(other.progress_cb_);
        log_cb_ = std::move(other.log_cb_);
    }
    return *this;
}

// ============================================================================
// Configuration
// ============================================================================

BcvClient& BcvClient::set_verbose(bool enabled) {
    int const rc =
        sqlite3_bcv_config(handle_, SQLITE_BCVCONFIG_VERBOSE, enabled ? 1 : 0);
    throw_bcv_error(rc, handle_, "sqlite3_bcv_config(VERBOSE) failed");
    return *this;
}

BcvClient& BcvClient::set_max_requests(int n) {
    int const rc = sqlite3_bcv_config(handle_, SQLITE_BCVCONFIG_NREQUEST, n);
    throw_bcv_error(rc, handle_, "sqlite3_bcv_config(NREQUEST) failed");
    return *this;
}

BcvClient& BcvClient::set_log_level(int level) {
    int const rc =
        sqlite3_bcv_config(handle_, SQLITE_BCVCONFIG_LOGLEVEL, level);
    throw_bcv_error(rc, handle_, "sqlite3_bcv_config(LOGLEVEL) failed");
    return *this;
}

BcvClient& BcvClient::set_http_timeout(std::chrono::seconds timeout) {
    int const rc = sqlite3_bcv_config(handle_, SQLITE_BCVCONFIG_HTTPTIMEOUT,
                                      static_cast<int>(timeout.count()));
    throw_bcv_error(rc, handle_, "sqlite3_bcv_config(HTTPTIMEOUT) failed");
    return *this;
}

BcvClient& BcvClient::set_find_orphans(bool enabled) {
    int const rc = sqlite3_bcv_config(handle_, SQLITE_BCVCONFIG_FINDORPHANS,
                                      enabled ? 1 : 0);
    throw_bcv_error(rc, handle_, "sqlite3_bcv_config(FINDORPHANS) failed");
    return *this;
}

BcvClient& BcvClient::set_test_no_kv(bool enabled) {
    int const rc =
        sqlite3_bcv_config(handle_, SQLITE_BCVCONFIG_TESTNOKV, enabled ? 1 : 0);
    throw_bcv_error(rc, handle_, "sqlite3_bcv_config(TESTNOKV) failed");
    return *this;
}

BcvClient& BcvClient::set_progress_callback(BcvProgressCallback cb) {
    if (!cb) {
        int (*null_fn)(void*, sqlite3_int64, sqlite3_int64) = nullptr;
        int const rc = sqlite3_bcv_config(handle_, SQLITE_BCVCONFIG_PROGRESS,
                                          static_cast<void*>(nullptr), null_fn);
        throw_bcv_error(rc, handle_, "sqlite3_bcv_config(PROGRESS) failed");
        progress_cb_.reset();
        return *this;
    }
    auto next = std::make_unique<BcvProgressCallback>(std::move(cb));
    int const rc =
        sqlite3_bcv_config(handle_, SQLITE_BCVCONFIG_PROGRESS,
                           static_cast<void*>(next.get()), bcv_progress_shim);
    throw_bcv_error(rc, handle_, "sqlite3_bcv_config(PROGRESS) failed");
    progress_cb_ = std::move(next);
    return *this;
}

BcvClient& BcvClient::set_log_callback(BcvLogCallback cb) {
    if (!cb) {
        void (*null_fn)(void*, const char*) = nullptr;
        int const rc = sqlite3_bcv_config(handle_, SQLITE_BCVCONFIG_LOG,
                                          static_cast<void*>(nullptr), null_fn);
        throw_bcv_error(rc, handle_, "sqlite3_bcv_config(LOG) failed");
        log_cb_.reset();
        return *this;
    }
    auto next = std::make_unique<BcvLogCallback>(std::move(cb));
    int const rc =
        sqlite3_bcv_config(handle_, SQLITE_BCVCONFIG_LOG,
                           static_cast<void*>(next.get()), bcv_log_shim);
    throw_bcv_error(rc, handle_, "sqlite3_bcv_config(LOG) failed");
    log_cb_ = std::move(next);
    return *this;
}

// ============================================================================
// Operations
// ============================================================================

void BcvClient::create(int name_size, int block_size) {
    int const rc = sqlite3_bcv_create(handle_, name_size, block_size);
    throw_bcv_error(rc, handle_, "sqlite3_bcv_create failed");
}

void BcvClient::destroy() {
    int const rc = sqlite3_bcv_destroy(handle_);
    throw_bcv_error(rc, handle_, "sqlite3_bcv_destroy failed");
}

void BcvClient::upload(std::string_view local_path,
                       std::string_view remote_name) {
    std::string local_s(local_path);
    std::string remote_s(remote_name);
    int const rc =
        sqlite3_bcv_upload(handle_, local_s.c_str(), remote_s.c_str());
    throw_bcv_error(rc, handle_, "sqlite3_bcv_upload failed");
}

void BcvClient::download(std::string_view remote_name,
                         std::string_view local_path) {
    std::string remote_s(remote_name);
    std::string local_s(local_path);
    int const rc =
        sqlite3_bcv_download(handle_, remote_s.c_str(), local_s.c_str());
    throw_bcv_error(rc, handle_, "sqlite3_bcv_download failed");
}

void BcvClient::copy(std::string_view from_name, std::string_view to_name) {
    std::string from_s(from_name);
    std::string to_s(to_name);
    int const rc = sqlite3_bcv_copy(handle_, from_s.c_str(), to_s.c_str());
    throw_bcv_error(rc, handle_, "sqlite3_bcv_copy failed");
}

void BcvClient::remove(std::string_view remote_name) {
    std::string remote_s(remote_name);
    int const rc = sqlite3_bcv_delete(handle_, remote_s.c_str());
    throw_bcv_error(rc, handle_, "sqlite3_bcv_delete failed");
}

void BcvClient::cleanup(int age_seconds) {
    int const rc = sqlite3_bcv_cleanup(handle_, age_seconds);
    throw_bcv_error(rc, handle_, "sqlite3_bcv_cleanup failed");
}

// ============================================================================
// Diagnostics
// ============================================================================

int BcvClient::errcode() const noexcept {
    return handle_ ? sqlite3_bcv_errcode(handle_) : SQLITE_MISUSE;
}

std::string BcvClient::errmsg() const {
    if (handle_ == nullptr) {
        return {};
    }
    const char* msg = sqlite3_bcv_errmsg(handle_);
    return msg ? std::string(msg) : std::string{};
}

} // namespace sqlitex

#else // !SQLITEX_HAS_BCVFS

namespace sqlitex {

BcvClient::BcvClient(std::string_view /*module*/, std::string_view /*user*/,
                     std::string_view /*auth*/,
                     std::string_view /*container*/) {
    throw Error(ErrorCode::Error, "bcvfs support not compiled in");
}

BcvClient::~BcvClient() = default;
BcvClient::BcvClient(BcvClient&&) noexcept = default;
BcvClient& BcvClient::operator=(BcvClient&&) noexcept = default;

BcvClient& BcvClient::set_verbose(bool /*unused*/) {
    return *this;
}
BcvClient& BcvClient::set_max_requests(int /*unused*/) {
    return *this;
}
BcvClient& BcvClient::set_log_level(int /*unused*/) {
    return *this;
}
BcvClient& BcvClient::set_http_timeout(std::chrono::seconds /*unused*/) {
    return *this;
}
BcvClient& BcvClient::set_find_orphans(bool /*unused*/) {
    return *this;
}
BcvClient& BcvClient::set_test_no_kv(bool /*unused*/) {
    return *this;
}
BcvClient& BcvClient::set_progress_callback(BcvProgressCallback /*unused*/) {
    return *this;
}
BcvClient& BcvClient::set_log_callback(BcvLogCallback /*unused*/) {
    return *this;
}

void BcvClient::create(int /*unused*/, int /*unused*/) {
}
void BcvClient::destroy() {
}
void BcvClient::upload(std::string_view /*unused*/,
                       std::string_view /*unused*/) {
}
void BcvClient::download(std::string_view /*unused*/,
                         std::string_view /*unused*/) {
}
void BcvClient::copy(std::string_view /*unused*/, std::string_view /*unused*/) {
}
void BcvClient::remove(std::string_view /*unused*/) {
}
void BcvClient::cleanup(int /*unused*/) {
}

int BcvClient::errcode() const noexcept {
    return SQLITE_ERROR;
}
std::string BcvClient::errmsg() const {
    return "bcvfs support not compiled in";
}

} // namespace sqlitex

#endif // SQLITEX_HAS_BCVFS
