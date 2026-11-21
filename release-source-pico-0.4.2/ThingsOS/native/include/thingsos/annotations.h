/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define THINGSOS_ANNOTATION_LIMIT 128
#define THINGSOS_ANNOTATION_QUOTE 1024
#define THINGSOS_ANNOTATION_THOUGHT 1024
typedef enum {
    THINGSOS_NOTE_LOCAL, THINGSOS_NOTE_PENDING, THINGSOS_NOTE_SENDING,
    THINGSOS_NOTE_SYNCED, THINGSOS_NOTE_FAILED, THINGSOS_NOTE_UNCERTAIN
} thingsos_note_sync_t;
typedef struct {
    uint32_t id, chapter, begin, end, text_crc, source_begin, source_end;
    uint32_t updated, sync, error;
    char uid[64], bookmark_id[96], review_id[96];
    char quote[THINGSOS_ANNOTATION_QUOTE+1],thought[THINGSOS_ANNOTATION_THOUGHT+1];
    uint32_t deleted; /* Cloud-bound underline removal, retained until verified. */
} thingsos_annotation_t;
typedef struct {
    uint64_t book;
    char owner[33],root[512];
    uint32_t count,next_id;
    bool recovered;
    thingsos_annotation_t notes[THINGSOS_ANNOTATION_LIMIT];
} thingsos_annotations_t;
/* Missing is an empty store; malformed snapshots fail closed. Owner is empty
   for local books and an account fingerprint for cloud-bound cached books. */
bool thingsos_annotations_load(thingsos_annotations_t*,const char*,uint64_t,const char*);
bool thingsos_annotations_put(thingsos_annotations_t*,thingsos_annotation_t*);
bool thingsos_annotations_remove(thingsos_annotations_t*,uint32_t);
uint32_t thingsos_annotation_crc(const void*,size_t);
