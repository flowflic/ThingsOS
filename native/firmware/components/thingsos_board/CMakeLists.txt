set(tusb "${CMAKE_CURRENT_LIST_DIR}/../../../vendor/tinyusb/src")
idf_component_register(SRCS "board.c" "diagnostics_device.c" "panel_waveform.c" "usb_storage.c"
 "${tusb}/tusb.c" "${tusb}/common/tusb_fifo.c" "${tusb}/device/usbd.c"
 "${tusb}/class/msc/msc_device.c" "${tusb}/portable/synopsys/dwc2/dcd_dwc2.c"
 "${tusb}/portable/synopsys/dwc2/dwc2_common.c"
 INCLUDE_DIRS "." "${tusb}" PRIV_INCLUDE_DIRS "${tusb}/device"
 REQUIRES nvs_flash app_update esp_timer metalio_bsp epdiy e0470_epaper_waveform sc7a20h thingsos_core esp_partition mbedtls bootloader_support esp_wifi esp_hw_support sdmmc esp_driver_sdmmc esp_driver_usb_serial_jtag esp_driver_gpio)
target_compile_definitions(${COMPONENT_LIB} PUBLIC CFG_TUSB_MCU=OPT_MCU_ESP32S31)
set_source_files_properties("${tusb}/device/usbd.c" PROPERTIES COMPILE_FLAGS "-Wno-type-limits")
