# Archives every entry in the working directory into OUTPUT with fixed mtimes,
# so the same inputs give the same .tar.gz.

if(NOT DEFINED OUTPUT)
    message(FATAL_ERROR "OUTPUT is required")
endif()

file(GLOB package_paths
    RELATIVE "${CMAKE_CURRENT_BINARY_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}/*")

file(ARCHIVE_CREATE
    OUTPUT "${OUTPUT}"
    PATHS ${package_paths}
    FORMAT gnutar
    COMPRESSION GZip
    COMPRESSION_LEVEL 9
    MTIME "1970-01-01 00:00:00 UTC")
