/* SPDX-License-Identifier: Apache-2.0
 * Independent bounded account-scoped note snapshots and progress protocol.
 */
#include "thingsos/weidu_reading.h"
#include "thingsos/weidu_readtime.h"
#include "thingsos/mem.h"
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "zlib.h"
#ifdef _WIN32
#include <direct.h>
#define mkdir_one(p) _mkdir(p)
#else
#define mkdir_one(p) mkdir(p, 0775)
#endif
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_log.h"
static void* wd_alloc(size_t n) {
    if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) < n + 512u * 1024u) return NULL;
    return heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
#else
static size_t host_block_limit = SIZE_MAX;
void thingsos_wd_host_block_limit(size_t n) {
    host_block_limit = n;
}
static void* wd_alloc(size_t n) {
    if (n > host_block_limit) return NULL;
    return calloc(1, n);
}
#endif
static bool safe(const char* s) {
    if (!s || !*s || strlen(s) >= 64) return false;
    for (; *s; ++s)
        if (!isalnum((unsigned char)*s) && *s != '_' && *s != '-') return false;
    return true;
}
static void* wd_resize(void* p, size_t n) {
#ifdef ESP_PLATFORM
    if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) < n + 512u * 1024u) return NULL;
    return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    if (n > host_block_limit) return NULL;
    return realloc(p, n);
#endif
}
static void cp(char* d, size_t n, const char* s) {
    size_t k = strlen(s);
    if (k >= n) {
        k = n - 1;
        while (k && ((unsigned char)s[k] & 192) == 128) --k;
    }
    memcpy(d, s, k);
    d[k] = 0;
}
static bool swap_file(const char* tmp, const char* final) {
    char bak[768];
    snprintf(bak, sizeof(bak), "%s.bak", final);
    FILE* f = fopen(final, "rb");
    bool old = f != NULL;
    if (f) fclose(f);
    if (old) {
        remove(bak);
        if (rename(final, bak)) return false;
    }
    if (rename(tmp, final)) {
        if (old) rename(bak, final);
        return false;
    }
    return true;
    /* retain recovery copy until the next complete transaction */
}
bool thingsos_wd_toc(
    const char* root, const char* id, unsigned index, thingsos_weidu_chapter_t* ch) {
    if (!safe(id) || index >= THINGSOS_SPINE_LIMIT) return false;
    char p[704];
    snprintf(p, sizeof(p), "%s/book-%s/toc.dat", root, id);
    FILE* f = fopen(p, "rb");
    bool ok =
        f && !fseek(f, (long)(index * sizeof(*ch)), SEEK_SET) && fread(ch, sizeof(*ch), 1, f) == 1;
    if (f) fclose(f);
    return ok && memchr(ch->uid, 0, sizeof(ch->uid)) && safe(ch->uid) &&
           memchr(ch->title, 0, sizeof(ch->title));
}
int thingsos_wd_chapter(const char* root, const char* id, const char* uid) {
    thingsos_weidu_chapter_t ch;
    for (unsigned i = 0; i < THINGSOS_SPINE_LIMIT && thingsos_wd_toc(root, id, i, &ch); ++i)
        if (!strcmp(uid, ch.uid)) return (int)i;
    return -1;
}
typedef struct {
    uint32_t magic, version, record_size, slot;
    char owner[33];
    thingsos_wd_notes_info_t info;
    uint32_t crc[3], seal;
} manifest_t;
#define NOTE_MAGIC 0x574e5331u
static void note_path(
    char* p, size_t n, const char* root, const char* id, unsigned slot, unsigned kind) {
    snprintf(p, n, "%s/book-%s/notes-%u-%u.dat", root, id, slot, kind);
}
static bool manifest_read(
    const char* root, const thingsos_weidu_session_t* s, const char* id, manifest_t* m,
    bool verify) {
    if (!safe(id) || !thingsos_weidu_session_valid(s)) return false;
    char p[768], owner[33];
    thingsos_weidu_md5(s->vid, strlen(s->vid), owner);
    for (unsigned recovery = 0; recovery < 2; ++recovery) {
        snprintf(p, sizeof(p), "%s/book-%s/notes.manifest%s", root, id, recovery ? ".bak" : "");
        FILE* f = fopen(p, "rb");
        bool ok = f && fread(m, sizeof(*m), 1, f) == 1 && fgetc(f) == EOF;
        if (f) fclose(f);
        if (ok && m->magic == NOTE_MAGIC && (m->version == 1 || m->version == 2) &&
            m->record_size == sizeof(thingsos_wd_note_t) && m->slot < 2 &&
            m->seal == crc32(0, (const Bytef*)m, (uInt)offsetof(manifest_t, seal)) &&
            !memcmp(owner, m->owner, 33)) {
            bool valid = true;
            for (unsigned k = 0; k < 3; ++k) {
                if (m->info.count[k] > (k == 2 ? WD_REVIEW_LIMIT : WD_NOTE_LIMIT)) {
                    valid = false;
                    break;
                }
                note_path(p, sizeof(p), root, id, m->slot, k);
                f = fopen(p, "rb");
                uint32_t crc = crc32(0, NULL, 0);
                size_t bytes = 0;
                unsigned char block[512];
                if (!f) {
                    valid = false;
                    break;
                }
                if (!verify) {
                    valid =
                        !fseek(f, 0, SEEK_END) &&
                        ftell(f) == (long)((size_t)m->info.count[k] * sizeof(thingsos_wd_note_t));
                    fclose(f);
                    if (!valid) break;
                    continue;
                }
                size_t n;
                while ((n = fread(block, 1, sizeof(block), f))) {
                    crc = crc32(crc, block, (uInt)n);
                    bytes += n;
                }
                valid = !ferror(f) &&
                        bytes == (size_t)m->info.count[k] * sizeof(thingsos_wd_note_t) &&
                        crc == m->crc[k];
                fclose(f);
                if (!valid) break;
            }
            if (valid) return true;
        }
    }
    return false;
}
bool thingsos_wd_notes_info(
    const char* root, const thingsos_weidu_session_t* s, const char* id,
    thingsos_wd_notes_info_t* out) {
    manifest_t m;
    memset(out, 0, sizeof(*out));
    if (!manifest_read(root, s, id, &m, true)) return false;
    *out = m.info;
    return true;
}
bool thingsos_wd_notes_view(const char* root,const thingsos_weidu_session_t* s,const char* id,thingsos_wd_notes_info_t* out) {
    manifest_t m;memset(out,0,sizeof(*out));
    if(!manifest_read(root,s,id,&m,false))return false;
    *out=m.info;return true;
}
static void note_interval(thingsos_wd_note_t* n, uint32_t version) {
    if(version==1 && n->range) {
        if(n->length>n->begin)n->length-=n->begin;
        else n->range=false;
    }
}
bool thingsos_wd_note(
    const char* root, const thingsos_weidu_session_t* s, const char* id, unsigned kind,
    unsigned index, thingsos_wd_note_t* out) {
    if (kind > 2) return false;
    manifest_t m;
    if (!manifest_read(root, s, id, &m, false) || index >= m.info.count[kind]) return false;
    char p[768];
    note_path(p, sizeof(p), root, id, m.slot, kind);
    FILE* f = fopen(p, "rb");
    bool ok = f && !fseek(f, (long)(index * sizeof(*out)), SEEK_SET) &&
              fread(out, sizeof(*out), 1, f) == 1;
    if (f) fclose(f);
    ok = ok &&
           out->integrity ==
               crc32(0, (const Bytef*)out, (uInt)offsetof(thingsos_wd_note_t, integrity)) &&
           memchr(out->text, 0, sizeof(out->text)) && memchr(out->uid, 0, sizeof(out->uid)) &&
           memchr(out->author, 0, sizeof(out->author)) &&
           memchr(out->chapter, 0, sizeof(out->chapter));
    /* v1 stored the wire interval's end as length. Convert after checking
       its original CRC; preserve the old snapshot without rewriting SD. */
    if(ok)note_interval(out,m.version);
    return ok;
}

extern bool thingsos_weidu_json_events(
    const char*, bool (*)(void*, const char*, const char*, char, unsigned), void*);
typedef struct {
    thingsos_wd_note_t* note;
    thingsos_wd_personal_t* personal;
    manifest_t* manifest;
    FILE* out;
    unsigned kind, array_depth, record_depth, keys, container_depth;
    bool array, record, seen, root, more, unknown_root;
    uint32_t next, advertised_total;
    char sync[40];
    thingsos_weidu_error_t error;
} note_parser_t;
static bool decimal(const char* s, uint32_t* out) {
    if (!*s) return false;
    uint64_t v = 0;
    for (; *s; ++s) {
        if (!isdigit((unsigned char)*s) || v > (UINT32_MAX - (unsigned)(*s - '0')) / 10)
            return false;
        v = v * 10 + (unsigned)(*s - '0');
    }
    *out = (uint32_t)v;
    return true;
}

/* Reviews may use htmlContent instead of content. Produce inert bounded text. */
static void review_html(const char* in, char* out, size_t cap) {
    size_t used = 0;
    char skip[8] = { 0 };
    for (size_t i = 0; in[i] && used + 1 < cap;) {
        if (in[i] == '<') {
            size_t at = ++i;
            char quote = 0;
            while (in[i]) {
                char c = in[i++];
                if (quote) {
                    if (c == quote) quote = 0;
                } else if (c == '"' || c == '\'')
                    quote = c;
                else if (c == '>')
                    break;
            }
            const char* p = in + at;
            bool close = *p == '/';
            if (close) ++p;
            char tag[8];
            size_t n = 0;
            while (isalpha((unsigned char)*p) && n + 1 < sizeof(tag))
                tag[n++] = (char)tolower((unsigned char)*p++);
            tag[n] = 0;
            if (skip[0]) {
                if (close && !strcmp(tag, skip)) skip[0] = 0;
                continue;
            }
            if (!close && (!strcmp(tag, "script") || !strcmp(tag, "style"))) {
                strcpy(skip, tag);
                continue;
            }
            if ((!strcmp(tag, "p") || !strcmp(tag, "br") || !strcmp(tag, "div")) && used &&
                out[used - 1] != '\n')
                out[used++] = '\n';
            continue;
        }
        if (skip[0]) {
            ++i;
            continue;
        }
        if (in[i] == '&') {
            const char* semi = strchr(in + i, ';');
            if (semi && (size_t)(semi - in - i) < 16) {
                char entity[16];
                size_t n = (size_t)(semi - in - i - 1);
                memcpy(entity, in + i + 1, n);
                entity[n] = 0;
                uint32_t v = 0;
                if (!strcmp(entity, "amp"))
                    v = '&';
                else if (!strcmp(entity, "lt"))
                    v = '<';
                else if (!strcmp(entity, "gt"))
                    v = '>';
                else if (!strcmp(entity, "quot"))
                    v = '"';
                else if (!strcmp(entity, "apos"))
                    v = '\'';
                else if (!strcmp(entity, "nbsp"))
                    v = ' ';
                else if (entity[0] == '#') {
                    char* end;
                    bool hex = entity[1] == 'x' || entity[1] == 'X';
                    unsigned long ncp = strtoul(entity + 1 + hex, &end, hex ? 16 : 10);
                    if (!*end && ncp <= 0x10ffff) v = (uint32_t)ncp;
                }
                if (v && v <= 0x10ffff && !(v >= 0xd800 && v <= 0xdfff)) {
                    unsigned k = v < 128 ? 1 : v < 2048 ? 2 : v < 65536 ? 3 : 4;
                    if (used + k >= cap) break;
                    for (unsigned j = k - 1; j; j--) {
                        out[used + j] = (char)(128 | (v & 63));
                        v >>= 6;
                    }
                    out[used] = (char)((k == 1 ? 0 : k == 2 ? 192 : k == 3 ? 224 : 240) | v);
                    used += k;
                    i = (size_t)(semi - in) + 1;
                    continue;
                }
            }
        }
        out[used++] = in[i++];
    }
    while (used && (out[used - 1] == '\n' || out[used - 1] == '\r')) --used;
    out[used] = 0;
}
static bool note_event(void* raw, const char* key, const char* text, char type, unsigned depth) {
    note_parser_t* p = raw;
    thingsos_wd_note_t* n = p->note;
    if (depth == 0 && type == '{') p->root = true;
    /* The live popular endpoint wraps its payload in bestBookMarks.
       Keep the original flat shape for older responses; ignore sibling
       chapter/user metadata without constructing an object tree. */
    if (p->kind == 0 && depth == 1 && !strcmp(key, "bestBookMarks")) {
        if (type == '{') {
            if (p->seen || p->container_depth != 1) return false;
            p->container_depth = 2;
        } else if (type != '}') return false;
    }
    if (depth == 1 && *key) {
        ++p->keys;
        if (strcmp(key, "errcode") && strcmp(key, "errCode") &&
            strcmp(
                key, p->kind == 0   ? "items"
                     : p->kind == 1 ? "updated"
                                    : "reviews"))
            p->unknown_root = true;
    }
    if (!strcmp(key, "errcode") || !strcmp(key, "errCode")) {
        if (strcmp(text, "0")) {
            p->error = (!strcmp(text, "-2012") || !strcmp(text, "-2010"))
                ? (p->kind == 1 ? WD_ACCESS : WD_AUTH) : WD_PROTOCOL;
            return false;
        }
    }
    const char* list = p->kind == 0 ? "items" : p->kind == 1 ? "updated" : "reviews";
    if (depth == p->container_depth && !strcmp(key, list) && type != ']') {
        if (type != '[' || p->seen) return false;
        p->seen = true;
        p->array = true;
        p->array_depth = depth;
    }
    if (p->array && depth == p->array_depth + 1 && type != '{' && type != '}') return false;
    if (p->array && type == '{' && depth == p->array_depth + 1) {
        p->record = true;
        p->record_depth = depth;
        memset(n, 0, sizeof(*n));
        if(p->personal)memset(p->personal,0,sizeof(*p->personal));
    }
    if (p->record && type == '}' && depth == p->record_depth) {
        unsigned kind = p->kind, cap = kind == 2 && !p->personal ? WD_REVIEW_LIMIT : WD_NOTE_LIMIT;
        ++p->manifest->info.total[kind];
        if (p->manifest->info.count[kind] < cap) {
            if (!n->text[0]) {if(p->personal){p->record=false;return true;}return false;}
            n->integrity = crc32(0, (const Bytef*)n, (uInt)offsetof(thingsos_wd_note_t, integrity));
            if(p->personal)p->personal->integrity=crc32(0,(const Bytef*)p->personal,(uInt)offsetof(thingsos_wd_personal_t,integrity));
            const void* payload=p->personal?(const void*)p->personal:(const void*)n;
            size_t payload_bytes=p->personal?sizeof(*p->personal):sizeof(*n);
            if (fwrite(payload, payload_bytes, 1, p->out) != 1) {
                p->error = WD_STORAGE;
                return false;
            }
            p->manifest->crc[kind] = crc32(p->manifest->crc[kind], payload, (uInt)payload_bytes);
            ++p->manifest->info.count[kind];
        } else
            p->manifest->info.limited[kind] = true;
        p->record = false;
    }
    if (type == ']' && p->array && depth == p->array_depth) p->array = false;
    if (type == '{' || type == '}' || type == '[' || type == ']') return true;
    if (!p->kind && !p->record && depth == p->container_depth && !strcmp(key, "totalCount")) {
        if (!decimal(text, &p->advertised_total)) return false;
    }
    if (!strcmp(key, "reviewsHasMore")) p->more = !strcmp(text, "1") || !strcmp(text, "true");
    if(p->personal && depth==1 && !strcmp(key,"hasMore"))p->more=!strcmp(text,"1") || !strcmp(text,"true");
    if (!strcmp(key, "synckey")) {
        size_t len = strlen(text);
        if (!len || len >= sizeof(p->sync)) return false;
        for (const char* t = text; *t; ++t)
            if (!isdigit((unsigned char)*t)) return false;
        strcpy(p->sync, text);
    }
    uint32_t v;
    if (!strcmp(key, "maxIdx") || (!strcmp(key, "idx") && p->record)) {
        if (!decimal(text, &p->next)) return false;
    }
    if (!p->record) return true;
    if(p->personal && !strcmp(key,"abstract"))cp(p->personal->quote,sizeof(p->personal->quote),text);
    if(p->personal && !strcmp(key,"reviewId"))cp(p->personal->review_id,sizeof(p->personal->review_id),text);
    if (!strcmp(key, "chapterUid")) {
        if (!safe(text)) return true;
        cp(n->uid, sizeof(n->uid), text);
    } else if (!strcmp(key, "chapterTitle") || !strcmp(key, "chapterName"))
        cp(n->chapter, sizeof(n->chapter), text);
    else if (
        !strcmp(key, "author") || !strcmp(key, "name") || !strcmp(key, "nick") ||
        !strcmp(key, "nickname")) {
        if (!n->author[0]) cp(n->author, sizeof(n->author), text);
    } else if ((p->kind==0 && !strcmp(key, "totalCount")) ||
               (p->kind==2 && !strcmp(key, "likeCount"))) {
        if (decimal(text, &v)) n->heat = v;
    } else if (!strcmp(key, p->kind == 2 ? "content" : "markText")) {
        if (type != 's' && type != 't') return false;
        if (*text) cp(n->text, sizeof(n->text), text);
        n->truncated = type == 't' || strlen(text) >= sizeof(n->text);
    } else if (p->kind == 2 && !strcmp(key, "htmlContent") && !n->text[0]) {
        review_html(text, n->text, sizeof(n->text));
        n->truncated = type == 't';
    } else if (!strcmp(key, "range")) {
        const char* dash = strchr(text, '-');
        if (dash && dash > text && (size_t)(dash - text) < 16) {
            char start[16];
            memcpy(start, text, (size_t)(dash - text));
            start[dash - text] = 0;
            uint32_t end;
            if (decimal(start, &n->begin) && decimal(dash + 1, &end) && end > n->begin) {
                n->length = end - n->begin;
                n->range = true;
            }
        }
    }
    return true;
}
thingsos_weidu_error_t thingsos_wd_notes_refresh(thingsos_weidu_client_t* c, const char* id) {
    if (!safe(id)) return WD_PROTOCOL;
    if (!thingsos_weidu_session_valid(&c->session)) return WD_AUTH;
    if (c->io.unix_time(c->io.context) < WD_SYNC_MIN_TIME) return WD_CLOCK;
    char dir[704], tmp[800], final[768], response[768], endpoint[512];
    snprintf(dir, sizeof(dir), "%s/book-%s", c->root, id);
    mkdir_one(dir);
    manifest_t old, m = { 0 };
    m.magic = NOTE_MAGIC;
    m.version = 2;
    m.record_size = sizeof(thingsos_wd_note_t);
    m.slot = manifest_read(c->root, &c->session, id, &old, true) ? 1 - old.slot : 0;
    thingsos_weidu_md5(c->session.vid, strlen(c->session.vid), m.owner);
    snprintf(response, sizeof(response), "%s/notes-response.part", dir);
    thingsos_wd_note_t* note = wd_alloc(sizeof(*note));
    if (!note) return WD_MEMORY;
    thingsos_weidu_error_t e = WD_OK;
    for (unsigned kind = 0; kind < 3 && e == WD_OK; ++kind) {
        note_path(final, sizeof(final), c->root, id, m.slot, kind);
        snprintf(tmp, sizeof(tmp), "%s.part", final);
        FILE* out = fopen(tmp, "wb");
        if (!out) {
            e = WD_STORAGE;
            break;
        }
        uint32_t maxidx = 0;
        char sync[40] = "0";
        bool more = false;
        unsigned pages = 0;
        do {
            if (++pages > 16) {
                e = WD_LIMIT;
                break;
            }
            if (c->io.cancelled && c->io.cancelled(c->io.context)) {
                e = WD_CANCEL;
                break;
            }
            if (kind == 0)
                snprintf(endpoint, sizeof(endpoint),
                    "/web/book/bestbookmarks?bookId=%s&count=%u", id, WD_NOTE_LIMIT);
            else if (kind == 1)
                strcpy(endpoint,"/api/agent/gateway");
            else
                snprintf(
                    endpoint, sizeof(endpoint),
                    "/web/review/"
                    "list?bookId=%s&listType=3&listMode=2&maxIdx=%u&count=20&synckey=%s",
                    id, (unsigned)maxidx, sync);
            char request[224];
            if(kind==1)snprintf(request,sizeof(request),
                "{\"api_name\":\"/book/bookmarklist\",\"skill_version\":\"1.0.3\",\"bookId\":\"%s\"}",id);
            e = c->io.http(
                c->io.context, kind==1?"POST":"GET", endpoint, kind==1?request:NULL, NULL, response, WD_RESPONSE_LIMIT,
                &c->session);
            if (e != WD_OK) break;
            note_parser_t parser = { 0 };
            parser.note = note;
            parser.manifest = &m;
            parser.out = out;
            parser.kind = kind;
            parser.container_depth = 1;
            parser.next = maxidx;
            strcpy(parser.sync, sync);
            bool parsed = thingsos_weidu_json_events(response, note_event, &parser);
            if (!parsed || !parser.root || (!parser.seen && parser.unknown_root)) {
                e = parser.error != WD_OK ? parser.error : WD_PROTOCOL;
                break;
            }
            if (parser.advertised_total > m.info.total[kind])
                m.info.total[kind] = parser.advertised_total;
            if (m.info.total[kind] > m.info.count[kind]) m.info.limited[kind] = true;
            more = kind == 2 && parser.more;
            unsigned cap = kind == 2 ? WD_REVIEW_LIMIT : WD_NOTE_LIMIT;
            if (more && m.info.count[kind] >= cap) m.info.limited[kind] = true;
            if (more && m.info.count[kind] < cap && parser.next == maxidx &&
                !strcmp(parser.sync, sync)) {
                e = WD_PROTOCOL;
                break;
            }
            maxidx = parser.next;
            strcpy(sync, parser.sync);
        } while (e == WD_OK && more && m.info.count[kind] < WD_REVIEW_LIMIT);
        if (!thingsos_weidu_flush(out)) e = WD_STORAGE;
        if (fclose(out) && e == WD_OK) e = WD_STORAGE;
        if (e == WD_OK) {
            FILE* check = fopen(tmp, "rb");
            uint32_t crc = crc32(0, NULL, 0);
            size_t bytes = 0;
            unsigned char block[512];
            if (!check)
                e = WD_STORAGE;
            else {
                size_t got;
                while ((got = fread(block, 1, sizeof(block), check))) {
                    crc = crc32(crc, block, (uInt)got);
                    bytes += got;
                }
                if (ferror(check) ||
                    bytes != (size_t)m.info.count[kind] * sizeof(thingsos_wd_note_t) ||
                    crc != m.crc[kind])
                    e = WD_STORAGE;
                fclose(check);
            }
        }
        if (e == WD_OK) {
            remove(final);
            if (rename(tmp, final)) e = WD_STORAGE;
        }
        remove(tmp);
    }
    free(note);
    remove(response);
    if (e == WD_OK && c->io.cancelled && c->io.cancelled(c->io.context)) e = WD_CANCEL;
    if (e == WD_OK) {
        m.seal = crc32(0, (const Bytef*)&m, (uInt)offsetof(manifest_t, seal));
        snprintf(final, sizeof(final), "%s/notes.manifest", dir);
        snprintf(tmp, sizeof(tmp), "%s.part", final);
        FILE* out = fopen(tmp, "wb");
        bool ok = out && fwrite(&m, sizeof(m), 1, out) == 1 && thingsos_weidu_flush(out);
        if (out && fclose(out)) ok = false;
        /* Validate the entire candidate before making it visible. */
        if (ok) {
            manifest_t check;
            FILE* verify = fopen(tmp, "rb");
            ok = verify && fread(&check, sizeof(check), 1, verify) == 1 && fgetc(verify) == EOF &&
                 !memcmp(&check, &m, sizeof(m));
            if (verify) fclose(verify);
        }
        if (!ok || !swap_file(tmp, final)) e = WD_STORAGE;
        remove(tmp);
    }
    return e;
}

thingsos_weidu_error_t thingsos_wd_cache_bundle(thingsos_weidu_client_t* c,const thingsos_weidu_book_t* book,bool restore,thingsos_weidu_error_t* notes) {
    *notes=WD_CANCEL;
    thingsos_weidu_error_t e=restore?thingsos_weidu_restore(c,book):thingsos_weidu_download(c,book);
    if(e!=WD_OK)return e;
    c->status.phase=WD_DOWNLOAD;
    cp(c->status.message,sizeof(c->status.message),"正文已缓存，正在缓存划线和书评");
    ++c->status.serial;if(c->io.publish)c->io.publish(c->io.context,&c->status);
    *notes=thingsos_wd_notes_refresh(c,book->id);
    c->status.phase=WD_READY;
    cp(c->status.message,sizeof(c->status.message),*notes==WD_OK?"正文、划线和书评已缓存，可离线阅读":
        *notes==WD_CANCEL?"正文已缓存，划线书评已取消，可稍后刷新":"正文已缓存，划线书评未完成，可稍后刷新");
    ++c->status.serial;if(c->io.publish)c->io.publish(c->io.context,&c->status);
    return WD_OK;
}

extern bool thingsos_weidu_json_memory_events(
    const char*, size_t, bool (*)(void*, const char*, const char*, char, unsigned), void*);

typedef struct {char* out;size_t capacity;bool found;} key_header_t;
static bool key_header_event(void* raw,const char* key,const char* text,char type,unsigned depth) {
    key_header_t* p=raw;
    if(depth!=1)return true;
    if(!strcmp(key,"errcode") || !strcmp(key,"errCode"))return !strcmp(text,"0");
    if(strcmp(key,"apikey"))return true;
    size_t n=strlen(text);
    if(type!='s' || p->found || n<8 || n>255 || n+8>p->capacity)return false;
    for(size_t i=0;i<n;++i)if(!isalnum((unsigned char)text[i]) && !strchr("_-./+=",text[i]))return false;
    snprintf(p->out,p->capacity,"Bearer %s",text);p->found=true;return true;
}
bool thingsos_wd_key_header(const char* text,size_t bytes,char* out,size_t capacity) {
    if(!out || !capacity)return false;
    out[0]=0;if(!text || !bytes)return false;
    key_header_t p={out,capacity,false};
    bool ok=thingsos_weidu_json_memory_events(text,bytes,key_header_event,&p) && p.found;
    if(!ok)out[0]=0;
    return ok;
}
typedef struct {
    thingsos_wd_progress_t candidate[17], best;
    char book[17][64];
    bool has_offset[17], bad_percent[17], root, succ, sync;
    const char* wanted;
    thingsos_weidu_error_t error;
} wd_response_t;
static bool response_event(
    void* raw, const char* key, const char* text, char type, unsigned depth) {
    wd_response_t* p = raw;
    if (depth > 16) return false;
    if (type == '{') {
        if (!depth) p->root = true;
        memset(&p->candidate[depth], 0, sizeof(p->candidate[depth]));
        p->book[depth][0] = 0;
        p->has_offset[depth] = p->bad_percent[depth] = false;
    }
    if (!strcmp(key, "errcode") || !strcmp(key, "errCode")) {
        if (strcmp(text, "0")) {
            p->error = !strcmp(text, "-2012") ? WD_AUTH : WD_PROTOCOL;
            return false;
        }
    }
    if (type == '}') {
        thingsos_wd_progress_t* c = &p->candidate[depth];
        c->valid = safe(c->uid) && p->has_offset[depth] && !p->bad_percent[depth] &&
                   c->percent <= 100 &&
                   (!p->wanted || !p->book[depth][0] || !strcmp(p->wanted, p->book[depth]));
        if (c->valid && (!p->best.valid || c->updated >= p->best.updated)) p->best = *c;
    }
    if (type == '{' || type == '}' || type == '[' || type == ']') return true;
    if (depth==1 && !strcmp(key, "succ")) p->succ = !strcmp(text, "1") || !strcmp(text, "true");
    if (depth==1 && !strcmp(key, "synckey")) {
        p->sync = *text != 0;
        for (const char* t = text; *t; ++t)
            if (!isdigit((unsigned char)*t)) p->sync = false;
    }
    if (!depth) return true;
    unsigned at = depth - 1;
    thingsos_wd_progress_t* c = &p->candidate[at];
    if (!strcmp(key, "bookId") || !strcmp(key, "book_id")) {
        if (safe(text))
            cp(p->book[at], sizeof(p->book[at]), text);
        else
            return false;
    } else if (
        !strcmp(key, "chapterUid") || !strcmp(key, "chapterId") || !strcmp(key, "chapter_uid")) {
        if (safe(text)) cp(c->uid, sizeof(c->uid), text);
    } else if (
        !strcmp(key, "chapterOffset") || !strcmp(key, "chapterPos") || !strcmp(key, "offset") ||
        !strcmp(key, "chapter_offset"))
        p->has_offset[at] = decimal(text, &c->offset);
    else if (
        !strcmp(key, "progress") || !strcmp(key, "readingProgress") ||
        !strcmp(key, "bookProgress")) {
        char* end;
        double percentage = strtod(text, &end);
        p->bad_percent[at] =
            !*text || *end || !isfinite(percentage) || percentage < 0 || percentage > 100;
        if (!p->bad_percent[at]) c->percent = (uint32_t)percentage;
    } else if (!strcmp(key, "updateTime") || !strcmp(key, "update_time"))
        decimal(text, &c->updated);
    else if(!strcmp(key,"readingTime"))c->reading_seconds_valid=decimal(text,&c->reading_seconds);
    return true;
}
static thingsos_weidu_error_t response(
    const char* text, const char* wanted, thingsos_wd_progress_t* out, unsigned acknowledge) {
    wd_response_t* p = wd_alloc(sizeof(*p));
    if (!p) return WD_MEMORY;
    p->wanted = wanted;
    if (out) memset(out, 0, sizeof(*out));
    bool ok = thingsos_weidu_json_memory_events(text, strlen(text), response_event, p);
    thingsos_weidu_error_t e = p->error;
    if (e == WD_OK && (!ok || !p->root || (wanted && p->book[0][0] && strcmp(wanted, p->book[0]))))
        e = WD_PROTOCOL;
    if(e==WD_OK && acknowledge && !p->succ && (acknowledge==2 || !p->sync))
        e=acknowledge==2?WD_UNCONFIRMED:WD_PROTOCOL;
    if (e == WD_OK && out) *out = p->best;
    free(p);
    return e;
}
typedef struct {char* out;bool found;thingsos_weidu_error_t error;} read_token_t;
static bool read_token_event(void* raw,const char* key,const char* text,char type,unsigned depth) {
    read_token_t* p=raw;
    if(depth!=1)return true;
    if(!strcmp(key,"errCode") || !strcmp(key,"errcode")) {
        if(strcmp(text,"0")){p->error=!strcmp(text,"-2012")?WD_AUTH:WD_PROTOCOL;return false;}
    }
    if(!strcmp(key,"token")) {
        if(type!='s' || p->found || !thingsos_weidu_token(text,128))return false;
        cp(p->out,128,text);p->found=true;
    }
    return true;
}
static thingsos_weidu_error_t read_token(thingsos_weidu_client_t* c,const char* ref,char token[128]) {
    char* data=wd_alloc(4096);if(!data)return WD_MEMORY;
    token[0]=0;size_t n=0;
    thingsos_weidu_error_t e=c->io.exchange
        ?c->io.exchange(c->io.context,"GET","/web/getConfig",NULL,ref,data,4096,&n,&c->session)
        :c->io.auth(c->io.context,"GET","/web/getConfig",NULL,data,4096,&n,&c->session);
    read_token_t parsed={.out=token};
    if(e==WD_OK && (!thingsos_weidu_json_memory_events(data,n,read_token_event,&parsed) || !parsed.found))e=parsed.error?parsed.error:WD_PROTOCOL;
    free(data);return e;
}
thingsos_weidu_error_t thingsos_wd_progress_parse(const char* text, thingsos_wd_progress_t* p) {
    return response(text, NULL, p, false);
}
thingsos_weidu_error_t thingsos_wd_pull(
    thingsos_weidu_client_t* c, const char* id, thingsos_wd_progress_t* p) {
    memset(p, 0, sizeof(*p));
    if (!safe(id)) return WD_PROTOCOL;
    if (!thingsos_weidu_session_valid(&c->session)) return WD_AUTH;
    uint32_t t = c->io.unix_time(c->io.context);
    if (t < WD_SYNC_MIN_TIME) return WD_CLOCK;
    char endpoint[256];
    snprintf(
        endpoint, sizeof(endpoint), "/web/book/getProgress?bookId=%s&timestamp=%u", id,
        (unsigned)t);
    char* body = wd_alloc(4096);
    if (!body) return WD_MEMORY;
    size_t bytes = 0;
    thingsos_weidu_error_t e =
        c->io.auth(c->io.context, "GET", endpoint, NULL, body, 4096, &bytes, &c->session);
    if (e == WD_OK) e = response(body, id, p, false);
    free(body);
    return e;
}

/* SHA-256 for short protocol tokens, independently implemented from FIPS 180-4. */
static uint32_t rr(uint32_t x, unsigned n) {
    return (x >> n) | (x << (32 - n));
}
bool thingsos_rt_hash(const void* text,size_t len,char out[65]) {
    static const uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
        0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
        0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
        0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
        0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
        0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
        0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
        0xc67178f2
    };
    if (!text || len > 8192) return false;
    size_t total = (len + 9 + 63) / 64 * 64;
    unsigned char* data = wd_alloc(total);
    if(!data)return false;
    memset(data,0,total);
    memcpy(data, text, len);
    data[len] = 128;
    uint64_t bits = len * 8;
    for (unsigned i = 0; i < 8; ++i) data[total - 1 - i] = (unsigned char)(bits >> (8 * i));
    uint32_t h[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    for (size_t o = 0; o < total; o += 64) {
        uint32_t w[64];
        for (unsigned i = 0; i < 16; ++i)
            w[i] = ((uint32_t)data[o + i * 4] << 24) | ((uint32_t)data[o + i * 4 + 1] << 16) |
                   ((uint32_t)data[o + i * 4 + 2] << 8) | data[o + i * 4 + 3];
        for (unsigned i = 16; i < 64; ++i) {
            uint32_t a = w[i - 15], b = w[i - 2];
            w[i] = w[i - 16] + (rr(a, 7) ^ rr(a, 18) ^ (a >> 3)) + w[i - 7] +
                   (rr(b, 17) ^ rr(b, 19) ^ (b >> 10));
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], v = h[5], g = h[6], z = h[7];
        for (unsigned i = 0; i < 64; ++i) {
            uint32_t t =
                z + (rr(e, 6) ^ rr(e, 11) ^ rr(e, 25)) + ((e & v) ^ ((~e) & g)) + k[i] + w[i];
            uint32_t u = (rr(a, 2) ^ rr(a, 13) ^ rr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            z = g;
            g = v;
            v = e;
            e = d + t;
            d = c;
            c = b;
            b = a;
            a = t + u;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
        h[5] += v;
        h[6] += g;
        h[7] += z;
    }
    for (unsigned i = 0; i < 8; ++i) snprintf(out + i * 8, 9, "%08x", (unsigned)h[i]);
    free(data);
    return true;
}
bool thingsos_rt_sha256(const char* text,char out[65]) {return text && thingsos_rt_hash(text,strlen(text),out);}
static bool sha(const char* text,char out[65]) {return strlen(text)<=255 && thingsos_rt_sha256(text,out);}
static bool url(const char* in, char* out, size_t cap) {
    size_t used = 0;
    for (const unsigned char* p = (const unsigned char*)in; *p; ++p) {
        if (isalnum(*p) || *p == '_' || *p == '-' || *p == '.' || *p == '~') {
            if (used + 1 >= cap) return false;
            out[used++] = (char)*p;
        } else {
            if (used + 3 >= cap) return false;
            snprintf(out + used, 4, "%%%02X", *p);
            used += 3;
        }
    }
    out[used] = 0;
    return true;
}
static void app_id(char out[64]) {
    const char* ua = WD_USER_AGENT;
    uint32_t h = 0;
    for (const unsigned char* p = (const unsigned char*)ua; *p; ++p)
        h = (h * 131 + *p) & 0x7fffffff;
    char digits[13];
    size_t n = 0;
    const char* p = ua;
    while (*p && n < 12) {
        const char* q = strchr(p, ' ');
        size_t len = q ? (size_t)(q - p) : strlen(p);
        digits[n++] = (char)('0' + len % 10);
        if (!q) break;
        p = q + 1;
    }
    digits[n] = 0;
    snprintf(out, 64, "wb%sh%u", digits, (unsigned)h);
}
static bool read_body_context(
    const thingsos_wd_upload_t* in, const thingsos_weidu_chapter_t* ch, const char* ps,
    const char* context_pc, const char* context_token, uint32_t t, uint32_t random, char* out,
    size_t cap) {
    if (t < WD_SYNC_MIN_TIME || in->percent > 100 || !safe(in->id) || !safe(ch->uid) ||
        !thingsos_weidu_token(ps, 128))
        return false;
    char b[160], c[160], pc[160], stamp[16], appid[64], summary[96], escaped[300], sig[17], sg[65],
        seed[160];
    char* query = out;
    if (!thingsos_weidu_encode_id(in->id, b) || !thingsos_weidu_encode_id(ch->uid, c)) return false;
    snprintf(stamp, sizeof(stamp), "%u", (unsigned)t);
    if (context_pc && *context_pc && strcmp(context_pc, "0")) {
        if (!thingsos_weidu_token(context_pc, 128)) return false;
        cp(pc, sizeof(pc), context_pc);
    } else if (!thingsos_weidu_encode_id(stamp, pc))
        return false;
    app_id(appid);
    size_t n = 0, chars = 0;
    while (ch->title[n] && chars < 20) {
        unsigned char a = (unsigned char)ch->title[n];
        size_t k = a < 128 ? 1 : a < 224 ? 2 : a < 240 ? 3 : 4;
        if (n + k >= sizeof(summary) || n + k > strlen(ch->title)) return false;
        memcpy(summary + n, ch->title + n, k);
        n += k;
        ++chars;
    }
    summary[n] = 0;
    if (!url(summary, escaped, sizeof(escaped))) return false;
    uint32_t rn = random % 1000;
    uint64_t ts = (uint64_t)t * 1000 + random % 1000;
    snprintf(
        seed, sizeof(seed), "%llu%u%s", (unsigned long long)ts, (unsigned)rn,
        context_token && *context_token ? context_token : "3c5c8717f3daf09iop3423zafeqoi");
    if (!sha(seed, sg)) return false;
    int written = snprintf(
        query, cap, "appId=%s&b=%s&c=%s&ci=%u&co=%u&ct=%u&pc=%s&pr=%u&ps=%s", appid, b, c,
        (unsigned)ch->index, (unsigned)in->offset, (unsigned)t, pc, (unsigned)in->percent, ps);
    if (written < 0 || (size_t)written >= cap) return false;
    n = (size_t)written;
    if (in->report)
        n += (size_t)snprintf(
            query + n, cap - n, "&rn=%u&rt=%u&sg=%s", (unsigned)rn, (unsigned)in->seconds, sg);
    n += (size_t)snprintf(query + n, cap - n, "&sm=%s", escaped);
    if (in->report) n += (size_t)snprintf(query + n, cap - n, "&ts=%llu", (unsigned long long)ts);
    if (n >= cap) return false;
    thingsos_weidu_sign(query, sig);

    char quoted[160];
    size_t used = 0;
    for (const unsigned char* p = (const unsigned char*)summary; *p; ++p) {
        if (*p == 34 || *p == 92) {
            quoted[used++] = 92;
            quoted[used++] = (char)*p;
        } else if (*p < 32) {
            quoted[used++] = 92;
            snprintf(quoted + used, sizeof(quoted) - used, "u%04x", (unsigned)*p);
            used += 5;
        } else
            quoted[used++] = (char)*p;
    }
    quoted[used] = 0;
    int nout = snprintf(
        out, cap,
        "{\"appId\":\"%s\",\"b\":\"%s\",\"c\":\"%s\",\"ci\":%u,\"co\":%u,\"ct\":%u,\"pc\":\"%s\","
        "\"pr\":%u,\"ps\":\"%s\",\"sm\":\"%s\"",
        appid, b, c, (unsigned)ch->index, (unsigned)in->offset, (unsigned)t, pc,
        (unsigned)in->percent, ps, quoted);
    if (nout < 0 || (size_t)nout >= cap) return false;
    used = (size_t)nout;
    if (in->report) {
        nout = snprintf(
            out + used, cap - used, ",\"rn\":%u,\"rt\":%u,\"sg\":\"%s\",\"ts\":%llu", (unsigned)rn,
            (unsigned)in->seconds, sg, (unsigned long long)ts);
        if (nout < 0 || (size_t)nout >= cap - used) return false;
        used += (size_t)nout;
    }
    nout = snprintf(out + used, cap - used, ",\"s\":\"%s\"}", sig);
    return nout > 0 && (size_t)nout < cap - used;
}
bool thingsos_wd_read_body(
    const thingsos_wd_upload_t* in, const thingsos_weidu_chapter_t* ch, const char* ps, uint32_t t,
    uint32_t random, char* out, size_t cap) {
    return read_body_context(in, ch, ps, NULL, NULL, t, random, out, cap);
}
static thingsos_weidu_error_t send_read(
    thingsos_weidu_client_t* c, const thingsos_wd_upload_t* in, const thingsos_weidu_chapter_t* ch,
    const char* ps, const char* pc, const char* token, const char* ref) {
    char* body = wd_alloc(4096);
    char* result = wd_alloc(4096);
    if (!body || !result) {
        free(body);
        free(result);
        return WD_MEMORY;
    }
    thingsos_weidu_error_t e = WD_PROTOCOL;
    if (read_body_context(
            in, ch, ps, pc, token, c->io.unix_time(c->io.context), c->io.random(c->io.context),
            body, 4096)) {
        size_t n = 0;
        e = c->io.exchange
                ? c->io.exchange(
                      c->io.context, "POST", "/web/book/read", body, ref, result, 4096, &n,
                      &c->session)
                : c->io.auth(
                      c->io.context, "POST", "/web/book/read", body, result, 4096, &n, &c->session);
        if (e == WD_OK) e = response(result, NULL, NULL, in->report?2:1);
#ifdef ESP_PLATFORM
        ESP_LOGI("ThingsOS_weidu","Read ack book=%s kind=%s seconds=%u bytes=%u result=%d",
            in->id,in->report?"duration":"progress",in->report?(unsigned)in->seconds:0,(unsigned)n,e);
#endif
    }
    free(body);
    free(result);
    return e;
}
thingsos_weidu_error_t thingsos_wd_upload(
    thingsos_weidu_client_t* c, const thingsos_wd_upload_t* input,
    thingsos_weidu_error_t* duration) {
    *duration = WD_CANCEL;
    if (!thingsos_weidu_session_valid(&c->session)) return WD_AUTH;
    if (c->io.unix_time(c->io.context) < WD_SYNC_MIN_TIME) return WD_CLOCK;
    thingsos_weidu_chapter_t ch;
    if (!thingsos_wd_toc(c->root, input->id, input->chapter, &ch)) return WD_STORAGE;
    char b[160], h[160], ref[400], ps[128], pc[128] = { 0 }, token[128] = { 0 };
    if (!thingsos_weidu_encode_id(input->id, b) || !thingsos_weidu_encode_id(ch.uid, h))
        return WD_PROTOCOL;
    snprintf(ref, sizeof(ref), "https://weread.qq.com/web/reader/%sk%s", b, h);
    thingsos_weidu_error_t e =
        c->io.reader_info ? c->io.reader_info(c->io.context, ref, ps, pc, token, &c->session)
        : c->io.reader    ? c->io.reader(c->io.context, ref, ps, sizeof(ps), &c->session)
                          : WD_PROTOCOL;
    if (e != WD_OK) return e;
    thingsos_wd_upload_t in = *input;
    in.report = false;
    e = send_read(c, &in, &ch, ps, pc, token, ref);
    /* No retry or queue for time, including definite and ambiguous failures. */
    if (e == WD_OK && input->report && input->seconds) {
        in.report = true;
        *duration=read_token(c,ref,token);
        if(*duration==WD_OK)*duration = send_read(c, &in, &ch, ps, pc, token, ref);
    }
    return e;
}
thingsos_weidu_error_t thingsos_wd_begin(thingsos_weidu_client_t* c,const thingsos_wd_upload_t* in,thingsos_wd_read_session_t* s) {
    memset(s,0,sizeof(*s));
    if(!thingsos_weidu_session_valid(&c->session))return WD_AUTH;
    if(c->io.unix_time(c->io.context)<WD_SYNC_MIN_TIME)return WD_CLOCK;
    thingsos_weidu_chapter_t ch;
    if(!thingsos_wd_toc(c->root,in->id,in->chapter,&ch))return WD_STORAGE;
    char b[160],h[160],ref[400];
    if(!thingsos_weidu_encode_id(in->id,b) || !thingsos_weidu_encode_id(ch.uid,h))return WD_PROTOCOL;
    snprintf(ref,sizeof(ref),"https://weread.qq.com/web/reader/%sk%s",b,h);
    thingsos_weidu_error_t e=c->io.reader_info?c->io.reader_info(c->io.context,ref,s->ps,s->pc,s->token,&c->session)
        :c->io.reader?c->io.reader(c->io.context,ref,s->ps,sizeof(s->ps),&c->session):WD_PROTOCOL;
    if(e==WD_OK)e=read_token(c,ref,s->token);
    if(e==WD_OK) {
        thingsos_wd_upload_t position=*in;position.report=false;position.seconds=0;
        e=send_read(c,&position,&ch,s->ps,s->pc,s->token,ref);
    }
    if(e==WD_OK){cp(s->id,sizeof(s->id),in->id);s->started=c->io.unix_time(c->io.context);s->entered=true;}
    else memset(s,0,sizeof(*s));
    return e;
}
thingsos_weidu_error_t thingsos_wd_upload_interval(thingsos_weidu_client_t* c,const thingsos_wd_upload_t* in,
    thingsos_weidu_error_t* duration,thingsos_wd_read_session_t* s,bool finish) {
    *duration=WD_CANCEL;
    if(s->consumed && in->report && !strcmp(s->id,in->id))return WD_CANCEL;
    s->last_seconds=0;
    s->last_end=0;
    if(!in->report || !in->seconds){s->entered=false;return thingsos_wd_upload(c,in,duration);}
    if(!s->entered || strcmp(s->id,in->id)) {
        /* A new page context at exit cannot establish past reading activity. */
        thingsos_wd_upload_t progress=*in;progress.report=false;progress.seconds=0;
        return thingsos_wd_upload(c,&progress,duration);
    }
    s->entered=!finish;s->consumed=finish;
    uint32_t clock=c->io.unix_time(c->io.context);
    if(clock<WD_SYNC_MIN_TIME || clock<s->started)return *duration=WD_CLOCK;
    if(!thingsos_weidu_session_valid(&c->session))return *duration=WD_AUTH;
    thingsos_weidu_chapter_t ch;
    if(!thingsos_wd_toc(c->root,in->id,in->chapter,&ch))return *duration=WD_STORAGE;
    char b[160],h[160],ref[400];
    if(!thingsos_weidu_encode_id(in->id,b)||!thingsos_weidu_encode_id(ch.uid,h))return *duration=WD_PROTOCOL;
    snprintf(ref,sizeof(ref),"https://weread.qq.com/web/reader/%sk%s",b,h);
    thingsos_wd_upload_t measured=*in;
    if(measured.seconds>clock-s->started)measured.seconds=clock-s->started;
    if(!measured.seconds)return WD_OK;
    if(measured.seconds>WD_REPORT_MAX_SECONDS)measured.seconds=WD_REPORT_MAX_SECONDS;
    /* Consume this interval before sending, even if its response is ambiguous.
       Only a later, newly measured interval can be reported next. */
    s->started=clock;
    s->last_seconds=measured.seconds;
    s->last_end=clock;
    thingsos_wd_progress_t before={0},after={0};
    thingsos_weidu_error_t before_result=thingsos_wd_pull(c,in->id,&before);
    /* Do not send another enter/progress-only POST immediately before time. */
    *duration=send_read(c,&measured,&ch,s->ps,s->pc,s->token,ref);
    if(*duration==WD_OK) {
        thingsos_weidu_error_t after_result=thingsos_wd_pull(c,in->id,&after);
#ifdef ESP_PLATFORM
        ESP_LOGI("ThingsOS_weidu","Reading cloud book=%s before_valid=%d before_seconds=%u after_valid=%d after_seconds=%u",
            in->id,before_result==WD_OK && before.reading_seconds_valid,(unsigned)before.reading_seconds,
            after_result==WD_OK && after.reading_seconds_valid,(unsigned)after.reading_seconds);
#else
        (void)after_result;
#endif
    }
    (void)before_result;
    return *duration;
}
thingsos_weidu_error_t thingsos_wd_upload_session(thingsos_weidu_client_t* c,const thingsos_wd_upload_t* in,
    thingsos_weidu_error_t* duration,thingsos_wd_read_session_t* s) {
    return thingsos_wd_upload_interval(c,in,duration,s,true);
}
/* Build a bounded source UTF-16 -> existing normalized UTF-8 map. Each emitted
 * byte must equal the existing reader chapter, including synthetic heading.
 * Unsupported markup/entities fail closed instead of guessing an occurrence. */
void thingsos_wd_map_free(thingsos_wd_map_t* m) {
    free(m->source);
    memset(m, 0, sizeof(*m));
}
static uint32_t utf(const char* s, size_t n, size_t* used) {
    if (!n) return 0;
    unsigned char a = (unsigned char)s[0];
    size_t k = a < 128 ? 1 : a >= 194 && a < 224 ? 2 : a < 240 ? 3 : a <= 244 ? 4 : 0;
    if (!k || k > n) return 0;
    uint32_t v = a & (k == 1 ? 127 : k == 2 ? 31 : k == 3 ? 15 : 7);
    for (size_t i = 1; i < k; ++i) {
        if (((unsigned char)s[i] & 192) != 128) return 0;
        v = (v << 6) | ((unsigned char)s[i] & 63);
    }
    if ((k == 2 && v < 128) || (k == 3 && v < 2048) || (k == 4 && v < 65536) || !v ||
        v > 0x10ffff || (v >= 0xd800 && v <= 0xdfff))
        return 0;
    *used = k;
    return v;
}
/* Sparse checkpoints every 128 bytes or at a source discontinuity. 64 KiB
 * absolute cap; lookup scans at most one text run. Source text is borrowed. */
static bool checkpoint(thingsos_wd_map_t* m, size_t local, uint32_t source) {
    if (m->count >= m->capacity) {
        size_t capacity = m->capacity ? m->capacity * 2 : 64;
        if (capacity > 8192) return false;
        void* resized = wd_resize(m->source, capacity * 2 * sizeof(uint32_t));
        if (!resized) return false;
        m->source = resized;
        m->capacity = capacity;
    }
    m->source[m->count * 2] = (uint32_t)local;
    m->source[m->count * 2 + 1] = source;
    ++m->count;
    return true;
}
static bool emit(
    const thingsos_chapter_t* ch, thingsos_wd_map_t* m, size_t* at, uint32_t cpv, uint32_t src) {
    char t[4];
    unsigned k = cpv < 128 ? 1 : cpv < 2048 ? 2 : cpv < 65536 ? 3 : 4;
    uint32_t v = cpv;
    for (unsigned i = k - 1; i; i--) {
        t[i] = (char)(128 | (v & 63));
        v >>= 6;
    }
    t[0] = (char)((k == 1 ? 0 : k == 2 ? 192 : k == 3 ? 224 : 240) | v);
    if (*at + k > ch->bytes || memcmp(ch->text + *at, t, k)) {
#ifndef ESP_PLATFORM
        if(getenv("THINGSOS_WD_MAP_TRACE"))fprintf(stderr,"map mismatch local=%zu source=%u emitted=U+%x existing=%02x\n",*at,src,cpv,*at<ch->bytes?(unsigned char)ch->text[*at]:0);
#endif
        return false;
    }
    if (!m->count || src == UINT32_MAX || src != m->expected ||
        *at - m->source[(m->count - 1) * 2] >= 128) {
        if (!checkpoint(m, *at, src)) return false;
    }
    m->expected = src == UINT32_MAX ? UINT32_MAX : src + (cpv > 65535 ? 2 : 1);
    *at += k;
    return true;
}
static bool newline(const thingsos_chapter_t* ch, thingsos_wd_map_t* m, size_t* at) {
    if (*at && ch->text[*at - 1] != '\n') return emit(ch, m, at, '\n', UINT32_MAX);
    return true;
}

/* Keep the 4 KiB PSRAM stdio buffer. Batch decoding reads as well: fgetc
   acquires the newlib FILE lock for every byte on the device. */
typedef struct {
    FILE* f;
    unsigned char chunk[512];
    size_t cursor, available;
    size_t bytes;
    uint32_t source;
    bool failed;
} raw_reader_t;
static int raw_byte(raw_reader_t* r) {
    if (r->cursor == r->available) {
        r->cursor = 0;
        r->available = fread(r->chunk, 1, sizeof(r->chunk), r->f);
        if (!r->available) return EOF;
    }
    return r->chunk[r->cursor++];
}
static uint32_t raw_cp(raw_reader_t* r) {
    int first = raw_byte(r);
    if (first == EOF) {
        if (ferror(r->f)) r->failed = true;
        return 0;
    }
    unsigned char a = (unsigned char)first;
    size_t width = a < 128                ? 1
                   : a >= 194 && a < 224  ? 2
                   : a >= 224 && a < 240  ? 3
                   : a >= 240 && a <= 244 ? 4
                                          : 0;
    if (!width || r->bytes + width > THINGSOS_XML_BYTES) {
        r->failed = true;
        return 0;
    }
    char encoded[4];
    encoded[0] = (char)a;
    for (size_t i = 1; i < width; ++i) {
        int c = raw_byte(r);
        if (c == EOF) {
            r->failed = true;
            return 0;
        }
        encoded[i] = (char)c;
    }
    size_t used;
    uint32_t cpv = utf(encoded, width, &used);
    if (!cpv) {
        r->failed = true;
        return 0;
    }
    r->bytes += width;
    r->source += cpv > 65535 ? 2 : 1;
    return cpv;
}
static bool map_profile(
    const char* root, const char* id, unsigned index, const thingsos_chapter_t* ch,
    thingsos_wd_map_t* m, bool legacy_head, bool keep_sup) {
    thingsos_wd_map_free(m);
    thingsos_weidu_chapter_t toc;
    if (!ch || !ch->text || ch->bytes > THINGSOS_XML_BYTES ||
        !thingsos_wd_toc(root, id, index, &toc))
        return false;
    char path[768];
    snprintf(path, sizeof(path), "%s/book-%s/chapter-%u.raw", root, id, index);
    FILE* file = fopen(path, "rb");
    if (!file) return false;
    char* buffer = wd_alloc(4096);
    if (!buffer) {
        fclose(file);
        return false;
    }
    if (setvbuf(file, buffer, _IOFBF, 4096)) {
        fclose(file);
        free(buffer);
        return false;
    }
    m->bytes = ch->bytes;
    m->text = ch->text;
    size_t at = strlen(toc.title);
    bool ok = at <= ch->bytes && !memcmp(ch->text, toc.title, at);
    if (ok) ok = newline(ch, m, &at);
    size_t img_cursor = 0;
    if (ok && !index && ch->image_count &&
        !strcmp(ch->images[0].path, "OPS/cover.jpg") &&
        ch->images[0].offset == at && at < ch->bytes && ch->text[at] == '\n') {
        ok = emit(ch, m, &at, '\n', UINT32_MAX);
        img_cursor = 1;
    }
    raw_reader_t r = { .f = file };
    char skip[24] = { 0 };
    unsigned depth = 0;
    while (ok) {
        uint32_t origin = r.source, v = raw_cp(&r);
        if (!v) break;
        if (!toc.plain && v == '<') {
            char tag[64];
            size_t n = 0;
            uint32_t quote = 0, last = 0, c;
            bool closed = false;
            while ((c = raw_cp(&r))) {
                if (quote) {
                    if (c == quote) quote = 0;
                } else if (c == '\'' || c == '"')
                    quote = c;
                else if (c == '>') {
                    closed = true;
                    break;
                }
                if (n + 1 < sizeof(tag)) tag[n++] = c < 128 ? (char)c : '?';
                if (c > 127 || !isspace((unsigned char)c)) last = c;
            }
            tag[n] = 0;
            if (!closed) {
                ok = false;
                break;
            }
            const char* p = tag;
            while (isspace((unsigned char)*p)) ++p;
            bool close = *p == '/';
            if (close) ++p;
            char name[24];
            size_t k = 0;
            while (isalnum((unsigned char)*p) && k + 1 < sizeof(name))
                name[k++] = (char)tolower((unsigned char)*p++);
            name[k] = 0;
            bool self = last == '/';
            if(keep_sup && !depth && ((!close && (!strcmp(name,"img") || !strcmp(name,"span"))) || (!strcmp(name,"a") && self))) {
                /* Icon/empty noterefs have a generated [n] label. They have no
                   source text position, so do not invent a cloud offset. */
                for(size_t i=0;i<ch->footnote_count;++i)if(ch->footnotes[i].begin==at) {
                    while(ok && at<ch->footnotes[i].end)ok=emit(ch,m,&at,(unsigned char)ch->text[at],UINT32_MAX);
                    break;
                }
            }
            if (!depth && !close && (!strcmp(name, "img") || !strcmp(name, "image"))) {
                while (img_cursor < ch->image_count && ch->images[img_cursor].offset < at) ++img_cursor;
                if (img_cursor < ch->image_count && ch->images[img_cursor].offset == at &&
                    at < ch->bytes && ch->text[at] == '\n') {
                    ok = emit(ch, m, &at, '\n', UINT32_MAX);
                    ++img_cursor;
                }
            }
            if (depth) {
                if (!strcmp(name, skip)) {
                    if (close)
                        --depth;
                    else if (!self)
                        ++depth;
                }
            } else if (
                !close &&
                ((!legacy_head && !strcmp(name, "head")) || !strcmp(name, "script") || !strcmp(name, "style") ||
                 (!keep_sup && !strcmp(name, "sup")) || !strcmp(name, "nav"))) {
                if (!self) {
                    strcpy(skip, name);
                    depth = 1;
                }
            } else if (
                !strcmp(name, "br") || !strcmp(name, "p") || !strcmp(name, "div") ||
                !strcmp(name, "li") ||
                (name[0] == 'h' && name[1] >= '1' && name[1] <= '6' && !name[2]))
                ok = newline(ch, m, &at);
            continue;
        }
        if (depth) continue;
        if (!toc.plain && v == '&') {
            char entity[32];
            size_t n = 0;
            uint32_t c = 0;
            while (n + 1 < sizeof(entity) && (c = raw_cp(&r)) && c != ';') {
                if (c >= 128) {
                    ok = false;
                    break;
                }
                entity[n++] = (char)c;
            }
            entity[n] = 0;
            if (!ok || c != ';') {
                ok = false;
                break;
            }
            if (!strcmp(entity, "amp"))
                v = '&';
            else if (!strcmp(entity, "lt"))
                v = '<';
            else if (!strcmp(entity, "gt"))
                v = '>';
            else if (!strcmp(entity, "quot"))
                v = '"';
            else if (!strcmp(entity, "apos"))
                v = '\'';
            else if (!strcmp(entity, "nbsp"))
                v = ' ';
            else if (entity[0] == '#') {
                char* end;
                bool hex = entity[1] == 'x' || entity[1] == 'X';
                unsigned long cpv = strtoul(entity + 1 + hex, &end, hex ? 16 : 10);
                if (*end || !cpv || cpv > 0x10ffff || (cpv >= 0xd800 && cpv <= 0xdfff)) {
                    ok = false;
                    break;
                }
                v = (uint32_t)cpv;
            } else {
                ok = false;
                break;
            }
        }
        if (v == '\n')
            ok = newline(ch, m, &at);
        else if (v >= 32 || v == '\t')
            ok = emit(ch, m, &at, v, origin);
    }
    fclose(file);
    free(buffer);
    if (!ok || r.failed || depth || at != ch->bytes || !checkpoint(m, m->bytes, r.source)) {
        thingsos_wd_map_free(m);
        return false;
    }
    return true;
}
typedef struct {
    uint32_t magic,version,text_crc,raw_crc,toc_crc,bytes,count,expected,payload_crc,seal;
} map_cache_header_t;
#ifndef ESP_PLATFORM
static unsigned map_cache_hits;
unsigned thingsos_wd_host_map_cache_hits(void) {return map_cache_hits;}
#endif
static bool map_cache(const char* root,const char* id,unsigned index,const thingsos_chapter_t* ch,
    thingsos_wd_map_t* m,bool save) {
    if(!ch || !ch->text || ch->bytes>THINGSOS_XML_BYTES)return false;
    thingsos_weidu_chapter_t toc;
    if(!thingsos_wd_toc(root,id,index,&toc))return false;
    char path[768],tmp[800];snprintf(path,sizeof(path),"%s/book-%s/chapter-%u.raw",root,id,index);
    FILE* f=fopen(path,"rb");if(!f)return false;
    unsigned char* block=wd_alloc(4096);if(!block){fclose(f);return false;}
    map_cache_header_t key={0};key.magic=0x574d4331;key.version=1;key.bytes=(uint32_t)ch->bytes;
    key.text_crc=crc32(0,(const Bytef*)ch->text,(uInt)ch->bytes);
    key.toc_crc=crc32(0,(const Bytef*)toc.uid,(uInt)strlen(toc.uid));
    key.toc_crc=crc32(key.toc_crc,(const Bytef*)toc.title,(uInt)strlen(toc.title));
    unsigned char plain=toc.plain?1:0;key.toc_crc=crc32(key.toc_crc,&plain,1);
    size_t n,bytes=0;while((n=fread(block,1,4096,f))) {key.raw_crc=crc32(key.raw_crc,block,(uInt)n);bytes+=n;if(bytes>THINGSOS_XML_BYTES)break;}
    bool ok=!ferror(f) && bytes<=THINGSOS_XML_BYTES;fclose(f);free(block);if(!ok)return false;
    snprintf(path,sizeof(path),"%s/book-%s/chapter-%u.map-cache",root,id,index);ok=false;
    if(!save) {
        map_cache_header_t h;f=fopen(path,"rb");uint32_t* checkpoints=NULL;
        if(f && fread(&h,sizeof(h),1,f)==1 && h.count && h.count<=8192 &&
           h.seal==crc32(0,(const Bytef*)&h,(uInt)offsetof(map_cache_header_t,seal))) {
            key.count=h.count;key.expected=h.expected;key.payload_crc=h.payload_crc;key.seal=h.seal;
            if(!memcmp(&key,&h,sizeof(h)))checkpoints=wd_alloc(h.count*2*sizeof(uint32_t));
            ok=checkpoints && fread(checkpoints,2*sizeof(uint32_t),h.count,f)==h.count && fgetc(f)==EOF && !ferror(f) &&
                h.payload_crc==crc32(0,(const Bytef*)checkpoints,h.count*2*sizeof(uint32_t));
            for(unsigned i=0;ok && i<h.count;++i)
                if(checkpoints[i*2]>ch->bytes || (i && checkpoints[i*2]<=checkpoints[(i-1)*2]))ok=false;
            if(ok && checkpoints[(h.count-1)*2]!=ch->bytes)ok=false;
            if(ok) {thingsos_wd_map_free(m);m->source=checkpoints;m->capacity=m->count=h.count;m->text=ch->text;m->bytes=ch->bytes;m->expected=h.expected;checkpoints=NULL;}
        }
        free(checkpoints);if(f)fclose(f);
    } else if(m->source && m->count && m->count<=8192) {
        key.count=(uint32_t)m->count;key.expected=m->expected;
        key.payload_crc=crc32(0,(const Bytef*)m->source,(uInt)(m->count*2*sizeof(uint32_t)));
        key.seal=crc32(0,(const Bytef*)&key,(uInt)offsetof(map_cache_header_t,seal));
        snprintf(tmp,sizeof(tmp),"%s.part",path);f=fopen(tmp,"wb");
        ok=f && fwrite(&key,sizeof(key),1,f)==1 && fwrite(m->source,2*sizeof(uint32_t),m->count,f)==m->count && thingsos_weidu_flush(f);
        if(f && fclose(f))ok=false;
        if(ok)ok=swap_file(tmp,path);
        remove(tmp);
    }
    return ok;
}
bool thingsos_wd_map(
    const char* root, const char* id, unsigned index, const thingsos_chapter_t* ch,
    thingsos_wd_map_t* m) {
    if(map_cache(root,id,index,ch,m,false)) {
#ifdef ESP_PLATFORM
        ESP_LOGI("ThingsOS_weidu","Source map cache hit chapter=%u",index);
#else
        ++map_cache_hits;
#endif
        return true;
    }
    /* Older committed EPUBs retained source head whitespace/text. Require
       full-chapter equality for that known packing profile as well. Each
       attempt releases its map/buffer before the next; no fuzzy search. */
    bool ok=map_profile(root,id,index,ch,m,false,false) || map_profile(root,id,index,ch,m,false,true) ||
        map_profile(root,id,index,ch,m,true,false);
    if(ok)map_cache(root,id,index,ch,m,true);
    return ok;
}
/* This is a navigation index, not a second spine. Its chapter/byte targets
   retain the exact text used by bookmarks, annotations and progress sync. */
bool thingsos_wd_navigation_step(const char* root,const char* id,thingsos_epub_t* e,bool* done) {
    *done=false;
    if(!root || !safe(id) || !e || !e->weidu_cache || !e->spine_count)return false;
    if(e->section_toc_ready){*done=true;return true;}
    uint32_t key=crc32(0,(const Bytef*)&e->zip.size,sizeof(e->zip.size));
    for(size_t i=0;i<e->zip.count;++i) {
        const thingsos_zip_entry_t* z=&e->zip.entries[i];
        key=crc32(key,(const Bytef*)z->name,(uInt)strlen(z->name));
        key=crc32(key,(const Bytef*)&z->crc,sizeof(z->crc));
        key=crc32(key,(const Bytef*)&z->bytes,sizeof(z->bytes));
    }
    for(size_t i=0;i<e->spine_count;++i) {
        key=crc32(key,(const Bytef*)e->chapter_names[i],(uInt)strlen(e->chapter_names[i]));
        key=crc32(key,(const Bytef*)&e->chapter_sources[i],sizeof(e->chapter_sources[i]));
    }
    char path[768],tmp[800];snprintf(path,sizeof(path),"%s/book-%s/navigation.cache",root,id);
    typedef struct {uint32_t magic,version,key,count,crc,seal;} nav_header_t;
    nav_header_t h;FILE* f=NULL;
    if(!e->section_toc) {
    thingsos_toc_entry_t* entries=NULL;
    f=e->weidu_online?NULL:fopen(path,"rb");
    bool hit=f && fread(&h,sizeof(h),1,f)==1 && h.magic==0x574e4331 && h.version==3 && h.key==key &&
        h.count>=e->spine_count && h.count<=THINGSOS_SPINE_LIMIT &&
        h.seal==crc32(0,(const Bytef*)&h,offsetof(nav_header_t,seal));
    if(hit) {
        entries=wd_alloc(h.count*sizeof(*entries));
        hit=entries && fread(entries,sizeof(*entries),h.count,f)==h.count && fgetc(f)==EOF && !ferror(f) &&
            h.crc==crc32(0,(const Bytef*)entries,(uInt)(h.count*sizeof(*entries)));
    }
    unsigned roots=0;
    for(unsigned i=0;hit && i<h.count;++i) {
        const thingsos_toc_entry_t* n=&entries[i];
        hit=n->chapter<e->spine_count && n->level<=15 && memchr(n->title,0,sizeof(n->title));
        if(hit) {
            const char* member=e->items[e->spine[n->chapter]].path;bool valid=false;
            for(size_t j=0;j<e->zip.count;++j)if(!strcmp(member,e->zip.entries[j].name)) {
                valid=n->offset<e->zip.entries[j].bytes;break;
            }
            hit=valid && (!i || n->chapter>entries[i-1].chapter ||
                (n->chapter==entries[i-1].chapter && n->offset>entries[i-1].offset));
            if(hit && !n->offset) {
                hit=n->chapter==roots && !strcmp(n->title,e->chapter_names[roots]);++roots;
            }
        }
    }
    hit=hit && roots==e->spine_count;
    if(f)fclose(f);
    if(hit) {
        e->section_toc=entries;e->section_toc_count=h.count;
        e->section_toc_ready=true;*done=true;
#ifdef ESP_PLATFORM
        ESP_LOGI("ThingsOS_weidu","Section navigation cache hit count=%u",h.count);
#endif
        return true;
    }
    free(entries);
    entries=wd_alloc(e->spine_count*sizeof(*entries));
    if(!entries)return false;
    memset(entries,0,e->spine_count*sizeof(*entries));
    for(unsigned i=0;i<e->spine_count;++i) {
        entries[i].chapter=i;entries[i].level=e->chapter_levels[i];
        strcpy(entries[i].title,e->chapter_names[i]);
    }
    e->section_toc=entries;e->section_toc_count=e->spine_count;
    e->section_toc_key=key;e->section_toc_next=0;e->section_toc_build_valid=true;
    return true; /* Root TOC is immediately usable; no chapter read here. */
    }
    thingsos_toc_entry_t* entries=e->section_toc;
    size_t count=e->section_toc_count;
    unsigned i=e->section_toc_next++;
    if(i<e->spine_count) {
        size_t base=0;while(base<count && (entries[base].chapter!=i || entries[base].offset))++base;
        size_t capacity=THINGSOS_SPINE_LIMIT-count;
        if(capacity && !e->chapter_fragments[i][0] && base<count) {
            snprintf(tmp,sizeof(tmp),"%s/book-%s/chapter-%u.raw",root,id,e->chapter_sources[i]);
            FILE* raw=fopen(tmp,"rb");long bytes=-1;
            if(raw && !fseek(raw,0,SEEK_END)){bytes=ftell(raw);rewind(raw);}
            unsigned char* xml=bytes>0 && bytes<=THINGSOS_XML_BYTES?wd_alloc((size_t)bytes+1):NULL;
            bool loaded=xml && fread(xml,1,(size_t)bytes,raw)==(size_t)bytes && !ferror(raw);
            if(raw)fclose(raw);
            thingsos_chapter_t* chapter=loaded?wd_alloc(sizeof(*chapter)):NULL;
            thingsos_toc_entry_t* extra=NULL;
            if(chapter) {
                xml[bytes]=0;memset(chapter,0,sizeof(*chapter));
                if(thingsos_epub_chapter(e,i,chapter)) {
                    size_t added=thingsos_epub_raw_headings(xml,(size_t)bytes,chapter,i,entries[base].level,NULL,capacity);
                    if(added) {
                        /* Local TOC anchors are exact unique lines in the reader's
                           current text. Cloud UTF-16 mapping can independently fail
                           on generated footnote markers; it must not hide these
                           verified local targets or supply cloud coordinates. */
                        extra=wd_alloc(added*sizeof(*extra));
                        bool valid=extra && thingsos_epub_raw_headings(xml,(size_t)bytes,chapter,i,entries[base].level,extra,added)==added;
                        thingsos_toc_entry_t* grown=valid?thingsos_psram_realloc(entries,(count+added)*sizeof(*entries)):NULL;
                        if(grown) {
                            entries=grown;e->section_toc=entries;
                            memmove(entries+base+1+added,entries+base+1,(count-base-1)*sizeof(*entries));
                            memcpy(entries+base+1,extra,added*sizeof(*extra));count+=added;
                        } else e->section_toc_build_valid=false;
                    }
                } else e->section_toc_build_valid=false;
                thingsos_chapter_close(chapter);
            } else e->section_toc_build_valid=false;
            free(extra);free(chapter);free(xml);
        }
    }
    e->section_toc_count=count;
    if(e->section_toc_next<e->spine_count)return true;
    e->section_toc_ready=true;*done=true;
    bool complete=e->section_toc_build_valid && !e->weidu_online;
    if(complete) {
        h=(nav_header_t){0x574e4331,3,e->section_toc_key,(uint32_t)count,0,0};
        h.crc=crc32(0,(const Bytef*)entries,(uInt)(count*sizeof(*entries)));
        h.seal=crc32(0,(const Bytef*)&h,offsetof(nav_header_t,seal));
        snprintf(tmp,sizeof(tmp),"%s.part",path);f=fopen(tmp,"wb");
        bool ok=f && fwrite(&h,sizeof(h),1,f)==1 && fwrite(entries,sizeof(*entries),count,f)==count && thingsos_weidu_flush(f);
        if(f && fclose(f))ok=false;
        if(ok)swap_file(tmp,path);
        remove(tmp);
    }
#ifdef ESP_PLATFORM
    ESP_LOGI("ThingsOS_weidu","Section navigation built count=%u complete=%d",(unsigned)count,complete);
#endif
    return true;
}
bool thingsos_wd_navigation(const char* root,const char* id,thingsos_epub_t* e) {
    bool done=false;
    while(!done)if(!thingsos_wd_navigation_step(root,id,e,&done))return false;
    return true;
}
bool thingsos_wd_source(const thingsos_wd_map_t* m, size_t at, uint32_t* out) {
    if (!m->source || at > m->bytes || !m->count) return false;
    size_t lo = 0, hi = m->count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (m->source[mid * 2] <= at)
            lo = mid + 1;
        else
            hi = mid;
    }
    size_t index = lo ? lo - 1 : 0;
    if (at < m->source[index * 2]) at = m->source[index * 2];
    if (m->source[index * 2 + 1] == UINT32_MAX) {
        while (index < m->count && m->source[index * 2 + 1] == UINT32_MAX) ++index;
        if (index == m->count) return false;
        *out = m->source[index * 2 + 1];
        return true;
    }
    size_t begin = m->source[index * 2];
    uint32_t source = m->source[index * 2 + 1];
    while (begin < at) {
        size_t k;
        uint32_t cpv = utf(m->text + begin, m->bytes - begin, &k);
        if (!cpv || begin + k > at) return false;
        source += cpv > 65535 ? 2 : 1;
        begin += k;
    }
    *out = source;
    return true;
}
static uint32_t monotonic_source(const thingsos_wd_map_t* m, size_t index) {
    if (m->source[index * 2 + 1] != UINT32_MAX) return m->source[index * 2 + 1];
    if (!index) return 0;
    size_t previous = index - 1;
    while (previous && m->source[previous * 2 + 1] == UINT32_MAX) --previous;
    if (m->source[previous * 2 + 1] == UINT32_MAX) return 0;
    size_t at = m->source[previous * 2], end = m->source[index * 2];
    uint32_t value = m->source[previous * 2 + 1];
    while (at < end) {
        size_t k;
        uint32_t v = utf(m->text + at, m->bytes - at, &k);
        if (!v) return value;
        value += v > 65535 ? 2 : 1;
        at += k;
    }
    return value;
}
bool thingsos_wd_local(const thingsos_wd_map_t* m, uint32_t source, size_t* out) {
    if (!m->source || !m->count) return false;
    size_t lo = 0, hi = m->count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (monotonic_source(m, mid) <= source)
            lo = mid + 1;
        else
            hi = mid;
    }
    size_t index = lo ? lo - 1 : 0;
    if (m->source[index * 2 + 1] == UINT32_MAX) {
        if (index + 1 >= m->count) return false;
        *out = m->source[(index + 1) * 2];
        return true;
    }
    size_t at = m->source[index * 2],
           end = index + 1 < m->count ? m->source[(index + 1) * 2] : m->bytes;
    uint32_t position = m->source[index * 2 + 1];
    while (at < end && position < source) {
        size_t k;
        uint32_t cpv = utf(m->text + at, m->bytes - at, &k);
        if (!cpv) return false;
        position += cpv > 65535 ? 2 : 1;
        at += k;
    }
    if (position >= source) {
        *out = at;
        return true;
    }
    if (index + 1 < m->count) {
        *out = m->source[(index + 1) * 2];
        return true;
    }
    return false;
}
bool thingsos_wd_map_range(
    const thingsos_wd_map_t* m, const thingsos_chapter_t* ch, const thingsos_wd_note_t* note,
    size_t* a, size_t* b) {
    if (!note->range || note->truncated || !note->length ||
        note->length > UINT32_MAX - note->begin || !thingsos_wd_local(m, note->begin, a) ||
        *a >= ch->bytes)
        return false;
    uint32_t src;
    if (!thingsos_wd_source(m, *a, &src) || src != note->begin) return false;
    size_t n = strlen(note->text), quote = 0;
    if (!n) return false;
    uint32_t end = note->begin + note->length, last_end = note->begin;
    *b = *a;
    /* Anchor both ends to the exact wire interval. Paragraph tags become
       synthetic newlines/indentation in cached EPUBs, while markText joins
       those paragraphs. Compare all non-whitespace characters in order at
       this fixed interval; never search elsewhere for the quote. */
    for (size_t at = *a; at < ch->bytes;) {
        size_t k;
        uint32_t cpv = utf(ch->text + at, ch->bytes - at, &k);
        if (!cpv || !thingsos_wd_source(m, at, &src)) return false;
        if (src >= end) break;
        bool space = cpv == ' ' || cpv == '\t' || cpv == '\n' || cpv == '\r';
        if (cpv != '\n' && cpv != '\r') {
            if (src < note->begin || src + (cpv > 65535 ? 2u : 1u) > end) return false;
            last_end = src + (cpv > 65535 ? 2u : 1u);
        }
        if (!space) {
            while (quote < n && (note->text[quote] == ' ' || note->text[quote] == '\t' ||
                   note->text[quote] == '\n' || note->text[quote] == '\r')) ++quote;
            size_t qk;
            if (quote == n || utf(note->text + quote, n - quote, &qk) != cpv) return false;
            quote += qk;
        }
        at += k;
        *b = at;
    }
    while (quote < n && (note->text[quote] == ' ' || note->text[quote] == '\t' ||
           note->text[quote] == '\n' || note->text[quote] == '\r')) ++quote;
    return quote == n && last_end == end && *b > *a;
}

typedef struct {
    uint32_t magic, version;
    char owner[33];
    thingsos_wd_upload_t progress;
    uint32_t checksum;
} wd_pending_t;
static bool pending_read(const char* path, const char* owner, thingsos_wd_upload_t* out) {
    wd_pending_t p;
    FILE* f = fopen(path, "rb");
    bool ok = f && fread(&p, sizeof(p), 1, f) == 1 && fgetc(f) == EOF;
    if (f) fclose(f);
    if (!ok || p.magic != 0x57505031u || p.version != 1 || strlen(owner) != 32 ||
        memcmp(p.owner, owner, 33) ||
        p.checksum != crc32(0, (const Bytef*)&p, (uInt)offsetof(wd_pending_t, checksum)) ||
        !memchr(p.progress.id, 0, sizeof(p.progress.id)) || !safe(p.progress.id) ||
        p.progress.percent > 100 || p.progress.chapter >= THINGSOS_SPINE_LIMIT ||
        p.progress.report || p.progress.seconds)
        return false;
    *out = p.progress;
    return true;
}
bool thingsos_wd_pending_load(const char* root, const char* owner, thingsos_wd_upload_t* out) {
    char path[768];
    snprintf(path, sizeof(path), "%s/progress.pending", root);
    if (pending_read(path, owner, out)) return true;
    snprintf(path, sizeof(path), "%s/progress.pending.bak", root);
    return pending_read(path, owner, out);
}
bool thingsos_wd_pending_save(const char* root, const char* owner, const thingsos_wd_upload_t* in) {
    if (strlen(owner) != 32 || !safe(in->id) || in->chapter >= THINGSOS_SPINE_LIMIT ||
        in->percent > 100)
        return false;
    wd_pending_t p = { 0 };
    p.magic = 0x57505031u;
    p.version = 1;
    memcpy(p.owner, owner, 33);
    p.progress = *in;
    p.progress.seconds = 0;
    p.progress.report = false;
    p.checksum = crc32(0, (const Bytef*)&p, (uInt)offsetof(wd_pending_t, checksum));
    char final[768], tmp[800];
    snprintf(final, sizeof(final), "%s/progress.pending", root);
    snprintf(tmp, sizeof(tmp), "%s.part", final);
    FILE* f = fopen(tmp, "wb");
    bool ok = f && fwrite(&p, sizeof(p), 1, f) == 1 && thingsos_weidu_flush(f);
    if (f && fclose(f)) ok = false;
    thingsos_wd_upload_t check;
    if (ok) ok = pending_read(tmp, owner, &check) && swap_file(tmp, final);
    remove(tmp);
    return ok;
}
void thingsos_wd_pending_clear(const char* root) {
    static const char* suffix[] = { "", ".bak", ".part" };
    char path[800];
    for (unsigned i = 0; i < 3; ++i) {
        snprintf(path, sizeof(path), "%s/progress.pending%s", root, suffix[i]);
        remove(path);
    }
}

#include "weidu_progress_queue.inc"
/* Derived intervals belong to one verified text/source map and committed note
   generation. No font/layout dependency: page pixels/hit boxes cache separately. */
typedef struct {
    uint32_t magic, version, text_crc, map_crc, manifest_seal, map_count, text_bytes;
    uint32_t cap, count, total, payload_crc;
    char owner[33], uid[64];
    uint32_t seal;
} lines_cache_header_t;
#ifndef ESP_PLATFORM
static unsigned lines_cache_hits;
unsigned thingsos_wd_host_lines_cache_hits(void) {return lines_cache_hits;}
#endif
static bool lines_cache(const char* root,const char* id,const char* uid,const manifest_t* m,
    const thingsos_wd_map_t* map,const thingsos_chapter_t* chapter,
    thingsos_text_underline_t* lines,size_t cap,size_t* count,size_t* total,bool save) {
    if(!safe(uid) || !map || !chapter || !lines || !map->source || !map->count || map->count>8192 ||
       map->text!=chapter->text || map->bytes!=chapter->bytes || cap>WD_DRAW_LIMIT)return false;
    lines_cache_header_t key={0};
    key.magic=0x574c4331;key.version=1;key.text_bytes=(uint32_t)chapter->bytes;
    key.text_crc=crc32(0,(const Bytef*)chapter->text,(uInt)chapter->bytes);
    key.map_crc=crc32(0,(const Bytef*)map->source,(uInt)(map->count*2*sizeof(uint32_t)));
    key.map_count=(uint32_t)map->count;key.manifest_seal=m->seal;key.cap=(uint32_t)cap;
    memcpy(key.owner,m->owner,sizeof(key.owner));strcpy(key.uid,uid);
    char path[768],tmp[800];snprintf(path,sizeof(path),"%s/book-%s/lines-%s.cache",root,id,uid);
    uint32_t* wire=wd_alloc((cap?cap:1)*3*sizeof(uint32_t));
    if(!wire)return false;
    bool ok=false;
    if(!save) {
        FILE* f=fopen(path,"rb");lines_cache_header_t h;
        if(f && fread(&h,sizeof(h),1,f)==1 && h.count<=cap && h.total<=2*WD_NOTE_LIMIT &&
           h.seal==crc32(0,(const Bytef*)&h,(uInt)offsetof(lines_cache_header_t,seal))) {
            key.count=h.count;key.total=h.total;key.payload_crc=h.payload_crc;key.seal=h.seal;
            ok=!memcmp(&h,&key,sizeof(h)) && fread(wire,3*sizeof(uint32_t),h.count,f)==h.count &&
                fgetc(f)==EOF && !ferror(f) && h.payload_crc==crc32(0,(const Bytef*)wire,h.count*3*sizeof(uint32_t));
            for(unsigned i=0;ok && i<h.count;++i) {
                if(wire[i*3]>=wire[i*3+1] || wire[i*3+1]>chapter->bytes || wire[i*3+2]>1)ok=false;
            }
            if(ok) {
                for(unsigned i=0;i<h.count;++i)lines[i]=(thingsos_text_underline_t){wire[i*3],wire[i*3+1],wire[i*3+2]!=0};
                *count=h.count;*total=h.total;
            }
        }
        if(f)fclose(f);
    } else if(*count<=cap && *total<=2*WD_NOTE_LIMIT) {
        for(size_t i=0;i<*count;++i) {wire[i*3]=(uint32_t)lines[i].begin;wire[i*3+1]=(uint32_t)lines[i].end;wire[i*3+2]=lines[i].mine?1:0;}
        key.count=(uint32_t)*count;key.total=(uint32_t)*total;
        key.payload_crc=crc32(0,(const Bytef*)wire,(uInt)(key.count*3*sizeof(uint32_t)));
        key.seal=crc32(0,(const Bytef*)&key,(uInt)offsetof(lines_cache_header_t,seal));
        snprintf(tmp,sizeof(tmp),"%s.part",path);FILE* f=fopen(tmp,"wb");
        ok=f && fwrite(&key,sizeof(key),1,f)==1 && fwrite(wire,3*sizeof(uint32_t),key.count,f)==key.count && thingsos_weidu_flush(f);
        if(f && fclose(f))ok=false;
        if(ok)ok=swap_file(tmp,path);
        remove(tmp);
    }
    free(wire);return ok;
}
bool thingsos_wd_lines(
    const char* root, const thingsos_weidu_session_t* session, const char* id, const char* uid,
    const thingsos_wd_map_t* map, const thingsos_chapter_t* chapter,
    thingsos_text_underline_t* lines, size_t cap, size_t* count, size_t* total) {
    *count = *total = 0;
    if (cap > WD_DRAW_LIMIT) return false;
    manifest_t m;
    if (!manifest_read(root, session, id, &m, false)) return false;
    if(lines_cache(root,id,uid,&m,map,chapter,lines,cap,count,total,false)) {
#ifdef ESP_PLATFORM
        ESP_LOGI("ThingsOS_weidu","Mapped lines cache hit count=%u",(unsigned)*count);
#else
        ++lines_cache_hits;
#endif
        return true;
    }
    thingsos_wd_note_t* n = wd_alloc(sizeof(*n));
    if (!n) return false;
    /* Batch SD reads across fixed-size records while retaining each CRC check.
       If optional buffering cannot be allocated, keep the original path. */
    char* file_buffer = wd_alloc(16384);
    bool ok = true;
    for (unsigned order = 0; order < 2 && ok; ++order) {
        unsigned kind = 1 - order;
        char path[768];
        note_path(path, sizeof(path), root, id, m.slot, kind);
        FILE* f = fopen(path, "rb");
        if (!f) {
            ok = false;
            break;
        }
        if (file_buffer) setvbuf(f, file_buffer, _IOFBF, 16384);
        for (unsigned i = 0; i < m.info.count[kind]; ++i) {
            if (fread(n, sizeof(*n), 1, f) != 1 ||
                n->integrity !=
                    crc32(0, (const Bytef*)n, (uInt)offsetof(thingsos_wd_note_t, integrity)) ||
                !memchr(n->uid, 0, sizeof(n->uid)) || !memchr(n->text, 0, sizeof(n->text))) {
                ok = false;
                break;
            }
            if (strcmp(n->uid, uid)) continue;
            note_interval(n,m.version);
            ++*total;
            size_t a, b;
            if (*count < cap && thingsos_wd_map_range(map, chapter, n, &a, &b))
                lines[(*count)++] = (thingsos_text_underline_t){ a, b, kind == 1 };
        }
        fclose(f);
    }
    free(n);
    free(file_buffer);
    if (!ok) *count = *total = 0;
    if(ok)lines_cache(root,id,uid,&m,map,chapter,lines,cap,count,total,true);
    return ok;
}

bool thingsos_wd_memory_ready(
    size_t internal_free, size_t internal_largest, size_t external_free, size_t external_largest) {
    return internal_free >= 34u * 1024u && internal_largest >= 17u * 1024u &&
           external_free >= 576u * 1024u && external_largest >= 64u * 1024u;
}

bool thingsos_wd_background_memory_ready(size_t dma_free, size_t dma_largest) {
    /* Admission budget before allocating the worker stack. Validate on Metalio
       with Wi-Fi and Bluetooth active, including fragmented DMA heaps. */
    return dma_free >= 20u * 1024u && dma_largest >= 4u * 1024u;
}

/* Range-specific thoughts, independent of whole-book reviews. Protocol evidence:
 * official reader ACTION_SYNC_BOOK_UNDERLINE_REVIEWS -> POST book/readReviews.
 * The envelope echoes the requested range and contains pageReviews. */
typedef struct {
    uint32_t magic, version;
    char owner[33];
    thingsos_wd_thought_query_t query;
    thingsos_wd_thought_info_t info;
    uint32_t seal;
} thought_header_t;
#define THOUGHT_MAGIC 0x57544831u
static bool thought_query_valid(const thingsos_wd_thought_query_t* q) {
    uint32_t uid;
    if(!q || !memchr(q->id,0,sizeof(q->id)) || !memchr(q->uid,0,sizeof(q->uid)) ||
       !memchr(q->sync,0,sizeof(q->sync)) || !safe(q->id) || !decimal(q->uid,&uid) ||
       q->begin>=q->end)return false;
    for(const char* p=q->sync;*p;++p)if(!isdigit((unsigned char)*p))return false;
    return true;
}
static bool thought_path(char* path,size_t cap,const char* root,const thingsos_weidu_session_t* s,const thingsos_wd_thought_query_t* q) {
    if(!thought_query_valid(q) || !thingsos_weidu_session_valid(s))return false;
    char owner[33],key[384],hash[33];thingsos_weidu_md5(s->vid,strlen(s->vid),owner);
    snprintf(key,sizeof(key),"%s/%s/%s/%u/%u/%u/%s",owner,q->id,q->uid,(unsigned)q->begin,(unsigned)q->end,(unsigned)q->max_idx,q->sync);
    thingsos_weidu_md5(key,strlen(key),hash);
    return snprintf(path,cap,"%s/book-%s/thought-%s.dat",root,q->id,hash)<(int)cap;
}
static bool thought_same(const thingsos_wd_thought_query_t* a,const thingsos_wd_thought_query_t* b) {
    return !strcmp(a->id,b->id)&&!strcmp(a->uid,b->uid)&&a->begin==b->begin&&a->end==b->end&&a->max_idx==b->max_idx&&!strcmp(a->sync,b->sync);
}
static FILE* thought_open(const char* root,const thingsos_weidu_session_t* s,const thingsos_wd_thought_query_t* q,thought_header_t* h) {
    char path[768],candidate[800],owner[33];
    if(!thought_path(path,sizeof(path),root,s,q))return NULL;
    thingsos_weidu_md5(s->vid,strlen(s->vid),owner);
    for(unsigned recovery=0;recovery<2;++recovery){
        snprintf(candidate,sizeof(candidate),"%s%s",path,recovery?".bak":"");FILE* f=fopen(candidate,"rb");
        bool ok=f && fread(h,sizeof(*h),1,f)==1 && h->magic==THOUGHT_MAGIC && h->version==1 &&
            h->seal==crc32(0,(const Bytef*)h,(uInt)offsetof(thought_header_t,seal)) &&
            !memcmp(owner,h->owner,33) && thought_query_valid(&h->query) && thought_same(q,&h->query) && h->info.count<=WD_THOUGHT_PAGE &&
            memchr(h->info.sync,0,sizeof(h->info.sync)) && !fseek(f,0,SEEK_END) &&
            ftell(f)==(long)(sizeof(*h)+h->info.count*sizeof(thingsos_wd_note_t));
        if(ok){fseek(f,sizeof(*h),SEEK_SET);return f;}if(f)fclose(f);
    }
    return NULL;
}
bool thingsos_wd_thought_info(const char* root,const thingsos_weidu_session_t* s,const thingsos_wd_thought_query_t* q,thingsos_wd_thought_info_t* info) {
    memset(info,0,sizeof(*info));thought_header_t h;FILE* f=thought_open(root,s,q,&h);
    if(!f)return false;
    fclose(f);*info=h.info;return true;
}
bool thingsos_wd_thought(const char* root,const thingsos_weidu_session_t* s,const thingsos_wd_thought_query_t* q,unsigned index,thingsos_wd_note_t* out) {
    thought_header_t h;FILE* f=thought_open(root,s,q,&h);bool ok=f && index<h.info.count &&
        !fseek(f,(long)(sizeof(h)+index*sizeof(*out)),SEEK_SET) && fread(out,sizeof(*out),1,f)==1 &&
        out->integrity==crc32(0,(const Bytef*)out,(uInt)offsetof(thingsos_wd_note_t,integrity)) &&
        memchr(out->text,0,sizeof(out->text)) && memchr(out->author,0,sizeof(out->author));
    if(f)fclose(f);
    if(!ok)memset(out,0,sizeof(*out));
    return ok;
}
typedef struct {
    const thingsos_wd_thought_query_t* query;
    thingsos_wd_thought_info_t info;
    thingsos_wd_note_t note;
    FILE* out;
    unsigned wrappers, records, record_depth, review_depth, author_depth;
    bool root, outer, wrapper, list, seen_list, echoed, bad_chapter, more_seen;
    thingsos_weidu_error_t error;
} thought_parser_t;
static bool thought_event(void* raw,const char* key,const char* text,char type,unsigned depth) {
    thought_parser_t* p=raw;
    if(!strcmp(key,"errCode")||!strcmp(key,"errcode")){
        if(strcmp(text,"0")){p->error=(!strcmp(text,"-2010")||!strcmp(text,"-2012"))?WD_AUTH:WD_PROTOCOL;return false;}
    }
    if(!depth && type=='{')p->root=true;
    if(depth==1 && !strcmp(key,"reviews") && type=='[')p->outer=true;
    if(p->outer && depth==2 && type=='{'){if(++p->wrappers!=1)return false;p->wrapper=true;}
    if(p->wrapper && depth==3 && !strcmp(key,"pageReviews") && type=='['){if(p->seen_list)return false;p->seen_list=p->list=true;}
    if(p->list && depth==4 && type=='{'){memset(&p->note,0,sizeof(p->note));p->record_depth=4;p->review_depth=p->author_depth=0;p->bad_chapter=false;++p->records;}
    if(p->record_depth && type=='{' && depth==5 && !strcmp(key,"review"))p->review_depth=5;
    if(p->record_depth && type=='{' && !strcmp(key,"author") && (depth==5 || (p->review_depth==5 && depth==6)))p->author_depth=depth;
    if(type=='}' && depth==p->author_depth)p->author_depth=0;
    if(type=='}' && depth==p->review_depth)p->review_depth=0;
    if(p->record_depth && depth==4 && type=='}'){
        if(!p->note.text[0] || p->bad_chapter)return false;
        /* The enclosing echoed range associates these reviews. Optional record
           chapter IDs must still agree; sibling comment/user fields cannot win. */
        if(p->info.count>=WD_THOUGHT_PAGE)return false;
        cp(p->note.uid,sizeof(p->note.uid),p->query->uid);
        p->note.integrity=crc32(0,(const Bytef*)&p->note,(uInt)offsetof(thingsos_wd_note_t,integrity));
        if(fwrite(&p->note,sizeof(p->note),1,p->out)!=1){p->error=WD_STORAGE;return false;}
        ++p->info.count;p->record_depth=0;
    }
    if(depth==3 && type==']' && !strcmp(key,"pageReviews"))p->list=false;
    if(depth==2 && type=='}')p->wrapper=false;
    if(depth==1 && type==']' && !strcmp(key,"reviews"))p->outer=false;
    if(type=='{'||type=='}'||type=='['||type==']')return true;
    if(p->wrapper && depth==3){
        uint32_t v;
        if(!strcmp(key,"range")){char expected[40];snprintf(expected,sizeof(expected),"%u-%u",(unsigned)p->query->begin,(unsigned)p->query->end);p->echoed=!strcmp(expected,text);if(!p->echoed)return false;}
        else if(!strcmp(key,"maxIdx")){if(!decimal(text,&p->info.next_idx))return false;}
        else if(!strcmp(key,"totalCount")){if(!decimal(text,&p->info.total))return false;}
        else if(!strcmp(key,"hasMore")){if(strcmp(text,"true")&&strcmp(text,"false")&&strcmp(text,"0")&&strcmp(text,"1"))return false;p->more_seen=true;p->info.more=!strcmp(text,"true")||!strcmp(text,"1");}
        else if(!strcmp(key,"synckey")){if(strlen(text)>=sizeof(p->info.sync)||type=='t')return false;for(const char* t=text;*t;++t)if(!isdigit((unsigned char)*t))return false;cp(p->info.sync,sizeof(p->info.sync),text);}
        else if(!strcmp(key,"chapterUid")){if(!decimal(text,&v)||strcmp(text,p->query->uid))return false;}
    }
    if(p->record_depth){
        /* Only record/review scalars and its direct author object. Never parse
           nested comment text as the thought being displayed. */
        if(depth==5 || (p->review_depth==5 && depth==6)){
            if(!strcmp(key,"content") && (type=='s'||type=='t')){cp(p->note.text,sizeof(p->note.text),text);p->note.truncated=type=='t';}
            else if(!strcmp(key,"htmlContent") && !p->note.text[0]){review_html(text,p->note.text,sizeof(p->note.text));p->note.truncated=type=='t';}
            else if(!strcmp(key,"chapterUid") && strcmp(text,p->query->uid))p->bad_chapter=true;
            else if(!strcmp(key,"likeCount"))decimal(text,&p->note.heat);
        }
        if(p->author_depth && depth==p->author_depth+1 && (!strcmp(key,"name")||!strcmp(key,"nick")||!strcmp(key,"nickname")) && !p->note.author[0])cp(p->note.author,sizeof(p->note.author),text);
    }
    return true;
}
thingsos_weidu_error_t thingsos_wd_thought_refresh(thingsos_weidu_client_t* c,const thingsos_wd_thought_query_t* q) {
    if(!thought_query_valid(q))return WD_PROTOCOL;
    if(!thingsos_weidu_session_valid(&c->session))return WD_AUTH;
    if(c->io.unix_time(c->io.context)<WD_SYNC_MIN_TIME)return WD_CLOCK;
    if(c->io.cancelled && c->io.cancelled(c->io.context))return WD_CANCEL;
    char final[768],tmp[800],response[800],dir[704],body[384];
    if(!thought_path(final,sizeof(final),c->root,&c->session,q))return WD_PROTOCOL;
    snprintf(tmp,sizeof(tmp),"%s.part",final);snprintf(response,sizeof(response),"%s.response",final);
    snprintf(dir,sizeof(dir),"%s/book-%s",c->root,q->id);mkdir_one(dir);
    snprintf(body,sizeof(body),"{\"bookId\":\"%s\",\"chapterUid\":%s,\"reviews\":[{\"range\":\"%u-%u\",\"maxIdx\":%u,\"count\":%u,\"synckey\":%s}]}",q->id,q->uid,(unsigned)q->begin,(unsigned)q->end,(unsigned)q->max_idx,WD_THOUGHT_PAGE,q->sync[0]?q->sync:"0");
    thingsos_weidu_error_t e=c->io.http(c->io.context,"POST","/web/book/readReviews",body,NULL,response,WD_RESPONSE_LIMIT,&c->session);
    /* A DNS failure cannot have reached this read-only endpoint. Retry once
       after a bounded backoff, honour cancellation and retain committed cache.
       Never replay uploads/duration reports or ambiguous transport failures. */
    if(e==WD_DNS) {
        remove(response);
        if(c->io.wait_ms)c->io.wait_ms(c->io.context,500);
        if(c->io.cancelled && c->io.cancelled(c->io.context))return WD_CANCEL;
        e=c->io.http(c->io.context,"POST","/web/book/readReviews",body,NULL,response,WD_RESPONSE_LIMIT,&c->session);
    }
    if(e!=WD_OK){remove(response);return e;}
    thought_parser_t* p=wd_alloc(sizeof(*p));if(!p){remove(response);return WD_MEMORY;}
    thought_header_t h={0};h.magic=THOUGHT_MAGIC;h.version=1;h.query=*q;
    thingsos_weidu_md5(c->session.vid,strlen(c->session.vid),h.owner);
    FILE* f=fopen(tmp,"wb");p->out=f;p->query=q;
    bool ok=f && fwrite(&h,sizeof(h),1,f)==1;
    if(!ok)p->error=WD_STORAGE;
    if(ok)ok=thingsos_weidu_json_events(response,thought_event,p) && p->root && p->wrappers==1 && p->seen_list && p->echoed && p->more_seen;
    if(ok && p->info.more && (p->info.next_idx<=q->max_idx || !p->info.count))ok=false;
    if(!ok)e=p->error==WD_OK?WD_PROTOCOL:p->error;
    if(ok && c->io.cancelled && c->io.cancelled(c->io.context)){ok=false;e=WD_CANCEL;}
    if(ok){h.info=p->info;h.seal=crc32(0,(const Bytef*)&h,(uInt)offsetof(thought_header_t,seal));ok=!fseek(f,0,SEEK_SET)&&fwrite(&h,sizeof(h),1,f)==1&&thingsos_weidu_flush(f);if(!ok)e=WD_STORAGE;}
    if(f && fclose(f)){ok=false;e=WD_STORAGE;}
    if(ok && !swap_file(tmp,final))e=WD_STORAGE;
#ifdef ESP_PLATFORM
    ESP_LOGI("ThingsOS_weidu","Thought page result=%d root=%d envelopes=%u list=%d range_match=%d count=%u more=%d next=%u",e,p->root,p->wrappers,p->seen_list,p->echoed,(unsigned)p->info.count,p->info.more,(unsigned)p->info.next_idx);
#endif
    remove(tmp);remove(response);free(p);return e;
}
bool thingsos_wd_line_note(const char* root,const thingsos_weidu_session_t* s,const char* id,const char* uid,const thingsos_wd_map_t* map,const thingsos_chapter_t* chapter,size_t begin,size_t end,thingsos_wd_note_t* out) {
    manifest_t m;if(!manifest_read(root,s,id,&m,false))return false;
    char path[768];FILE* f=NULL;
    bool found=false;
    for(unsigned kind=1;!found;kind=0) {
    note_path(path,sizeof(path),root,id,m.slot,kind);f=fopen(path,"rb");
    if(f)for(unsigned i=0;i<m.info.count[kind];++i){
        if(fread(out,sizeof(*out),1,f)!=1 || out->integrity!=crc32(0,(const Bytef*)out,(uInt)offsetof(thingsos_wd_note_t,integrity)) || !memchr(out->uid,0,sizeof(out->uid)) || !memchr(out->text,0,sizeof(out->text)))break;
        note_interval(out,m.version);size_t a,b;
        if(!strcmp(out->uid,uid) && thingsos_wd_map_range(map,chapter,out,&a,&b) && a==begin && b==end){found=true;break;}
    }
    if(f)fclose(f);
    if(kind==0)break;
    }
    if(!found)memset(out,0,sizeof(*out));
    return found;
}


thingsos_weidu_error_t thingsos_wd_authenticated_read(thingsos_weidu_client_t* c,bool expired,thingsos_wd_read_fn read,void* context,bool* renewed) {
    *renewed=false;
    if(!read)return WD_PROTOCOL;
    for(unsigned attempt=0;attempt<2;++attempt){
        if(c->io.cancelled && c->io.cancelled(c->io.context))return WD_CANCEL;
        if(expired){
            if(*renewed || !c->session.rt[0])return WD_AUTH;
            thingsos_weidu_error_t e=thingsos_weidu_renew(c);
            if(e!=WD_OK)return e;
            *renewed=true;
        }
        if(c->io.cancelled && c->io.cancelled(c->io.context))return WD_CANCEL;
        thingsos_weidu_error_t e=read(c,context);
        if(e!=WD_AUTH || *renewed || !c->session.rt[0])return e;
        expired=true;
    }
    return WD_AUTH;
}
#include "weidu_annotations.inc"
