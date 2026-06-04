cmake_minimum_required(VERSION 3.15)

# Example targets
add_executable(basic_usage basic_usage.cpp)
target_link_libraries(basic_usage PRIVATE sqlitex)

if(SQLITEX_HAS_SESSION)
    add_executable(session_tracking session_tracking.cpp)
    target_link_libraries(session_tracking PRIVATE sqlitex)

    add_executable(changeset_apply changeset_apply.cpp)
    target_link_libraries(changeset_apply PRIVATE sqlitex)

    add_executable(changegroup_merge changegroup_merge.cpp)
    target_link_libraries(changegroup_merge PRIVATE sqlitex)
endif()

if(SQLITEX_BCVFS)
    # bcvfs example compiles but requires cloud storage credentials to actually run
    add_executable(bcvfs_basic bcvfs_basic.cpp)
    target_link_libraries(bcvfs_basic PRIVATE sqlitex)
endif()
