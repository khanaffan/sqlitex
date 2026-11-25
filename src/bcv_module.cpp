#include "sqlitex/bcv_module.h"

#include <sqlite3.h>

#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef SQLITEX_HAS_BCVFS

#include "bcvmodule.h"

namespace sqlitex {

namespace {

static_assert(static_cast<int>(BcvHttpMethod::Get) == SQLITE_BCV_METHOD_GET);
static_assert(static_cast<int>(BcvHttpMethod::Put) == SQLITE_BCV_METHOD_PUT);
static_assert(static_cast<int>(BcvHttpMethod::Delete) ==
              SQLITE_BCV_METHOD_DELETE);
static_assert(static_cast<int>(BcvHttpMethod::Head) == SQLITE_BCV_METHOD_HEAD);
static_assert(static_cast<int>(BcvFetchCondition::Md5) ==
              BCV_MODULE_CONDITION_MD5);
static_assert(static_cast<int>(BcvFetchCondition::ETag) ==
              BCV_MODULE_CONDITION_ETAG);

struct ContainerHolder {
    std::unique_ptr<BcvContainer> impl;
};

std::mutex& module_registry_mutex() {
    static std::mutex m;
    return m;
}

std::unordered_map<std::string, std::unique_ptr<BcvModuleFactory>>&
module_registry() {
    static std::unordered_map<std::string, std::unique_ptr<BcvModuleFactory>>
        reg;
    return reg;
}

void bcv_reply_shim(sqlite3_bcv_job* job, sqlite3_bcv_request* req,
                    void* ctx) noexcept {
    std::unique_ptr<BcvJob::ReplyCallback> cb(
        static_cast<BcvJob::ReplyCallback*>(ctx));
    if (!cb || !*cb) {
        return;
    }
    try {
        BcvJob j(job);
        BcvRequest r(req);
        (*cb)(j, r);
    } catch (const Error& e) {
        sqlite3_bcv_job_error(job, static_cast<int>(e.code()), e.what());
    } catch (const std::exception& e) {
        sqlite3_bcv_job_error(job, SQLITE_ERROR, e.what());
    } catch (...) {
        sqlite3_bcv_job_error(job, SQLITE_ERROR,
                              "unknown error in bcv reply callback");
    }
}

int bcv_open_shim(void* pCtx, const char** azParam, const char* zUser,
                  const char* zAuth, const char* zContainer,
                  sqlite3_bcv_container** pp, char** pzErrmsg) noexcept {
    auto* factory = static_cast<BcvModuleFactory*>(pCtx);
    try {
        std::vector<std::pair<std::string_view, std::string_view>> params;
        if (azParam != nullptr) {
            for (int i = 0; azParam[i] != nullptr; i += 2) {
                std::string_view k(azParam[i]);
                std::string_view v(azParam[i + 1] ? azParam[i + 1] : "");
                params.emplace_back(k, v);
                if (azParam[i + 1] == nullptr) {
                    break;
                }
            }
        }
        auto impl = (*factory)(params, zUser ? zUser : "", zAuth ? zAuth : "",
                               zContainer ? zContainer : "");
        if (!impl) {
            if (pzErrmsg != nullptr) {
                *pzErrmsg = sqlite3_mprintf("module factory returned null");
            }
            return SQLITE_ERROR;
        }
        auto* holder = new ContainerHolder{std::move(impl)};
        *pp = reinterpret_cast<sqlite3_bcv_container*>(holder);
        return SQLITE_OK;
    } catch (const Error& e) {
        if (pzErrmsg != nullptr) {
            *pzErrmsg = sqlite3_mprintf("%s", e.what());
        }
        return static_cast<int>(e.code());
    } catch (const std::exception& e) {
        if (pzErrmsg != nullptr) {
            *pzErrmsg = sqlite3_mprintf("%s", e.what());
        }
        return SQLITE_ERROR;
    } catch (...) {
        if (pzErrmsg != nullptr) {
            *pzErrmsg = sqlite3_mprintf("unknown error in bcv xOpen");
        }
        return SQLITE_ERROR;
    }
}

void bcv_close_shim(sqlite3_bcv_container* p) noexcept {
    delete reinterpret_cast<ContainerHolder*>(p);
}

void bcv_fetch_shim(sqlite3_bcv_container* p, sqlite3_bcv_job* job,
                    const char* zFile, int flags, const void* zCond,
                    int nCond) noexcept {
    auto* holder = reinterpret_cast<ContainerHolder*>(p);
    try {
        BcvJob j(job);
        std::span<const std::byte> cond;
        if (zCond != nullptr && nCond > 0) {
            cond = {static_cast<const std::byte*>(zCond),
                    static_cast<std::size_t>(nCond)};
        }
        holder->impl->fetch(j, zFile ? zFile : "",
                            BcvFetchConditions::from_raw(flags), cond);
    } catch (const Error& e) {
        sqlite3_bcv_job_error(job, static_cast<int>(e.code()), e.what());
    } catch (const std::exception& e) {
        sqlite3_bcv_job_error(job, SQLITE_ERROR, e.what());
    } catch (...) {
        sqlite3_bcv_job_error(job, SQLITE_ERROR, "unknown error in bcv xFetch");
    }
}

void bcv_put_shim(sqlite3_bcv_container* p, sqlite3_bcv_job* job,
                  const char* zFile, const unsigned char* zData, int nData,
                  const char* zETag) noexcept {
    auto* holder = reinterpret_cast<ContainerHolder*>(p);
    try {
        BcvJob j(job);
        std::span<const std::byte> data;
        if (zData != nullptr && nData > 0) {
            data = {reinterpret_cast<const std::byte*>(zData),
                    static_cast<std::size_t>(nData)};
        }
        holder->impl->put(j, zFile ? zFile : "", data, zETag ? zETag : "");
    } catch (const Error& e) {
        sqlite3_bcv_job_error(job, static_cast<int>(e.code()), e.what());
    } catch (const std::exception& e) {
        sqlite3_bcv_job_error(job, SQLITE_ERROR, e.what());
    } catch (...) {
        sqlite3_bcv_job_error(job, SQLITE_ERROR, "unknown error in bcv xPut");
    }
}

void bcv_delete_shim(sqlite3_bcv_container* p, sqlite3_bcv_job* job,
                     const char* zFile, const char* zETag) noexcept {
    auto* holder = reinterpret_cast<ContainerHolder*>(p);
    try {
        BcvJob j(job);
        holder->impl->remove(j, zFile ? zFile : "", zETag ? zETag : "");
    } catch (const Error& e) {
        sqlite3_bcv_job_error(job, static_cast<int>(e.code()), e.what());
    } catch (const std::exception& e) {
        sqlite3_bcv_job_error(job, SQLITE_ERROR, e.what());
    } catch (...) {
        sqlite3_bcv_job_error(job, SQLITE_ERROR,
                              "unknown error in bcv xDelete");
    }
}

void bcv_list_shim(sqlite3_bcv_container* p, sqlite3_bcv_job* job) noexcept {
    auto* holder = reinterpret_cast<ContainerHolder*>(p);
    try {
        BcvJob j(job);
        holder->impl->list(j);
    } catch (const Error& e) {
        sqlite3_bcv_job_error(job, static_cast<int>(e.code()), e.what());
    } catch (const std::exception& e) {
        sqlite3_bcv_job_error(job, SQLITE_ERROR, e.what());
    } catch (...) {
        sqlite3_bcv_job_error(job, SQLITE_ERROR, "unknown error in bcv xList");
    }
}

void bcv_create_shim(sqlite3_bcv_container* p, sqlite3_bcv_job* job) noexcept {
    auto* holder = reinterpret_cast<ContainerHolder*>(p);
    try {
        BcvJob j(job);
        holder->impl->create_container(j);
    } catch (const Error& e) {
        sqlite3_bcv_job_error(job, static_cast<int>(e.code()), e.what());
    } catch (const std::exception& e) {
        sqlite3_bcv_job_error(job, SQLITE_ERROR, e.what());
    } catch (...) {
        sqlite3_bcv_job_error(job, SQLITE_ERROR,
                              "unknown error in bcv xCreate");
    }
}

void bcv_destroy_shim(sqlite3_bcv_container* p, sqlite3_bcv_job* job) noexcept {
    auto* holder = reinterpret_cast<ContainerHolder*>(p);
    try {
        BcvJob j(job);
        holder->impl->destroy_container(j);
    } catch (const Error& e) {
        sqlite3_bcv_job_error(job, static_cast<int>(e.code()), e.what());
    } catch (const std::exception& e) {
        sqlite3_bcv_job_error(job, SQLITE_ERROR, e.what());
    } catch (...) {
        sqlite3_bcv_job_error(job, SQLITE_ERROR,
                              "unknown error in bcv xDestroy");
    }
}

sqlite3_bcv_module kBcvModuleCallbacks = {
    bcv_open_shim,   bcv_close_shim, bcv_fetch_shim,  bcv_put_shim,
    bcv_delete_shim, bcv_list_shim,  bcv_create_shim, bcv_destroy_shim,
};

} // anonymous namespace

// ============================================================================
// BcvRequest
// ============================================================================

void BcvRequest::set_method(BcvHttpMethod method) {
    sqlite3_bcv_request_set_method(req_, static_cast<int>(method));
}

void BcvRequest::set_uri(std::string_view uri) {
    std::string s(uri);
    sqlite3_bcv_request_set_uri(req_, s.c_str());
}

void BcvRequest::set_log(std::string_view log_text) {
    std::string s(log_text);
    sqlite3_bcv_request_set_log(req_, s.c_str());
}

void BcvRequest::set_header(std::string_view header) {
    std::string s(header);
    sqlite3_bcv_request_set_header(req_, s.c_str());
}

void BcvRequest::set_body(std::span<const std::byte> data) {
    static const unsigned char kEmpty = 0;
    const unsigned char* ptr =
        !data.empty() ? reinterpret_cast<const unsigned char*>(data.data())
                      : &kEmpty;
    sqlite3_bcv_request_set_body(req_, ptr, static_cast<int>(data.size()));
}

void BcvRequest::set_body(std::string_view data) {
    set_body(std::as_bytes(std::span(data.data(), data.size())));
}

int BcvRequest::status(std::string* status_msg) const {
    const char* msg = nullptr;
    int const rc =
        sqlite3_bcv_request_status(req_, status_msg ? &msg : nullptr);
    if (status_msg != nullptr) {
        *status_msg = msg ? std::string(msg) : std::string{};
    }
    return rc;
}

std::optional<std::string_view> BcvRequest::header(
    std::string_view name) const {
    std::string name_s(name);
    const char* val = sqlite3_bcv_request_header(req_, name_s.c_str());
    if (val == nullptr) {
        return std::nullopt;
    }
    return std::string_view(val);
}

std::string_view BcvRequest::raw_headers() const {
    int n = 0;
    const unsigned char* p = sqlite3_bcv_request_hdrs(req_, &n);
    if (p == nullptr || n <= 0) {
        return {};
    }
    return {reinterpret_cast<const char*>(p), static_cast<std::size_t>(n)};
}

std::span<const std::byte> BcvRequest::body() const {
    int n = 0;
    const unsigned char* p = sqlite3_bcv_request_body(req_, &n);
    if (p == nullptr || n <= 0) {
        return {};
    }
    return {reinterpret_cast<const std::byte*>(p), static_cast<std::size_t>(n)};
}

// ============================================================================
// BcvJob
// ============================================================================

BcvRequest BcvJob::create_request(ReplyCallback on_reply) {
    auto cb = std::make_unique<ReplyCallback>(std::move(on_reply));
    sqlite3_bcv_request* req =
        sqlite3_bcv_job_request(job_, cb.get(), bcv_reply_shim);
    if (req == nullptr) {
        throw Error(ErrorCode::NoMem, "sqlite3_bcv_job_request failed");
    }
    cb.release();
    return BcvRequest(req);
}

void BcvJob::set_result(std::span<const std::byte> data) {
    static const unsigned char kEmpty = 0;
    const unsigned char* ptr =
        !data.empty() ? reinterpret_cast<const unsigned char*>(data.data())
                      : &kEmpty;
    sqlite3_bcv_job_result(job_, ptr, static_cast<int>(data.size()));
}

void BcvJob::set_result(std::string_view data) {
    set_result(std::as_bytes(std::span(data.data(), data.size())));
}

void BcvJob::add_list_entry(std::string_view entry) {
    std::string s(entry);
    sqlite3_bcv_job_result(
        job_, reinterpret_cast<const unsigned char*>(s.c_str()), -1);
}

void BcvJob::set_headers(std::span<const std::byte> data) {
    static const unsigned char kEmpty = 0;
    const unsigned char* ptr =
        !data.empty() ? reinterpret_cast<const unsigned char*>(data.data())
                      : &kEmpty;
    sqlite3_bcv_job_hdrs(job_, ptr, static_cast<int>(data.size()));
}

void BcvJob::set_headers(std::string_view data) {
    set_headers(std::as_bytes(std::span(data.data(), data.size())));
}

void BcvJob::set_etag(std::string_view etag) {
    std::string s(etag);
    sqlite3_bcv_job_etag(job_, s.c_str());
}

void BcvJob::set_error(int code, std::string_view message) {
    if (message.empty()) {
        sqlite3_bcv_job_error(job_, code, nullptr);
    } else {
        std::string s(message);
        sqlite3_bcv_job_error(job_, code, s.c_str());
    }
}

std::string_view BcvJob::prefix() const {
    const char* p = sqlite3_bcv_job_prefix(job_);
    return p ? std::string_view(p) : std::string_view{};
}

// ============================================================================
// Module registration & shutdown
// ============================================================================

void register_bcv_module(std::string_view name, BcvModuleFactory factory) {
    if (!factory) {
        throw Error(ErrorCode::Misuse, "bcv module factory must not be empty");
    }
    std::string key(name);
    auto owned = std::make_unique<BcvModuleFactory>(std::move(factory));
    std::lock_guard<std::mutex> lock(module_registry_mutex());

    int const rc = sqlite3_bcv_create_module(key.c_str(), &kBcvModuleCallbacks,
                                             owned.get());
    throw_on_error(rc, "sqlite3_bcv_create_module failed");
    module_registry()[std::move(key)] = std::move(owned);
}

void bcv_shutdown() noexcept {
    std::lock_guard<std::mutex> lock(module_registry_mutex());
    sqlite3_bcv_shutdown();
    module_registry().clear();
}

} // namespace sqlitex

#else // !SQLITEX_HAS_BCVFS

namespace sqlitex {

void BcvRequest::set_method(BcvHttpMethod /*unused*/) {
}
void BcvRequest::set_uri(std::string_view /*unused*/) {
}
void BcvRequest::set_log(std::string_view /*unused*/) {
}
void BcvRequest::set_header(std::string_view /*unused*/) {
}
void BcvRequest::set_body(std::span<const std::byte> /*unused*/) {
}
void BcvRequest::set_body(std::string_view /*unused*/) {
}
int BcvRequest::status(std::string* /*unused*/) const {
    return SQLITE_ERROR;
}
std::optional<std::string_view> BcvRequest::header(
    std::string_view /*unused*/) const {
    return std::nullopt;
}
std::string_view BcvRequest::raw_headers() const {
    return {};
}
std::span<const std::byte> BcvRequest::body() const {
    return {};
}

BcvRequest BcvJob::create_request(ReplyCallback /*unused*/) {
    throw Error(ErrorCode::Error, "bcvfs support not compiled in");
}
void BcvJob::set_result(std::span<const std::byte> /*unused*/) {
}
void BcvJob::set_result(std::string_view /*unused*/) {
}
void BcvJob::add_list_entry(std::string_view /*unused*/) {
}
void BcvJob::set_headers(std::span<const std::byte> /*unused*/) {
}
void BcvJob::set_headers(std::string_view /*unused*/) {
}
void BcvJob::set_etag(std::string_view /*unused*/) {
}
void BcvJob::set_error(int /*unused*/, std::string_view /*unused*/) {
}
std::string_view BcvJob::prefix() const {
    return {};
}

void register_bcv_module(std::string_view /*unused*/,
                         BcvModuleFactory /*unused*/) {
    throw Error(ErrorCode::Error, "bcvfs support not compiled in");
}

void bcv_shutdown() noexcept {
}

} // namespace sqlitex

#endif // SQLITEX_HAS_BCVFS
