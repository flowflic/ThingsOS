cmake_minimum_required(VERSION 3.22)
# Metalio E-Ink4-Plus (ESP32-S31). Local epdiy fork and Metalio BSP; the panel
# waveform and accelerometer drivers come unchanged from the Read Pico reference.
set(metalio "${CMAKE_CURRENT_LIST_DIR}/../metalio/components")
set(bsp "${CMAKE_CURRENT_LIST_DIR}/../../references/read-pico-official/components")
set(EXTRA_COMPONENT_DIRS "${metalio}/epdiy" "${metalio}/metalio_bsp"
 "${bsp}/e0470_epaper_waveform" "${bsp}/sc7a20h"
 "${CMAKE_CURRENT_LIST_DIR}/../vendor/dl_fft")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/../VERSION" THINGSOS_BASE_VERSION LIMIT_COUNT 1)
if(NOT DEFINED THINGSOS_BUILD_VERSION)
 set(THINGSOS_BUILD_VERSION "${THINGSOS_BASE_VERSION}")
endif()
set(PROJECT_VER "${THINGSOS_BUILD_VERSION}")
project(ThingsOS_native VERSION ${THINGSOS_BASE_VERSION})
include("../scan_adapter.cmake")
include("../waveform_workspace.cmake")
include("../development_gate.cmake")
include("../reader_tls_poll.cmake")
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
