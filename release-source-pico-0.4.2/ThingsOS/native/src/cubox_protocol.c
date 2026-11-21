// SPDX-License-Identifier: Apache-2.0
#include "thingsos/cubox_protocol.h"
#include "thingsos/content.h"
#include "cJSON.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool ascii_ieq(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

static void copy_str(char* dst, size_t cap, const char* src) {
    if (!dst || !cap) return;
    if (!src) {
        dst[0] = 0;
        return;
    }
    size_t n = strlen(src);
    if (n >= cap) {
        n = cap - 1;
        while (n > 0 && (((unsigned char)src[n] & 0xC0u) == 0x80u)) --n;
    }
    memcpy(dst, src, n);
    dst[n] = 0;
}

static bool token_char_ok(unsigned char c) {
    if (c <= 0x20 || c >= 0x7Fu) return false;
    return isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~' || c == '+' || c == '=' ||
           c == ':';
}

bool thingsos_cubox_server_valid(const char* server) {
    if (!server) return false;
    return ascii_ieq(server, "cubox.pro") || ascii_ieq(server, "cubox.cc");
}

bool thingsos_cubox_id_valid(const char* id) {
    if (!id || !id[0]) return false;
    size_t n = 0;
    for (const unsigned char* p = (const unsigned char*)id; *p; ++p, ++n) {
        if (n + 1 >= THINGSOS_CUBOX_ID_LEN) return false;
        if (!(isalnum(*p) || *p == '_' || *p == '-')) return false;
    }
    return n > 0;
}

bool thingsos_cubox_http_url_valid(const char* url) {
    if (!url || !url[0]) return false;
    size_t off = 0;
    if (!strncmp(url, "https://", 8))
        off = 8;
    else if (!strncmp(url, "http://", 7))
        off = 7;
    else
        return false;
    if (!url[off] || url[off] == '/') return false;
    size_t len = strlen(url);
    if (len >= THINGSOS_CUBOX_URL_LEN) return false;
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)url[i];
        if (c <= 0x20 || c == 0x7Fu) return false;
    }
    return true;
}

bool thingsos_cubox_parse_credentials(
    const char* raw_input,
    const char* preferred_server,
    char* server_out,
    size_t server_cap,
    char* token_out,
    size_t token_cap) {
    if (!raw_input || !server_out || !server_cap || !token_out || !token_cap) return false;
    server_out[0] = 0;
    token_out[0] = 0;

    while (*raw_input && (*raw_input == ' ' || *raw_input == '\t' || *raw_input == '\r' ||
                         *raw_input == '\n'))
        ++raw_input;
    size_t len = strlen(raw_input);
    while (len > 0 &&
           (raw_input[len - 1] == ' ' || raw_input[len - 1] == '\t' ||
            raw_input[len - 1] == '\r' || raw_input[len - 1] == '\n'))
        --len;
    if (!len || len >= 512) return false;

    char buf[512];
    memcpy(buf, raw_input, len);
    buf[len] = 0;

    const char* cursor = buf;
    bool has_scheme = false;
    if (!strncmp(cursor, "https://", 8)) {
        cursor += 8;
        has_scheme = true;
    } else if (!strncmp(cursor, "http://", 7)) {
        return false;
    }

    const char* server = NULL;
    const char* token = NULL;
    if (has_scheme || strchr(cursor, '/') != NULL) {
        const char* slash = strchr(cursor, '/');
        if (!slash) return false;
        size_t host_len = (size_t)(slash - cursor);
        char host[48];
        if (!host_len || host_len >= sizeof(host)) return false;
        memcpy(host, cursor, host_len);
        host[host_len] = 0;
        if (ascii_ieq(host, "cubox.pro"))
            server = "cubox.pro";
        else if (ascii_ieq(host, "cubox.cc"))
            server = "cubox.cc";
        else
            return false;

        if (strncmp(slash, "/c/api/save/", 12) != 0) return false;
        const char* t = slash + 12;
        size_t tlen = strlen(t);
        const char* q = strchr(t, '?');
        if (q && (size_t)(q - t) < tlen) tlen = (size_t)(q - t);
        const char* frag = strchr(t, '#');
        if (frag && (size_t)(frag - t) < tlen) tlen = (size_t)(frag - t);
        while (tlen > 0 && t[tlen - 1] == '/') --tlen;
        if (tlen < 8 || tlen >= THINGSOS_CUBOX_TOKEN_LEN || tlen >= token_cap) return false;
        for (size_t i = 0; i < tlen; ++i) {
            if (!token_char_ok((unsigned char)t[i])) return false;
        }
        memcpy(token_out, t, tlen);
        token_out[tlen] = 0;
        copy_str(server_out, server_cap, server);
        return true;
    }

    if (preferred_server && preferred_server[0]) {
        if (ascii_ieq(preferred_server, "cubox.pro"))
            server = "cubox.pro";
        else if (ascii_ieq(preferred_server, "cubox.cc"))
            server = "cubox.cc";
        else
            return false;
    } else {
        server = "cubox.pro";
    }

    token = cursor;
    size_t tlen = strlen(token);
    if (tlen < 8 || tlen >= THINGSOS_CUBOX_TOKEN_LEN || tlen >= token_cap) return false;
    for (size_t i = 0; i < tlen; ++i) {
        if (!token_char_ok((unsigned char)token[i])) return false;
    }
    copy_str(server_out, server_cap, server);
    memcpy(token_out, token, tlen + 1);
    return true;
}

const char* thingsos_cubox_image_ext(const void* data, size_t size) {
    if (!data || size < 6) return NULL;
    const unsigned char* b = (const unsigned char*)data;
    if (b[0] == 0xFFu && b[1] == 0xD8u && b[2] == 0xFFu) return ".jpg";
    if (size >= 8 && b[0] == 0x89u && b[1] == 'P' && b[2] == 'N' && b[3] == 'G' &&
        b[4] == 0x0Du && b[5] == 0x0Au && b[6] == 0x1Au && b[7] == 0x0Au)
        return ".png";
    if (!memcmp(b, "GIF87a", 6) || !memcmp(b, "GIF89a", 6)) return ".gif";
    if (size >= 12 && !memcmp(b, "RIFF", 4) && !memcmp(b + 8, "WEBP", 4)) return ".webp";
    return NULL;
}

static bool emit_cjson_string(cJSON* root, char* out, size_t cap) {
    if (!root || !out || !cap) {
        cJSON_Delete(root);
        return false;
    }
    bool ok = cJSON_PrintPreallocated(root, out, (int)cap, false) != 0;
    cJSON_Delete(root);
    return ok;
}

bool thingsos_cubox_build_filter_request(
    thingsos_cubox_filter_t filter,
    const char* folder_id,
    const char* query,
    const char* last_card_id,
    unsigned page,
    unsigned limit,
    char* out,
    size_t cap) {
    if (!out || !cap || limit == 0 || limit > 100) return false;
    if (folder_id && folder_id[0] && !thingsos_cubox_id_valid(folder_id)) return false;
    if (last_card_id && last_card_id[0] && !thingsos_cubox_id_valid(last_card_id)) return false;

    cJSON* root = cJSON_CreateObject();
    if (!root) return false;
    cJSON_AddBoolToObject(root, "archived", false);
    if (filter == THINGSOS_CUBOX_FILTER_UNREAD) {
        cJSON_AddBoolToObject(root, "read", false);
    } else if (filter == THINGSOS_CUBOX_FILTER_STARRED) {
        cJSON_AddBoolToObject(root, "starred", true);
    }
    if (folder_id && folder_id[0]) {
        cJSON* arr = cJSON_CreateArray();
        if (!arr) {
            cJSON_Delete(root);
            return false;
        }
        cJSON_AddItemToArray(arr, cJSON_CreateString(folder_id));
        cJSON_AddItemToObject(root, "folder_filters", arr);
    }
    cJSON_AddNumberToObject(root, "limit", (double)limit);
    if (query && query[0]) {
        cJSON_AddStringToObject(root, "keyword", query);
        cJSON_AddNumberToObject(root, "page", (double)(page ? page : 1));
    } else if (last_card_id && last_card_id[0]) {
        cJSON_AddStringToObject(root, "last_card_id", last_card_id);
    }
    return emit_cjson_string(root, out, cap);
}

bool thingsos_cubox_build_update_request(
    const char* card_id,
    int set_read,
    int set_starred,
    char* out,
    size_t cap) {
    if (!out || !cap || !thingsos_cubox_id_valid(card_id)) return false;
    if (set_read < 0 && set_starred < 0) return false;
    cJSON* root = cJSON_CreateObject();
    if (!root) return false;
    cJSON_AddStringToObject(root, "id", card_id);
    if (set_read >= 0) cJSON_AddBoolToObject(root, "read", set_read != 0);
    if (set_starred >= 0) cJSON_AddBoolToObject(root, "starred", set_starred != 0);
    return emit_cjson_string(root, out, cap);
}

bool thingsos_cubox_build_archive_request(const char* card_id, char* out, size_t cap) {
    if (!out || !cap || !thingsos_cubox_id_valid(card_id)) return false;
    cJSON* root = cJSON_CreateObject();
    cJSON* arr = cJSON_CreateArray();
    if (!root || !arr) {
        cJSON_Delete(root);
        cJSON_Delete(arr);
        return false;
    }
    cJSON_AddItemToArray(arr, cJSON_CreateString(card_id));
    cJSON_AddItemToObject(root, "card_ids", arr);
    return emit_cjson_string(root, out, cap);
}

static const char* json_str(const cJSON* obj, const char* key) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(item) && item->valuestring ? item->valuestring : "";
}

static bool json_bool(const cJSON* obj, const char* key, bool fallback) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsBool(item)) return cJSON_IsTrue(item);
    return fallback;
}

static cJSON* parse_envelope_root(
    const char* json,
    size_t length,
    int* code_out,
    char* message_out,
    size_t message_cap) {
    if (code_out) *code_out = -1;
    if (message_out && message_cap) message_out[0] = 0;
    if (!json || !length) return NULL;
    cJSON* root = cJSON_ParseWithLengthOpts(json, length, NULL, false);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return NULL;
    }
    const cJSON* code = cJSON_GetObjectItemCaseSensitive(root, "code");
    if (!cJSON_IsNumber(code)) {
        cJSON_Delete(root);
        return NULL;
    }
    if (code_out) *code_out = code->valueint;
    if (message_out && message_cap) {
        copy_str(message_out, message_cap, json_str(root, "message"));
    }
    return root;
}

bool thingsos_cubox_parse_envelope(
    const char* json,
    size_t length,
    int* code_out,
    char* message_out,
    size_t message_cap) {
    int code = -1;
    cJSON* root = parse_envelope_root(json, length, &code, message_out, message_cap);
    if (!root) return false;
    if (code_out) *code_out = code;
    cJSON_Delete(root);
    return code == 200;
}

bool thingsos_cubox_parse_folders(
    const char* json,
    size_t length,
    thingsos_cubox_folder_t* out,
    unsigned capacity,
    unsigned* count_out,
    int* code_out,
    char* message_out,
    size_t message_cap) {
    if (count_out) *count_out = 0;
    int code = -1;
    cJSON* root = parse_envelope_root(json, length, &code, message_out, message_cap);
    if (code_out) *code_out = code;
    if (!root) return false;
    if (code != 200) {
        cJSON_Delete(root);
        return false;
    }
    const cJSON* data = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (!cJSON_IsArray(data)) {
        cJSON_Delete(root);
        return true;
    }
    unsigned n = 0;
    const cJSON* item = NULL;
    cJSON_ArrayForEach(item, data) {
        if (n >= capacity) break;
        if (!cJSON_IsObject(item)) continue;
        const char* id = json_str(item, "id");
        if (!thingsos_cubox_id_valid(id)) continue;
        thingsos_cubox_folder_t* f = out + n;
        memset(f, 0, sizeof(*f));
        copy_str(f->id, sizeof(f->id), id);
        copy_str(f->name, sizeof(f->name), json_str(item, "name"));
        copy_str(f->nested_name, sizeof(f->nested_name), json_str(item, "nested_name"));
        copy_str(f->parent_id, sizeof(f->parent_id), json_str(item, "parent_id"));
        f->uncategorized = json_bool(item, "uncategorized", false);
        ++n;
    }
    if (count_out) *count_out = n;
    cJSON_Delete(root);
    return true;
}

static void fill_card(const cJSON* item, thingsos_cubox_card_t* c) {
    memset(c, 0, sizeof(*c));
    copy_str(c->id, sizeof(c->id), json_str(item, "id"));
    copy_str(c->title, sizeof(c->title), json_str(item, "title"));
    copy_str(c->article_title, sizeof(c->article_title), json_str(item, "article_title"));
    copy_str(c->description, sizeof(c->description), json_str(item, "description"));
    copy_str(c->domain, sizeof(c->domain), json_str(item, "domain"));
    copy_str(c->url, sizeof(c->url), json_str(item, "url"));
    copy_str(c->create_time, sizeof(c->create_time), json_str(item, "create_time"));
    copy_str(c->update_time, sizeof(c->update_time), json_str(item, "update_time"));
    c->read = json_bool(item, "read", false);
    c->starred = json_bool(item, "starred", false);
    c->archived = json_bool(item, "archived", false);

    const cJSON* folder = cJSON_GetObjectItemCaseSensitive(item, "folder");
    if (cJSON_IsObject(folder)) {
        const char* fid = json_str(folder, "id");
        if (thingsos_cubox_id_valid(fid)) copy_str(c->folder_id, sizeof(c->folder_id), fid);
        const char* fname = json_str(folder, "nested_name");
        if (!fname[0]) fname = json_str(folder, "name");
        copy_str(c->folder_name, sizeof(c->folder_name), fname);
    }

    const cJSON* tags = cJSON_GetObjectItemCaseSensitive(item, "tags");
    if (cJSON_IsArray(tags)) {
        size_t used = 0;
        const cJSON* tag = NULL;
        cJSON_ArrayForEach(tag, tags) {
            if (!cJSON_IsObject(tag)) continue;
            const char* tname = json_str(tag, "name");
            if (!tname[0]) tname = json_str(tag, "nested_name");
            if (!tname[0]) continue;
            if (used > 0 && used + 3 < sizeof(c->tag_summary)) {
                memcpy(c->tag_summary + used, " · ", 4);
                used += 3;
            }
            size_t add = strlen(tname);
            if (used + add >= sizeof(c->tag_summary)) break;
            memcpy(c->tag_summary + used, tname, add + 1);
            used += add;
        }
    }
}

bool thingsos_cubox_parse_cards(
    const char* json,
    size_t length,
    thingsos_cubox_card_t* out,
    unsigned capacity,
    unsigned* count_out,
    int* code_out,
    char* message_out,
    size_t message_cap) {
    if (count_out) *count_out = 0;
    int code = -1;
    cJSON* root = parse_envelope_root(json, length, &code, message_out, message_cap);
    if (code_out) *code_out = code;
    if (!root) return false;
    if (code != 200) {
        cJSON_Delete(root);
        return false;
    }
    const cJSON* data = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (!cJSON_IsArray(data)) {
        cJSON_Delete(root);
        return true;
    }
    unsigned n = 0;
    const cJSON* item = NULL;
    cJSON_ArrayForEach(item, data) {
        if (n >= capacity) break;
        if (!cJSON_IsObject(item)) continue;
        const char* id = json_str(item, "id");
        if (!thingsos_cubox_id_valid(id)) continue;
        fill_card(item, out + n);
        ++n;
    }
    if (count_out) *count_out = n;
    cJSON_Delete(root);
    return true;
}

bool thingsos_cubox_parse_detail(
    const char* json,
    size_t length,
    thingsos_cubox_detail_t* detail_out,
    char* content_out,
    size_t content_cap,
    size_t* content_len_out,
    int* code_out,
    char* message_out,
    size_t message_cap) {
    if (content_len_out) *content_len_out = 0;
    if (content_out && content_cap) content_out[0] = 0;
    if (detail_out) memset(detail_out, 0, sizeof(*detail_out));

    int code = -1;
    cJSON* root = parse_envelope_root(json, length, &code, message_out, message_cap);
    if (code_out) *code_out = code;
    if (!root) return false;
    if (code != 200) {
        cJSON_Delete(root);
        return false;
    }
    const cJSON* data = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (!cJSON_IsObject(data)) {
        cJSON_Delete(root);
        return false;
    }
    const char* id = json_str(data, "id");
    if (!thingsos_cubox_id_valid(id)) {
        cJSON_Delete(root);
        return false;
    }

    if (detail_out) {
        fill_card(data, &detail_out->card);
        copy_str(detail_out->author, sizeof(detail_out->author), json_str(data, "author"));

        const cJSON* insight = cJSON_GetObjectItemCaseSensitive(data, "insight");
        if (cJSON_IsObject(insight)) {
            const char* summary = json_str(insight, "summary");
            copy_str(detail_out->summary, sizeof(detail_out->summary), summary);
            const cJSON* qas = cJSON_GetObjectItemCaseSensitive(insight, "qas");
            if (cJSON_IsArray(qas)) {
                const cJSON* qa = NULL;
                cJSON_ArrayForEach(qa, qas) {
                    if (detail_out->qa_count >= THINGSOS_CUBOX_QA_MAX) break;
                    if (!cJSON_IsObject(qa)) continue;
                    const char* q = json_str(qa, "q");
                    const char* a = json_str(qa, "a");
                    if (!q[0] && !a[0]) continue;
                    thingsos_cubox_qa_t* dst = &detail_out->qas[detail_out->qa_count++];
                    copy_str(dst->q, sizeof(dst->q), q);
                    copy_str(dst->a, sizeof(dst->a), a);
                }
            }
            detail_out->has_insight = detail_out->summary[0] != 0 || detail_out->qa_count > 0;
        }

        const cJSON* anns = cJSON_GetObjectItemCaseSensitive(data, "annotations");
        if (cJSON_IsArray(anns)) {
            const cJSON* ann = NULL;
            cJSON_ArrayForEach(ann, anns) {
                if (detail_out->annotation_count >= THINGSOS_CUBOX_ANNOTATION_MAX) break;
                if (!cJSON_IsObject(ann)) continue;
                const char* text = json_str(ann, "text");
                const char* note = json_str(ann, "note");
                if (!text[0] && !note[0]) continue;
                thingsos_cubox_annotation_t* dst =
                    &detail_out->annotations[detail_out->annotation_count++];
                copy_str(dst->text, sizeof(dst->text), text);
                copy_str(dst->note, sizeof(dst->note), note);
                copy_str(dst->color, sizeof(dst->color), json_str(ann, "color"));
                copy_str(dst->create_time, sizeof(dst->create_time), json_str(ann, "create_time"));
            }
        }
    }

    if (content_out && content_cap) {
        const char* content = json_str(data, "content");
        copy_str(content_out, content_cap, content);
        if (content_len_out) *content_len_out = strlen(content_out);
    }

    cJSON_Delete(root);
    return true;
}

static bool md_escaped(const char* s, size_t i) {
    size_t bs = 0;
    while (i > 0 && s[i - 1] == '\\') {
        ++bs;
        --i;
    }
    return (bs & 1u) != 0;
}

static size_t fence_run(const char* s, size_t len, size_t line_start, char* ch_out) {
    size_t i = line_start;
    while (i < len && i < line_start + 3 && s[i] == ' ') ++i;
    if (i >= len || (s[i] != '`' && s[i] != '~')) return 0;
    char ch = s[i];
    size_t run = 0;
    while (i + run < len && s[i + run] == ch) ++run;
    if (run < 3) return 0;
    if (ch_out) *ch_out = ch;
    return run;
}

static bool extract_md_target(
    const char* inner,
    size_t inner_len,
    char* url_out,
    size_t url_cap) {
    if (!inner || !url_out || !url_cap) return false;
    url_out[0] = 0;
    size_t a = 0;
    while (a < inner_len && (inner[a] == ' ' || inner[a] == '\t' || inner[a] == '\r' ||
                             inner[a] == '\n'))
        ++a;
    if (a >= inner_len) return false;
    size_t b = inner_len;
    if (inner[a] == '<') {
        ++a;
        b = a;
        while (b < inner_len && inner[b] != '>') ++b;
        if (b >= inner_len) return false;
    } else {
        b = a;
        while (b < inner_len && inner[b] != ' ' && inner[b] != '\t' && inner[b] != '\r' &&
               inner[b] != '\n')
            ++b;
    }
    size_t n = b - a;
    if (!n || n >= url_cap) return false;
    memcpy(url_out, inner + a, n);
    url_out[n] = 0;
    return thingsos_cubox_http_url_valid(url_out);
}

static bool parse_md_image_at(
    const char* s,
    size_t len,
    size_t pos,
    size_t* end_out,
    size_t* alt_start,
    size_t* alt_len,
    char* url_out,
    size_t url_cap) {
    if (pos + 4 >= len || s[pos] != '!' || s[pos + 1] != '[') return false;
    if (pos > 0 && md_escaped(s, pos)) return false;
    size_t close = pos + 2;
    while (close < len && s[close] != '\n' && (s[close] != ']' || md_escaped(s, close))) ++close;
    if (close + 1 >= len || s[close] != ']' || s[close + 1] != '(') return false;

    size_t end = close + 2;
    unsigned depth = 1;
    while (end < len && s[end] != '\n' && depth > 0) {
        if (!md_escaped(s, end)) {
            if (s[end] == '(')
                ++depth;
            else if (s[end] == ')') {
                --depth;
                if (depth == 0) break;
            }
        }
        ++end;
    }
    if (end >= len || s[end] != ')' || depth != 0) return false;

    size_t inner_len = end - (close + 2);
    if (!extract_md_target(s + close + 2, inner_len, url_out, url_cap)) {
        url_out[0] = 0;
    }
    *alt_start = pos + 2;
    *alt_len = close - (pos + 2);
    *end_out = end + 1;
    return true;
}

unsigned thingsos_cubox_extract_images(
    const char* markdown,
    thingsos_cubox_image_ref_t* refs,
    unsigned max_refs) {
    if (!markdown || !refs || !max_refs) return 0;
    if (max_refs > THINGSOS_IMAGE_LIMIT) max_refs = THINGSOS_IMAGE_LIMIT;

    unsigned count = 0;
    size_t len = strlen(markdown);
    bool fenced = false;
    char fence_ch = 0;
    size_t fence_width = 0;

    for (size_t line = 0; line < len;) {
        size_t line_end = line;
        while (line_end < len && markdown[line_end] != '\n') ++line_end;
        char fch = 0;
        size_t frun = fence_run(markdown, line_end, line, &fch);
        if (frun) {
            if (!fenced) {
                fenced = true;
                fence_ch = fch;
                fence_width = frun;
            } else if (fch == fence_ch && frun >= fence_width) {
                size_t tail = line;
                while (tail < line_end && markdown[tail] == ' ') ++tail;
                tail += frun;
                while (tail < line_end && (markdown[tail] == ' ' || markdown[tail] == '\t' ||
                                           markdown[tail] == '\r'))
                    ++tail;
                if (tail == line_end) fenced = false;
            }
        } else if (!fenced) {
            for (size_t i = line; i < line_end;) {
                size_t end = 0, alt_s = 0, alt_l = 0;
                char url[THINGSOS_CUBOX_URL_LEN];
                if (parse_md_image_at(
                        markdown, line_end, i, &end, &alt_s, &alt_l, url, sizeof(url))) {
                    if (url[0]) {
                        bool exists = false;
                        for (unsigned k = 0; k < count; ++k) {
                            if (!strcmp(refs[k].url, url)) {
                                exists = true;
                                break;
                            }
                        }
                        if (!exists && count < max_refs) {
                            memset(&refs[count], 0, sizeof(refs[count]));
                            copy_str(refs[count].url, sizeof(refs[count].url), url);
                            snprintf(
                                refs[count].filename, sizeof(refs[count].filename), "img_%02u.jpg",
                                count);
                            ++count;
                        }
                    }
                    i = end;
                    continue;
                }
                ++i;
            }
        }
        line = line_end < len ? line_end + 1 : len;
    }
    return count;
}

typedef struct {
    char* out;
    size_t cap;
    size_t len;
    bool truncated;
} md_writer_t;

static void md_append_n(md_writer_t* w, const char* s, size_t n) {
    if (!w || !w->out || w->truncated || !n) return;
    static const char note[] = "\n\n...（正文过长，已截断）\n";
    size_t reserve = sizeof(note) - 1;
    if (w->cap <= reserve + 1) {
        w->truncated = true;
        return;
    }
    size_t limit = w->cap - reserve - 1;
    if (w->len + n > limit) {
        size_t take = limit > w->len ? limit - w->len : 0;
        while (take > 0 && (((unsigned char)s[take] & 0xC0u) == 0x80u)) --take;
        if (take > 0) {
            memcpy(w->out + w->len, s, take);
            w->len += take;
        }
        memcpy(w->out + w->len, note, reserve);
        w->len += reserve;
        w->out[w->len] = 0;
        w->truncated = true;
        return;
    }
    memcpy(w->out + w->len, s, n);
    w->len += n;
    w->out[w->len] = 0;
}

static void md_append(md_writer_t* w, const char* s) {
    if (s) md_append_n(w, s, strlen(s));
}

static void md_append_sanitized_alt(md_writer_t* w, const char* s, size_t n) {
    for (size_t i = 0; i < n && !w->truncated; ++i) {
        char c = s[i];
        if (c == '\r' || c == '\n' || c == '[' || c == ']') c = ' ';
        md_append_n(w, &c, 1);
    }
}

bool thingsos_cubox_build_article_markdown(
    const thingsos_cubox_detail_t* detail,
    const char* raw_markdown,
    const thingsos_cubox_image_ref_t* refs,
    unsigned ref_count,
    char* out,
    size_t cap,
    size_t* written_out) {
    if (!detail || !out || cap < 256) return false;
    out[0] = 0;
    md_writer_t w = {.out = out, .cap = cap, .len = 0, .truncated = false};

    const char* title = detail->card.title[0] ? detail->card.title
                      : detail->card.article_title[0] ? detail->card.article_title
                      : "未命名收藏";
    md_append(&w, "# ");
    md_append(&w, title);
    md_append(&w, "\n\n");

    if (detail->card.domain[0]) {
        md_append(&w, "- 来源：");
        md_append(&w, detail->card.domain);
        md_append(&w, "\n");
    }
    if (detail->author[0]) {
        md_append(&w, "- 作者：");
        md_append(&w, detail->author);
        md_append(&w, "\n");
    }
    if (detail->card.folder_name[0]) {
        md_append(&w, "- 文件夹：");
        md_append(&w, detail->card.folder_name);
        md_append(&w, "\n");
    }
    if (detail->card.tag_summary[0]) {
        md_append(&w, "- 标签：");
        md_append(&w, detail->card.tag_summary);
        md_append(&w, "\n");
    }
    if (detail->card.create_time[0]) {
        md_append(&w, "- 收藏时间：");
        md_append(&w, detail->card.create_time);
        md_append(&w, "\n");
    }
    if (detail->card.url[0]) {
        md_append(&w, "- 原文：");
        md_append(&w, detail->card.url);
        md_append(&w, "\n");
    }
    md_append(&w, "\n");

    if (detail->card.description[0]) {
        md_append(&w, "> ");
        md_append(&w, detail->card.description);
        md_append(&w, "\n\n");
    }

    if (detail->has_insight && (detail->summary[0] || detail->qa_count > 0)) {
        md_append(&w, "## AI 解读\n\n");
        if (detail->summary[0]) {
            md_append(&w, detail->summary);
            md_append(&w, "\n\n");
        }
        for (unsigned i = 0; i < detail->qa_count && !w.truncated; ++i) {
            if (detail->qas[i].q[0]) {
                md_append(&w, "**问：");
                md_append(&w, detail->qas[i].q);
                md_append(&w, "**\n");
            }
            if (detail->qas[i].a[0]) {
                md_append(&w, detail->qas[i].a);
                md_append(&w, "\n\n");
            }
        }
    }

    md_append(&w, "## 正文\n\n");
    if (raw_markdown && raw_markdown[0]) {
        size_t len = strlen(raw_markdown);
        bool fenced = false;
        char fence_ch = 0;
        size_t fence_width = 0;

        for (size_t line = 0; line < len && !w.truncated;) {
            size_t line_end = line;
            while (line_end < len && raw_markdown[line_end] != '\n') ++line_end;

            char fch = 0;
            size_t frun = fence_run(raw_markdown, line_end, line, &fch);
            if (frun) {
                if (!fenced) {
                    fenced = true;
                    fence_ch = fch;
                    fence_width = frun;
                } else if (fch == fence_ch && frun >= fence_width) {
                    size_t tail = line;
                    while (tail < line_end && raw_markdown[tail] == ' ') ++tail;
                    tail += frun;
                    while (tail < line_end && (raw_markdown[tail] == ' ' ||
                                               raw_markdown[tail] == '\t' ||
                                               raw_markdown[tail] == '\r'))
                        ++tail;
                    if (tail == line_end) fenced = false;
                }
                md_append_n(&w, raw_markdown + line, line_end - line);
            } else if (fenced) {
                md_append_n(&w, raw_markdown + line, line_end - line);
            } else {
                for (size_t i = line; i < line_end && !w.truncated;) {
                    size_t end = 0, alt_s = 0, alt_l = 0;
                    char url[THINGSOS_CUBOX_URL_LEN];
                    if (parse_md_image_at(
                            raw_markdown, line_end, i, &end, &alt_s, &alt_l, url, sizeof(url))) {
                        const thingsos_cubox_image_ref_t* match = NULL;
                        if (url[0] && refs) {
                            for (unsigned k = 0; k < ref_count; ++k) {
                                if (!strcmp(refs[k].url, url)) {
                                    match = &refs[k];
                                    break;
                                }
                            }
                        }
                        if (match && match->downloaded && match->filename[0]) {
                            if (w.len > 0 && w.out[w.len - 1] != '\n') md_append(&w, "\n");
                            md_append(&w, "![");
                            md_append_sanitized_alt(&w, raw_markdown + alt_s, alt_l);
                            md_append(&w, "](");
                            md_append(&w, match->filename);
                            md_append(&w, ")\n");
                        } else {
                            md_append(&w, "[图片");
                            if (alt_l > 0) {
                                md_append(&w, ": ");
                                md_append_sanitized_alt(&w, raw_markdown + alt_s, alt_l);
                            }
                            md_append(&w, "]");
                        }
                        i = end;
                        continue;
                    }
                    md_append_n(&w, raw_markdown + i, 1);
                    ++i;
                }
            }
            if (line_end < len && !w.truncated) md_append_n(&w, "\n", 1);
            line = line_end < len ? line_end + 1 : len;
        }
    } else {
        md_append(&w, "（暂无正文内容，仅保存摘要与元信息）\n");
    }

    if (detail->annotation_count > 0 && !w.truncated) {
        md_append(&w, "\n## 标注与笔记\n\n");
        for (unsigned i = 0; i < detail->annotation_count && !w.truncated; ++i) {
            const thingsos_cubox_annotation_t* ann = &detail->annotations[i];
            if (ann->text[0]) {
                md_append(&w, "> ");
                md_append(&w, ann->text);
                md_append(&w, "\n");
            }
            if (ann->note[0]) {
                md_append(&w, "笔记：");
                md_append(&w, ann->note);
                md_append(&w, "\n");
            }
            if (ann->create_time[0]) {
                md_append(&w, "时间：");
                md_append(&w, ann->create_time);
                md_append(&w, "\n");
            }
            md_append(&w, "\n");
        }
    }

    if (written_out) *written_out = w.len;
    return true;
}

uint32_t thingsos_cubox_crc32(const void* data, size_t size) {
    const uint8_t* p = (const uint8_t*)data;
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= p[i];
        for (unsigned bit = 0; bit < 8; ++bit) {
            crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

#define CUBOX_FOLDER_MAGIC 0x43424631u
#define CUBOX_CARD_MAGIC 0x43424331u

typedef struct {
    uint32_t magic;
    uint32_t count;
    uint32_t record_size;
    uint32_t crc32;
} cubox_blob_header_t;

static bool save_blob(
    const char* path,
    uint32_t magic,
    const void* items,
    unsigned count,
    size_t record_size) {
    if (!path || !path[0] || (count > 0 && !items)) return false;
    char tmp[520];
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp)) return false;
    FILE* f = fopen(tmp, "wb");
    if (!f) return false;

    size_t payload = (size_t)count * record_size;
    cubox_blob_header_t h = {
        .magic = magic,
        .count = count,
        .record_size = (uint32_t)record_size,
        .crc32 = payload ? thingsos_cubox_crc32(items, payload) : 0u};

    bool ok = fwrite(&h, sizeof(h), 1, f) == 1 &&
              (payload == 0 || fwrite(items, record_size, count, f) == count);
    if (fflush(f) != 0) ok = false;
    if (fclose(f) != 0) ok = false;
    if (!ok) {
        remove(tmp);
        return false;
    }
    remove(path);
    if (rename(tmp, path) != 0) {
        remove(tmp);
        return false;
    }
    return true;
}

static bool load_blob(
    const char* path,
    uint32_t magic,
    void* items,
    unsigned capacity,
    size_t record_size,
    unsigned* count_out) {
    if (count_out) *count_out = 0;
    if (!path || !path[0] || !items || !capacity) return false;
    FILE* f = fopen(path, "rb");
    if (!f) return false;

    cubox_blob_header_t h = {0};
    bool ok = fread(&h, sizeof(h), 1, f) == 1 && h.magic == magic &&
              h.record_size == record_size && h.count <= capacity;
    size_t payload = (size_t)h.count * record_size;
    if (ok && payload > 0) ok = fread(items, record_size, h.count, f) == h.count;
    fclose(f);
    if (!ok) return false;
    uint32_t crc = payload ? thingsos_cubox_crc32(items, payload) : 0u;
    if (crc != h.crc32) return false;
    if (count_out) *count_out = h.count;
    return true;
}

bool thingsos_cubox_save_folders(
    const char* path,
    const thingsos_cubox_folder_t* folders,
    unsigned count) {
    if (count > THINGSOS_CUBOX_FOLDER_MAX) return false;
    return save_blob(path, CUBOX_FOLDER_MAGIC, folders, count, sizeof(*folders));
}

bool thingsos_cubox_load_folders(
    const char* path,
    thingsos_cubox_folder_t* folders,
    unsigned capacity,
    unsigned* count_out) {
    return load_blob(path, CUBOX_FOLDER_MAGIC, folders, capacity, sizeof(*folders), count_out);
}

bool thingsos_cubox_save_cards(
    const char* path,
    const thingsos_cubox_card_t* cards,
    unsigned count) {
    return save_blob(path, CUBOX_CARD_MAGIC, cards, count, sizeof(*cards));
}

bool thingsos_cubox_load_cards(
    const char* path,
    thingsos_cubox_card_t* cards,
    unsigned capacity,
    unsigned* count_out) {
    return load_blob(path, CUBOX_CARD_MAGIC, cards, capacity, sizeof(*cards), count_out);
}
