#pragma once

#include "sqlitex_export.h"

#include "sqlitex/concepts.h"
#include "sqlitex/error.h"
#include "sqlitex/flags.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

struct sqlite3_bcv_container;
struct sqlite3_bcv_request;
struct sqlite3_bcv_job;

namespace sqlitex {

// ============================================================================
// Constants matching bcvmodule.h
// ============================================================================

enum class BcvHttpMethod : int {
    Get = 1,    // SQLITE_BCV_METHOD_GET
    Put = 2,    // SQLITE_BCV_METHOD_PUT
    Delete = 3, // SQLITE_BCV_METHOD_DELETE
    Head = 4,   // SQLITE_BCV_METHOD_HEAD
};

enum class BcvFetchCondition : int {
    None = 0,
    Md5 = 0x01,  // BCV_MODULE_CONDITION_MD5
    ETag = 0x02, // BCV_MODULE_CONDITION_ETAG
};

template <>
struct EnableBitmaskOperators<BcvFetchCondition> : std::true_type {};

using BcvFetchConditions = Flags<BcvFetchCondition>;

// ============================================================================
// BcvRequest — non-owning wrapper for sqlite3_bcv_request*
// ============================================================================

class SQLITEX_EXPORT BcvRequest {
    public:
    explicit BcvRequest(sqlite3_bcv_request* req) noexcept : req_(req) {
    }

    explicit operator bool() const noexcept {
        return req_ != nullptr;
    }

    [[nodiscard]] sqlite3_bcv_request* handle() noexcept {
        return req_;
    }

    // === Request configuration (before returning from module/reply cb) ===

    void set_method(BcvHttpMethod method);
    void set_uri(std::string_view uri);
    void set_log(std::string_view log_text);
    void set_header(std::string_view header);
    void set_body(std::span<const std::byte> data);

    template <ConstByteRange R>
        requires(
            !std::same_as<std::remove_cvref_t<R>, std::span<const std::byte>>)
    void set_body(const R& data) {
        set_body(to_const_bytes(data));
    }

    void set_body(std::string_view data);

    // === Reply inspection (inside reply callback) ===

    /// Return SQLITE_OK on 2xx, or SQLite/HTTP status code (400..599).
    [[nodiscard]] int status(std::string* status_msg = nullptr) const;

    /// Look up a response header value (case-insensitive name).
    [[nodiscard]] std::optional<std::string_view> header(
        std::string_view name) const;

    /// Raw response headers buffer.
    [[nodiscard]] std::string_view raw_headers() const;

    /// Response body bytes.
    [[nodiscard]] std::span<const std::byte> body() const;

    private:
    sqlite3_bcv_request* req_ = nullptr;
};

// ============================================================================
// BcvJob — non-owning wrapper for sqlite3_bcv_job*
// ============================================================================

class SQLITEX_EXPORT BcvJob {
    public:
    using ReplyCallback = std::function<void(BcvJob&, BcvRequest&)>;

    explicit BcvJob(sqlite3_bcv_job* job) noexcept : job_(job) {
    }

    explicit operator bool() const noexcept {
        return job_ != nullptr;
    }

    [[nodiscard]] sqlite3_bcv_job* handle() noexcept {
        return job_;
    }

    /// Create an asynchronous HTTP(S) request tied to this job.
    [[nodiscard]] BcvRequest create_request(ReplyCallback on_reply);

    /// Return object data (for fetch jobs).
    void set_result(std::span<const std::byte> data);

    template <ConstByteRange R>
        requires(
            !std::same_as<std::remove_cvref_t<R>, std::span<const std::byte>>)
    void set_result(const R& data) {
        set_result(to_const_bytes(data));
    }

    void set_result(std::string_view data);

    /// Append a nul-terminated entry name (for list jobs; passes n = -1 as
    /// required by CloudSQLite's BCV_DISPATCH_LIST handler).
    void add_list_entry(std::string_view entry);

    /// Supply response headers associated with a fetch result.
    void set_headers(std::span<const std::byte> data);

    template <ConstByteRange R>
        requires(
            !std::same_as<std::remove_cvref_t<R>, std::span<const std::byte>>)
    void set_headers(const R& data) {
        set_headers(to_const_bytes(data));
    }

    void set_headers(std::string_view data);

    /// Supply the object ETag (for fetch/put).
    void set_etag(std::string_view etag);

    /// Report a SQLite or HTTP (400..599) error for this job.
    void set_error(int code, std::string_view message = {});

    /// Return the prefix filter for list jobs (empty if none).
    [[nodiscard]] std::string_view prefix() const;

    private:
    sqlite3_bcv_job* job_ = nullptr;
};

// ============================================================================
// BcvContainer — abstract interface for custom cloud storage modules
// ============================================================================

class SQLITEX_EXPORT BcvContainer {
    public:
    virtual ~BcvContainer() = default;

    virtual void fetch(BcvJob& job, std::string_view file,
                       BcvFetchConditions conditions,
                       std::span<const std::byte> cond_data) = 0;

    virtual void put(BcvJob& job, std::string_view file,
                     std::span<const std::byte> data,
                     std::string_view etag) = 0;

    virtual void remove(BcvJob& job, std::string_view file,
                        std::string_view etag) = 0;

    virtual void list(BcvJob& job) = 0;

    virtual void create_container(BcvJob& job) = 0;

    virtual void destroy_container(BcvJob& job) = 0;
};

/// Factory creating a custom cloud storage container instance.
using BcvModuleFactory = std::function<std::unique_ptr<BcvContainer>(
    std::span<const std::pair<std::string_view, std::string_view>> params,
    std::string_view user, std::string_view auth, std::string_view container)>;

/// Register a custom cloud storage module (sqlite3_bcv_create_module).
SQLITEX_EXPORT void register_bcv_module(std::string_view name,
                                        BcvModuleFactory factory);

/// Release all global cloud storage module resources (sqlite3_bcv_shutdown).
/// Caller must ensure no BcvClient/Bcvfs handles are live when invoked.
SQLITEX_EXPORT void bcv_shutdown() noexcept;

} // namespace sqlitex
