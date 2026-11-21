# Portable decoder-only sources from the pinned libwebp distribution.
set(webp_root "${CMAKE_CURRENT_LIST_DIR}/vendor/libwebp")
set(webp_sources
  dec/alpha_dec.c dec/buffer_dec.c dec/frame_dec.c dec/idec_dec.c dec/io_dec.c
  dec/quant_dec.c dec/tree_dec.c dec/vp8_dec.c dec/vp8l_dec.c dec/webp_dec.c
  dsp/alpha_processing.c dsp/cpu.c dsp/dec.c dsp/dec_clip_tables.c
  dsp/filters.c dsp/lossless.c dsp/rescaler.c dsp/upsampling.c dsp/yuv.c
  utils/bit_reader_utils.c utils/color_cache_utils.c utils/filters_utils.c
  utils/huffman_utils.c utils/palette.c utils/quant_levels_dec_utils.c
  utils/random_utils.c utils/rescaler_utils.c utils/thread_utils.c)
list(TRANSFORM webp_sources PREPEND "${webp_root}/src/")
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(webp_alpha "${CMAKE_CURRENT_BINARY_DIR}/thingsos-webp-alpha/vp8l_dec.c")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/patch_webp_alpha.py"
 "${webp_root}/src/dec/vp8l_dec.c" "${webp_alpha}" COMMAND_ERROR_IS_FATAL ANY)
list(REMOVE_ITEM webp_sources "${webp_root}/src/dec/vp8l_dec.c")
list(REMOVE_ITEM webp_sources "${webp_root}/src/dec/alpha_dec.c")
list(APPEND webp_sources "${webp_alpha}" "${CMAKE_CURRENT_BINARY_DIR}/thingsos-webp-alpha/alpha_dec.c")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
 "${CMAKE_CURRENT_LIST_DIR}/patch_webp_alpha.py" "${CMAKE_CURRENT_LIST_DIR}/src/webp_alpha_history.inc"
 "${webp_root}/src/dec/vp8l_dec.c")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
 "${webp_root}/src/dec/alpha_dec.c" "${webp_root}/src/dec/alphai_dec.h" "${webp_root}/src/dec/vp8li_dec.h")
set(webp_config "${CMAKE_CURRENT_BINARY_DIR}/thingsos-webp-config")
file(MAKE_DIRECTORY "${webp_config}/src/webp")
file(CONFIGURE OUTPUT "${webp_config}/src/webp/config.h" CONTENT "/* Portable single-threaded decoder. */\n" @ONLY)
add_library(thingsos_webp_decoder STATIC ${webp_sources}
  "${CMAKE_CURRENT_LIST_DIR}/src/webp_memory.c" "${CMAKE_CURRENT_LIST_DIR}/src/webp_utils_adapter.c")
target_include_directories(thingsos_webp_decoder PUBLIC "${webp_root}" PRIVATE "${webp_config}")
target_include_directories(thingsos_webp_decoder BEFORE PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/thingsos-webp-alpha")
target_compile_definitions(thingsos_webp_decoder PRIVATE HAVE_CONFIG_H WEBP_MAX_ALLOCABLE_MEMORY=8388608)
