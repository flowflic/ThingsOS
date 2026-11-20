/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stddef.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_INTERNAL 4
void* heap_caps_malloc(size_t bytes,unsigned caps);
void heap_caps_free(void* block);
