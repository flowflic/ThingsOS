cmake_minimum_required(VERSION 3.22)
set(bsp "${CMAKE_CURRENT_LIST_DIR}/../../../references/read-pico-official/components")
set(EXTRA_COMPONENT_DIRS "${bsp}/read_pico" "${bsp}/epdiy" "${bsp}/e0470_epaper_waveform"
 "${bsp}/cst836u" "${bsp}/fca9555" "${bsp}/sy7636a" "${bsp}/read_pico_pmu" "${bsp}/sc7a20h" "${bsp}/pwm_audio")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
if(NOT DEFINED THINGSOS_BUILD_VERSION)
 set(THINGSOS_BUILD_VERSION "0.4.2")
endif()
set(PROJECT_VER "${THINGSOS_BUILD_VERSION}")
project(ThingsOS_native VERSION 0.4.2)
include("../scan_adapter.cmake")
include("../development_gate.cmake")
# Compile the ESP-IDF 6.1 async-select fix without editing the shared SDK checkout.
idf_component_get_property(thingsos_tls esp-tls COMPONENT_LIB)
idf_component_get_property(thingsos_tls_dir esp-tls COMPONENT_DIR)
idf_build_get_property(thingsos_tls_python PYTHON)
set(thingsos_tls_patch "${CMAKE_CURRENT_LIST_DIR}/../patch_esp_tls.py")
set(thingsos_tls_source "${thingsos_tls_dir}/esp_tls.c")
set(thingsos_tls_output "${CMAKE_BINARY_DIR}/thingsos-esp-tls/esp_tls.c")
execute_process(COMMAND "${thingsos_tls_python}" "${thingsos_tls_patch}"
 "${thingsos_tls_source}" "${thingsos_tls_output}" COMMAND_ERROR_IS_FATAL ANY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${thingsos_tls_patch}" "${thingsos_tls_source}")
get_target_property(thingsos_tls_sources ${thingsos_tls} SOURCES)
list(FILTER thingsos_tls_sources EXCLUDE REGEX "(^|/)esp_tls\\.c$")
set_property(TARGET ${thingsos_tls} PROPERTY SOURCES "${thingsos_tls_sources}")
target_sources(${thingsos_tls} PRIVATE "${thingsos_tls_output}")
# IDF otherwise chooses factory because it is the first application partition.
# Keep even generated development arguments from pointing the normal app at rescue.
foreach(target flash app-flash)
 foreach(property FLASH_FILE FLASH_ENTRY IMAGES)
  get_property(entries TARGET ${target} PROPERTY ${property})
  string(REPLACE "0x20000" "0x220000" entries "${entries}")
  set_property(TARGET ${target} PROPERTY ${property} "${entries}")
 endforeach()
endforeach()
# USB default install must boot the reader rather than the factory rescue.
# Add the otherwise missing recovery/font parts for a blank device. This
# service target preserves NVS; flash-usb.ps1 is the backed-up full reset path.
idf_build_get_property(thingsos_usb_python PYTHON)
execute_process(COMMAND ${thingsos_usb_python} "${CMAKE_CURRENT_LIST_DIR}/../package_usb.py"
 --ota-selector "${CMAKE_BINARY_DIR}/ota_data_usb.bin" COMMAND_ERROR_IS_FATAL ANY)
foreach(property FLASH_FILE FLASH_ENTRY IMAGES)
 get_property(entries TARGET flash PROPERTY ${property})
 string(REPLACE "ota_data_initial.bin" "ota_data_usb.bin" entries "${entries}")
 set_property(TARGET flash PROPERTY ${property} "${entries}")
endforeach()
esptool_py_flash_target_image(flash rescue "0x20000" "${CMAKE_CURRENT_LIST_DIR}/../../build/native-rescue/ThingsOS_rescue.bin")
esptool_py_flash_target_image(flash system-font "0xd00000" "${CMAKE_BINARY_DIR}/system_font.bin")
