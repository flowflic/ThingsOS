// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "thingsos/cubox_protocol.h"

typedef enum {
    THINGSOS_CUBOX_CONFIG_IDLE = 0,
    THINGSOS_CUBOX_CONFIG_VERIFYING,
    THINGSOS_CUBOX_CONFIG_FAILED,
    THINGSOS_CUBOX_CONFIG_READY
} thingsos_cubox_config_phase_t;

typedef enum {
    THINGSOS_CUBOX_OP_NONE = 0,
    THINGSOS_CUBOX_OP_FOLDERS,
    THINGSOS_CUBOX_OP_LIST,
    THINGSOS_CUBOX_OP_MORE,
    THINGSOS_CUBOX_OP_DETAIL,
    THINGSOS_CUBOX_OP_DOWNLOAD,
    THINGSOS_CUBOX_OP_UPDATE,
    THINGSOS_CUBOX_OP_ARCHIVE
} thingsos_cubox_op_t;

typedef struct {
    bool configured;
    bool busy;
    bool pairing;
    bool offline_mode;
    bool has_more;
    thingsos_cubox_config_phase_t config_phase;
    thingsos_cubox_op_t op;
    thingsos_cubox_filter_t filter;
    unsigned serial;
    unsigned connection_serial;
    unsigned folder_count;
    unsigned card_count;
    unsigned page;
    unsigned slot; /* list slot: 0 = recent tab, 1 = folder tab */
    unsigned pair_seconds;
    unsigned dl_done;
    unsigned dl_total;
    char code[9];
    char server[THINGSOS_CUBOX_SERVER_LEN];
    char folder_id[THINGSOS_CUBOX_ID_LEN];
    char folder_name[THINGSOS_CUBOX_NAME_LEN];
    char query[97];
    char message[THINGSOS_CUBOX_MESSAGE_LEN];
    char active_card_id[THINGSOS_CUBOX_ID_LEN];
} thingsos_cubox_status_t;

#ifndef THINGSOS_HOST_UI
void thingsos_cubox_init(void);
/* Optional UI hook, called on the UI task with the shared UI/SD mutex held when
   a job is refused the network claim; returns true once the claim was freed. */
void thingsos_cubox_status(thingsos_cubox_status_t* out);
bool thingsos_cubox_active(void);
bool thingsos_cubox_pairing(void);
bool thingsos_cubox_pair(bool enabled);
void thingsos_cubox_pair_poll(void); /* call outside the shared UI/SD mutex */
bool thingsos_cubox_disconnect(void);
bool thingsos_cubox_quiesce_locked(void);
bool thingsos_cubox_folder(unsigned index, thingsos_cubox_folder_t* out);
bool thingsos_cubox_card(unsigned index, thingsos_cubox_card_t* out);
bool thingsos_cubox_detail_copy(const char* card_id, thingsos_cubox_detail_t* out);
bool thingsos_cubox_article_path(const char* card_id, char* out, size_t cap);
bool thingsos_cubox_set_filter(
    thingsos_cubox_filter_t filter,
    const char* folder_id,
    const char* folder_name,
    const char* query,
    bool fetch_online);
bool thingsos_cubox_select_slot(unsigned slot);
bool thingsos_cubox_refresh_folders(void);
bool thingsos_cubox_refresh_list(bool more);
bool thingsos_cubox_fetch_detail(const char* card_id);
bool thingsos_cubox_download(const char* card_id);
bool thingsos_cubox_mark(const char* card_id, int set_read, int set_starred);
bool thingsos_cubox_archive(const char* card_id);
bool thingsos_cubox_delete_cache(const char* card_id);
void thingsos_cubox_load_offline(void);
void thingsos_cubox_cancel(void);
#endif
