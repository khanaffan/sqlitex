#pragma once

// ============================================================================
// detail/sqlite3_fwd.h — minimal forward declarations for <sqlite3.h>
// ============================================================================
//
// Public sqlitex headers must NOT include <sqlite3.h>.  Doing so would
// transitively expose every consumer of the library to the entire underlying
// SQLite C API and its types.  Instead, public headers include this header,
// which provides only the small surface they actually reference:
//
//   * opaque handle types (forward-declared structs)
//   * the 64-bit integer typedefs
//   * the handful of C functions invoked from inline code in the headers
//   * the SQLITE_* constant values used as public enum values / defaults
//
// Everything here is kept source- and binary-compatible with <sqlite3.h>.  The
// library's own .cpp files still include the real <sqlite3.h>; the #ifndef
// guards below ensure there is no conflict regardless of include order.

// ----------------------------------------------------------------------------
// Opaque handle types
// ----------------------------------------------------------------------------
// Forward declarations only — the full definitions live in <sqlite3.h> and are
// never needed by the public headers (handles are passed around as pointers).
struct sqlite3;
struct sqlite3_stmt;
struct sqlite3_value;
struct sqlite3_blob;
struct sqlite3_context;
struct sqlite3_index_info;

// ----------------------------------------------------------------------------
// 64-bit integer typedefs
// ----------------------------------------------------------------------------
// These name exactly the same types as <sqlite3.h> (sqlite3_int64 ==
// `long long int`).  A typedef redeclaration to the same type is well-formed,
// so this stays compatible when <sqlite3.h> is also included.
typedef long long int sqlite3_int64;
typedef unsigned long long int sqlite3_uint64;

// ----------------------------------------------------------------------------
// C functions invoked from inline code in the public headers
// ----------------------------------------------------------------------------
// Only the routines actually called from header-inline code are declared.
// Signatures match <sqlite3.h> exactly (SQLITE_API expands to nothing in the
// bundled amalgamation), so these are plain redeclarations.
#ifdef __cplusplus
extern "C" {
#endif

int sqlite3_close_v2(sqlite3*);
void sqlite3_free(void*);

#ifdef __cplusplus
}
#endif

// ----------------------------------------------------------------------------
// Constant values referenced by public enums / inline code
// ----------------------------------------------------------------------------
// Each is guarded so that if <sqlite3.h> is (or has been) included, its
// definition wins and no redefinition occurs.  Values are copied verbatim from
// the bundled third_party/sqlite3/sqlite3.h and are stable parts of the SQLite
// public API.  (Session/changeset constants live in detail/session_api.h.)

// Primary result codes
#ifndef SQLITE_OK
#define SQLITE_OK 0
#endif
#ifndef SQLITE_ERROR
#define SQLITE_ERROR 1
#endif
#ifndef SQLITE_INTERNAL
#define SQLITE_INTERNAL 2
#endif
#ifndef SQLITE_PERM
#define SQLITE_PERM 3
#endif
#ifndef SQLITE_ABORT
#define SQLITE_ABORT 4
#endif
#ifndef SQLITE_BUSY
#define SQLITE_BUSY 5
#endif
#ifndef SQLITE_LOCKED
#define SQLITE_LOCKED 6
#endif
#ifndef SQLITE_NOMEM
#define SQLITE_NOMEM 7
#endif
#ifndef SQLITE_READONLY
#define SQLITE_READONLY 8
#endif
#ifndef SQLITE_INTERRUPT
#define SQLITE_INTERRUPT 9
#endif
#ifndef SQLITE_IOERR
#define SQLITE_IOERR 10
#endif
#ifndef SQLITE_CORRUPT
#define SQLITE_CORRUPT 11
#endif
#ifndef SQLITE_NOTFOUND
#define SQLITE_NOTFOUND 12
#endif
#ifndef SQLITE_FULL
#define SQLITE_FULL 13
#endif
#ifndef SQLITE_CANTOPEN
#define SQLITE_CANTOPEN 14
#endif
#ifndef SQLITE_PROTOCOL
#define SQLITE_PROTOCOL 15
#endif
#ifndef SQLITE_EMPTY
#define SQLITE_EMPTY 16
#endif
#ifndef SQLITE_SCHEMA
#define SQLITE_SCHEMA 17
#endif
#ifndef SQLITE_TOOBIG
#define SQLITE_TOOBIG 18
#endif
#ifndef SQLITE_CONSTRAINT
#define SQLITE_CONSTRAINT 19
#endif
#ifndef SQLITE_MISMATCH
#define SQLITE_MISMATCH 20
#endif
#ifndef SQLITE_MISUSE
#define SQLITE_MISUSE 21
#endif
#ifndef SQLITE_NOLFS
#define SQLITE_NOLFS 22
#endif
#ifndef SQLITE_AUTH
#define SQLITE_AUTH 23
#endif
#ifndef SQLITE_FORMAT
#define SQLITE_FORMAT 24
#endif
#ifndef SQLITE_RANGE
#define SQLITE_RANGE 25
#endif
#ifndef SQLITE_NOTADB
#define SQLITE_NOTADB 26
#endif
#ifndef SQLITE_NOTICE
#define SQLITE_NOTICE 27
#endif
#ifndef SQLITE_WARNING
#define SQLITE_WARNING 28
#endif
#ifndef SQLITE_ROW
#define SQLITE_ROW 100
#endif
#ifndef SQLITE_DONE
#define SQLITE_DONE 101
#endif

// Flags for the database open call
#ifndef SQLITE_OPEN_READONLY
#define SQLITE_OPEN_READONLY 0x00000001
#endif
#ifndef SQLITE_OPEN_READWRITE
#define SQLITE_OPEN_READWRITE 0x00000002
#endif
#ifndef SQLITE_OPEN_CREATE
#define SQLITE_OPEN_CREATE 0x00000004
#endif
#ifndef SQLITE_OPEN_URI
#define SQLITE_OPEN_URI 0x00000040
#endif
#ifndef SQLITE_OPEN_MEMORY
#define SQLITE_OPEN_MEMORY 0x00000080
#endif
#ifndef SQLITE_OPEN_NOMUTEX
#define SQLITE_OPEN_NOMUTEX 0x00008000
#endif
#ifndef SQLITE_OPEN_FULLMUTEX
#define SQLITE_OPEN_FULLMUTEX 0x00010000
#endif
#ifndef SQLITE_OPEN_SHAREDCACHE
#define SQLITE_OPEN_SHAREDCACHE 0x00020000
#endif
#ifndef SQLITE_OPEN_PRIVATECACHE
#define SQLITE_OPEN_PRIVATECACHE 0x00040000
#endif

// Text encoding / function-registration flag
#ifndef SQLITE_UTF8
#define SQLITE_UTF8 1
#endif

// Authorizer action codes reused as changeset operation codes
#ifndef SQLITE_DELETE
#define SQLITE_DELETE 9
#endif
#ifndef SQLITE_INSERT
#define SQLITE_INSERT 18
#endif
#ifndef SQLITE_UPDATE
#define SQLITE_UPDATE 23
#endif

// Virtual-table index-constraint operators (xBestIndex)
#ifndef SQLITE_INDEX_CONSTRAINT_EQ
#define SQLITE_INDEX_CONSTRAINT_EQ 2
#endif
#ifndef SQLITE_INDEX_CONSTRAINT_GT
#define SQLITE_INDEX_CONSTRAINT_GT 4
#endif
#ifndef SQLITE_INDEX_CONSTRAINT_LE
#define SQLITE_INDEX_CONSTRAINT_LE 8
#endif
#ifndef SQLITE_INDEX_CONSTRAINT_LT
#define SQLITE_INDEX_CONSTRAINT_LT 16
#endif
#ifndef SQLITE_INDEX_CONSTRAINT_GE
#define SQLITE_INDEX_CONSTRAINT_GE 32
#endif
#ifndef SQLITE_INDEX_CONSTRAINT_MATCH
#define SQLITE_INDEX_CONSTRAINT_MATCH 64
#endif
#ifndef SQLITE_INDEX_CONSTRAINT_LIKE
#define SQLITE_INDEX_CONSTRAINT_LIKE 65
#endif
#ifndef SQLITE_INDEX_CONSTRAINT_GLOB
#define SQLITE_INDEX_CONSTRAINT_GLOB 66
#endif
#ifndef SQLITE_INDEX_CONSTRAINT_REGEXP
#define SQLITE_INDEX_CONSTRAINT_REGEXP 67
#endif
#ifndef SQLITE_INDEX_CONSTRAINT_NE
#define SQLITE_INDEX_CONSTRAINT_NE 68
#endif
#ifndef SQLITE_INDEX_CONSTRAINT_ISNOT
#define SQLITE_INDEX_CONSTRAINT_ISNOT 69
#endif
#ifndef SQLITE_INDEX_CONSTRAINT_ISNOTNULL
#define SQLITE_INDEX_CONSTRAINT_ISNOTNULL 70
#endif
#ifndef SQLITE_INDEX_CONSTRAINT_ISNULL
#define SQLITE_INDEX_CONSTRAINT_ISNULL 71
#endif
#ifndef SQLITE_INDEX_CONSTRAINT_IS
#define SQLITE_INDEX_CONSTRAINT_IS 72
#endif
#ifndef SQLITE_INDEX_CONSTRAINT_LIMIT
#define SQLITE_INDEX_CONSTRAINT_LIMIT 73
#endif
#ifndef SQLITE_INDEX_CONSTRAINT_OFFSET
#define SQLITE_INDEX_CONSTRAINT_OFFSET 74
#endif
#ifndef SQLITE_INDEX_CONSTRAINT_FUNCTION
#define SQLITE_INDEX_CONSTRAINT_FUNCTION 150
#endif
