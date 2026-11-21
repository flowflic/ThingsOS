set(sr "${CMAKE_CURRENT_LIST_DIR}/../../../vendor/esp-sr")
idf_component_register(SRCS "audio.c" "record_denoise.c" "../../../src/audio_format.c" "../../../src/audio_denoise.c"
 INCLUDE_DIRS "../../../include" PRIV_INCLUDE_DIRS "../../../vendor/minimp3"
 "${sr}/include/esp32s31" "${sr}/src/include" "../../../vendor/cjson"
 REQUIRES thingsos_net metalio_bsp esp_timer esp_psram
 PRIV_REQUIRES dl_fft esp_partition
 LDFRAGMENTS "memory.lf")

if(NOT IDF_TARGET STREQUAL "esp32s31")
 message(FATAL_ERROR "The pinned recording NSNet3 archive is for ESP32-S31 only")
endif()
add_prebuilt_library(thingsos_nsnet3 "${sr}/lib/esp32s31/libnsnet3.a"
 PRIV_REQUIRES ${COMPONENT_NAME} dl_fft)
target_link_libraries(${COMPONENT_LIB} PRIVATE thingsos_nsnet3)
target_add_binary_data(${COMPONENT_LIB} "${sr}/model/nsnet_model/nsnet3/nsnet3_data" BINARY)
target_add_binary_data(${COMPONENT_LIB} "${sr}/model/nsnet_model/nsnet3/nsnet3_index" BINARY)
target_compile_definitions(${COMPONENT_LIB} PRIVATE MINIMP3_ONLY_MP3 MINIMP3_NO_SIMD)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-misleading-indentation)
# The project builds at -Og. MP3 decode and resampling must beat real time on
# CPU1 while the panel scan preempts it; -Og measured ~33 ms per 26 ms frame.
# Only these hot sources are optimized; behavior and the rest of the image are unchanged.
set_source_files_properties("audio.c" "../../../src/audio_format.c" PROPERTIES COMPILE_OPTIONS "-O2")
