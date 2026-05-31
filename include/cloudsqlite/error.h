#pragma once

#include <sqlite3.h>

#include <stdexcept>
#include <string>

namespace cloudsqlite {

// ============================================================================
// ErrorCode — strongly-typed mapping of SQLite primary result codes
// ============================================================================

enum class ErrorCode {
    Ok          = SQLITE_OK,
    Error       = SQLITE_ERROR,
    Internal    = SQLITE_INTERNAL,
    Perm        = SQLITE_PERM,
    Abort       = SQLITE_ABORT,
    Busy        = SQLITE_BUSY,
    Locked      = SQLITE_LOCKED,
    NoMem       = SQLITE_NOMEM,
    ReadOnly    = SQLITE_READONLY,
    Interrupt   = SQLITE_INTERRUPT,
    IoErr       = SQLITE_IOERR,
    Corrupt     = SQLITE_CORRUPT,
    NotFound    = SQLITE_NOTFOUND,
    Full        = SQLITE_FULL,
    CantOpen    = SQLITE_CANTOPEN,
    Protocol    = SQLITE_PROTOCOL,
    Empty       = SQLITE_EMPTY,
    Schema      = SQLITE_SCHEMA,
    TooBig      = SQLITE_TOOBIG,
    Constraint  = SQLITE_CONSTRAINT,
    Mismatch    = SQLITE_MISMATCH,
    Misuse      = SQLITE_MISUSE,
    NoLfs       = SQLITE_NOLFS,
    Auth        = SQLITE_AUTH,
    Format      = SQLITE_FORMAT,
    Range       = SQLITE_RANGE,
    NotADb      = SQLITE_NOTADB,
    Notice      = SQLITE_NOTICE,
    Warning     = SQLITE_WARNING,
    Row         = SQLITE_ROW,
    Done        = SQLITE_DONE,
};

// ============================================================================
// Error — base exception class for all cloudsqlite errors
// ============================================================================

class Error : public std::runtime_error {
public:
    /// Construct from a raw SQLite result code and message.
    Error(int sqlite_rc, std::string msg);

    /// Construct from an ErrorCode and message.
    Error(ErrorCode ec, std::string msg);

    /// Primary SQLite error code (e.g. SQLITE_BUSY).
    int code() const noexcept { return code_; }

    /// Extended SQLite error code (e.g. SQLITE_BUSY_SNAPSHOT).
    int extended_code() const noexcept { return extended_code_; }

    /// Error code as enum.
    ErrorCode error_code() const noexcept { return ec_; }

protected:
    int code_ = SQLITE_ERROR;
    int extended_code_ = 0;
    ErrorCode ec_ = ErrorCode::Error;
};

// ============================================================================
// Typed exception subclasses
// ============================================================================

class RuntimeError : public Error {
public:
    using Error::Error;
};

class PermissionDenied : public Error {
public:
    using Error::Error;
};

class AbortError : public Error {
public:
    using Error::Error;
};

class BusyError : public Error {
public:
    using Error::Error;
};

class IoError : public Error {
public:
    using Error::Error;
};

class CorruptError : public Error {
public:
    using Error::Error;
};

class NotFoundError : public Error {
public:
    using Error::Error;
};

class ConstraintError : public Error {
public:
    using Error::Error;
};

class MismatchError : public Error {
public:
    using Error::Error;
};

class MisuseError : public Error {
public:
    using Error::Error;
};

class AuthError : public Error {
public:
    using Error::Error;
};

class RangeError : public Error {
public:
    using Error::Error;
};

class NotADbError : public Error {
public:
    using Error::Error;
};

class InternalError : public Error {
public:
    using Error::Error;
};

class ProtocolError : public Error {
public:
    using Error::Error;
};

class CantOpenError : public Error {
public:
    using Error::Error;
};

class FullError : public Error {
public:
    using Error::Error;
};

class SchemaError : public Error {
public:
    using Error::Error;
};

// ============================================================================
// Changeset-specific errors
// ============================================================================

class ChangesetError : public Error {
public:
    using Error::Error;
};

class ChangesetApplyError : public Error {
public:
    using Error::Error;
};

// ============================================================================
// Error-throwing utilities
// ============================================================================

/// Throw an exception corresponding to the SQLite result code and message
/// obtained from the given database handle.
[[noreturn]] void throw_sqlite_error(int rc, sqlite3* db);

/// If `rc` is an error (not OK, ROW, or DONE), throw an exception.
/// Uses `db` to obtain the error message.
inline void throw_on_error(int rc, sqlite3* db) {
    if (rc != SQLITE_OK && rc != SQLITE_ROW && rc != SQLITE_DONE) {
        throw_sqlite_error(rc, db);
    }
}

/// If `rc` is an error, throw using a fixed message.
void throw_on_error(int rc, const char* message);

/// Map a raw SQLite result code to an ErrorCode enum value.
ErrorCode map_error_code(int sqlite_rc) noexcept;

} // namespace cloudsqlite
