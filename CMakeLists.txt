cmake_minimum_required(VERSION 3.24)

project(wclap-test-plugins VERSION 0.1.0 LANGUAGES C CXX)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "^wasm")
    set(ANY_WASM_TOOLCHAIN TRUE)
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

if(ANY_WASM_TOOLCHAIN)
    # Licences for the code linked into module.wasm, shipped in every archive.
    set(TEST_PLUGIN_THIRD_PARTY_LICENSES_DIR "${CMAKE_CURRENT_BINARY_DIR}/third-party-licenses")
    set(TEST_PLUGIN_THIRD_PARTY_LICENSE_FILES
        "${CLAP_SDK_ROOT}/LICENSE"
        "${CLAP_HELPERS_ROOT}/LICENSE"
        "${CLAP_WRAPPER_ROOT}/LICENSE")
    configure_file("${CLAP_SDK_ROOT}/LICENSE"
        "${TEST_PLUGIN_THIRD_PARTY_LICENSES_DIR}/clap.txt" COPYONLY)
    configure_file("${CLAP_HELPERS_ROOT}/LICENSE"
        "${TEST_PLUGIN_THIRD_PARTY_LICENSES_DIR}/clap-helpers.txt" COPYONLY)
    configure_file("${CLAP_WRAPPER_ROOT}/LICENSE"
        "${TEST_PLUGIN_THIRD_PARTY_LICENSES_DIR}/clap-wrapper.txt" COPYONLY)
endif()

include(cmake/AddTestPlugin.cmake)

# Each plugins/<name>/ holding a Plugin.cpp is one plug-in. Its resources/
# directory, if present, is bundled next to the module.
file(GLOB plugin_dirs LIST_DIRECTORIES true "${CMAKE_CURRENT_SOURCE_DIR}/plugins/*")
foreach(plugin_dir IN LISTS plugin_dirs)
    if(NOT EXISTS "${plugin_dir}/Plugin.cpp")
        continue()
    endif()
    get_filename_component(plugin_name "${plugin_dir}" NAME)
    if(IS_DIRECTORY "${plugin_dir}/resources")
        add_test_plugin(NAME ${plugin_name} RESOURCES resources)
    else()
        add_test_plugin(NAME ${plugin_name})
    endif()
endforeach()
