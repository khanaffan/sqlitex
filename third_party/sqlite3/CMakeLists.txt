# Bundled SQLite3 amalgamation
# Compiled as a static PIC library so it can be linked into both
# static and shared builds of sqlitex.
add_library(sqlite3_amalgamation STATIC
    sqlite3.c
    sqlite3.h
)

target_compile_definitions(sqlite3_amalgamation PRIVATE
    # Session extension (required by sqlitex)
    SQLITE_ENABLE_SESSION
    SQLITE_ENABLE_PREUPDATE_HOOK
    # Useful extras and optional public APIs
    SQLITE_ENABLE_COLUMN_METADATA
    SQLITE_ENABLE_DBSTAT_VTAB
    SQLITE_ENABLE_FTS5
    SQLITE_ENABLE_JSON1
    SQLITE_ENABLE_RTREE
    SQLITE_ENABLE_CARRAY
    SQLITE_ENABLE_SNAPSHOT
    SQLITE_ENABLE_STMT_SCANSTATUS
    SQLITE_ENABLE_UNLOCK_NOTIFY
    SQLITE_ENABLE_NORMALIZE
    SQLITE_ENABLE_SETLK_TIMEOUT
    SQLITE_SOUNDEX
    # Thread-safe mode
    SQLITE_THREADSAFE=1
    # Suppress MSVC deprecation noise
    $<$<C_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
)

target_compile_options(sqlite3_amalgamation PRIVATE
    $<$<C_COMPILER_ID:MSVC>:/bigobj>
)

set_target_properties(sqlite3_amalgamation PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
)

target_include_directories(sqlite3_amalgamation
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)
