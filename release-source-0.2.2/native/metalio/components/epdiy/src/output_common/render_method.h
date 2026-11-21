#pragma once

#include "sdkconfig.h"

// 本 fork 只保留 LCD/CAM 外设输出路径：ESP32-S3（Read Pico）与 ESP32-S31（Metalio）。
// / LCD/CAM output path only: ESP32-S3 (Read Pico) and ESP32-S31 (Metalio).
#if !defined(CONFIG_IDF_TARGET_ESP32S3) && !defined(CONFIG_IDF_TARGET_ESP32S31)
#error "this epdiy fork only supports ESP32-S3 / ESP32-S31 (LCD render path)"
#endif
#define RENDER_METHOD_LCD 1

// S31 的 8 位总线经 IOMUX 直连 D0..D7，没有 S3 那样靠 GPIO 矩阵交换位对；
// 面板沿 X 先取字节高 2 位，所以每个输出字节内 4 个像素的顺序要在软件里反转。
// / S31 drives D0..D7 straight through IOMUX, without the S3 bit-pair swap in
// the GPIO matrix. The panel shifts the high 2 bits first, so the four pixels
// inside each output byte are reversed in software.
#if defined(CONFIG_IDF_TARGET_ESP32S31)
#define EPD_OUTPUT_REVERSE_PIXELS 1
#else
#define EPD_OUTPUT_REVERSE_PIXELS 0
#endif

#ifdef __clang__
#define IRAM_ATTR
// define this if we're using clangd to make it accept the GCC builtin
void __assert_func(const char* file, int line, const char* func, const char* failedexpr);
#endif
