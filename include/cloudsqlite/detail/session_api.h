#pragma once

/// Forward declarations and prototypes for the SQLite Session Extension.
///
/// Some distributions of SQLite (including Homebrew) ship the session
/// extension functions in the library but do NOT expose the types or
/// constants in sqlite3.h (they are gated behind SQLITE_ENABLE_SESSION
/// which is an internal compile-time flag).
///
/// This header provides the minimum declarations needed to use the session
/// API.  If your SQLite build exposes these in sqlite3.h, define
/// CLOUDSQLITE_SESSION_IN_HEADER to skip these declarations.

#ifndef CLOUDSQLITE_SESSION_IN_HEADER

// Opaque types
struct sqlite3_session;
struct sqlite3_changeset_iter;
struct sqlite3_changegroup;

#ifdef __cplusplus
extern "C" {
#endif

// Session lifecycle
int sqlite3session_create(sqlite3*, const char*, sqlite3_session**);
void sqlite3session_delete(sqlite3_session*);
int sqlite3session_enable(sqlite3_session*, int);
int sqlite3session_indirect(sqlite3_session*, int);
int sqlite3session_attach(sqlite3_session*, const char*);
int sqlite3session_table_filter(sqlite3_session*,
    int(*)(void*, const char*), void*);
int sqlite3session_changeset(sqlite3_session*, int*, void**);
int sqlite3session_patchset(sqlite3_session*, int*, void**);
int sqlite3session_diff(sqlite3_session*, const char*, const char*, char**);
int sqlite3session_isempty(sqlite3_session*);
sqlite3_int64 sqlite3session_memory_used(sqlite3_session*);
int sqlite3session_object_config(sqlite3_session*, int, void*);

// Changeset iteration
int sqlite3changeset_start(sqlite3_changeset_iter**, int, void*);
int sqlite3changeset_start_v2(sqlite3_changeset_iter**, int, void*, int);
int sqlite3changeset_finalize(sqlite3_changeset_iter*);
int sqlite3changeset_next(sqlite3_changeset_iter*);
int sqlite3changeset_op(sqlite3_changeset_iter*,
    const char**, int*, int*, int*);
int sqlite3changeset_pk(sqlite3_changeset_iter*,
    unsigned char**, int*);
int sqlite3changeset_old(sqlite3_changeset_iter*, int, sqlite3_value**);
int sqlite3changeset_new(sqlite3_changeset_iter*, int, sqlite3_value**);
int sqlite3changeset_conflict(sqlite3_changeset_iter*, int, sqlite3_value**);
int sqlite3changeset_fk_conflicts(sqlite3_changeset_iter*, int*);

// Changeset manipulation
int sqlite3changeset_concat(int, void*, int, void*, int*, void**);
int sqlite3changeset_invert(int, void*, int*, void**);

// Changeset application
int sqlite3changeset_apply(sqlite3*, int, void*,
    int(*)(void*, const char*),
    int(*)(void*, int, sqlite3_changeset_iter*),
    void*);
int sqlite3changeset_apply_v2(sqlite3*, int, void*,
    int(*)(void*, const char*),
    int(*)(void*, int, sqlite3_changeset_iter*),
    void*,
    void**, int*, int);
int sqlite3changeset_apply_v3(sqlite3*, int, void*,
    int(*)(void*, sqlite3_changeset_iter*),
    int(*)(void*, int, sqlite3_changeset_iter*),
    void*,
    void**, int*, int);

// Changegroup
int sqlite3changegroup_new(sqlite3_changegroup**);
void sqlite3changegroup_delete(sqlite3_changegroup*);
int sqlite3changegroup_add(sqlite3_changegroup*, int, void*);
int sqlite3changegroup_output(sqlite3_changegroup*, int*, void**);

// Constants (only define if not already provided by sqlite3.h)
#ifndef SQLITE_CHANGESET_DATA
#define SQLITE_CHANGESET_DATA        1
#endif
#ifndef SQLITE_CHANGESET_NOTFOUND
#define SQLITE_CHANGESET_NOTFOUND    2
#endif
#ifndef SQLITE_CHANGESET_CONFLICT
#define SQLITE_CHANGESET_CONFLICT    3
#endif
#ifndef SQLITE_CHANGESET_CONSTRAINT
#define SQLITE_CHANGESET_CONSTRAINT  4
#endif
#ifndef SQLITE_CHANGESET_FOREIGN_KEY
#define SQLITE_CHANGESET_FOREIGN_KEY 5
#endif
#ifndef SQLITE_CHANGESET_OMIT
#define SQLITE_CHANGESET_OMIT        0
#endif
#ifndef SQLITE_CHANGESET_REPLACE
#define SQLITE_CHANGESET_REPLACE     1
#endif
#ifndef SQLITE_CHANGESET_ABORT
#define SQLITE_CHANGESET_ABORT       2
#endif

#ifndef SQLITE_CHANGESETSTART_INVERT
#define SQLITE_CHANGESETSTART_INVERT       0x0002
#endif
#ifndef SQLITE_CHANGESETAPPLY_NOSAVEPOINT
#define SQLITE_CHANGESETAPPLY_NOSAVEPOINT  0x0001
#endif
#ifndef SQLITE_CHANGESETAPPLY_INVERT
#define SQLITE_CHANGESETAPPLY_INVERT       0x0002
#endif
#ifndef SQLITE_CHANGESETAPPLY_IGNORENOOP
#define SQLITE_CHANGESETAPPLY_IGNORENOOP   0x0004
#endif
#ifndef SQLITE_CHANGESETAPPLY_FKNOACTION
#define SQLITE_CHANGESETAPPLY_FKNOACTION   0x0008
#endif

#ifndef SQLITE_SESSION_OBJCONFIG_SIZE
#define SQLITE_SESSION_OBJCONFIG_SIZE   1
#endif
#ifndef SQLITE_SESSION_OBJCONFIG_ROWID
#define SQLITE_SESSION_OBJCONFIG_ROWID  2
#endif

#ifdef __cplusplus
}
#endif

#endif // CLOUDSQLITE_SESSION_IN_HEADER
