cmake_minimum_required(VERSION 3.15)
project(sqlitex_node LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# When invoked via cmake-js this file is the CMake source root. Pull in the
# parent sqlitex project so the addon can link the real CMake target.
if(NOT TARGET sqlitex)
    set(SQLITEX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(SQLITEX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SQLITEX_BUILD_NODE OFF CACHE BOOL "" FORCE)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/.." "${CMAKE_BINARY_DIR}/sqlitex")
endif()

if(NOT DEFINED CMAKE_JS_VERSION)
    message(STATUS "Skipping Node addon (not invoked via cmake-js)")
    return()
endif()

set(NODE_SOURCES
    native/addon.cpp
    native/database_wrap.cpp
)

if(SQLITEX_SESSION)
    list(APPEND NODE_SOURCES
        native/session_wrap.cpp
        native/changeset_wrap.cpp
        native/changegroup_wrap.cpp
    )
endif()

if(SQLITEX_BCVFS)
    list(APPEND NODE_SOURCES
        native/bcvfs_wrap.cpp
        native/container_wrap.cpp
    )
endif()

add_library(sqlitex_node SHARED ${NODE_SOURCES} ${CMAKE_JS_SRC})

set_target_properties(sqlitex_node PROPERTIES
    PREFIX ""
    SUFFIX ".node"
    OUTPUT_NAME "sqlitex_node"
    POSITION_INDEPENDENT_CODE ON
)

# node-addon-api include path
execute_process(
    COMMAND node -p "require('node-addon-api').include"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE NODE_ADDON_API_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE NODE_ADDON_API_RESULT
)
if(NOT NODE_ADDON_API_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to resolve node-addon-api include path")
endif()
string(REPLACE "\"" "" NODE_ADDON_API_DIR "${NODE_ADDON_API_DIR}")

target_include_directories(sqlitex_node PRIVATE
    ${CMAKE_JS_INC}
    ${NODE_ADDON_API_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/native
)

if(SQLITEX_BCVFS)
    target_include_directories(sqlitex_node PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/bcvfs
    )
endif()

target_compile_definitions(sqlitex_node PRIVATE
    NAPI_VERSION=8
    NAPI_CPP_EXCEPTIONS
    NODE_ADDON_API_DISABLE_DEPRECATED
)

target_link_libraries(sqlitex_node PRIVATE sqlitex ${CMAKE_JS_LIB})

if(APPLE)
    target_link_options(sqlitex_node PRIVATE
        "LINKER:-undefined,dynamic_lookup"
    )
endif()

if(MSVC)
    target_compile_options(sqlitex_node PRIVATE /EHsc /bigobj)
    target_compile_definitions(sqlitex_node PRIVATE
        _CRT_SECURE_NO_WARNINGS
        _CRT_NONSTDC_NO_WARNINGS
    )
endif()
