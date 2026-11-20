/* SPDX-License-Identifier: Apache-2.0
 * Main-task-owned diagnostics. No display, storage writes, tuning or worker.
 */
#include "system_monitor_device.h"
#include "board.h"
#include "esp_private/esp_clk.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_ipc.h"
#include "esp_flash.h"
#include "esp_ota_ops.h"
#include "esp_image_format.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include <string.h>
#if !CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS || !CONFIG_FREERTOS_RUN_TIME_COUNTER_TYPE_U64 || !CONFIG_FREERTOS_RUN_TIME_STATS_USING_ESP_TIMER
#error "System monitor requires 64-bit ESP-timer FreeRTOS runtime counters"
#endif
#if CONFIG_FREERTOS_NUMBER_OF_CORES != 2 || CONFIG_FREERTOS_SMP
#error "Revalidate the per-core idle runtime adapter for this kernel configuration"
#endif
typedef struct {uint64_t stamp,idle;} cpu_read_t;
static thingsos_system_cpu_window_t cpu_window;
static thingsos_system_memory_t flash_info;
static bool flash_read;
static void cpu_local(void* argument) {
    cpu_read_t* sample=argument;
    /* Read each core from a running non-idle task. IPC wakes the other core
       so an uninterrupted idle interval has been committed by its scheduler.
       Reading a remote idle TCB directly can otherwise report stale runtime. */
    sample->stamp=(uint64_t)esp_timer_get_time();
    sample->idle=ulTaskGetIdleRunTimeCounterForCore(xPortGetCoreID());
}
static thingsos_system_memory_t heap_info(unsigned caps) {
    multi_heap_info_t info;heap_caps_get_info(&info,caps);
    uint64_t free=info.total_free_bytes,total=heap_caps_get_total_size(caps);
    return (thingsos_system_memory_t){.total=total,.free=free,.largest=info.largest_free_block,
        .valid=total&&free<=total&&info.largest_free_block<=free};
}
void thingsos_system_reset(void) {
    memset(&cpu_window,0,sizeof(cpu_window));flash_read=false;
}
void thingsos_system_read(thingsos_system_snapshot_t* out) {
    if(!out)return;
    memset(out,0,sizeof(*out));
    int hz=esp_clk_cpu_freq();out->cpu_mhz=hz>0?(unsigned)hz/1000000:0;
    out->cpu_max_mhz=CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
    if(out->cpu_mhz>out->cpu_max_mhz)out->cpu_max_mhz=out->cpu_mhz;
    out->sram=heap_info(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    out->psram=heap_info(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    thingsos_board_display_stats(&out->display);
    if(!flash_read) {
        uint32_t chip=0;const esp_partition_t* running=esp_ota_get_running_partition();
        esp_image_metadata_t metadata;
        memset(&flash_info,0,sizeof(flash_info));
        if(running&&esp_flash_get_size(NULL,&chip)==ESP_OK) {
            esp_partition_pos_t position={.offset=running->address,.size=running->size};
            if(esp_image_get_metadata(&position,&metadata)==ESP_OK)
                thingsos_system_flash_layout(chip,running->size,metadata.image_len,&flash_info);
        }
        flash_read=flash_info.valid;
    }
    out->flash=flash_info;
    cpu_read_t cpu[2]={{0}};unsigned local=(unsigned)xPortGetCoreID(),remote=local^1;
    cpu_local(&cpu[local]);
    if(esp_ipc_call_blocking(remote,cpu_local,&cpu[remote])!=ESP_OK) {
        out->cpu_error=true;memset(&cpu_window,0,sizeof(cpu_window));return;
    }
    uint64_t stamp[2]={cpu[0].stamp,cpu[1].stamp},idle[2]={cpu[0].idle,cpu[1].idle};
    out->cpu_valid=thingsos_system_cpu_step(&cpu_window,stamp,idle,&out->cpu_used_bp);
}
