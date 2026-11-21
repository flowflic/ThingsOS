# Compile the pinned official LCD renderer with a local per-row LUT extension.
# Never edit the shared reference checkout.
idf_component_get_property(thingsos_epd_library epdiy COMPONENT_LIB)
get_target_property(thingsos_epd_sources ${thingsos_epd_library} SOURCES)
list(FILTER thingsos_epd_sources EXCLUDE REGEX "output_lcd/render_lcd\\.c$")
set_property(TARGET ${thingsos_epd_library} PROPERTY SOURCES "${thingsos_epd_sources}")
target_sources(${thingsos_epd_library} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/firmware/components/thingsos_board/scan_adapter.c")
