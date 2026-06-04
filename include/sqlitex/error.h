#pragma once

#include "sqlitex_export.h"

#include <sqlite3.h>

#include <stdexcept>
#include <string>

namespace sqlitex {

// ============================================================================
// ErrorCode — strongly-typed mapping of SQLite primary result codes
// ============================================================================

enum class ErrorCode {
    Ok = SQLITE_OK,
    Error = SQLITE_ERROR,
    Internal = SQLITE_INTERNAL,
    Perm = SQLITE_PERM,
    Abort = SQLITE_ABORT,
    Busy = SQLITE_BUSY,
    Locked = SQLITE_LOCKED,
    NoMem = SQLITE_NOMEM,
    ReadOnly = SQLITE_READONLY,
    Interrupt = SQLITE_INTERRUPT,
    IoErr = SQLITE_IOERR,
    Corrupt = SQLITE_CORRUPT,
    NotFound = SQLITE_NOTFOUND,
    Full = SQLITE_FULL,
    CantOpen = SQLITE_CANTOPEN,
    Protocol = SQLITE_PROTOCOL,
    Empty = SQLITE_EMPTY,
    Schema = SQLITE_SCHEMA,
    TooBig = SQLITE_TOOBIG,
    Constraint = SQLITE_CONSTRAINT,
    Mismatch = SQLITE_MISMATCH,
    Misuse = SQLITE_MISUSE,
    NoLfs = SQLITE_NOLFS,
    Auth = SQLITE_AUTH,
    Format = SQLITE_FORMAT,
    Range = SQLITE_RANGE,
    NotADb = SQLITE_NOTADB,
    Notice = SQLITE_NOTICE,
    Warning = SQLITE_WARNING,
    Row = SQLITE_ROW,
    Done = SQLITE_DONE,
};

// ============================================================================
// Error — base exception class for all sqlitex errors
// ============================================================================

class SQLITEX_EXPORT Error : public std::runtime_error {
    public:
    /// Construct from a raw SQLite result code and message.
    Error(int sqlite_rc, std::string msg);

    /// Construct from an ErrorCode and message.
    Error(ErrorCode ec, std::string msg);

    /// Primary SQLite error code (e.g. SQLITE_BUSY).
    int code() const noexcept {
        return code_;
    }

    /// Extended SQLite error code (e.g. SQLITE_BUSY_SNAPSHOT).
    int extended_code() const noexcept {
        return extended_code_;
    }

    /// Error code as enum.
    ErrorCode error_code() const noexcept {
        return ec_;
    }

    protected:
    int code_ = SQLITE_ERROR;
    int extended_code_ = 0;
    ErrorCode ec_ = ErrorCode::Error;
};

// ============================================================================
// Typed exception subclasses
// ============================================================================

class SQLITEX_EXPORT RuntimeError : public Error {
    public:
    using Error::Error;
};

class SQLITEX_EXPORT PermissionDenied : public Error {
    public:
    using Error::Error;
};

class SQLITEX_EXPORT AbortError : public Error {
    public:
    using Error::Error;
};

class SQLITEX_EXPORT BusyError : public Error {
    public:
    using Error::Error;
};

class SQLITEX_EXPORT IoError : public Error {
    public:
    using Error::Error;
};

class SQLITEX_EXPORT CorruptError : public Error {
    public:
    using Error::Error;
};

class SQLITEX_EXPORT NotFoundError : public Error {
    public:
    using Error::Error;
};

class SQLITEX_EXPORT ConstraintError : public Error {
    public:
    using Error::Error;
};

class SQLITEX_EXPORT MismatchError : public Error {
    public:
    using Error::Error;
};

class SQLITEX_EXPORT MisuseError : public Error {
    public:
    using Error::Error;
};

class SQLITEX_EXPORT AuthError : public Error {
    public:
    using Error::Error;
};

class SQLITEX_EXPORT RangeError : public Error {
    public:
    using Error::Error;
};

class SQLITEX_EXPORT NotADbError : public Error {
    public:
    using Error::Error;
};

class SQLITEX_EXPORT InternalError : public Error {
    public:
    using Error::Error;
};

class SQLITEX_EXPORT ProtocolError : public Error {
    public:
    using Error::Error;
};

class SQLITEX_EXPORT CantOpenError : public Error {
    public:
    using Error::Error;
};

class SQLITEX_EXPORT FullError : public Error {
    public:
    using Error::Error;
};

class SQLITEX_EXPORT SchemaError : public Error {
    public:
    using Error::Error;
};

// ============================================================================
// Changeset-specific errors
// ============================================================================

class SQLITEX_EXPORT ChangesetError : public Error {
    public:
    using Error::Error;
};

class SQLITEX_EXPORT ChangesetApplyError : public Error {
    public:
    using Error::Error;
};

// ============================================================================
// Error-throwing utilities
// ============================================================================

/// Throw an exception corresponding to the SQLite result code and message
/// obtained from the given database handle.
[[noreturn]] SQLITEX_EXPORT void throw_sqlite_error(int rc, sqlite3* db);

/// If `rc` is an error (not OK, ROW, or DONE), throw an exception.
/// Uses `db` to obtain the error message.
inline void throw_on_error(int rc, sqlite3* db) {
    if (rc != SQLITE_OK && rc != SQLITE_ROW && rc != SQLITE_DONE) {
        throw_sqlite_error(rc, db);
    }
}

/// If `rc` is an error, throw using a fixed message.
SQLITEX_EXPORT void throw_on_error(int rc, const char* message);

/// Map a raw SQLite result code to an ErrorCode enum value.
SQLITEX_EXPORT ErrorCode map_error_code(int sqlite_rc) noexcept;

} // namespace sqlitex
