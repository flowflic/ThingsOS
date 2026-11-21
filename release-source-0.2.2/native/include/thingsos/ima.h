// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "thingsos/ima_protocol.h"
typedef struct {char id[160],name[192];} thingsos_ima_library_t;
typedef enum {THINGSOS_IMA_CONFIG_IDLE,THINGSOS_IMA_CONFIG_VERIFYING,THINGSOS_IMA_CONFIG_FAILED,THINGSOS_IMA_CONFIG_READY} thingsos_ima_config_phase_t;
typedef struct {bool configured,busy,pairing,more;thingsos_ima_config_phase_t config_phase;unsigned serial,connection_serial,count,pair_seconds;char code[9],kb_name[192],message[192];thingsos_ima_job_t job;} thingsos_ima_status_t;
#ifndef THINGSOS_HOST_UI
void thingsos_ima_init(void);
void thingsos_ima_status(thingsos_ima_status_t*);
bool thingsos_ima_library(unsigned,thingsos_ima_library_t*);
bool thingsos_ima_active(void);
bool thingsos_ima_pairing(void);
bool thingsos_ima_pair(bool);
void thingsos_ima_pair_poll(void); /* call outside the shared UI/SD mutex */
bool thingsos_ima_refresh(bool next);
bool thingsos_ima_choose(unsigned,bool persist);
bool thingsos_ima_upload(const char*,bool keep_both);
bool thingsos_ima_check(void);
void thingsos_ima_cancel(void);
bool thingsos_ima_disconnect(void);
bool thingsos_ima_quiesce_locked(void);
bool thingsos_ima_file_status(const char*,char*,size_t);
#endif
