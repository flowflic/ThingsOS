// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "thingsos/flomo_protocol.h"

typedef enum {
    THINGSOS_FLOMO_CONFIG_IDLE = 0,
    THINGSOS_FLOMO_CONFIG_VERIFYING,
    THINGSOS_FLOMO_CONFIG_FAILED,
    THINGSOS_FLOMO_CONFIG_READY
} thingsos_flomo_config_phase_t;

typedef enum {
    THINGSOS_FLOMO_MODE_RECENT = 0,
    THINGSOS_FLOMO_MODE_REVIEW = 1,
    THINGSOS_FLOMO_MODE_TAG = 2
} thingsos_flomo_mode_t;

typedef enum {
    THINGSOS_FLOMO_OP_NONE = 0,
    THINGSOS_FLOMO_OP_VERIFY,
    THINGSOS_FLOMO_OP_LIST,
    THINGSOS_FLOMO_OP_MORE,
    THINGSOS_FLOMO_OP_DETAIL,
    THINGSOS_FLOMO_OP_TAGS
} thingsos_flomo_op_t;

typedef struct {
    bool configured;
    bool busy;
    bool pairing;
    bool offline_mode;
    bool has_more;
    thingsos_flomo_config_phase_t config_phase;
    thingsos_flomo_op_t op;
    thingsos_flomo_mode_t mode;
    unsigned serial;
    unsigned connection_serial;
    unsigned memo_count;
    unsigned tag_count;
    unsigned related_count;
    unsigned pair_seconds;
    char code[9];
    char tag[THINGSOS_FLOMO_TAG_LEN];
    char query[97];
    char message[THINGSOS_FLOMO_MESSAGE_LEN];
    char active_id[THINGSOS_FLOMO_ID_LEN];
    char related_for[THINGSOS_FLOMO_ID_LEN];
} thingsos_flomo_status_t;

#ifndef THINGSOS_HOST_UI
void thingsos_flomo_init(void);
/* Optional UI hook, called on the UI task with the shared UI/SD mutex held when
   a job is refused the network claim; returns true once the claim was freed. */
void thingsos_flomo_set_network_yield(bool (*yield_locked)(void));
void thingsos_flomo_status(thingsos_flomo_status_t* out);
bool thingsos_flomo_active(void);
bool thingsos_flomo_pairing(void);
bool thingsos_flomo_pair(bool enabled);
void thingsos_flomo_pair_poll(void); /* call outside the shared UI/SD mutex */
bool thingsos_flomo_disconnect(void);
bool thingsos_flomo_quiesce_locked(void);
bool thingsos_flomo_memo(unsigned index, thingsos_flomo_memo_t* out);
bool thingsos_flomo_tag(unsigned index, thingsos_flomo_tag_t* out);
bool thingsos_flomo_related(unsigned index, thingsos_flomo_memo_t* out);
/* Full text when fetched (or cached on SD), else the list preview. */
bool thingsos_flomo_detail_copy(
    const char* memo_id, thingsos_flomo_memo_t* memo, char* content, size_t cap, size_t* len_out,
    bool* complete_out);
bool thingsos_flomo_set_filter(
    thingsos_flomo_mode_t mode, const char* tag, const char* query, bool fetch_online);
bool thingsos_flomo_refresh_list(bool more);
bool thingsos_flomo_refresh_tags(void);
bool thingsos_flomo_fetch_detail(const char* memo_id);
void thingsos_flomo_load_offline(void);
void thingsos_flomo_cancel(void);
#endif
