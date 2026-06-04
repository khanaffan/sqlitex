#include "sqlitex/error.h"

#include <sqlite3.h>

#include <cassert>
#include <string>

namespace sqlitex {

// ============================================================================
// ErrorCode mapping
// ============================================================================

ErrorCode map_error_code(int sqlite_rc) noexcept {
    switch (sqlite_rc & 0xFF) {
    case SQLITE_OK:
        return ErrorCode::Ok;
    case SQLITE_ERROR:
        return ErrorCode::Error;
    case SQLITE_INTERNAL:
        return ErrorCode::Internal;
    case SQLITE_PERM:
        return ErrorCode::Perm;
    case SQLITE_ABORT:
        return ErrorCode::Abort;
    case SQLITE_BUSY:
        return ErrorCode::Busy;
    case SQLITE_LOCKED:
        return ErrorCode::Locked;
    case SQLITE_NOMEM:
        return ErrorCode::NoMem;
    case SQLITE_READONLY:
        return ErrorCode::ReadOnly;
    case SQLITE_INTERRUPT:
        return ErrorCode::Interrupt;
    case SQLITE_IOERR:
        return ErrorCode::IoErr;
    case SQLITE_CORRUPT:
        return ErrorCode::Corrupt;
    case SQLITE_NOTFOUND:
        return ErrorCode::NotFound;
    case SQLITE_FULL:
        return ErrorCode::Full;
    case SQLITE_CANTOPEN:
        return ErrorCode::CantOpen;
    case SQLITE_PROTOCOL:
        return ErrorCode::Protocol;
    case SQLITE_EMPTY:
        return ErrorCode::Empty;
    case SQLITE_SCHEMA:
        return ErrorCode::Schema;
    case SQLITE_TOOBIG:
        return ErrorCode::TooBig;
    case SQLITE_CONSTRAINT:
        return ErrorCode::Constraint;
    case SQLITE_MISMATCH:
        return ErrorCode::Mismatch;
    case SQLITE_MISUSE:
        return ErrorCode::Misuse;
    case SQLITE_NOLFS:
        return ErrorCode::NoLfs;
    case SQLITE_AUTH:
        return ErrorCode::Auth;
    case SQLITE_FORMAT:
        return ErrorCode::Format;
    case SQLITE_RANGE:
        return ErrorCode::Range;
    case SQLITE_NOTADB:
        return ErrorCode::NotADb;
    case SQLITE_NOTICE:
        return ErrorCode::Notice;
    case SQLITE_WARNING:
        return ErrorCode::Warning;
    case SQLITE_ROW:
        return ErrorCode::Row;
    case SQLITE_DONE:
        return ErrorCode::Done;
    default:
        return ErrorCode::Error;
    }
}

// ============================================================================
// Error
// ============================================================================

Error::Error(int sqlite_rc, std::string msg)
    : std::runtime_error(std::move(msg)),
      code_(sqlite_rc & 0xFF),
      extended_code_(sqlite_rc),
      ec_(map_error_code(sqlite_rc)) {
}

Error::Error(ErrorCode ec, std::string msg)
    : std::runtime_error(std::move(msg)),
      code_(static_cast<int>(ec)),
      extended_code_(static_cast<int>(ec)),
      ec_(ec) {
}

// ============================================================================
// Factory for throwing the correct subclass
// ============================================================================

namespace {

template <typename ExceptionType>
[[noreturn]] void raise(const std::string& msg, int code, int ext_code) {
    throw ExceptionType(ext_code ? ext_code : code, msg);
}

[[noreturn]] void dispatch_error(int primary, int rc,
                                 const std::string& err_msg) {
    switch (primary) {
    case SQLITE_ERROR:
        raise<RuntimeError>(err_msg, primary, rc);
        break;
    case SQLITE_INTERNAL:
        raise<InternalError>(err_msg, primary, rc);
        break;
    case SQLITE_PERM:
        raise<PermissionDenied>(err_msg, primary, rc);
        break;
    case SQLITE_ABORT:
        raise<AbortError>(err_msg, primary, rc);
        break;
    case SQLITE_BUSY:
        raise<BusyError>(err_msg, primary, rc);
        break;
    case SQLITE_LOCKED:
        raise<BusyError>(err_msg, primary, rc);
        break;
    case SQLITE_IOERR:
        raise<IoError>(err_msg, primary, rc);
        break;
    case SQLITE_CORRUPT:
        raise<CorruptError>(err_msg, primary, rc);
        break;
    case SQLITE_NOTFOUND:
        raise<NotFoundError>(err_msg, primary, rc);
        break;
    case SQLITE_FULL:
        raise<FullError>(err_msg, primary, rc);
        break;
    case SQLITE_CANTOPEN:
        raise<CantOpenError>(err_msg, primary, rc);
        break;
    case SQLITE_PROTOCOL:
        raise<ProtocolError>(err_msg, primary, rc);
        break;
    case SQLITE_SCHEMA:
        raise<SchemaError>(err_msg, primary, rc);
        break;
    case SQLITE_CONSTRAINT:
        raise<ConstraintError>(err_msg, primary, rc);
        break;
    case SQLITE_MISMATCH:
        raise<MismatchError>(err_msg, primary, rc);
        break;
    case SQLITE_MISUSE:
        raise<MisuseError>(err_msg, primary, rc);
        break;
    case SQLITE_AUTH:
        raise<AuthError>(err_msg, primary, rc);
        break;
    case SQLITE_RANGE:
        raise<RangeError>(err_msg, primary, rc);
        break;
    case SQLITE_NOTADB:
        raise<NotADbError>(err_msg, primary, rc);
        break;
    case SQLITE_READONLY:
        raise<PermissionDenied>(err_msg, primary, rc);
        break;
    case SQLITE_NOMEM:
        throw std::bad_alloc();
    default:
        raise<Error>(err_msg, primary, rc);
        break;
    }
}

} // anonymous namespace

void throw_sqlite_error(int rc, sqlite3* db) {
    const char* msg = (db != nullptr) ? sqlite3_errmsg(db) : "unknown error";
    if (msg == nullptr) {
        msg = "unknown error";
    }
    dispatch_error(rc & 0xFF, rc, std::string(msg));
}

void throw_on_error(int rc, const char* message) {
    if (rc == SQLITE_OK || rc == SQLITE_ROW || rc == SQLITE_DONE) {
        return;
    }
    dispatch_error(rc & 0xFF, rc,
                   std::string(message ? message : "unknown error"));
}

} // namespace sqlitex
