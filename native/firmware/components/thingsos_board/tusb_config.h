// SPDX-License-Identifier: Apache-2.0
#pragma once
/* Metalio: ESP32-S31 OTG is high-speed only (UTMI PHY), slave FIFO mode. */
#define CFG_TUSB_MCU OPT_MCU_ESP32S31
#define CFG_TUSB_OS OPT_OS_FREERTOS
#define CFG_TUSB_DEBUG 0
#define CFG_TUSB_RHPORT0_MODE (OPT_MODE_DEVICE | OPT_MODE_HIGH_SPEED)
#define CFG_TUD_MAX_SPEED OPT_MODE_HIGH_SPEED
#define CFG_TUD_ENABLED 1
#define CFG_TUD_ENDPOINT0_SIZE 64
#define CFG_TUD_MSC 1
#define CFG_TUD_MSC_EP_BUFSIZE 4096
#define CFG_TUSB_MEM_ALIGN __attribute__((aligned(4)))
