# Generate a memory-only patch; never modify the reference checkout.
set(wave_pool_root "${CMAKE_CURRENT_LIST_DIR}")
find_package(Python3 COMPONENTS Interpreter REQUIRED)
idf_component_get_property(wave_pool_library e0470_epaper_waveform COMPONENT_LIB)
idf_component_get_property(wave_pool_source e0470_epaper_waveform COMPONENT_DIR)
set(wave_pool_output "${CMAKE_BINARY_DIR}/waveform-workspace")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${wave_pool_root}/patch_waveform_workspace.py"
 "${wave_pool_source}/e0470_waveform_trim.c" "${wave_pool_output}"
 COMMAND_ERROR_IS_FATAL ANY)
get_target_property(wave_pool_sources ${wave_pool_library} SOURCES)
list(FILTER wave_pool_sources EXCLUDE REGEX "e0470_waveform_trim[.]c$")
list(APPEND wave_pool_sources "${wave_pool_output}/e0470_waveform_trim.c")
set_property(TARGET ${wave_pool_library} PROPERTY SOURCES "${wave_pool_sources}")
idf_component_get_property(wave_pool_heap heap COMPONENT_LIB)
idf_component_get_property(wave_pool_psram esp_psram COMPONENT_LIB)
target_link_libraries(${wave_pool_library} PRIVATE ${wave_pool_heap} ${wave_pool_psram})
if(PROJECT_NAME STREQUAL "ThingsOS_rescue")
 target_compile_definitions(${wave_pool_library} PRIVATE THINGSOS_TRIM_RESCUE=1)
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
 "${wave_pool_root}/patch_waveform_workspace.py" "${wave_pool_source}/e0470_waveform_trim.c")
