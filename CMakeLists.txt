cmake_minimum_required(VERSION 3.24)

project(wclap-test-plugins VERSION 0.1.0 LANGUAGES C CXX)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^wasm")
    set(ANY_WASM_TOOLCHAIN TRUE)
    set(plugin_formats WCLAP)
else()
    set(plugin_formats CLAP)
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

set(CLAP_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/external/clap" CACHE PATH "CLAP SDK")
set(CLAP_HELPERS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/external/clap-helpers" CACHE PATH "clap-helpers")
set(CLAP_WRAPPER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/external/clap-wrapper" CACHE PATH "clap-wrapper")

add_subdirectory("${CLAP_SDK_ROOT}" clap EXCLUDE_FROM_ALL)
add_subdirectory("${CLAP_HELPERS_ROOT}" clap-helpers EXCLUDE_FROM_ALL)
add_subdirectory("${CLAP_WRAPPER_ROOT}" clap-wrapper EXCLUDE_FROM_ALL)

set(gui_resources "${CMAKE_CURRENT_SOURCE_DIR}/gui-lifecycle/resources")
file(GLOB_RECURSE gui_resource_files CONFIGURE_DEPENDS "${gui_resources}/*")

add_library(gui-lifecycle-impl STATIC gui-lifecycle/Plugin.cpp)
target_include_directories(gui-lifecycle-impl PUBLIC gui-lifecycle)
target_link_libraries(gui-lifecycle-impl PUBLIC clap clap-helpers)

make_clapfirst_plugins(
    TARGET_NAME gui-lifecycle
    IMPL_TARGET gui-lifecycle-impl
    OUTPUT_NAME gui-lifecycle
    ENTRY_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/gui-lifecycle/Entry.cpp"
    RESOURCE_DIRECTORY "${gui_resources}"
    BUNDLE_IDENTIFIER "com.charlieculbert.wclap-test-plugins.gui-lifecycle"
    BUNDLE_VERSION "${PROJECT_VERSION}"
    PLUGIN_FORMATS ${plugin_formats}
    ASSET_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/artifacts"
    COPY_AFTER_BUILD FALSE)

add_library(process-lifecycle-impl STATIC process-lifecycle/Plugin.cpp)
target_include_directories(process-lifecycle-impl PUBLIC process-lifecycle)
target_link_libraries(process-lifecycle-impl PUBLIC clap clap-helpers)

make_clapfirst_plugins(
    TARGET_NAME process-lifecycle
    IMPL_TARGET process-lifecycle-impl
    OUTPUT_NAME process-lifecycle
    ENTRY_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/process-lifecycle/Entry.cpp"
    BUNDLE_IDENTIFIER "com.charlieculbert.wclap-test-plugins.process-lifecycle"
    BUNDLE_VERSION "${PROJECT_VERSION}"
    PLUGIN_FORMATS ${plugin_formats}
    ASSET_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/artifacts"
    COPY_AFTER_BUILD FALSE)

if(BUILD_TESTING AND NOT ANY_WASM_TOOLCHAIN)
    add_executable(process-lifecycle-smoke
        tests/process-lifecycle-smoke.cpp
        process-lifecycle/Entry.cpp)
    target_link_libraries(process-lifecycle-smoke PRIVATE process-lifecycle-impl)
    add_test(NAME process-lifecycle-smoke COMMAND process-lifecycle-smoke)
endif()

if(ANY_WASM_TOOLCHAIN)
    # clap-wrapper builds the module; package fresh source resources at the archive
    # root so hosts resolve module.wasm and the WebView from the same plug-in path.
    set(gui_package_staging "${CMAKE_CURRENT_BINARY_DIR}/gui-lifecycle-package")
    file(MAKE_DIRECTORY "${gui_package_staging}")
    add_custom_target(gui-lifecycle-package ALL
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${gui_package_staging}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${gui_package_staging}"
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:gui-lifecycle_wclap>"
            "${gui_package_staging}/module.wasm"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${gui_resources}"
            "${gui_package_staging}"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
            "${gui_package_staging}/LICENSE.txt"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${gui_package_staging}/THIRD_PARTY_LICENSES"
        COMMAND ${CMAKE_COMMAND} -E copy "${CLAP_SDK_ROOT}/LICENSE"
            "${gui_package_staging}/THIRD_PARTY_LICENSES/clap.txt"
        COMMAND ${CMAKE_COMMAND} -E copy "${CLAP_HELPERS_ROOT}/LICENSE"
            "${gui_package_staging}/THIRD_PARTY_LICENSES/clap-helpers.txt"
        COMMAND ${CMAKE_COMMAND} -E copy "${CLAP_WRAPPER_ROOT}/LICENSE"
            "${gui_package_staging}/THIRD_PARTY_LICENSES/clap-wrapper.txt"
        COMMAND ${CMAKE_COMMAND} -E chdir "${gui_package_staging}"
            ${CMAKE_COMMAND} -E env SOURCE_DATE_EPOCH=0
            ${CMAKE_COMMAND}
            "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/artifacts/gui-lifecycle.wclap.tar.gz"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PackageWclap.cmake"
        DEPENDS gui-lifecycle_wclap ${gui_resource_files}
            "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
            "${CLAP_SDK_ROOT}/LICENSE"
            "${CLAP_HELPERS_ROOT}/LICENSE"
            "${CLAP_WRAPPER_ROOT}/LICENSE"
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PackageWclap.cmake"
        VERBATIM)

    set(process_package_staging "${CMAKE_CURRENT_BINARY_DIR}/process-lifecycle-package")
    file(MAKE_DIRECTORY "${process_package_staging}")
    add_custom_target(process-lifecycle-package ALL
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${process_package_staging}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${process_package_staging}"
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:process-lifecycle_wclap>"
            "${process_package_staging}/module.wasm"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
            "${process_package_staging}/LICENSE.txt"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${process_package_staging}/THIRD_PARTY_LICENSES"
        COMMAND ${CMAKE_COMMAND} -E copy "${CLAP_SDK_ROOT}/LICENSE"
            "${process_package_staging}/THIRD_PARTY_LICENSES/clap.txt"
        COMMAND ${CMAKE_COMMAND} -E copy "${CLAP_HELPERS_ROOT}/LICENSE"
            "${process_package_staging}/THIRD_PARTY_LICENSES/clap-helpers.txt"
        COMMAND ${CMAKE_COMMAND} -E copy "${CLAP_WRAPPER_ROOT}/LICENSE"
            "${process_package_staging}/THIRD_PARTY_LICENSES/clap-wrapper.txt"
        COMMAND ${CMAKE_COMMAND} -E chdir "${process_package_staging}"
            ${CMAKE_COMMAND} -E env SOURCE_DATE_EPOCH=0
            ${CMAKE_COMMAND}
            "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/artifacts/process-lifecycle.wclap.tar.gz"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PackageWclap.cmake"
        DEPENDS process-lifecycle_wclap
            "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
            "${CLAP_SDK_ROOT}/LICENSE"
            "${CLAP_HELPERS_ROOT}/LICENSE"
            "${CLAP_WRAPPER_ROOT}/LICENSE"
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PackageWclap.cmake"
        VERBATIM)
endif()
