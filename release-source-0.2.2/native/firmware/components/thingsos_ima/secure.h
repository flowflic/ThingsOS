// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {char client[256],key[1024],kb[160],name[192];} ima_config_t;
void ima_wipe(void*,size_t);
bool ima_config_load(ima_config_t*);
bool ima_config_save(const ima_config_t*);
bool ima_config_clear(void);
bool ima_sha1(const void*,size_t,unsigned char[20]);
bool ima_hmac1(const void*,size_t,const void*,size_t,unsigned char[20]);
