// SPDX-License-Identifier: Apache-2.0
/* Exercise the production ESP allocator with a fragmented 1,736,692-byte
   largest block and Pico's historical 1,860,680-byte remaining PSRAM sample.
   This is an allocation simulation, not current device acceptance. */
#include <stdlib.h>
#include <stdint.h>
#include "image_platform/esp_heap_caps.h"
size_t image_test_limit=1736692,image_test_rejected,image_test_max_request;
void* heap_caps_malloc(size_t n,unsigned caps) {
    (void)caps;if(n>image_test_max_request)image_test_max_request=n;
    if(n>image_test_limit){++image_test_rejected;return NULL;}return malloc(n);
}
void* heap_caps_realloc(void* p,size_t n,unsigned caps) {
    (void)caps;if(n>image_test_max_request)image_test_max_request=n;
    if(n>image_test_limit){++image_test_rejected;return NULL;}return realloc(p,n);
}
size_t heap_caps_get_free_size(unsigned caps){(void)caps;return 1860680;}
size_t heap_caps_get_largest_free_block(unsigned caps){(void)caps;return image_test_limit;}
#define ESP_PLATFORM
#include "../src/image_decode.c"
