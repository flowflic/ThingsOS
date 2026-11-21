cmake_minimum_required(VERSION 3.22)
set(bsp "${CMAKE_CURRENT_LIST_DIR}/../../../references/read-pico-official/components")
set(EXTRA_COMPONENT_DIRS "../firmware/components/thingsos_board" "../firmware/components/thingsos_core"
 "../firmware/components/thingsos_libs" "../firmware/components/thingsos_net"
 "${bsp}/read_pico" "${bsp}/epdiy" "${bsp}/e0470_epaper_waveform" "${bsp}/cst836u"
 "${bsp}/fca9555" "${bsp}/sy7636a" "${bsp}/read_pico_pmu" "${bsp}/sc7a20h" "${bsp}/pwm_audio")
set(SDKCONFIG_DEFAULTS "../firmware/sdkconfig.defaults;sdkconfig.defaults")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
set(PROJECT_VER "0.4.2")
project(ThingsOS_rescue VERSION 0.4.2)
include("../scan_adapter.cmake")
include("../development_gate.cmake")
