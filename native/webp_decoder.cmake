# Portable decoder-only sources from the pinned libwebp distribution.
set(webp_root "${CMAKE_CURRENT_LIST_DIR}/vendor/libwebp")
set(webp_sources
  dec/alpha_dec.c dec/buffer_dec.c dec/frame_dec.c dec/idec_dec.c dec/io_dec.c
  dec/quant_dec.c dec/tree_dec.c dec/vp8_dec.c dec/vp8l_dec.c dec/webp_dec.c
  dsp/alpha_processing.c dsp/cpu.c dsp/dec.c dsp/dec_clip_tables.c
  dsp/filters.c dsp/lossless.c dsp/rescaler.c dsp/upsampling.c dsp/yuv.c
  utils/bit_reader_utils.c utils/color_cache_utils.c utils/filters_utils.c
  utils/huffman_utils.c utils/palette.c utils/quant_levels_dec_utils.c
  utils/random_utils.c utils/rescaler_utils.c utils/thread_utils.c utils/utils.c)
list(TRANSFORM webp_sources PREPEND "${webp_root}/src/")
set(webp_config "${CMAKE_CURRENT_BINARY_DIR}/thingsos-webp-config")
file(MAKE_DIRECTORY "${webp_config}/src/webp")
file(WRITE "${webp_config}/src/webp/config.h" "/* Portable single-threaded decoder. */\n")
add_library(thingsos_webp_decoder STATIC ${webp_sources})
target_include_directories(thingsos_webp_decoder PUBLIC "${webp_root}" PRIVATE "${webp_config}")
target_compile_definitions(thingsos_webp_decoder PRIVATE HAVE_CONFIG_H WEBP_MAX_ALLOCABLE_MEMORY=8388608)
if(ESP_PLATFORM)
  set_property(SOURCE "${webp_root}/src/utils/utils.c" APPEND PROPERTY
    COMPILE_DEFINITIONS malloc=thingsos_webp_malloc calloc=thingsos_webp_calloc)
endif()
