/* SPDX-License-Identifier: Apache-2.0 */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"SD DMA:%d: %s\n",__LINE__,#x);exit(1);}}while(0)
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NO_MEM 0x101
#define MALLOC_CAP_DMA 1
#define MALLOC_CAP_INTERNAL 2
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
typedef struct {void* dma_aligned_buffer;size_t unaligned_multi_block_rw_max_chunk_size;unsigned max_freq_khz,flags;} sdmmc_host_t;
static size_t largest_dma;
static unsigned allocations;
static uint8_t storage[8192+64];
static void* heap_caps_aligned_alloc(size_t alignment,size_t bytes,unsigned caps){
    CHECK(alignment==64 && caps==(MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL));
    CHECK(bytes>=512 && bytes<=8192 && bytes%512==0);++allocations;
    return bytes<=largest_dma?(void*)(((uintptr_t)storage+63)&~(uintptr_t)63):NULL;
}
#include "../metalio/components/metalio_bsp/metalio_sd_dma.inc"
int main(void){
    for(size_t capacity=8192;capacity>=512;capacity/=2){
        sd_dma_buffer=NULL;sd_dma_bytes=0;allocations=0;largest_dma=capacity;
        sdmmc_host_t mounted={.max_freq_khz=40000,.flags=7};
        CHECK(sd_dma_prepare(&mounted)==ESP_OK);
        CHECK(sd_dma_bytes==capacity && mounted.unaligned_multi_block_rw_max_chunk_size==capacity/512);
        CHECK(mounted.dma_aligned_buffer && !((uintptr_t)mounted.dma_aligned_buffer&63));
        CHECK(mounted.max_freq_khz==40000 && mounted.flags==7);
        /* Refill after I2S consumes the remaining DMA heap, remount, and USB
           raw ownership all reuse the already reserved buffer without malloc. */
        unsigned before=allocations;largest_dma=0;
        for(unsigned i=0;i<32;++i){sdmmc_host_t next={0};CHECK(sd_dma_prepare(&next)==ESP_OK);
            CHECK(next.dma_aligned_buffer==mounted.dma_aligned_buffer);
            CHECK(next.unaligned_multi_block_rw_max_chunk_size==capacity/512);}
        CHECK(allocations==before);
    }
    sd_dma_buffer=NULL;sd_dma_bytes=0;allocations=0;largest_dma=511;
    sdmmc_host_t failed={.max_freq_khz=40000,.flags=7};
    CHECK(sd_dma_prepare(&failed)==ESP_ERR_NO_MEM && allocations==5);
    CHECK(!failed.dma_aligned_buffer && !failed.unaligned_multi_block_rw_max_chunk_size);
    CHECK(failed.max_freq_khz==40000 && failed.flags==7);
    puts("PASS: SD DMA reserved before playback; bounded fallback; zero later allocations across heap starvation/remount/raw ownership; mount fails safely below one sector");
    return 0;
}
