# ============================================================================
# Sqlitex Test Suite
# ============================================================================

# Collect all test sources
set(TEST_SOURCES
    test_error.cpp
    test_sqlite_global.cpp
    test_value.cpp
    test_database.cpp
    test_database_advanced.cpp
    test_backup_snapshot.cpp
    test_statement.cpp
    test_statement_cache.cpp
    test_statement_value_vtab.cpp
    test_function.cpp
    test_virtual_table.cpp
    test_extensions_fts5_rtree.cpp
    test_blobio.cpp
    test_policy.cpp
    test_bcvfs.cpp
    test_cpp23.cpp
)

if(SQLITEX_HAS_SESSION)
    list(APPEND TEST_SOURCES
        test_session.cpp
        test_changeset.cpp
        test_changegroup.cpp
        test_session_rebaser.cpp
    )
endif()

if(SQLITEX_BCVFS)
    list(APPEND TEST_SOURCES
        test_bcvfs_integration.cpp
        test_cloudsqlite_offline.cpp
    )
endif()

# Create the test executable
add_executable(sqlitex_tests ${TEST_SOURCES})

target_link_libraries(sqlitex_tests
    PRIVATE
        sqlitex
        GTest::gtest
        GTest::gtest_main
)

target_include_directories(sqlitex_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}
)

# Discover and register tests with CTest
include(GoogleTest)
gtest_discover_tests(sqlitex_tests
    DISCOVERY_TIMEOUT 30
    PROPERTIES
        LABELS "sqlitex"
        TIMEOUT 60
)
