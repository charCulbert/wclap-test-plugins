# add_test_plugin(NAME <name> [RESOURCES <dir>])
#
# Builds plugins/<name>/ as a CLAP (native) or WCLAP (Wasm toolchain) plug-in.
# Plugin.cpp becomes <name>-impl, which carries the plug-in ID, version and
# project URL as compile definitions so the descriptor matches the bundle.
# Under a Wasm toolchain it also packages artifacts/<name>.wclap.tar.gz with
# module.wasm, the resources and the licences at the archive root.
# Any tests/*.cpp file becomes a native ctest linked against <name>-impl.
#
# write_test_plugin_list() writes plugins.json (version, and each plug-in's
# name and ID) to the build directory for the release manifest.

set(TEST_PLUGIN_ID_PREFIX "com.charlieculbert.wclap-test-plugins"
    CACHE STRING "Prefix for plug-in IDs; each ID is <prefix>.<name>")
set(TEST_PLUGIN_URL "https://github.com/charCulbert/wclap-test-plugins"
    CACHE STRING "URL written to each descriptor's url, manual_url and support_url")

set(_test_plugin_package_script "${CMAKE_CURRENT_LIST_DIR}/PackageWclap.cmake")

function(add_test_plugin)
    cmake_parse_arguments(ARG "" "NAME;RESOURCES" "" ${ARGN})
    if(NOT ARG_NAME)
        message(FATAL_ERROR "add_test_plugin: NAME is required")
    endif()

    set(name "${ARG_NAME}")
    set(dir "${PROJECT_SOURCE_DIR}/plugins/${name}")
    set(id "${TEST_PLUGIN_ID_PREFIX}.${name}")

    set_property(GLOBAL APPEND PROPERTY TEST_PLUGIN_NAMES "${name}")

    add_library(${name}-impl STATIC "${dir}/Plugin.cpp")
    # Linked into a shared .clap on native platforms.
    set_target_properties(${name}-impl PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_include_directories(${name}-impl PUBLIC "${dir}")
    target_link_libraries(${name}-impl PUBLIC clap clap-helpers)
    target_compile_definitions(${name}-impl PUBLIC
        "TEST_PLUGIN_ID=\"${id}\""
        "TEST_PLUGIN_VERSION=\"${PROJECT_VERSION}\""
        "TEST_PLUGIN_URL=\"${TEST_PLUGIN_URL}\"")

    set(resource_args)
    if(ARG_RESOURCES)
        get_filename_component(resources "${ARG_RESOURCES}" ABSOLUTE BASE_DIR "${dir}")
        set(resource_args RESOURCE_DIRECTORY "${resources}")
    endif()

    if(ANY_WASM_TOOLCHAIN)
        set(formats WCLAP)
    else()
        set(formats CLAP)
    endif()

    make_clapfirst_plugins(
        TARGET_NAME ${name}
        IMPL_TARGET ${name}-impl
        OUTPUT_NAME ${name}
        ENTRY_SOURCE "${dir}/Entry.cpp"
        ${resource_args}
        BUNDLE_IDENTIFIER "${id}"
        BUNDLE_VERSION "${PROJECT_VERSION}"
        PLUGIN_FORMATS ${formats}
        ASSET_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/artifacts"
        COPY_AFTER_BUILD FALSE)

    if(ANY_WASM_TOOLCHAIN)
        set(resource_files)
        if(ARG_RESOURCES)
            file(GLOB_RECURSE resource_files CONFIGURE_DEPENDS "${resources}/*")
        endif()
        set(archive "${PROJECT_BINARY_DIR}/artifacts/${name}.wclap.tar.gz")
        add_custom_command(
            OUTPUT "${archive}"
            COMMAND ${CMAKE_COMMAND}
                "-DMODULE=$<TARGET_FILE:${name}_wclap>"
                "-DRESOURCES=${resources}"
                "-DSTAGING=${PROJECT_BINARY_DIR}/${name}-package"
                "-DOUTPUT=${archive}"
                "-DSTRIP=${TEST_PLUGIN_STRIP}"
                "-DLICENSE=${PROJECT_SOURCE_DIR}/LICENSE"
                "-DTHIRD_PARTY_LICENSES=${TEST_PLUGIN_THIRD_PARTY_LICENSES_DIR}"
                -P "${_test_plugin_package_script}"
            DEPENDS ${name}_wclap ${resource_files} "${PROJECT_SOURCE_DIR}/LICENSE"
                ${TEST_PLUGIN_THIRD_PARTY_LICENSE_FILES}
                "${_test_plugin_package_script}"
                "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/ArchiveDirectory.cmake"
            VERBATIM)
        add_custom_target(${name}-package ALL DEPENDS "${archive}")
    endif()

    if(BUILD_TESTING AND NOT ANY_WASM_TOOLCHAIN)
        file(GLOB tests CONFIGURE_DEPENDS "${dir}/tests/*.cpp")
        foreach(test_source IN LISTS tests)
            get_filename_component(test "${test_source}" NAME_WE)
            set(test_target ${name}-${test})
            add_executable(${test_target} "${test_source}" "${dir}/Entry.cpp")
            target_link_libraries(${test_target} PRIVATE ${name}-impl)
            add_test(NAME ${test_target} COMMAND ${test_target})
        endforeach()
    endif()
endfunction()

function(write_test_plugin_list)
    get_property(names GLOBAL PROPERTY TEST_PLUGIN_NAMES)
    set(entries)
    foreach(name IN LISTS names)
        list(APPEND entries
            "    {\"name\": \"${name}\", \"id\": \"${TEST_PLUGIN_ID_PREFIX}.${name}\"}")
    endforeach()
    list(JOIN entries ",\n" entries)
    file(WRITE "${PROJECT_BINARY_DIR}/plugins.json"
        "{\n  \"version\": \"${PROJECT_VERSION}\",\n  \"plugins\": [\n${entries}\n  ]\n}\n")
endfunction()
