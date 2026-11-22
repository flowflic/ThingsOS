// SPDX-License-Identifier: Apache-2.0
/* flomo remote MCP protocol helpers: request bodies, SSE/JSON-RPC unwrapping,
   memo/tag parsing, display text cleanup and the SD list cache format. */
#include "thingsos/flomo_protocol.h"
#include "cJSON.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void copy_utf8(char* dst, size_t cap, const char* src) {
    if (!dst || !cap) return;
    if (!src) src = "";
    size_t n = strlen(src);
    if (n >= cap) {
        n = cap - 1;
        while (n > 0 && (((unsigned char)src[n] & 0xC0u) == 0x80u)) --n;
    }
    memcpy(dst, src, n);
    dst[n] = 0;
}

static bool token_char_ok(unsigned char c) {
    return c > 0x20 && c < 0x7F && c != '"' && c != '\'' && c != '\\' && c != ',' && c != '}' &&
           c != '{' && c != '<' && c != '>';
}

bool thingsos_flomo_parse_token(const char* raw, char* out, size_t cap) {
    if (!raw || !out || cap < 2) return false;
    out[0] = 0;
    const char* p = raw;
    /* Pasted config: take the text after the last "Bearer". */
    for (const char* q = raw; (q = strstr(q, "Bearer")) != NULL; ++q) p = q + 6;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == '"' || *p == '\'' ||
           *p == ':' || *p == '=')
        ++p;
    size_t n = 0;
    while (token_char_ok((unsigned char)p[n])) ++n;
    const char* rest = p + n;
    while (*rest == ' ' || *rest == '\t' || *rest == '\r' || *rest == '\n' || *rest == '"' ||
           *rest == '\'' || *rest == ',' || *rest == '}')
        ++rest;
    /* A bare token must be the whole input; config text may continue. */
    if (p == raw && *rest) return false;
    if (n < 8 || n >= cap || n >= THINGSOS_FLOMO_TOKEN_LEN) return false;
    memcpy(out, p, n);
    out[n] = 0;
    return true;
}

bool thingsos_flomo_id_valid(const char* id) {
    if (!id || !id[0]) return false;
    size_t n = 0;
    for (; id[n]; ++n) {
        unsigned char c = (unsigned char)id[n];
        if (n + 1 >= THINGSOS_FLOMO_ID_LEN || !(isalnum(c) || c == '-' || c == '_')) return false;
    }
    return true;
}

static bool print_call(unsigned rpc_id, const char* tool, cJSON* args, char* out, size_t cap) {
    cJSON* root = cJSON_CreateObject();
    cJSON* params = cJSON_CreateObject();
    bool ok = root && params && args;
    if (ok) {
        cJSON_AddStringToObject(root, "jsonrpc", "2.0");
        cJSON_AddNumberToObject(root, "id", rpc_id);
        cJSON_AddStringToObject(root, "method", "tools/call");
        cJSON_AddStringToObject(params, "name", tool);
        cJSON_AddItemToObject(params, "arguments", args);
        args = NULL;
        cJSON_AddItemToObject(root, "params", params);
        params = NULL;
        ok = cJSON_PrintPreallocated(root, out, (int)cap, false) != 0;
    }
    cJSON_Delete(args);
    cJSON_Delete(params);
    cJSON_Delete(root);
    if (!ok && out && cap) out[0] = 0;
    return ok;
}

bool thingsos_flomo_build_search(
    unsigned rpc_id, const char* keywords, const char* tag, const char* end_date,
    unsigned limit, char* out, size_t cap) {
    cJSON* args = cJSON_CreateObject();
    if (!args) return false;
    if (keywords && keywords[0]) cJSON_AddStringToObject(args, "keywords", keywords);
    if (tag && tag[0]) cJSON_AddStringToObject(args, "tag", tag[0] == '#' ? tag + 1 : tag);
    if (end_date && end_date[0]) cJSON_AddStringToObject(args, "end_date", end_date);
    if (limit < 1) limit = 1;
    if (limit > THINGSOS_FLOMO_SEARCH_MAX) limit = THINGSOS_FLOMO_SEARCH_MAX;
    cJSON_AddNumberToObject(args, "limit", limit);
    return print_call(rpc_id, "memo_search", args, out, cap);
}

bool thingsos_flomo_build_batch_get(unsigned rpc_id, const char* memo_id, char* out, size_t cap) {
    if (!thingsos_flomo_id_valid(memo_id)) return false;
    cJSON* args = cJSON_CreateObject();
    if (!args) return false;
    cJSON_AddStringToObject(args, "id", memo_id);
    return print_call(rpc_id, "memo_batch_get", args, out, cap);
}

bool thingsos_flomo_build_recommended(
    unsigned rpc_id, const char* memo_id, unsigned limit, char* out, size_t cap) {
    if (!thingsos_flomo_id_valid(memo_id)) return false;
    cJSON* args = cJSON_CreateObject();
    if (!args) return false;
    cJSON_AddStringToObject(args, "id", memo_id);
    if (limit < 1) limit = 1;
    if (limit > THINGSOS_FLOMO_RELATED_MAX) limit = THINGSOS_FLOMO_RELATED_MAX;
    cJSON_AddNumberToObject(args, "limit", limit);
    return print_call(rpc_id, "memo_recommended", args, out, cap);
}

bool thingsos_flomo_build_tag_tree(unsigned rpc_id, unsigned limit, char* out, size_t cap) {
    cJSON* args = cJSON_CreateObject();
    if (!args) return false;
    if (limit < 1) limit = 1;
    if (limit > 1000) limit = 1000;
    cJSON_AddNumberToObject(args, "limit", limit);
    return print_call(rpc_id, "tag_tree", args, out, cap);
}

bool thingsos_flomo_build_daily_review(unsigned rpc_id, char* out, size_t cap) {
    cJSON* args = cJSON_CreateObject();
    return args && print_call(rpc_id, "get_daily_review", args, out, cap);
}

bool thingsos_flomo_unwrap_sse(const char* body, size_t length, char* out, size_t cap) {
    if (!body || !out || !cap) return false;
    size_t i = 0;
    while (i < length && isspace((unsigned char)body[i])) ++i;
    if (i < length && body[i] == '{') {
        size_t n = length - i;
        if (n >= cap) return false;
        memmove(out, body + i, n);
        out[n] = 0;
        return true;
    }
    /* SSE: keep the data of the last event that carries a JSON object. Data
       lines of one event are joined with a newline per the SSE specification. */
    char* cur = (char*)malloc(length + 1);
    char* best = (char*)malloc(length + 1);
    size_t cur_len = 0, best_len = 0;
    bool in_event = false;
    if (!cur || !best) {
        free(cur);
        free(best);
        return false;
    }
    for (size_t line = 0; line <= length;) {
        size_t end = line;
        while (end < length && body[end] != 10) ++end;
        size_t len = end - line;
        if (len && body[line + len - 1] == 13) --len; /* carriage return */
        bool last = end >= length;
        if (len >= 5 && !memcmp(body + line, "data:", 5)) {
            size_t s = line + 5;
            if (s < line + len && body[s] == ' ') ++s;
            if (in_event) cur[cur_len++] = 10; /* newline */
            memcpy(cur + cur_len, body + s, line + len - s);
            cur_len += line + len - s;
            in_event = true;
        }
        if ((len == 0 || last) && in_event) {
            if (cur_len && cur[0] == '{') {
                memcpy(best, cur, cur_len);
                best_len = cur_len;
            }
            in_event = false;
            cur_len = 0;
        }
        if (last) break;
        line = end + 1;
    }
    bool ok = best_len > 0 && best_len < cap;
    if (ok) {
        memcpy(out, best, best_len);
        out[best_len] = 0;
    }
    free(cur);
    free(best);
    return ok;
}

static void set_msg(char* out, size_t cap, const char* text) {
    if (out && cap) copy_utf8(out, cap, text);
}

bool thingsos_flomo_parse_result(
    const char* json, size_t length, void** result_out, char* message_out, size_t message_cap) {
    if (result_out) *result_out = NULL;
    set_msg(message_out, message_cap, "");
    if (!json || !result_out) return false;
    cJSON* root = cJSON_ParseWithLength(json, length);
    if (!root) {
        set_msg(message_out, message_cap, "flomo 返回内容无法解析");
        return false;
    }
    bool ok = false;
    const cJSON* error = cJSON_GetObjectItemCaseSensitive(root, "error");
    cJSON* result = cJSON_GetObjectItemCaseSensitive(root, "result");
    if (cJSON_IsObject(error)) {
        const cJSON* m = cJSON_GetObjectItemCaseSensitive(error, "message");
        set_msg(message_out, message_cap, cJSON_IsString(m) ? m->valuestring : "flomo 服务返回错误");
    } else if (!cJSON_IsObject(result)) {
        set_msg(message_out, message_cap, "flomo 返回缺少结果");
    } else {
        const cJSON* content = cJSON_GetObjectItemCaseSensitive(result, "content");
        const cJSON* first = cJSON_IsArray(content) ? cJSON_GetArrayItem(content, 0) : NULL;
        const cJSON* text = first ? cJSON_GetObjectItemCaseSensitive(first, "text") : NULL;
        if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(result, "isError"))) {
            set_msg(message_out, message_cap, cJSON_IsString(text) ? text->valuestring : "flomo 工具调用失败");
        } else {
            cJSON* structured = cJSON_DetachItemFromObjectCaseSensitive(result, "structuredContent");
            if (cJSON_IsObject(structured)) {
                *result_out = structured;
                ok = true;
            } else {
                cJSON_Delete(structured);
                cJSON* parsed = cJSON_IsString(text) ? cJSON_Parse(text->valuestring) : NULL;
                if (cJSON_IsObject(parsed)) {
                    *result_out = parsed;
                    ok = true;
                } else {
                    cJSON_Delete(parsed);
                    set_msg(message_out, message_cap, "flomo 返回格式不受支持");
                }
            }
        }
    }
    cJSON_Delete(root);
    return ok;
}

static const char* json_str(const cJSON* obj, const char* key) {
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(v) ? v->valuestring : "";
}

static bool json_true(const cJSON* obj, const char* key) {
    return cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(obj, key));
}

static const cJSON* memo_array(const cJSON* result) {
    const cJSON* memos = cJSON_GetObjectItemCaseSensitive(result, "memos");
    if (cJSON_IsArray(memos)) return memos;
    const cJSON* item = NULL;
    cJSON_ArrayForEach(item, result) if (cJSON_IsArray(item)) return item;
    return NULL;
}

static void join_tags(const cJSON* tags, char* out, size_t cap) {
    out[0] = 0;
    size_t used = 0;
    const cJSON* t = NULL;
    cJSON_ArrayForEach(t, tags) {
        if (!cJSON_IsString(t) || !t->valuestring[0]) continue;
        size_t n = strlen(t->valuestring);
        size_t need = (used ? 1 : 0) + 1 + n;
        if (used + need >= cap) break;
        if (used) out[used++] = ' ';
        out[used++] = '#';
        memcpy(out + used, t->valuestring, n);
        used += n;
        out[used] = 0;
    }
}

static bool read_memo(const cJSON* m, thingsos_flomo_memo_t* out) {
    if (!cJSON_IsObject(m) || !thingsos_flomo_id_valid(json_str(m, "id"))) return false;
    memset(out, 0, sizeof(*out));
    copy_utf8(out->id, sizeof(out->id), json_str(m, "id"));
    copy_utf8(out->created_at, sizeof(out->created_at), json_str(m, "created_at"));
    copy_utf8(out->updated_at, sizeof(out->updated_at), json_str(m, "updated_at"));
    join_tags(cJSON_GetObjectItemCaseSensitive(m, "tags"), out->tags, sizeof(out->tags));
    const char* content = json_str(m, "content");
    size_t n = thingsos_flomo_plain_text(content, strlen(content), out->preview, sizeof(out->preview));
    const cJSON* wc = cJSON_GetObjectItemCaseSensitive(m, "word_count");
    out->word_count = cJSON_IsNumber(wc) && wc->valuedouble > 0 ? (uint32_t)wc->valuedouble : 0;
    out->truncated = json_true(m, "content_truncated") || n + 1 >= sizeof(out->preview) ||
                     strlen(content) >= sizeof(out->preview);
    out->has_image = json_true(m, "has_image");
    out->has_voice = json_true(m, "has_voice");
    out->has_link = json_true(m, "has_link");
    return true;
}

unsigned thingsos_flomo_read_memos(const void* result, thingsos_flomo_memo_t* out, unsigned capacity) {
    const cJSON* memos = result ? memo_array((const cJSON*)result) : NULL;
    unsigned count = 0;
    const cJSON* m = NULL;
    cJSON_ArrayForEach(m, memos) {
        if (count >= capacity) break;
        if (read_memo(m, &out[count])) ++count;
    }
    return count;
}

bool thingsos_flomo_read_full(
    const void* result, const char* memo_id, thingsos_flomo_memo_t* memo_out,
    char* content, size_t cap, size_t* len_out) {
    if (len_out) *len_out = 0;
    const cJSON* memos = result ? memo_array((const cJSON*)result) : NULL;
    const cJSON* m = NULL;
    cJSON_ArrayForEach(m, memos) {
        if (memo_id && memo_id[0] && strcmp(json_str(m, "id"), memo_id)) continue;
        thingsos_flomo_memo_t tmp;
        if (!read_memo(m, &tmp)) continue;
        const char* raw = json_str(m, "content");
        size_t n = thingsos_flomo_plain_text(raw, strlen(raw), content, cap);
        tmp.truncated = json_true(m, "content_truncated") || n + 1 >= cap;
        if (memo_out) *memo_out = tmp;
        if (len_out) *len_out = n;
        return true;
    }
    return false;
}

unsigned thingsos_flomo_read_tags(const void* result, thingsos_flomo_tag_t* out, unsigned capacity) {
    const cJSON* tags = result ? cJSON_GetObjectItemCaseSensitive((const cJSON*)result, "tags") : NULL;
    unsigned count = 0;
    const cJSON* t = NULL;
    cJSON_ArrayForEach(t, tags) {
        if (count >= capacity) break;
        const char* name = cJSON_IsString(t) ? t->valuestring
                         : cJSON_IsObject(t) ? json_str(t, "name") : "";
        if (!name[0] || strlen(name) >= sizeof(out->name)) continue;
        copy_utf8(out[count].name, sizeof(out[count].name), name);
        ++count;
    }
    return count;
}

static bool starts_ci(const char* s, size_t n, const char* word) {
    size_t w = strlen(word);
    if (n < w) return false;
    for (size_t i = 0; i < w; ++i)
        if (tolower((unsigned char)s[i]) != word[i]) return false;
    return true;
}

size_t thingsos_flomo_plain_text(const char* in, size_t length, char* out, size_t cap) {
    if (!out || !cap) return 0;
    if (!in) length = 0;
    size_t o = 0, i = 0;
    unsigned newlines = 0;
    bool at_start = true;
#define PUT(ch)                                                                 \
    do {                                                                        \
        char c_ = (ch);                                                         \
        if (c_ == '\n') {                                                       \
            if (at_start || newlines >= 2) break;                               \
            ++newlines;                                                         \
        } else {                                                                \
            newlines = 0;                                                       \
            at_start = false;                                                   \
        }                                                                       \
        if (o + 1 < cap) out[o++] = c_;                                         \
    } while (0)
    while (i < length && o + 1 < cap) {
        char c = in[i];
        if (c == '<') {
            size_t end = i + 1;
            while (end < length && end - i < 24 && in[end] != '>' && in[end] != '<') ++end;
            if (end < length && in[end] == '>') {
                const char* tag = in + i + 1;
                size_t tl = end - i - 1;
                bool closing = tl && tag[0] == '/';
                const char* name = closing ? tag + 1 : tag;
                size_t nl = closing ? tl - 1 : tl;
                if (nl && isalpha((unsigned char)name[0])) {
                    if (closing && starts_ci(name, nl, "li")) {
                        /* "<li><p>x</p></li>": the paragraph already ended the item. */
                        if (o && out[o - 1] != '\n') PUT('\n');
                    } else if (starts_ci(name, nl, "br") || (closing && starts_ci(name, nl, "p")))
                        PUT('\n');
                    else if (!closing && starts_ci(name, nl, "li")) {
                        PUT('-');
                        PUT(' ');
                    }
                    i = end + 1;
                    continue;
                }
            }
        } else if (c == '\\' && i + 1 < length && in[i + 1] && strchr("\\`*_{}[]()#+-.!>~|", in[i + 1])) {
            /* Markdown escape such as "fmcp\_token": keep the character only. */
            PUT(in[i + 1]);
            i += 2;
            continue;
        } else if (c == '*' && i + 1 < length && in[i + 1] == '*') {
            i += 2;
            continue;
        } else if (c == '&') {
            static const struct { const char* e; char c; } ents[] = {
                {"&lt;", '<'}, {"&gt;", '>'}, {"&amp;", '&'}, {"&quot;", '"'}, {"&#39;", '\''},
                {"&nbsp;", ' '}};
            bool hit = false;
            for (size_t k = 0; k < sizeof(ents) / sizeof(ents[0]); ++k) {
                size_t el = strlen(ents[k].e);
                if (i + el <= length && !memcmp(in + i, ents[k].e, el)) {
                    PUT(ents[k].c);
                    i += el;
                    hit = true;
                    break;
                }
            }
            if (hit) continue;
        } else if (c == '\r') {
            ++i;
            continue;
        }
        PUT(c);
        ++i;
    }
#undef PUT
    while (o > 0 && (out[o - 1] == '\n' || out[o - 1] == ' ')) --o;
    /* Never leave a split UTF-8 sequence at a capacity cut. */
    if (o + 1 >= cap) {
        size_t k = o;
        while (k > 0 && (((unsigned char)out[k - 1] & 0xC0u) == 0x80u)) --k;
        if (k > 0 && ((unsigned char)out[k - 1] & 0x80u)) {
            unsigned char lead = (unsigned char)out[k - 1];
            size_t want = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
            if (o - (k - 1) < want) o = k - 1;
        }
    }
    out[o] = 0;
    return o;
}

static bool leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

bool thingsos_flomo_day_before(const char* iso, char* out, size_t cap) {
    int y = 0, m = 0, d = 0;
    if (!iso || !out || cap < 11 || sscanf(iso, "%4d-%2d-%2d", &y, &m, &d) != 3) return false;
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (y < 1970 || m < 1 || m > 12 || d < 1 || d > days[m - 1] + (m == 2 && leap(y))) return false;
    if (--d == 0) {
        if (--m == 0) {
            m = 12;
            --y;
        }
        d = days[m - 1] + (m == 2 && leap(y));
    }
    snprintf(out, cap, "%04d-%02d-%02d", y, m, d);
    return true;
}

uint32_t thingsos_flomo_crc32(const void* data, size_t size) {
    const uint8_t* p = (const uint8_t*)data;
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= p[i];
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
    }
    return crc ^ 0xFFFFFFFFu;
}

#define FLOMO_LIST_MAGIC 0x464C4D31u

typedef struct {
    uint32_t magic;
    uint32_t count;
    uint32_t record_size;
    uint32_t crc32;
} flomo_blob_header_t;

bool thingsos_flomo_save_memos(const char* path, const thingsos_flomo_memo_t* memos, unsigned count) {
    if (!path || !path[0] || (count && !memos)) return false;
    char tmp[520];
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp)) return false;
    FILE* f = fopen(tmp, "wb");
    if (!f) return false;
    size_t payload = (size_t)count * sizeof(*memos);
    flomo_blob_header_t h = {FLOMO_LIST_MAGIC, count, (uint32_t)sizeof(*memos),
                             payload ? thingsos_flomo_crc32(memos, payload) : 0u};
    bool ok = fwrite(&h, sizeof(h), 1, f) == 1 &&
              (!payload || fwrite(memos, sizeof(*memos), count, f) == count);
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

bool thingsos_flomo_load_memos(
    const char* path, thingsos_flomo_memo_t* memos, unsigned capacity, unsigned* count_out) {
    if (count_out) *count_out = 0;
    if (!path || !memos || !capacity) return false;
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    flomo_blob_header_t h = {0};
    bool ok = fread(&h, sizeof(h), 1, f) == 1 && h.magic == FLOMO_LIST_MAGIC &&
              h.record_size == sizeof(*memos) && h.count <= capacity;
    size_t payload = (size_t)h.count * sizeof(*memos);
    if (ok && payload) ok = fread(memos, sizeof(*memos), h.count, f) == h.count;
    fclose(f);
    if (!ok || (payload ? thingsos_flomo_crc32(memos, payload) : 0u) != h.crc32) return false;
    for (unsigned i = 0; i < h.count; ++i) {
        memos[i].id[sizeof(memos[i].id) - 1] = 0;
        memos[i].preview[sizeof(memos[i].preview) - 1] = 0;
        memos[i].tags[sizeof(memos[i].tags) - 1] = 0;
        memos[i].created_at[sizeof(memos[i].created_at) - 1] = 0;
        memos[i].updated_at[sizeof(memos[i].updated_at) - 1] = 0;
    }
    if (count_out) *count_out = h.count;
    return true;
}
