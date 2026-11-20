/* SPDX-License-Identifier: Apache-2.0
 * Reserve the SD bounce buffer before optional I2S/WiFi DMA allocations.
 * FAT and USB raw-card ownership are exclusive; keep the buffer for the
 * board lifetime so remounts never depend on a fragmented playback heap. */
static void* sd_dma_buffer;
static size_t sd_dma_bytes;
static esp_err_t sd_dma_prepare(sdmmc_host_t* host) {
    if(!sd_dma_buffer) {
        for(size_t bytes=8192;bytes>=512;bytes/=2) {
            sd_dma_buffer=heap_caps_aligned_alloc(64,bytes,MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL);
            if(sd_dma_buffer){sd_dma_bytes=bytes;break;}
        }
        if(!sd_dma_buffer) {
            ESP_LOGE(TAG,"Cannot reserve SD DMA buffer before mount");
            return ESP_ERR_NO_MEM;
        }
        ESP_LOGI(TAG,"SD DMA buffer reserved=%u bytes, max chunk=%u sectors",
                 (unsigned)sd_dma_bytes,(unsigned)(sd_dma_bytes/512));
    }
    host->dma_aligned_buffer=sd_dma_buffer;
    host->unaligned_multi_block_rw_max_chunk_size=sd_dma_bytes/512;
    return ESP_OK;
}
