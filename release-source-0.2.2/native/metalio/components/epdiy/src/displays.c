#include <stddef.h>

#include "epd_display.h"

// E0470A01 / 684×1216 40pin。总线按本板 16bit，不是原厂 demo 板的 8bit/20MHz。
// 波形在应用侧 epd_hl_init(&E0470_WAVEFORM)，这里不挂社区 LUT。
const EpdDisplay_t E0470_DISPLAY = {
    .width = 1216,
    .height = 684,
    .bus_width = 16,
    .bus_speed = 24,
    .default_waveform = NULL,
};

// Metalio E-Ink4-Plus V1.5：E0470A14-AF 34Pin 面板，8 位总线 20MHz，与厂商固件一致。
// 供货方示例波形与 Read Pico E0470 表逐字节相同，出厂 LUT 提供前仍属临时表。
// / Metalio E-Ink4-Plus V1.5: E0470A14-AF 34-pin panel on an 8-bit bus at 20 MHz,
// as in the vendor firmware. The supplier's sample waveforms are byte-identical
// to the Read Pico E0470 tables and stay provisional until a factory LUT exists.
const EpdDisplay_t E0470_DISPLAY_8BIT = {
    .width = 1216,
    .height = 684,
    .bus_width = 8,
    .bus_speed = 20,
    .default_waveform = NULL,
};
