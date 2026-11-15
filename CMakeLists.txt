cmake_minimum_required(VERSION 3.24)

project(wclap-test-plugins VERSION 0.3.3 LANGUAGES C CXX)

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
    # WASI SDK's prebuilt libc, libc++ and compiler-rt carry DWARF, which is
    # most of module.wasm. Archives ship a stripped copy; the build tree keeps
    # the original for debugging.
    get_filename_component(wasi_sdk_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    find_program(TEST_PLUGIN_STRIP llvm-strip HINTS "${wasi_sdk_bin}" REQUIRED)

    # Licences for the code linked into module.wasm, shipped in every archive.
    # licenses/ holds the toolchain's (wasi-libc, libc++), which WASI SDK
    # does not install.
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
    file(GLOB toolchain_licenses "${CMAKE_CURRENT_SOURCE_DIR}/licenses/*.txt")
    foreach(license IN LISTS toolchain_licenses)
        get_filename_component(license_name "${license}" NAME)
        configure_file("${license}"
            "${TEST_PLUGIN_THIRD_PARTY_LICENSES_DIR}/${license_name}" COPYONLY)
        list(APPEND TEST_PLUGIN_THIRD_PARTY_LICENSE_FILES "${license}")
    endforeach()
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

write_test_plugin_list()
