cmake_minimum_required(VERSION 3.15)
project(sqlitex VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# ============================================================================
# Options
# ============================================================================
option(SQLITEX_BCVFS "Build with bcvfs (Block Cache VFS) support" ON)
option(SQLITEX_SESSION "Build with session/changeset support" ON)
option(SQLITEX_BUILD_EXAMPLES "Build example programs" ON)
option(SQLITEX_BUILD_TESTS "Build tests (uses Google Test via FetchContent)" ON)
option(SQLITEX_LINT "Enable clang-tidy static analysis during build" OFF)

# ============================================================================
# Linter setup (clang-tidy)
# ============================================================================
if(SQLITEX_LINT)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy)
    if(CLANG_TIDY_EXE)
        message(STATUS "Found clang-tidy: ${CLANG_TIDY_EXE}")
        set(CMAKE_CXX_CLANG_TIDY
            "${CLANG_TIDY_EXE};--config-file=${CMAKE_SOURCE_DIR}/.clang-tidy"
        )
    else()
        message(WARNING "SQLITEX_LINT=ON but clang-tidy not found")
    endif()
endif()

# clang-format target (always available if found)
find_program(CLANG_FORMAT_EXE NAMES clang-format)
if(CLANG_FORMAT_EXE)
    message(STATUS "Found clang-format: ${CLANG_FORMAT_EXE}")

    # Collect all source files for formatting
    file(GLOB_RECURSE FORMAT_SOURCES
        ${CMAKE_SOURCE_DIR}/src/*.cpp
        ${CMAKE_SOURCE_DIR}/include/sqlitex/*.h
        ${CMAKE_SOURCE_DIR}/include/sqlitex/detail/*.h
        ${CMAKE_SOURCE_DIR}/tests/*.cpp
        ${CMAKE_SOURCE_DIR}/examples/*.cpp
    )

    add_custom_target(clang-format
        COMMAND ${CLANG_FORMAT_EXE} -i ${FORMAT_SOURCES}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Running clang-format on all source files"
    )

    add_custom_target(clang-format-check
        COMMAND ${CLANG_FORMAT_EXE} --dry-run --Werror ${FORMAT_SOURCES}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Checking code formatting with clang-format"
    )
else()
    message(STATUS "clang-format not found (format targets unavailable)")
endif()

# ============================================================================
# Bundled SQLite3 amalgamation
# ============================================================================
# SQLite3 is compiled from source as part of the library. This guarantees
# session extension availability and eliminates external dependency on a
# system SQLite3 installation.
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/third_party/sqlite3/sqlite3.c")
    message(STATUS "SQLite3 amalgamation not found. Downloading...")
    file(DOWNLOAD
        "https://www.sqlite.org/2026/sqlite-amalgamation-3530100.zip"
        "${CMAKE_BINARY_DIR}/sqlite-amalgamation.zip"
        STATUS DOWNLOAD_STATUS
    )
    list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
    if(NOT STATUS_CODE EQUAL 0)
        message(FATAL_ERROR
            "Failed to download SQLite3 amalgamation. "
            "Place sqlite3.c and sqlite3.h in third_party/sqlite3/"
        )
    endif()
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E tar xf
        "${CMAKE_BINARY_DIR}/sqlite-amalgamation.zip"
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
    file(GLOB SQLITE_AMALG_DIR
        "${CMAKE_BINARY_DIR}/sqlite-amalgamation-*"
    )
    list(GET SQLITE_AMALG_DIR 0 AMALG_DIR)
    file(COPY "${AMALG_DIR}/sqlite3.c" "${AMALG_DIR}/sqlite3.h"
        DESTINATION "${CMAKE_SOURCE_DIR}/third_party/sqlite3/"
    )
    message(STATUS "SQLite3 amalgamation installed to third_party/sqlite3/")
endif()

add_subdirectory(third_party/sqlite3)

# Session extension is always available when SQLITEX_SESSION=ON since we
# compile SQLite3 with SQLITE_ENABLE_SESSION.

# ============================================================================
# Library sources
# ============================================================================
set(SQLITEX_SOURCES
    src/error.cpp
    src/database.cpp
    src/statement.cpp
    src/function.cpp
    src/blobio.cpp
    src/policy.cpp
    src/policy_internal.h
    src/virtual_table.cpp
    src/bcvfs.cpp
)

set(SQLITEX_HEADERS
    include/sqlitex/sqlitex.h
    include/sqlitex/error.h
    include/sqlitex/database.h
    include/sqlitex/statement.h
    include/sqlitex/function.h
    include/sqlitex/virtual_table.h
    include/sqlitex/blobio.h
    include/sqlitex/policy.h
    include/sqlitex/value.h
    include/sqlitex/detail/blob.h
    include/sqlitex/detail/callbacks.h
    include/sqlitex/detail/session_api.h
)

# Session extension support (optional)
if(SQLITEX_SESSION)
    list(APPEND SQLITEX_SOURCES
        src/session.cpp
        src/changeset.cpp
        src/changeset_iterator.cpp
        src/changegroup.cpp
    )
    list(APPEND SQLITEX_HEADERS
        include/sqlitex/session.h
        include/sqlitex/changeset.h
        include/sqlitex/changeset_iterator.h
        include/sqlitex/changegroup.h
    )
    set(SQLITEX_HAS_SESSION TRUE)
endif()

# bcvfs support (optional)
if(SQLITEX_BCVFS)
    # bcVFS source files are bundled in third_party/bcvfs/.
    # They can also be provided via SQLITEX_BCVFS_SOURCE_DIR.
    find_path(BCVFS_INCLUDE_DIR blockcachevfs.h
        PATHS
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/bcvfs
            ${SQLITEX_BCVFS_SOURCE_DIR}
            ENV SQLITEX_BCVFS_DIR
        NO_DEFAULT_PATH
    )
    find_file(BCVFS_SOURCE blockcachevfs.c
        PATHS
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/bcvfs
            ${SQLITEX_BCVFS_SOURCE_DIR}
            ENV SQLITEX_BCVFS_DIR
        NO_DEFAULT_PATH
    )

    if(BCVFS_SOURCE AND BCVFS_INCLUDE_DIR)
        message(STATUS "Found bcvfs source: ${BCVFS_SOURCE}")
        message(STATUS "Found bcvfs include: ${BCVFS_INCLUDE_DIR}")
        list(APPEND SQLITEX_SOURCES
            ${BCVFS_SOURCE}
            ${BCVFS_INCLUDE_DIR}/bcvutil.c
            ${BCVFS_INCLUDE_DIR}/bcvmodule.c
            ${BCVFS_INCLUDE_DIR}/simplexml.c
            ${BCVFS_INCLUDE_DIR}/bcvencrypt.c
            ${BCVFS_INCLUDE_DIR}/bcvlog.c
        )
        list(APPEND SQLITEX_HEADERS
            include/sqlitex/bcvfs.h
            include/sqlitex/prefetch.h
        )
        # bcVFS needs curl and openssl
        find_package(CURL REQUIRED)
        find_package(OpenSSL REQUIRED)
    else()
        message(WARNING
            "blockcachevfs.h/c not found. bcvfs support disabled. "
            "Place bcVFS source files in third_party/bcvfs/ or "
            "set SQLITEX_BCVFS_SOURCE_DIR."
        )
        set(SQLITEX_BCVFS OFF)
    endif()
endif()

# ============================================================================
# Library target
# ============================================================================
add_library(sqlitex ${SQLITEX_SOURCES} ${SQLITEX_HEADERS})

# DLL export/import support for Windows shared library builds
include(GenerateExportHeader)
generate_export_header(sqlitex
    BASE_NAME SQLITEX
    EXPORT_MACRO_NAME SQLITEX_EXPORT
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/sqlitex_export.h
    STATIC_DEFINE SQLITEX_STATIC
)

target_include_directories(sqlitex
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(sqlitex PUBLIC sqlite3_amalgamation)

# Internal bcvfs include if available
if(SQLITEX_BCVFS)
    target_compile_definitions(sqlitex PUBLIC SQLITEX_HAS_BCVFS=1)
    target_include_directories(sqlitex PRIVATE ${BCVFS_INCLUDE_DIR})
    target_link_libraries(sqlitex PUBLIC
        CURL::libcurl
        OpenSSL::SSL
        OpenSSL::Crypto
    )
endif()

# Session extension compile definition
if(SQLITEX_HAS_SESSION)
    target_compile_definitions(sqlitex PUBLIC SQLITEX_HAS_SESSION=1)
endif()

# ============================================================================
# OS-specific settings
# ============================================================================

# Portable threading
find_package(Threads REQUIRED)
target_link_libraries(sqlitex PUBLIC Threads::Threads)
if(UNIX)
    target_link_libraries(sqlitex PRIVATE ${CMAKE_DL_LIBS})
endif()

# Windows-specific
if(WIN32)
    target_link_libraries(sqlitex PRIVATE ws2_32 crypt32)
endif()
if(MSVC)
    target_compile_definitions(sqlitex PRIVATE
        _CRT_SECURE_NO_WARNINGS
        _CRT_NONSTDC_NO_WARNINGS
    )
endif()

# ============================================================================
# bcVFS Daemon (blockcachevfsd)
# ============================================================================
if(SQLITEX_BCVFS)
    find_file(BCVFSD_SOURCE blockcachevfsd.c
        PATHS
            ${CMAKE_CURRENT_SOURCE_DIR}/third_party/bcvfs
            ${SQLITEX_BCVFS_SOURCE_DIR}
            ENV SQLITEX_BCVFS_DIR
        NO_DEFAULT_PATH
    )
    if(BCVFSD_SOURCE)
        add_executable(blockcachevfsd ${BCVFSD_SOURCE})
        target_include_directories(blockcachevfsd PRIVATE
            ${BCVFS_INCLUDE_DIR}
        )
        target_link_libraries(blockcachevfsd PRIVATE
            sqlitex
            CURL::libcurl
            OpenSSL::SSL
            OpenSSL::Crypto
            Threads::Threads
        )
        if(UNIX)
            target_link_libraries(blockcachevfsd PRIVATE ${CMAKE_DL_LIBS})
        endif()
        if(WIN32)
            target_link_libraries(blockcachevfsd PRIVATE ws2_32)
        endif()
        install(TARGETS blockcachevfsd
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
        message(STATUS "Building bcvfs daemon: blockcachevfsd")
    else()
        message(WARNING "blockcachevfsd.c not found. Daemon will not be built.")
    endif()
endif()

# ============================================================================
# Install
# ============================================================================
include(GNUInstallDirs)
install(TARGETS sqlitex
    EXPORT sqlitex-targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/sqlitex_export.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/sqlitex
)
install(DIRECTORY include/sqlitex
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h"
)

# ============================================================================
# Tests (Google Test via FetchContent)
# ============================================================================
if(SQLITEX_BUILD_TESTS)
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
if(SQLITEX_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
