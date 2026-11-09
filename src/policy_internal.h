#pragma once

// Internal helpers shared by the ScopedPolicy implementation and the sqlitex
// entry points that compile SQL.  Not installed.

#include "sqlitex/policy.h"

#include <exception>
#include <string>
#include <string_view>

struct sqlite3;

namespace sqlitex {
namespace detail {

/// Thread-local, nestable exception sink for one sqlitex operation on one
/// connection.  The authorizer shim stores callback exceptions and the first
/// policy denial in the innermost sink for its connection on the current
/// thread; check() reports them once the SQLite call has returned.
class AuthSink {
    public:
    /// `forbid_ignore` marks Session/Changeset operations, during which an
    /// Ignore decision is always converted to Deny.
    explicit AuthSink(sqlite3* db, bool forbid_ignore = false) noexcept;
    ~AuthSink();

    AuthSink(const AuthSink&) = delete;
    AuthSink& operator=(const AuthSink&) = delete;
    AuthSink(AuthSink&&) = delete;
    AuthSink& operator=(AuthSink&&) = delete;

    /// Rethrow a captured exception.  Otherwise, if `rc` is a failure caused
    /// by a recorded policy denial, throw AuthError.  `message` defaults to
    /// sqlite3_errmsg().
    void check(int rc, const char* message = nullptr);

    static AuthSink* find(sqlite3* db) noexcept;
    static bool ignore_forbidden(sqlite3* db) noexcept;

    void capture(std::exception_ptr exc) noexcept;
    void record_denial(const AuthorizerRequest& request) noexcept;

    private:
    sqlite3* db_;
    bool forbid_ignore_;
    AuthSink* prev_;
    std::exception_ptr exception_{};
    std::string denial_{};
    bool denied_ = false;
};

/// Evaluate the active policy (if any) for a blob handle open.  Throws
/// AuthError when the synthesized Read (and Update when writable) request is
/// not allowed; callback exceptions propagate unchanged.
void check_blob_access(Database& db, std::string_view schema,
                       std::string_view table, std::string_view column,
                       bool writable);

} // namespace detail
} // namespace sqlitex
