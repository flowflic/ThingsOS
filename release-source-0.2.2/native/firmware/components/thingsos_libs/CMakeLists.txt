set(vendor "${CMAKE_CURRENT_LIST_DIR}/../../../vendor")
set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
idf_component_register(SRCS "${vendor}/cjson/cJSON.c" "${vendor}/zlib/adler32.c" "${vendor}/zlib/crc32.c" "${vendor}/zlib/inflate.c"
 "${vendor}/zlib/inftrees.c" "${vendor}/zlib/inffast.c" "${vendor}/zlib/zutil.c"
 INCLUDE_DIRS "${vendor}/zlib" "${vendor}/stb" "${vendor}/cjson")
add_subdirectory("${vendor}/freetype" "${CMAKE_CURRENT_BINARY_DIR}/freetype")
include("${CMAKE_CURRENT_LIST_DIR}/../../../freetype_pool.cmake")
thingsos_freetype_pool()
# Verify the gray workspace is no longer in the target call frame.
target_compile_options(freetype PRIVATE -fstack-usage)
include("${CMAKE_CURRENT_LIST_DIR}/../../../webp_decoder.cmake")
target_link_libraries(${COMPONENT_LIB} PUBLIC freetype thingsos_webp_decoder)
