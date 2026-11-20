// SPDX-License-Identifier: Apache-2.0
/* Independent adapter for a pinned, unmodified Espressif NSNet3 archive.
 * One local_audio worker owns this state. Immutable weights are embedded in
 * the main application, so OTA/rescue/font partition formats stay unchanged. */
#include "record_denoise.h"
#include "thingsos/audio_denoise.h"
#include "esp_nsn_iface.h"
#include "model_path.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"

/* Select only this model's exported interface. The general selector also pulls
 * NSNet2/4, speech recognition and unrelated inference kernels into the link. */
extern const esp_nsn_iface_t esp_nsnet3;
extern const uint8_t nsnet3_data_start[] asm("_binary_nsnet3_data_start");
extern const uint8_t nsnet3_data_end[] asm("_binary_nsnet3_data_end");
extern const uint8_t nsnet3_index_start[] asm("_binary_nsnet3_index_start");
extern const uint8_t nsnet3_index_end[] asm("_binary_nsnet3_index_end");

/* Same in-memory model catalogue ABI as ESP-SR model_path.h. The library copies
 * tensors into PSRAM; these flash bytes and catalogue entries are never freed. */
static char* model_names[] = {"nsnet3"};
static char* model_info[] = {"nsnet3_v1_ns_0_0.0_0.0"};
static char* model_files[] = {"nsnet3_index", "nsnet3_data"};
static char* model_bytes[] = {(char*)nsnet3_index_start, (char*)nsnet3_data_start};
static int model_sizes[] = {5940, 102800};
static srmodel_data_t model_data = {.num=2, .files=model_files, .data=model_bytes, .sizes=model_sizes};
static srmodel_data_t* model_entries[] = {&model_data};
static srmodel_list_t models = {.model_name=model_names, .model_info=model_info, .num=1, .model_data=model_entries};
srmodel_list_t* get_static_srmodels(void) { return &models; }

static esp_nsn_data_t* instance;
static thingsos_audio_denoise_t stream;
static uint64_t calls, total_us;
static int64_t max_us;

static bool process(void* context, int16_t* in, int16_t* out) {
    int64_t start = esp_timer_get_time();
    int result = esp_nsnet3.process(context, in, out);
    int64_t elapsed = esp_timer_get_time() - start;
    ++calls;
    total_us += elapsed;
    if (elapsed > max_us) max_us = elapsed;
    if (result != 0) ESP_LOGE("ThingsOS_audio", "NSNet3 inference failed: %d", result);
    return result == 0;
}

bool thingsos_record_denoise_open(void) {
    thingsos_record_denoise_close();
    if (nsnet3_index_end - nsnet3_index_start != model_sizes[0] ||
        nsnet3_data_end - nsnet3_data_start != model_sizes[1]) {
        ESP_LOGE("ThingsOS_audio", "Embedded NSNet3 model size mismatch");
        return false;
    }
    instance = esp_nsnet3.create(model_names[0]);
    if (!instance) return false;
    if (esp_nsnet3.get_samp_rate(instance) != 16000 ||
        esp_nsnet3.get_samp_chunksize(instance) != THINGSOS_DENOISE_HOP) {
        thingsos_record_denoise_close();
        return false;
    }
    calls = total_us = 0;
    max_us = 0;
    thingsos_audio_denoise_init(&stream, process, instance);
    ESP_LOGI("ThingsOS_audio", "Recording NSNet3 on: mono16 16000Hz hop=256 weights=embedded PSRAM_free=%u",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    return true;
}

bool thingsos_record_denoise_feed(const int16_t* in, size_t n, int16_t* out,
                                 size_t capacity, size_t* produced) {
    return instance && thingsos_audio_denoise_feed(&stream, in, n, out, capacity, produced);
}

bool thingsos_record_denoise_finish(int16_t* out, size_t capacity, size_t* produced) {
    if (produced) *produced = 0;
    return !instance || thingsos_audio_denoise_finish(&stream, out, capacity, produced);
}

void thingsos_record_denoise_close(void) {
    if (!instance) return;
    ESP_LOGI("ThingsOS_audio", "NSNet3 segment in=%llu out=%llu frames=%llu mean_us=%llu max_us=%lld failed=%u",
             (unsigned long long)stream.accepted, (unsigned long long)stream.emitted,
             (unsigned long long)calls, (unsigned long long)(calls ? total_us / calls : 0),
             (long long)max_us, (unsigned)stream.failed);
    esp_nsnet3.destroy(instance);
    instance = NULL;
}
