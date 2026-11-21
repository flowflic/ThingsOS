// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define THINGSOS_MANAGE_BATCH 64
typedef struct {
 unsigned job;
 uint64_t card_id;
 size_t count;
 char paths[THINGSOS_MANAGE_BATCH][512], new_name[97];
} thingsos_manage_job_t;
typedef struct {
 unsigned serial, percent;
 bool active, failed, ota;
 char name[192], message[192];
 /* Monotonic server time after a transfer ends, including failure/cancel. */
 uint64_t finished_ms;
} thingsos_transfer_state_t;
bool thingsos_network_job(thingsos_manage_job_t*);
void thingsos_network_job_done(unsigned job,unsigned done,unsigned failed);
void thingsos_network_transfer(thingsos_transfer_state_t*);
const char* thingsos_network_failure(void);

void thingsos_network_maintenance(void);
const char* thingsos_network_saved_name(void);

bool thingsos_network_recover_files(void);

bool thingsos_network_pause_requested(void);
void thingsos_network_pause_complete(bool);
void thingsos_network_card(uint64_t);
