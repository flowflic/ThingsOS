// SPDX-License-Identifier: Apache-2.0
/* Persistent flomo MCP token in NVS with a SHA-256 integrity digest. */
#include "secure.h"
#include "network.h"
#include "nvs.h"
#include "psa/crypto.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>

#define FLOMO_NVS_MAGIC 0x464C4F31u
#define FLOMO_NVS_NAMESPACE "thingsos-flomo"
#define FLOMO_NVS_KEY "connection"

typedef struct {
    uint32_t magic;
    flomo_config_t config;
    uint8_t digest[32];
} flomo_record_t;

void flomo_wipe(void* p, size_t n) {
    volatile uint8_t* b = p;
    while (n--) *b++ = 0;
}

static bool sha256_hash(const void* p, size_t n, uint8_t out[32]) {
    size_t wrote = 0;
    return psa_hash_compute(PSA_ALG_SHA_256, p, n, out, 32, &wrote) == PSA_SUCCESS && wrote == 32;
}

static bool config_valid(const flomo_config_t* c) {
    char check[THINGSOS_FLOMO_TOKEN_LEN];
    bool ok = c && memchr(c->token, 0, sizeof(c->token)) &&
              thingsos_flomo_parse_token(c->token, check, sizeof(check)) && !strcmp(check, c->token);
    flomo_wipe(check, sizeof(check));
    return ok;
}

static bool io_lock_take(void) {
    bool take = thingsos_io_lock && xSemaphoreGetMutexHolder(thingsos_io_lock) != xTaskGetCurrentTaskHandle();
    if (take) xSemaphoreTake(thingsos_io_lock, portMAX_DELAY);
    return take;
}

bool flomo_config_load(flomo_config_t* out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    flomo_record_t* record = calloc(1, sizeof(*record));
    if (!record) return false;
    nvs_handle_t h;
    bool ok = nvs_open(FLOMO_NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK;
    if (ok) {
        size_t n = sizeof(*record);
        ok = nvs_get_blob(h, FLOMO_NVS_KEY, record, &n) == ESP_OK && n == sizeof(*record);
        nvs_close(h);
    }
    uint8_t digest[32] = {0};
    ok = ok && record->magic == FLOMO_NVS_MAGIC && config_valid(&record->config) &&
         sha256_hash(&record->config, sizeof(record->config), digest) &&
         memcmp(digest, record->digest, sizeof(digest)) == 0;
    if (ok) *out = record->config;
    flomo_wipe(digest, sizeof(digest));
    flomo_wipe(record, sizeof(*record));
    free(record);
    return ok;
}

bool flomo_config_save(const flomo_config_t* config) {
    if (!config_valid(config)) return false;
    flomo_record_t* record = calloc(1, sizeof(*record));
    if (!record) return false;
    record->magic = FLOMO_NVS_MAGIC;
    record->config = *config;
    bool ok = sha256_hash(&record->config, sizeof(record->config), record->digest);
    bool took = io_lock_take();
    nvs_handle_t h;
    if (ok) {
        ok = nvs_open(FLOMO_NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK;
        if (ok) {
            ok = nvs_set_blob(h, FLOMO_NVS_KEY, record, sizeof(*record)) == ESP_OK && nvs_commit(h) == ESP_OK;
            nvs_close(h);
        }
    }
    if (took) xSemaphoreGive(thingsos_io_lock);
    flomo_wipe(record, sizeof(*record));
    free(record);
    return ok;
}

bool flomo_config_clear(void) {
    bool took = io_lock_take();
    nvs_handle_t h;
    bool ok = nvs_open(FLOMO_NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK;
    if (ok) {
        ok = nvs_erase_all(h) == ESP_OK && nvs_commit(h) == ESP_OK;
        nvs_close(h);
    }
    if (took) xSemaphoreGive(thingsos_io_lock);
    return ok;
}
