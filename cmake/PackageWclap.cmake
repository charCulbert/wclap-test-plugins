# Stages and archives one WCLAP. Run with cmake -P and these definitions:
#   MODULE                the built module.wasm
#   RESOURCES             resource directory copied to the archive root (optional)
#   STAGING               scratch directory, recreated on every run
#   OUTPUT                the .wclap.tar.gz to write
#   STRIP                 llvm-strip, used to drop debug sections (optional)
#   LICENSE               the project's licence, shipped as LICENSE.txt
#   THIRD_PARTY_LICENSES  directory shipped as THIRD_PARTY_LICENSES/

foreach(var MODULE STAGING OUTPUT LICENSE THIRD_PARTY_LICENSES)
    if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
        message(FATAL_ERROR "PackageWclap: ${var} is required")
    endif()
endforeach()

file(REMOVE_RECURSE "${STAGING}")
file(MAKE_DIRECTORY "${STAGING}")

if(RESOURCES)
    file(COPY "${RESOURCES}/" DESTINATION "${STAGING}")
endif()

if(STRIP)
    execute_process(
        COMMAND "${STRIP}" --strip-debug -o "${STAGING}/module.wasm" "${MODULE}"
        COMMAND_ERROR_IS_FATAL ANY)
else()
    file(COPY_FILE "${MODULE}" "${STAGING}/module.wasm")
endif()

file(COPY_FILE "${LICENSE}" "${STAGING}/LICENSE.txt")
file(COPY "${THIRD_PARTY_LICENSES}/" DESTINATION "${STAGING}/THIRD_PARTY_LICENSES")

get_filename_component(output_dir "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${output_dir}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env SOURCE_DATE_EPOCH=0
        "${CMAKE_COMMAND}" "-DOUTPUT=${OUTPUT}"
        -P "${CMAKE_CURRENT_LIST_DIR}/ArchiveDirectory.cmake"
    WORKING_DIRECTORY "${STAGING}"
    COMMAND_ERROR_IS_FATAL ANY)
