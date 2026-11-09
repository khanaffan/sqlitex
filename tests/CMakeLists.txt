# ============================================================================
# Sqlitex Test Suite
# ============================================================================

# Collect all test sources
set(TEST_SOURCES
    test_error.cpp
    test_value.cpp
    test_database.cpp
    test_statement.cpp
    test_function.cpp
    test_virtual_table.cpp
    test_blobio.cpp
    test_policy.cpp
    test_bcvfs.cpp
)

if(SQLITEX_HAS_SESSION)
    list(APPEND TEST_SOURCES
        test_session.cpp
        test_changeset.cpp
        test_changegroup.cpp
    )
endif()

if(SQLITEX_BCVFS)
    list(APPEND TEST_SOURCES test_bcvfs_integration.cpp)
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
