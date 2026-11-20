/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
/* Host-only stand-in for the ESP log macros. `panel_waveform.c` is compiled
   into the stagger test and only logs when the trimmed turn table is missing;
   keep the ESP logging headers outside the host target. */
#define ESP_LOGE(tag, fmt, ...) ((void)0)
#define ESP_LOGW(tag, fmt, ...) ((void)0)
#define ESP_LOGI(tag, fmt, ...) ((void)0)
#define ESP_LOGD(tag, fmt, ...) ((void)0)
#define ESP_LOGV(tag, fmt, ...) ((void)0)
