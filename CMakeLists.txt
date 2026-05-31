cmake_minimum_required(VERSION 3.15)
project(cloudsqlite VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# ============================================================================
# Options
# ============================================================================
option(CLOUDSQLITE_BCVFS "Build with bcvfs (Block Cache VFS) support" ON)
option(CLOUDSQLITE_BUILD_EXAMPLES "Build example programs" ON)
option(CLOUDSQLITE_BUILD_TESTS "Build tests (uses Google Test via FetchContent)" ON)

# ============================================================================
# Find SQLite3
# ============================================================================
find_package(SQLite3 QUIET)

if(NOT SQLite3_FOUND OR SQLite3_INCLUDE_DIR MATCHES "/CommandLineTools/")
    # Try Homebrew paths first (Apple's system sqlite3 lacks session extension)
    find_library(SQLITE3_LIBRARY sqlite3
        PATHS /opt/homebrew/opt/sqlite/lib /usr/local/opt/sqlite/lib
        NO_DEFAULT_PATH
    )
    find_path(SQLITE3_INCLUDE_DIR sqlite3.h
        PATHS /opt/homebrew/opt/sqlite/include /usr/local/opt/sqlite/include
        NO_DEFAULT_PATH
    )
    if(SQLITE3_LIBRARY AND SQLITE3_INCLUDE_DIR)
        set(SQLite3_FOUND TRUE)
        set(SQLite3_LIBRARIES ${SQLITE3_LIBRARY})
        set(SQLite3_INCLUDE_DIRS ${SQLITE3_INCLUDE_DIR})
        message(STATUS "Found SQLite3 (Homebrew): ${SQLITE3_LIBRARY}")
    endif()
endif()

if(NOT SQLite3_FOUND)
    find_library(SQLITE3_LIBRARY sqlite3)
    find_path(SQLITE3_INCLUDE_DIR sqlite3.h)
    if(SQLITE3_LIBRARY AND SQLITE3_INCLUDE_DIR)
        set(SQLite3_FOUND TRUE)
        set(SQLite3_LIBRARIES ${SQLITE3_LIBRARY})
        set(SQLite3_INCLUDE_DIRS ${SQLITE3_INCLUDE_DIR})
        message(STATUS "Found SQLite3: ${SQLITE3_LIBRARY}")
    endif()
endif()

if(NOT SQLite3_FOUND)
    message(FATAL_ERROR "SQLite3 not found. Install it or set SQLITE3_LIBRARY and SQLITE3_INCLUDE_DIR.")
endif()

# Check that SQLite was compiled with session support
include(CheckCSourceCompiles)
set(CMAKE_REQUIRED_INCLUDES ${SQLite3_INCLUDE_DIRS})
set(CMAKE_REQUIRED_LIBRARIES ${SQLite3_LIBRARIES})
check_c_source_compiles(
    "#include <sqlite3.h>
     int main() {
       sqlite3_session *s = 0;
       (void)s;
       return 0;
     }"
    SQLITE_HAS_SESSION_SUPPORT
)
if(NOT SQLITE_HAS_SESSION_SUPPORT)
    message(WARNING
        "SQLite3 does not have the session extension. "
        "Session and changeset features will not be available."
    )
endif()

# ============================================================================
# Library sources
# ============================================================================
set(CLOUDSQLITE_SOURCES
    src/error.cpp
    src/database.cpp
    src/statement.cpp
    src/session.cpp
    src/changeset.cpp
    src/changeset_iterator.cpp
    src/changegroup.cpp
)

set(CLOUDSQLITE_HEADERS
    include/cloudsqlite/cloudsqlite.h
    include/cloudsqlite/error.h
    include/cloudsqlite/database.h
    include/cloudsqlite/statement.h
    include/cloudsqlite/value.h
    include/cloudsqlite/session.h
    include/cloudsqlite/changeset.h
    include/cloudsqlite/changeset_iterator.h
    include/cloudsqlite/changegroup.h
    include/cloudsqlite/detail/blob.h
    include/cloudsqlite/detail/callbacks.h
)

# bcvfs support (optional)
if(CLOUDSQLITE_BCVFS)
    # User must provide blockcachevfs.c and blockcachevfs.h from the
    # SQLite source tree (ext/bcvfs/).  Place them in src/ or set
    # CLOUDSQLITE_BCVFS_SOURCE_DIR to the directory containing them.
    find_path(BCVFS_INCLUDE_DIR blockcachevfs.h
        PATHS
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CLOUDSQLITE_BCVFS_SOURCE_DIR}
            ENV CLOUDSQLITE_BCVFS_DIR
        NO_DEFAULT_PATH
    )
    find_file(BCVFS_SOURCE blockcachevfs.c
        PATHS
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CLOUDSQLITE_BCVFS_SOURCE_DIR}
            ENV CLOUDSQLITE_BCVFS_DIR
        NO_DEFAULT_PATH
    )

    if(BCVFS_SOURCE AND BCVFS_INCLUDE_DIR)
        message(STATUS "Found bcvfs source: ${BCVFS_SOURCE}")
        message(STATUS "Found bcvfs include: ${BCVFS_INCLUDE_DIR}")
        list(APPEND CLOUDSQLITE_SOURCES ${BCVFS_SOURCE} src/bcvfs.cpp)
        list(APPEND CLOUDSQLITE_HEADERS
            include/cloudsqlite/bcvfs.h
            include/cloudsqlite/prefetch.h
        )
        target_compile_definitions(cloudsqlite PUBLIC CLOUDSQLITE_HAS_BCVFS=1)
    else()
        message(WARNING
            "blockcachevfs.h/c not found. bcvfs support disabled. "
            "Download from https://sqlite.org/cloudsqlite/doc/trunk/src/ and "
            "place in src/ or set CLOUDSQLITE_BCVFS_SOURCE_DIR."
        )
        set(CLOUDSQLITE_BCVFS OFF)
    endif()
endif()

# ============================================================================
# Library target
# ============================================================================
add_library(cloudsqlite ${CLOUDSQLITE_SOURCES} ${CLOUDSQLITE_HEADERS})

target_include_directories(cloudsqlite
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${SQLite3_INCLUDE_DIRS}
)

target_link_libraries(cloudsqlite PUBLIC ${SQLite3_LIBRARIES})

# Internal bcvfs include if available
if(CLOUDSQLITE_BCVFS)
    target_include_directories(cloudsqlite PRIVATE ${BCVFS_INCLUDE_DIR})
endif()

# OS-specific libraries
if(UNIX AND NOT APPLE)
    target_link_libraries(cloudsqlite PUBLIC pthread dl)
endif()

# ============================================================================
# Install
# ============================================================================
include(GNUInstallDirs)
install(TARGETS cloudsqlite
    EXPORT cloudsqlite-targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/cloudsqlite
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h"
)

# ============================================================================
# Tests (Google Test via FetchContent)
# ============================================================================
if(CLOUDSQLITE_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.15.2
    )
    # Prevent GTest from installing itself alongside our project
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(GTEST_HAS_ABSL OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()
    add_subdirectory(tests)
endif()

# ============================================================================
# Examples
# ============================================================================
if(CLOUDSQLITE_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
