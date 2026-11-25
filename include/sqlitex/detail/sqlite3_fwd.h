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
struct sqlite3_backup;
struct sqlite3_snapshot;
struct sqlite3_vfs;
struct sqlite3_file;
struct sqlite3_rtree_geometry;
struct sqlite3_rtree_query_info;
struct Fts5Context;
struct Fts5Tokenizer;
struct Fts5ExtensionApi;
struct fts5_api;

// Filename typedef matching <sqlite3.h>
typedef const char* sqlite3_filename;

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
// definitions remain authoritative.

// Fundamental datatypes (note: SQLITE_TEXT is deliberately omitted because
// <sqlite3.h> #undefs SQLITE_TEXT if pre-defined; use SQLITE3_TEXT instead).
#ifndef SQLITE_INTEGER
#define SQLITE_INTEGER 1
#endif
#ifndef SQLITE_FLOAT
#define SQLITE_FLOAT 2
#endif
#ifndef SQLITE3_TEXT
#define SQLITE3_TEXT 3
#endif
#ifndef SQLITE_BLOB
#define SQLITE_BLOB 4
#endif
#ifndef SQLITE_NULL
#define SQLITE_NULL 5
#endif
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
#ifndef SQLITE_OPEN_NOFOLLOW
#define SQLITE_OPEN_NOFOLLOW 0x01000000
#endif
#ifndef SQLITE_OPEN_EXRESCODE
#define SQLITE_OPEN_EXRESCODE 0x02000000
#endif

// Text encoding & function-registration flags
#ifndef SQLITE_UTF8
#define SQLITE_UTF8 1
#endif
#ifndef SQLITE_UTF16LE
#define SQLITE_UTF16LE 2
#endif
#ifndef SQLITE_UTF16BE
#define SQLITE_UTF16BE 3
#endif
#ifndef SQLITE_UTF16
#define SQLITE_UTF16 4
#endif
#ifndef SQLITE_UTF16_ALIGNED
#define SQLITE_UTF16_ALIGNED 8
#endif
#ifndef SQLITE_UTF8_ZT
#define SQLITE_UTF8_ZT 16
#endif
#ifndef SQLITE_DETERMINISTIC
#define SQLITE_DETERMINISTIC 0x000000800
#endif
#ifndef SQLITE_DIRECTONLY
#define SQLITE_DIRECTONLY 0x000080000
#endif
#ifndef SQLITE_SUBTYPE
#define SQLITE_SUBTYPE 0x000100000
#endif
#ifndef SQLITE_INNOCUOUS
#define SQLITE_INNOCUOUS 0x000200000
#endif
#ifndef SQLITE_RESULT_SUBTYPE
#define SQLITE_RESULT_SUBTYPE 0x001000000
#endif
#ifndef SQLITE_SELFORDER1
#define SQLITE_SELFORDER1 0x002000000
#endif
#ifndef SQLITE_SELFORDER1
#define SQLITE_SELFORDER1 0x002000000
#endif

// Prepare flags (sqlite3_prepare_v3)
#ifndef SQLITE_PREPARE_PERSISTENT
#define SQLITE_PREPARE_PERSISTENT 0x01
#endif
#ifndef SQLITE_PREPARE_NORMALIZE
#define SQLITE_PREPARE_NORMALIZE 0x02
#endif
#ifndef SQLITE_PREPARE_NO_VTAB
#define SQLITE_PREPARE_NO_VTAB 0x04
#endif
#ifndef SQLITE_PREPARE_DONT_LOG
#define SQLITE_PREPARE_DONT_LOG 0x10
#endif
#ifndef SQLITE_PREPARE_FROM_DDL
#define SQLITE_PREPARE_FROM_DDL 0x20
#endif

// Trace event masks (sqlite3_trace_v2)
#ifndef SQLITE_TRACE_STMT
#define SQLITE_TRACE_STMT 0x01
#endif
#ifndef SQLITE_TRACE_PROFILE
#define SQLITE_TRACE_PROFILE 0x02
#endif
#ifndef SQLITE_TRACE_ROW
#define SQLITE_TRACE_ROW 0x04
#endif
#ifndef SQLITE_TRACE_CLOSE
#define SQLITE_TRACE_CLOSE 0x08
#endif

// Transaction states (sqlite3_txn_state)
#ifndef SQLITE_TXN_NONE
#define SQLITE_TXN_NONE 0
#endif
#ifndef SQLITE_TXN_READ
#define SQLITE_TXN_READ 1
#endif
#ifndef SQLITE_TXN_WRITE
#define SQLITE_TXN_WRITE 2
#endif

// WAL checkpoint modes (sqlite3_wal_checkpoint_v2)
#ifndef SQLITE_CHECKPOINT_NOOP
#define SQLITE_CHECKPOINT_NOOP -1
#endif
#ifndef SQLITE_CHECKPOINT_PASSIVE
#define SQLITE_CHECKPOINT_PASSIVE 0
#endif
#ifndef SQLITE_CHECKPOINT_FULL
#define SQLITE_CHECKPOINT_FULL 1
#endif
#ifndef SQLITE_CHECKPOINT_RESTART
#define SQLITE_CHECKPOINT_RESTART 2
#endif
#ifndef SQLITE_CHECKPOINT_TRUNCATE
#define SQLITE_CHECKPOINT_TRUNCATE 3
#endif

// Serialize / Deserialize flags
#ifndef SQLITE_SERIALIZE_NOCOPY
#define SQLITE_SERIALIZE_NOCOPY 0x001
#endif
#ifndef SQLITE_DESERIALIZE_FREEONCLOSE
#define SQLITE_DESERIALIZE_FREEONCLOSE 1
#endif
#ifndef SQLITE_DESERIALIZE_RESIZEABLE
#define SQLITE_DESERIALIZE_RESIZEABLE 2
#endif
#ifndef SQLITE_DESERIALIZE_READONLY
#define SQLITE_DESERIALIZE_READONLY 4
#endif

// Setlk timeout flags
#ifndef SQLITE_SETLK_BLOCK_ON_CONNECT
#define SQLITE_SETLK_BLOCK_ON_CONNECT 0x01
#endif

// Connection limit categories (sqlite3_limit)
#ifndef SQLITE_LIMIT_LENGTH
#define SQLITE_LIMIT_LENGTH 0
#endif
#ifndef SQLITE_LIMIT_SQL_LENGTH
#define SQLITE_LIMIT_SQL_LENGTH 1
#endif
#ifndef SQLITE_LIMIT_COLUMN
#define SQLITE_LIMIT_COLUMN 2
#endif
#ifndef SQLITE_LIMIT_EXPR_DEPTH
#define SQLITE_LIMIT_EXPR_DEPTH 3
#endif
#ifndef SQLITE_LIMIT_COMPOUND_SELECT
#define SQLITE_LIMIT_COMPOUND_SELECT 4
#endif
#ifndef SQLITE_LIMIT_VDBE_OP
#define SQLITE_LIMIT_VDBE_OP 5
#endif
#ifndef SQLITE_LIMIT_FUNCTION_ARG
#define SQLITE_LIMIT_FUNCTION_ARG 6
#endif
#ifndef SQLITE_LIMIT_ATTACHED
#define SQLITE_LIMIT_ATTACHED 7
#endif
#ifndef SQLITE_LIMIT_LIKE_PATTERN_LENGTH
#define SQLITE_LIMIT_LIKE_PATTERN_LENGTH 8
#endif
#ifndef SQLITE_LIMIT_VARIABLE_NUMBER
#define SQLITE_LIMIT_VARIABLE_NUMBER 9
#endif
#ifndef SQLITE_LIMIT_TRIGGER_DEPTH
#define SQLITE_LIMIT_TRIGGER_DEPTH 10
#endif
#ifndef SQLITE_LIMIT_WORKER_THREADS
#define SQLITE_LIMIT_WORKER_THREADS 11
#endif
#ifndef SQLITE_LIMIT_PARSER_DEPTH
#define SQLITE_LIMIT_PARSER_DEPTH 12
#endif
#ifndef SQLITE_LIMIT_SCHEMA
#define SQLITE_LIMIT_SCHEMA 13
#endif
#ifndef SQLITE_LIMIT_TRIGGER_STEPS
#define SQLITE_LIMIT_TRIGGER_STEPS 14
#endif

// Connection configuration verbs (sqlite3_db_config)
#ifndef SQLITE_DBCONFIG_MAINDBNAME
#define SQLITE_DBCONFIG_MAINDBNAME 1000
#endif
#ifndef SQLITE_DBCONFIG_LOOKASIDE
#define SQLITE_DBCONFIG_LOOKASIDE 1001
#endif
#ifndef SQLITE_DBCONFIG_ENABLE_FKEY
#define SQLITE_DBCONFIG_ENABLE_FKEY 1002
#endif
#ifndef SQLITE_DBCONFIG_ENABLE_TRIGGER
#define SQLITE_DBCONFIG_ENABLE_TRIGGER 1003
#endif
#ifndef SQLITE_DBCONFIG_ENABLE_FTS3_TOKENIZER
#define SQLITE_DBCONFIG_ENABLE_FTS3_TOKENIZER 1004
#endif
#ifndef SQLITE_DBCONFIG_ENABLE_LOAD_EXTENSION
#define SQLITE_DBCONFIG_ENABLE_LOAD_EXTENSION 1005
#endif
#ifndef SQLITE_DBCONFIG_NO_CKPT_ON_CLOSE
#define SQLITE_DBCONFIG_NO_CKPT_ON_CLOSE 1006
#endif
#ifndef SQLITE_DBCONFIG_ENABLE_QPSG
#define SQLITE_DBCONFIG_ENABLE_QPSG 1007
#endif
#ifndef SQLITE_DBCONFIG_TRIGGER_EQP
#define SQLITE_DBCONFIG_TRIGGER_EQP 1008
#endif
#ifndef SQLITE_DBCONFIG_RESET_DATABASE
#define SQLITE_DBCONFIG_RESET_DATABASE 1009
#endif
#ifndef SQLITE_DBCONFIG_DEFENSIVE
#define SQLITE_DBCONFIG_DEFENSIVE 1010
#endif
#ifndef SQLITE_DBCONFIG_WRITABLE_SCHEMA
#define SQLITE_DBCONFIG_WRITABLE_SCHEMA 1011
#endif
#ifndef SQLITE_DBCONFIG_LEGACY_ALTER_TABLE
#define SQLITE_DBCONFIG_LEGACY_ALTER_TABLE 1012
#endif
#ifndef SQLITE_DBCONFIG_DQS_DML
#define SQLITE_DBCONFIG_DQS_DML 1013
#endif
#ifndef SQLITE_DBCONFIG_DQS_DDL
#define SQLITE_DBCONFIG_DQS_DDL 1014
#endif
#ifndef SQLITE_DBCONFIG_ENABLE_VIEW
#define SQLITE_DBCONFIG_ENABLE_VIEW 1015
#endif
#ifndef SQLITE_DBCONFIG_LEGACY_FILE_FORMAT
#define SQLITE_DBCONFIG_LEGACY_FILE_FORMAT 1016
#endif
#ifndef SQLITE_DBCONFIG_TRUSTED_SCHEMA
#define SQLITE_DBCONFIG_TRUSTED_SCHEMA 1017
#endif
#ifndef SQLITE_DBCONFIG_STMT_SCANSTATUS
#define SQLITE_DBCONFIG_STMT_SCANSTATUS 1018
#endif
#ifndef SQLITE_DBCONFIG_REVERSE_SCANORDER
#define SQLITE_DBCONFIG_REVERSE_SCANORDER 1019
#endif
#ifndef SQLITE_DBCONFIG_ENABLE_ATTACH_CREATE
#define SQLITE_DBCONFIG_ENABLE_ATTACH_CREATE 1020
#endif
#ifndef SQLITE_DBCONFIG_ENABLE_ATTACH_WRITE
#define SQLITE_DBCONFIG_ENABLE_ATTACH_WRITE 1021
#endif
#ifndef SQLITE_DBCONFIG_ENABLE_COMMENTS
#define SQLITE_DBCONFIG_ENABLE_COMMENTS 1022
#endif
#ifndef SQLITE_DBCONFIG_FP_DIGITS
#define SQLITE_DBCONFIG_FP_DIGITS 1023
#endif

// Global status parameters (sqlite3_status64)
#ifndef SQLITE_STATUS_MEMORY_USED
#define SQLITE_STATUS_MEMORY_USED 0
#endif
#ifndef SQLITE_STATUS_PAGECACHE_USED
#define SQLITE_STATUS_PAGECACHE_USED 1
#endif
#ifndef SQLITE_STATUS_PAGECACHE_OVERFLOW
#define SQLITE_STATUS_PAGECACHE_OVERFLOW 2
#endif
#ifndef SQLITE_STATUS_MALLOC_SIZE
#define SQLITE_STATUS_MALLOC_SIZE 5
#endif
#ifndef SQLITE_STATUS_PARSER_STACK
#define SQLITE_STATUS_PARSER_STACK 6
#endif
#ifndef SQLITE_STATUS_PAGECACHE_SIZE
#define SQLITE_STATUS_PAGECACHE_SIZE 7
#endif
#ifndef SQLITE_STATUS_MALLOC_COUNT
#define SQLITE_STATUS_MALLOC_COUNT 9
#endif

// Database status parameters (sqlite3_db_status64)
#ifndef SQLITE_DBSTATUS_LOOKASIDE_USED
#define SQLITE_DBSTATUS_LOOKASIDE_USED 0
#endif
#ifndef SQLITE_DBSTATUS_CACHE_USED
#define SQLITE_DBSTATUS_CACHE_USED 1
#endif
#ifndef SQLITE_DBSTATUS_SCHEMA_USED
#define SQLITE_DBSTATUS_SCHEMA_USED 2
#endif
#ifndef SQLITE_DBSTATUS_STMT_USED
#define SQLITE_DBSTATUS_STMT_USED 3
#endif
#ifndef SQLITE_DBSTATUS_LOOKASIDE_HIT
#define SQLITE_DBSTATUS_LOOKASIDE_HIT 4
#endif
#ifndef SQLITE_DBSTATUS_LOOKASIDE_MISS_SIZE
#define SQLITE_DBSTATUS_LOOKASIDE_MISS_SIZE 5
#endif
#ifndef SQLITE_DBSTATUS_LOOKASIDE_MISS_FULL
#define SQLITE_DBSTATUS_LOOKASIDE_MISS_FULL 6
#endif
#ifndef SQLITE_DBSTATUS_CACHE_HIT
#define SQLITE_DBSTATUS_CACHE_HIT 7
#endif
#ifndef SQLITE_DBSTATUS_CACHE_MISS
#define SQLITE_DBSTATUS_CACHE_MISS 8
#endif
#ifndef SQLITE_DBSTATUS_CACHE_WRITE
#define SQLITE_DBSTATUS_CACHE_WRITE 9
#endif
#ifndef SQLITE_DBSTATUS_DEFERRED_FKS
#define SQLITE_DBSTATUS_DEFERRED_FKS 10
#endif
#ifndef SQLITE_DBSTATUS_CACHE_USED_SHARED
#define SQLITE_DBSTATUS_CACHE_USED_SHARED 11
#endif
#ifndef SQLITE_DBSTATUS_CACHE_SPILL
#define SQLITE_DBSTATUS_CACHE_SPILL 12
#endif
#ifndef SQLITE_DBSTATUS_TEMPBUF_SPILL
#define SQLITE_DBSTATUS_TEMPBUF_SPILL 13
#endif

// Statement status counters (sqlite3_stmt_status)
#ifndef SQLITE_STMTSTATUS_FULLSCAN_STEP
#define SQLITE_STMTSTATUS_FULLSCAN_STEP 1
#endif
#ifndef SQLITE_STMTSTATUS_SORT
#define SQLITE_STMTSTATUS_SORT 2
#endif
#ifndef SQLITE_STMTSTATUS_AUTOINDEX
#define SQLITE_STMTSTATUS_AUTOINDEX 3
#endif
#ifndef SQLITE_STMTSTATUS_VM_STEP
#define SQLITE_STMTSTATUS_VM_STEP 4
#endif
#ifndef SQLITE_STMTSTATUS_REPREPARE
#define SQLITE_STMTSTATUS_REPREPARE 5
#endif
#ifndef SQLITE_STMTSTATUS_RUN
#define SQLITE_STMTSTATUS_RUN 6
#endif
#ifndef SQLITE_STMTSTATUS_FILTER_MISS
#define SQLITE_STMTSTATUS_FILTER_MISS 7
#endif
#ifndef SQLITE_STMTSTATUS_FILTER_HIT
#define SQLITE_STMTSTATUS_FILTER_HIT 8
#endif
#ifndef SQLITE_STMTSTATUS_MEMUSED
#define SQLITE_STMTSTATUS_MEMUSED 99
#endif

// Statement scanstatus constants (sqlite3_stmt_scanstatus_v2)
#ifndef SQLITE_SCANSTAT_NLOOP
#define SQLITE_SCANSTAT_NLOOP 0
#endif
#ifndef SQLITE_SCANSTAT_NVISIT
#define SQLITE_SCANSTAT_NVISIT 1
#endif
#ifndef SQLITE_SCANSTAT_EST
#define SQLITE_SCANSTAT_EST 2
#endif
#ifndef SQLITE_SCANSTAT_NAME
#define SQLITE_SCANSTAT_NAME 3
#endif
#ifndef SQLITE_SCANSTAT_EXPLAIN
#define SQLITE_SCANSTAT_EXPLAIN 4
#endif
#ifndef SQLITE_SCANSTAT_SELECTID
#define SQLITE_SCANSTAT_SELECTID 5
#endif
#ifndef SQLITE_SCANSTAT_PARENTID
#define SQLITE_SCANSTAT_PARENTID 6
#endif
#ifndef SQLITE_SCANSTAT_NCYCLE
#define SQLITE_SCANSTAT_NCYCLE 7
#endif
#ifndef SQLITE_SCANSTAT_COMPLEX
#define SQLITE_SCANSTAT_COMPLEX 0x0001
#endif

// Virtual table configuration & conflict modes
#ifndef SQLITE_VTAB_CONSTRAINT_SUPPORT
#define SQLITE_VTAB_CONSTRAINT_SUPPORT 1
#endif
#ifndef SQLITE_VTAB_INNOCUOUS
#define SQLITE_VTAB_INNOCUOUS 2
#endif
#ifndef SQLITE_VTAB_DIRECTONLY
#define SQLITE_VTAB_DIRECTONLY 3
#endif
#ifndef SQLITE_VTAB_USES_ALL_SCHEMAS
#define SQLITE_VTAB_USES_ALL_SCHEMAS 4
#endif
#ifndef SQLITE_ROLLBACK
#define SQLITE_ROLLBACK 1
#endif
#ifndef SQLITE_IGNORE
#define SQLITE_IGNORE 2
#endif
#ifndef SQLITE_FAIL
#define SQLITE_FAIL 3
#endif
#ifndef SQLITE_REPLACE
#define SQLITE_REPLACE 5
#endif
#ifndef SQLITE_INDEX_SCAN_UNIQUE
#define SQLITE_INDEX_SCAN_UNIQUE 0x00000001
#endif
#ifndef SQLITE_INDEX_SCAN_HEX
#define SQLITE_INDEX_SCAN_HEX 0x00000002
#endif

// C-Array datatypes (sqlite3_carray_bind)
#ifndef SQLITE_CARRAY_INT32
#define SQLITE_CARRAY_INT32 0
#endif
#ifndef SQLITE_CARRAY_INT64
#define SQLITE_CARRAY_INT64 1
#endif
#ifndef SQLITE_CARRAY_DOUBLE
#define SQLITE_CARRAY_DOUBLE 2
#endif
#ifndef SQLITE_CARRAY_TEXT
#define SQLITE_CARRAY_TEXT 3
#endif
#ifndef SQLITE_CARRAY_BLOB
#define SQLITE_CARRAY_BLOB 4
#endif

// R-Tree visibility return values
#ifndef NOT_WITHIN
#define NOT_WITHIN 0
#endif
#ifndef PARTLY_WITHIN
#define PARTLY_WITHIN 1
#endif
#ifndef FULLY_WITHIN
#define FULLY_WITHIN 2
#endif

// FTS5 tokenizer flags
#ifndef FTS5_TOKENIZE_QUERY
#define FTS5_TOKENIZE_QUERY 0x0001
#endif
#ifndef FTS5_TOKENIZE_PREFIX
#define FTS5_TOKENIZE_PREFIX 0x0002
#endif
#ifndef FTS5_TOKENIZE_DOCUMENT
#define FTS5_TOKENIZE_DOCUMENT 0x0004
#endif
#ifndef FTS5_TOKENIZE_AUX
#define FTS5_TOKENIZE_AUX 0x0008
#endif
#ifndef FTS5_TOKEN_COLOCATED
#define FTS5_TOKEN_COLOCATED 0x0001
#endif

// File control opcodes (sqlite3_file_control)
#ifndef SQLITE_FCNTL_LOCKSTATE
#define SQLITE_FCNTL_LOCKSTATE 1
#endif
#ifndef SQLITE_FCNTL_GET_LOCKPROXYFILE
#define SQLITE_FCNTL_GET_LOCKPROXYFILE 2
#endif
#ifndef SQLITE_FCNTL_SET_LOCKPROXYFILE
#define SQLITE_FCNTL_SET_LOCKPROXYFILE 3
#endif
#ifndef SQLITE_FCNTL_LAST_ERRNO
#define SQLITE_FCNTL_LAST_ERRNO 4
#endif
#ifndef SQLITE_FCNTL_SIZE_HINT
#define SQLITE_FCNTL_SIZE_HINT 5
#endif
#ifndef SQLITE_FCNTL_CHUNK_SIZE
#define SQLITE_FCNTL_CHUNK_SIZE 6
#endif
#ifndef SQLITE_FCNTL_FILE_POINTER
#define SQLITE_FCNTL_FILE_POINTER 7
#endif
#ifndef SQLITE_FCNTL_SYNC_OMITTED
#define SQLITE_FCNTL_SYNC_OMITTED 8
#endif
#ifndef SQLITE_FCNTL_WIN32_AV_RETRY
#define SQLITE_FCNTL_WIN32_AV_RETRY 9
#endif
#ifndef SQLITE_FCNTL_PERSIST_WAL
#define SQLITE_FCNTL_PERSIST_WAL 10
#endif
#ifndef SQLITE_FCNTL_OVERWRITE
#define SQLITE_FCNTL_OVERWRITE 11
#endif
#ifndef SQLITE_FCNTL_VFSNAME
#define SQLITE_FCNTL_VFSNAME 12
#endif
#ifndef SQLITE_FCNTL_POWERSAFE_OVERWRITE
#define SQLITE_FCNTL_POWERSAFE_OVERWRITE 13
#endif
#ifndef SQLITE_FCNTL_PRAGMA
#define SQLITE_FCNTL_PRAGMA 14
#endif
#ifndef SQLITE_FCNTL_BUSYHANDLER
#define SQLITE_FCNTL_BUSYHANDLER 15
#endif
#ifndef SQLITE_FCNTL_TEMPFILENAME
#define SQLITE_FCNTL_TEMPFILENAME 16
#endif
#ifndef SQLITE_FCNTL_MMAP_SIZE
#define SQLITE_FCNTL_MMAP_SIZE 18
#endif
#ifndef SQLITE_FCNTL_TRACE
#define SQLITE_FCNTL_TRACE 19
#endif
#ifndef SQLITE_FCNTL_HAS_MOVED
#define SQLITE_FCNTL_HAS_MOVED 20
#endif
#ifndef SQLITE_FCNTL_SYNC
#define SQLITE_FCNTL_SYNC 21
#endif
#ifndef SQLITE_FCNTL_COMMIT_PHASETWO
#define SQLITE_FCNTL_COMMIT_PHASETWO 22
#endif
#ifndef SQLITE_FCNTL_WIN32_SET_HANDLE
#define SQLITE_FCNTL_WIN32_SET_HANDLE 23
#endif
#ifndef SQLITE_FCNTL_WAL_BLOCK
#define SQLITE_FCNTL_WAL_BLOCK 24
#endif
#ifndef SQLITE_FCNTL_ZIPVFS
#define SQLITE_FCNTL_ZIPVFS 25
#endif
#ifndef SQLITE_FCNTL_RBU
#define SQLITE_FCNTL_RBU 26
#endif
#ifndef SQLITE_FCNTL_VFS_POINTER
#define SQLITE_FCNTL_VFS_POINTER 27
#endif
#ifndef SQLITE_FCNTL_JOURNAL_POINTER
#define SQLITE_FCNTL_JOURNAL_POINTER 28
#endif
#ifndef SQLITE_FCNTL_WIN32_GET_HANDLE
#define SQLITE_FCNTL_WIN32_GET_HANDLE 29
#endif
#ifndef SQLITE_FCNTL_PDB
#define SQLITE_FCNTL_PDB 30
#endif
#ifndef SQLITE_FCNTL_BEGIN_ATOMIC_WRITE
#define SQLITE_FCNTL_BEGIN_ATOMIC_WRITE 31
#endif
#ifndef SQLITE_FCNTL_COMMIT_ATOMIC_WRITE
#define SQLITE_FCNTL_COMMIT_ATOMIC_WRITE 32
#endif
#ifndef SQLITE_FCNTL_ROLLBACK_ATOMIC_WRITE
#define SQLITE_FCNTL_ROLLBACK_ATOMIC_WRITE 33
#endif
#ifndef SQLITE_FCNTL_LOCK_TIMEOUT
#define SQLITE_FCNTL_LOCK_TIMEOUT 34
#endif
#ifndef SQLITE_FCNTL_DATA_VERSION
#define SQLITE_FCNTL_DATA_VERSION 35
#endif
#ifndef SQLITE_FCNTL_SIZE_LIMIT
#define SQLITE_FCNTL_SIZE_LIMIT 36
#endif
#ifndef SQLITE_FCNTL_CKPT_DONE
#define SQLITE_FCNTL_CKPT_DONE 37
#endif
#ifndef SQLITE_FCNTL_RESERVE_BYTES
#define SQLITE_FCNTL_RESERVE_BYTES 38
#endif
#ifndef SQLITE_FCNTL_CKPT_START
#define SQLITE_FCNTL_CKPT_START 39
#endif
#ifndef SQLITE_FCNTL_EXTERNAL_READER
#define SQLITE_FCNTL_EXTERNAL_READER 40
#endif
#ifndef SQLITE_FCNTL_CKSM_FILE
#define SQLITE_FCNTL_CKSM_FILE 41
#endif
#ifndef SQLITE_FCNTL_RESET_CACHE
#define SQLITE_FCNTL_RESET_CACHE 42
#endif
#ifndef SQLITE_FCNTL_NULL_IO
#define SQLITE_FCNTL_NULL_IO 43
#endif
#ifndef SQLITE_FCNTL_BLOCK_ON_CONNECT
#define SQLITE_FCNTL_BLOCK_ON_CONNECT 44
#endif
#ifndef SQLITE_FCNTL_FILESTAT
#define SQLITE_FCNTL_FILESTAT 45
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
