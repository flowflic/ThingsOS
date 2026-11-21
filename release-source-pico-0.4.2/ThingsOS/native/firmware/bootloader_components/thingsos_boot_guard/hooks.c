// SPDX-License-Identifier: Apache-2.0
#include <string.h>
#include "bootloader_common.h"
#include "bootloader_utility.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "thingsos/guard.h"
void bootloader_hooks_include(void) {}
void bootloader_after_init(void) {
    rtc_retain_mem_t* memory = bootloader_common_get_rtc_retain_mem();
    thingsos_boot_guard_t guard;
    memcpy(&guard, memory->custom, sizeof(guard));
    bool rescue = thingsos_guard_begin(&guard, esp_rom_get_reset_reason(0) == 1);
    memcpy(memory->custom, &guard, sizeof(guard));
    if (rescue) {
        bootloader_state_t state = { 0 };
        if (bootloader_utility_load_partition_table(&state) && state.factory.offset) {
            ESP_LOGW(
                "ThingsOS", "Three unconfirmed starts; verify and boot independent factory rescue");
            bootloader_utility_load_boot_image(&state, FACTORY_INDEX);
        }
    }
}
