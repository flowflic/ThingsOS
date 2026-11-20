idf_component_register(SRCS "../../../src/diagnostics.c" "../../../src/image_thumbnail.c" "../../../src/jpeg_decoder.c" "../../../src/tilt.c" "../../../src/service.c" "../../../src/download.c" "../../../src/release.c" "../../../src/guard.c" "../../../src/runtime.c" "../../../src/reader.c" "../../../src/ui.c"
 "../../../src/archive.c" "../../../src/epub.c" "../../../src/preview.c" "../../../src/graphics.c" "../../../src/reader_ui.c" "../../../src/image_decode.c" "../../../src/webp_decoder.c" "../../../src/update.c"
 "../../../src/alarm.c" "../../../src/system_monitor.c" "../../../src/annotations.c" "../../../src/input.c" "../../../src/ime.c" "../../../src/manage.c" "../../../src/store.c" "../../../src/files.c" "../../../src/pagination.c"
 "../../../src/usb_disk.c" "../../../src/display_buffer.c" "../../../src/thumbnail_cache.c" "../../../src/cover_cache.c" "../../../src/refresh_mask.c"
 "../../../src/weidu.c" "../../../src/weidu_protocol.c" "../../../src/weidu_pack.c" "../../../src/weidu_reading.c" "../../../src/weidu_readtime.c" "../../../src/weidu_readtime_protocol.c" "../../../src/weidu_mp.c" "../../../src/markdown.c" "../../../src/cubox_protocol.c"
 INCLUDE_DIRS "../../../include" REQUIRES thingsos_libs)

# Static memory evidence for the new reading protocol and its bounded JSON scanner.
set_source_files_properties("${CMAKE_CURRENT_LIST_DIR}/../../../src/weidu_reading.c" "${CMAKE_CURRENT_LIST_DIR}/../../../src/weidu.c" "${CMAKE_CURRENT_LIST_DIR}/../../../src/markdown.c" PROPERTIES COMPILE_OPTIONS "-fstack-usage")
