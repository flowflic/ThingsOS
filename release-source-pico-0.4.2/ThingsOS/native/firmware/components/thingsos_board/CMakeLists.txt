set(tusb "${CMAKE_CURRENT_LIST_DIR}/../../../vendor/tinyusb/src")
idf_component_register(SRCS "board.c" "usb_storage.c" "diagnostics_device.c"
 "${tusb}/tusb.c" "${tusb}/common/tusb_fifo.c" "${tusb}/device/usbd.c"
 "${tusb}/class/msc/msc_device.c" "${tusb}/portable/synopsys/dwc2/dcd_dwc2.c"
 "${tusb}/portable/synopsys/dwc2/dwc2_common.c"
 INCLUDE_DIRS "." "${tusb}" PRIV_INCLUDE_DIRS "${tusb}/device"
 REQUIRES read_pico thingsos_core esp_partition mbedtls bootloader_support esp_wifi esp_hw_support sdmmc esp_driver_sdmmc esp_driver_usb_serial_jtag PRIV_REQUIRES app_update nvs_flash esp_timer)
target_compile_definitions(${COMPONENT_LIB} PUBLIC CFG_TUSB_MCU=OPT_MCU_ESP32S3)
set_source_files_properties("${tusb}/device/usbd.c" PROPERTIES COMPILE_FLAGS "-Wno-type-limits")
