// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {
    void *context;
    int (*read)(void *, uint8_t *, size_t);
    bool (*header)(void *, const uint8_t *, size_t);
    bool (*hash_begin)(void *);
    bool (*hash_update)(void *, const uint8_t *, size_t);
    bool (*hash_finish)(void *, uint8_t[32]);
    void (*hash_abort)(void *);
    bool (*begin)(void *, size_t);
    bool (*write)(void *, const uint8_t *, size_t);
    /* end consumes the handle on success and failure, matching ESP-IDF. */
    bool (*end)(void *);
    void (*abort)(void *);
    bool (*activate)(void *);
} thingsos_update_ops_t;
typedef struct {
    size_t bytes, header_bytes, capacity;
    uint32_t running_address, target_address;
    bool target_is_ota;
    uint8_t expected_sha256[32];
} thingsos_update_request_t;
bool thingsos_update_run(const thingsos_update_request_t *, const thingsos_update_ops_t *);
