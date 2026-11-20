# Keep the pinned upstream tree pristine. Compile only this generated smooth
# module, with a per-raster pool instead of an on-stack pool.
function(thingsos_freetype_pool)
 set(native_pool_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")
 find_package(Python3 COMPONENTS Interpreter REQUIRED)
 set(pool_output "${CMAKE_CURRENT_BINARY_DIR}/freetype-pool")
 execute_process(COMMAND "${Python3_EXECUTABLE}" "${native_pool_root}/patch_freetype_pool.py"
  "${native_pool_root}/vendor/freetype/src/smooth" "${pool_output}"
  COMMAND_ERROR_IS_FATAL ANY)
 get_target_property(ft_sources freetype SOURCES)
 list(REMOVE_ITEM ft_sources src/smooth/smooth.c)
 list(APPEND ft_sources "${pool_output}/smooth.c")
 set_property(TARGET freetype PROPERTY SOURCES "${ft_sources}")
 if(ESP_PLATFORM)
  idf_component_get_property(pool_heap heap COMPONENT_LIB)
  idf_component_get_property(pool_log log COMPONENT_LIB)
  target_link_libraries(freetype PRIVATE ${pool_heap} ${pool_log})
 endif()
 set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${native_pool_root}/patch_freetype_pool.py" "${native_pool_root}/vendor/freetype/src/smooth/ftgrays.c")
endfunction()
