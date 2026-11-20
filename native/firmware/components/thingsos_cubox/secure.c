// SPDX-License-Identifier: Apache-2.0
/* Persistent Cubox server + token credentials in NVS with SHA-256 integrity digest. */
#include "secure.h"
#include "network.h"
#include "nvs.h"
#include "psa/crypto.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>

void cubox_wipe(void* p, size_t n) {
    volatile uint8_t* b = p;
    while (n--) *b++ = 0;
}

static bool sha256_hash(const void* p, size_t n, uint8_t out[32]) {
    size_t wrote = 0;
    return psa_hash_compute(PSA_ALG_SHA_256, p, n, out, 32, &wrote) == PSA_SUCCESS && wrote == 32;
}

typedef struct {
    uint32_t magic;
    cubox_config_t config;
    uint8_t digest[32];
} cubox_record_t;

#define CUBOX_NVS_MAGIC 0x43425831u
#define CUBOX_NVS_NAMESPACE "thingsos-cubox"
#define CUBOX_NVS_KEY "connection"

static bool config_valid(const cubox_config_t* c) {
    return c && thingsos_cubox_server_valid(c->server) && c->token[0] &&
           memchr(c->server, 0, sizeof(c->server)) && memchr(c->token, 0, sizeof(c->token));
}

bool cubox_config_load(cubox_config_t* out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    cubox_record_t* record = calloc(1, sizeof(*record));
    if (!record) return false;
    nvs_handle_t h;
    bool ok = nvs_open(CUBOX_NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK;
    if (ok) {
        size_t n = sizeof(*record);
        ok = nvs_get_blob(h, CUBOX_NVS_KEY, record, &n) == ESP_OK && n == sizeof(*record);
        nvs_close(h);
    }
    uint8_t digest[32] = {0};
    ok = ok && record->magic == CUBOX_NVS_MAGIC && config_valid(&record->config) &&
         sha256_hash(&record->config, sizeof(record->config), digest) &&
         memcmp(digest, record->digest, sizeof(digest)) == 0;
    if (ok) *out = record->config;
    cubox_wipe(digest, sizeof(digest));
    cubox_wipe(record, sizeof(*record));
    free(record);
    return ok;
}

bool cubox_config_save(const cubox_config_t* config) {
    if (!config_valid(config)) return false;
    cubox_record_t* record = calloc(1, sizeof(*record));
    if (!record) return false;
    record->magic = CUBOX_NVS_MAGIC;
    record->config = *config;
    bool ok = sha256_hash(&record->config, sizeof(record->config), record->digest);
    bool take_lock = thingsos_io_lock &&
                     xSemaphoreGetMutexHolder(thingsos_io_lock) != xTaskGetCurrentTaskHandle();
    if (take_lock) xSemaphoreTake(thingsos_io_lock, portMAX_DELAY);
    nvs_handle_t h;
    if (ok) {
        ok = nvs_open(CUBOX_NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK;
        if (ok) {
            ok = nvs_set_blob(h, CUBOX_NVS_KEY, record, sizeof(*record)) == ESP_OK &&
                 nvs_commit(h) == ESP_OK;
            nvs_close(h);
        }
    }
    if (take_lock) xSemaphoreGive(thingsos_io_lock);
    cubox_wipe(record, sizeof(*record));
    free(record);
    return ok;
}

bool cubox_config_clear(void) {
    bool take_lock = thingsos_io_lock &&
                     xSemaphoreGetMutexHolder(thingsos_io_lock) != xTaskGetCurrentTaskHandle();
    if (take_lock) xSemaphoreTake(thingsos_io_lock, portMAX_DELAY);
    nvs_handle_t h;
    bool ok = nvs_open(CUBOX_NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK;
    if (ok) {
        ok = nvs_erase_all(h) == ESP_OK && nvs_commit(h) == ESP_OK;
        nvs_close(h);
    }
    if (take_lock) xSemaphoreGive(thingsos_io_lock);
    return ok;
}
