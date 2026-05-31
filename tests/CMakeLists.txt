# ============================================================================
# CloudsQLite Test Suite
# ============================================================================

# Collect all test sources
set(TEST_SOURCES
    test_error.cpp
    test_value.cpp
    test_database.cpp
    test_statement.cpp
    test_session.cpp
    test_changeset.cpp
    test_changegroup.cpp
)

# Create the test executable
add_executable(cloudsqlite_tests ${TEST_SOURCES})

target_link_libraries(cloudsqlite_tests
    PRIVATE
        cloudsqlite
        GTest::gtest
        GTest::gtest_main
)

target_include_directories(cloudsqlite_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${SQLite3_INCLUDE_DIRS}
)

# Discover and register tests with CTest
include(GoogleTest)
gtest_discover_tests(cloudsqlite_tests
    DISCOVERY_TIMEOUT 30
    PROPERTIES
        LABELS "cloudsqlite"
        TIMEOUT 60
)
