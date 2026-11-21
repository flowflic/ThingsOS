/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdlib.h>
#include <stddef.h>
void local_epub_free(void*);
extern size_t local_epub_budget,local_epub_live,local_epub_peak,local_epub_denied;
#define ESP_PLATFORM
#define free local_epub_free
