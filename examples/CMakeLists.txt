function(sqlitex_add_example name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE sqlitex::sqlitex)
    if(WIN32 AND BUILD_SHARED_LIBS)
        add_custom_command(TARGET ${name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:${name}>
                $<TARGET_FILE_DIR:${name}>
            COMMAND_EXPAND_LISTS
        )
    endif()
endfunction()

# Example targets
sqlitex_add_example(basic_usage basic_usage.cpp)
sqlitex_add_example(statement_cache statement_cache.cpp)
sqlitex_add_example(scoped_policy scoped_policy.cpp)

if(SQLITEX_HAS_SESSION)
    sqlitex_add_example(session_tracking session_tracking.cpp)
    sqlitex_add_example(changeset_apply changeset_apply.cpp)
    sqlitex_add_example(changegroup_merge changegroup_merge.cpp)
endif()

if(SQLITEX_BCVFS)
    # bcvfs example compiles but requires cloud storage credentials to actually run
    sqlitex_add_example(bcvfs_basic bcvfs_basic.cpp)
endif()
