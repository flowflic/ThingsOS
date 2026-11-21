find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(stb_gray_root "${CMAKE_CURRENT_BINARY_DIR}/thingsos-stb")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/patch_stb_grayscale.py"
 "${CMAKE_CURRENT_LIST_DIR}/vendor/stb/stb_image.h" "${stb_gray_root}/thingsos_stb_image.h" COMMAND_ERROR_IS_FATAL ANY)
add_library(thingsos_stb_grayscale INTERFACE)
target_include_directories(thingsos_stb_grayscale INTERFACE "${stb_gray_root}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
 "${CMAKE_CURRENT_LIST_DIR}/patch_stb_grayscale.py" "${CMAKE_CURRENT_LIST_DIR}/vendor/stb/stb_image.h")
