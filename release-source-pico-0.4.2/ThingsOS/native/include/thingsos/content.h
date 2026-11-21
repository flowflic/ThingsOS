// SPDX-License-Identifier: Apache-2.0
#ifndef THINGSOS_CONTENT_H
#define THINGSOS_CONTENT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#define THINGSOS_ZIP_ENTRIES 10240
#define THINGSOS_MEMBER_BYTES (2u * 1024u * 1024u)
#define THINGSOS_XML_BYTES (512u * 1024u)
/* Large package/navigation XML has its own bound. Chapter text stays bounded
   by THINGSOS_XML_BYTES. Resource and title storage follows actual counts. */
#define THINGSOS_PACKAGE_BYTES THINGSOS_MEMBER_BYTES
#define THINGSOS_SPINE_LIMIT 10240
#define THINGSOS_IMAGE_LIMIT 64
#define THINGSOS_PATH_BYTES 256
#define THINGSOS_TXT_BYTES (64u * 1024u * 1024u)
#define THINGSOS_TXT_SEGMENT (128u * 1024u)
typedef struct {
    char name[THINGSOS_PATH_BYTES];
    uint32_t crc, compressed, bytes, offset;
    uint16_t method, flags;
    uint16_t image_width,image_height;
    bool image_probed;
} thingsos_zip_entry_t;
typedef struct {
    FILE* file;
    uint64_t size;
    size_t count;
    thingsos_zip_entry_t* entries;
} thingsos_zip_t;
enum { THINGSOS_EPUB_OTHER, THINGSOS_EPUB_XHTML, THINGSOS_EPUB_CSS, THINGSOS_EPUB_NCX };
enum { THINGSOS_EPUB_NAV=1, THINGSOS_EPUB_COVER=2, THINGSOS_EPUB_OWN_PATH=4 };
typedef struct {
    /* Existing ZIP names are stable for the lifetime of the open book. */
    const char* path;
    uint8_t kind, flags;
} thingsos_epub_item_t;

/* Navigation targets are separate from reading segments: adding a section
   must not renumber saved positions or WeRead source chapter identifiers. */
typedef struct {
    uint32_t chapter, offset;
    uint8_t level;
    char title[160];
} thingsos_toc_entry_t;
typedef struct {
    thingsos_zip_t zip;
    char title[160], author[160], opf[256], cover[256], cover_document[256], toc_document[256];
    bool weidu_cache, weidu_online;
    bool plain_text;
    uint8_t text_encoding;
    uint64_t text_signature;
    char online_dir[512];
    bool navigation_loaded, navigation_nested;
    unsigned titles_next;
    thingsos_epub_item_t* items;
    size_t item_count, spine_count;
    unsigned *spine, *chapter_sources;
    char (*chapter_names)[160];
    char (*chapter_fragments)[160];
    size_t chapter_capacity;
    bool chapter_fragments_deferred;
    uint8_t* chapter_levels; /* Zero-based logical TOC depth. */
    thingsos_toc_entry_t* section_toc;
    size_t section_toc_count;
    unsigned section_toc_next;
    uint32_t section_toc_key;
    bool section_toc_ready, section_toc_build_valid;
} thingsos_epub_t;
typedef struct {
    size_t offset;
    char path[256], alt[160];
    uint16_t width,height;
} thingsos_inline_t;
typedef struct { size_t begin,end; bool mine; } thingsos_text_underline_t;
#define THINGSOS_FOOTNOTE_LIMIT 64
#define THINGSOS_NOTE_BYTES 65536 /* PSRAM, allocated only for chapters with notes. */
/* One clickable footnote marker: the byte range of the marker inside the
   chapter text plus the note body it points at. note_bytes == 0 means the
   reference was found but its target text could not be resolved. */
typedef struct {
    size_t begin, end;
    size_t note_offset, note_bytes;
} thingsos_footnote_t;

typedef struct {
    const thingsos_text_underline_t* underlines;
    size_t underline_count;
    char* text;
    size_t bytes, source_start;
    thingsos_inline_t images[THINGSOS_IMAGE_LIMIT];
    size_t image_count;
    size_t heading_start, heading_end;
    size_t hidden_start, hidden_end;
    bool heading;
    thingsos_footnote_t* footnotes;
    size_t footnote_count;
    char* notes;
    size_t notes_bytes;
    uint8_t cover_image; /* 1-based first-page cover image, zero for ordinary illustrations. */
    char* markdown_index; /* Disposable SD style index; raw text offsets are retained. */
} thingsos_chapter_t;
unsigned thingsos_toc_number_level(const char* title);
bool thingsos_archive_path(const char* base, const char* reference, char out[256]);
bool thingsos_zip_open(thingsos_zip_t* zip, const char* path);
void thingsos_zip_close(thingsos_zip_t* zip);
bool thingsos_zip_read(
    thingsos_zip_t* zip, const char* name, size_t limit, unsigned char** out, size_t* bytes);
bool thingsos_zip_image_info(thingsos_zip_t*,const char*,unsigned*,unsigned*);
bool thingsos_file_image_info(const char*,unsigned*,unsigned*);
bool thingsos_epub_open(thingsos_epub_t* epub, const char* path);
/* Bounded, read-only plain-text provider. Index path is optional. */
bool thingsos_txt_open(thingsos_epub_t*,const char*,const char*);
/* Resolve an EPUB2 guide cover on demand when there is no manifest cover. */
bool thingsos_epub_resolve_cover(thingsos_epub_t* epub);
/* Grow title/fragment storage transactionally, preserving current chapters. */
bool thingsos_epub_reserve_chapters(thingsos_epub_t* epub, size_t count);
void thingsos_epub_navigation(thingsos_epub_t*);
void thingsos_epub_navigation_fast(thingsos_epub_t*);
void thingsos_epub_titles(thingsos_epub_t*);
bool thingsos_epub_titles_step(thingsos_epub_t*);
/* A NULL output counts matching headings without allocating a maximum table. */
size_t thingsos_epub_raw_headings(const unsigned char*,size_t,const thingsos_chapter_t*,
    unsigned,unsigned,thingsos_toc_entry_t*,size_t);
void thingsos_epub_close(thingsos_epub_t* epub);
bool thingsos_epub_chapter(thingsos_epub_t* epub, size_t index, thingsos_chapter_t* chapter);
void thingsos_chapter_close(thingsos_chapter_t* chapter);
#endif
