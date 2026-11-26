vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO khanaffan/sqlitex
    REF "v${VERSION}"
    SHA512 52e3c00f584f8343d83185b5cfcea9ebe86df90adf45a8e9ed47e736414b60586bbd60cde19df0bc61660eecdb919a6dedac2e612c395f92f0faa481878f299c
    HEAD_REF main
)

vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        session SQLITEX_SESSION
        bcvfs   SQLITEX_BCVFS
        tools   SQLITEX_BUILD_DAEMON
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        ${FEATURE_OPTIONS}
        -DSQLITEX_BUILD_TESTS=OFF
        -DSQLITEX_BUILD_EXAMPLES=OFF
        -DSQLITEX_LINT=OFF
        -DSQLITEX_INSTALL=ON
)

vcpkg_cmake_build()
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/sqlitex)

if("tools" IN_LIST FEATURES)
    vcpkg_copy_tools(TOOL_NAMES blockcachevfsd AUTO_CLEAN)
endif()

if(VCPKG_LIBRARY_LINKAGE STREQUAL "static")
    vcpkg_replace_string(
        "${CURRENT_PACKAGES_DIR}/include/sqlitex/sqlitex_export.h"
        "#ifdef SQLITEX_STATIC"
        "#if 1"
    )
endif()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")

file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
)
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
