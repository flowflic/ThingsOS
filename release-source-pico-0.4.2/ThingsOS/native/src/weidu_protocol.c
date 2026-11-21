/* SPDX-License-Identifier: Apache-2.0
 * Independent implementation of the observed Web wire format, not CrossMux code.
 * MD5 is required by this protocol for identifiers/integrity, never TLS trust.
 */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "thingsos/weidu.h"
#include "thingsos/mem.h"
typedef struct {
    uint32_t h[4];
    uint64_t bytes;
    unsigned used;
    unsigned char block[64];
} md5_t;
static uint32_t rot(uint32_t x, unsigned n) {
    return (x << n) | (x >> (32 - n));
}
static void md5_block(md5_t* m, const unsigned char* p) {
    static const uint32_t k[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613,
        0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193,
        0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d,
        0x02441453, 0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
        0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122,
        0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa,
        0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665, 0xf4292244,
        0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb,
        0xeb86d391
    };
    static const unsigned shifts[16] = {
        7, 12, 17, 22, 5, 9, 14, 20, 4, 11, 16, 23, 6, 10, 15, 21
    };
    uint32_t w[16];
    for (unsigned i = 0; i < 16; ++i)
        w[i] = (uint32_t)p[i * 4] | (uint32_t)p[i * 4 + 1] << 8 | (uint32_t)p[i * 4 + 2] << 16 |
               (uint32_t)p[i * 4 + 3] << 24;
    uint32_t a = m->h[0], b = m->h[1], c = m->h[2], d = m->h[3];
    for (unsigned i = 0; i < 64; ++i) {
        unsigned phase = i / 16, index = phase == 0   ? i
                                         : phase == 1 ? (5 * i + 1) % 16
                                         : phase == 2 ? (3 * i + 5) % 16
                                                      : (7 * i) % 16;
        uint32_t v = phase == 0   ? ((b & c) | (~b & d))
                     : phase == 1 ? ((d & b) | (~d & c))
                     : phase == 2 ? (b ^ c ^ d)
                                  : (c ^ (b | ~d));
        uint32_t next = b + rot(a + v + k[i] + w[index], shifts[phase * 4 + i % 4]);
        a = d;
        d = c;
        c = b;
        b = next;
    }
    m->h[0] += a;
    m->h[1] += b;
    m->h[2] += c;
    m->h[3] += d;
}
static void md5_init(md5_t* m) {
    memset(m, 0, sizeof(*m));
    m->h[0] = 0x67452301;
    m->h[1] = 0xefcdab89;
    m->h[2] = 0x98badcfe;
    m->h[3] = 0x10325476;
}
static void md5_add(md5_t* m, const void* data, size_t n) {
    const unsigned char* p = data;
    m->bytes += n;
    while (n) {
        size_t take = 64 - m->used;
        if (take > n) take = n;
        memcpy(m->block + m->used, p, take);
        m->used += (unsigned)take;
        p += take;
        n -= take;
        if (m->used == 64) {
            md5_block(m, m->block);
            m->used = 0;
        }
    }
}
static void md5_end(md5_t* m, char out[33]) {
    uint64_t bits = m->bytes * 8;
    unsigned char p[72] = { 0x80 };
    size_t n = m->used < 56 ? 56 - m->used : 120 - m->used;
    md5_add(m, p, n);
    for (unsigned i = 0; i < 8; ++i) p[i] = (unsigned char)(bits >> (8 * i));
    md5_add(m, p, 8);
    for (unsigned i = 0; i < 16; ++i)
        snprintf(out + i * 2, 3, "%02x", (unsigned)((m->h[i / 4] >> (8 * (i % 4))) & 255));
}
void thingsos_weidu_md5(const void* p, size_t n, char out[33]) {
    md5_t m;
    md5_init(&m);
    md5_add(&m, p, n);
    md5_end(&m, out);
}
bool thingsos_weidu_token(const char* s, size_t max) {
    if (!s || !*s || strlen(s) >= max) return false;
    for (const unsigned char* p = (const unsigned char*)s; *p; ++p)
        if (!isalnum(*p) && *p != '_' && *p != '-' && *p != '.') return false;
    return true;
}
bool thingsos_weidu_field_feed(thingsos_weidu_ps_t* p,const char* key,const unsigned char* data,size_t count) {
    for (size_t i = 0; i < count && p->state != 4; ++i) {
        unsigned char c = data[i];
        ++p->offset;
        if (!p->state) {
            if (c == key[p->match]) {
                if (!key[++p->match]) {
                    p->state = 1;
                    p->match = 0;
                }
            } else
                p->match = c == key[0] ? 1 : 0;
        } else if (p->state == 1) {
            if (isspace(c)) continue;
            if (c == ':')
                p->state = 2;
            else {
                p->state = 0;
                p->match = c == '"' ? 1 : 0;
            }
        } else if (p->state == 2) {
            if (isspace(c)) continue;
            if (c != '"') return false;
            p->state = 3;
        } else if (p->state == 3) {
            if (c == '"') {
                p->value[p->bytes] = 0;
                if (!thingsos_weidu_token(p->value, sizeof(p->value))) return false;
                p->state = 4;
            } else {
                if (p->bytes + 1 >= sizeof(p->value) || c < 33 || c >= 127) return false;
                p->value[p->bytes++] = (char)c;
            }
        }
    }
    return true;
}
bool thingsos_weidu_ps_feed(thingsos_weidu_ps_t* p,const unsigned char* data,size_t count){
    return thingsos_weidu_field_feed(p,"\"psvts\"",data,count);
}
bool thingsos_weidu_session_valid(const thingsos_weidu_session_t* s) {
    if (!s || !memchr(s->vid, 0, sizeof(s->vid)) || !memchr(s->skey, 0, sizeof(s->skey)) ||
        !memchr(s->rt, 0, sizeof(s->rt)) || !thingsos_weidu_token(s->vid, sizeof(s->vid)) ||
        !s->skey[0])
        return false;
    const char* values[] = { s->skey, s->rt };
    for (unsigned i = 0; i < 2; ++i)
        for (const unsigned char* p = (const unsigned char*)values[i]; *p; ++p)
            if (*p <= 32 || *p >= 127 || *p == ';' || *p == ',' || *p == '"' || *p == '\\')
                return false;
    return true;
}
bool thingsos_weidu_encode_id(const char* value, char out[160]) {
    if (!value || !*value || strlen(value) > 64) return false;
    char hash[33], tail[33];
    thingsos_weidu_md5(value, strlen(value), hash);
    bool decimal = true;
    for (const char* p = value; *p; ++p)
        if (*p < '0' || *p > '9') decimal = false;
    size_t used = (size_t)snprintf(out, 160, "%.3s%c2%.2s", hash, decimal ? '3' : '4', hash + 30);
    if (decimal) {
        size_t len = strlen(value);
        for (size_t at = 0; at < len; at += 9) {
            char digits[10], hex[16];
            size_t n = len - at;
            if (n > 9) n = 9;
            memcpy(digits, value + at, n);
            digits[n] = 0;
            snprintf(hex, sizeof(hex), "%x", (unsigned)strtoul(digits, NULL, 10));
            used += (size_t)snprintf(
                out + used, 160 - used, "%s%02x%s", at ? "g" : "", (unsigned)strlen(hex), hex);
        }
    } else {
        char hex[129];
        size_t n = 0;
        for (const unsigned char* p = (const unsigned char*)value; *p; ++p)
            n += (size_t)snprintf(hex + n, sizeof(hex) - n, "%x", *p);
        used += (size_t)snprintf(out + used, 160 - used, "%02x%s", (unsigned)n, hex);
    }
    while (used < 20) {
        size_t n = 20 - used;
        if (n > 32) n = 32;
        memcpy(out + used, hash, n);
        used += n;
        out[used] = 0;
    }
    if (used + 4 >= 160) return false;
    thingsos_weidu_md5(out, used, tail);
    memcpy(out + used, tail, 3);
    out[used + 3] = 0;
    return true;
}
void thingsos_weidu_sign(const char* query, char out[17]) {
    size_t n = strlen(query);
    uint32_t a = 0x15051505, b = a;
    for (size_t i = n; i > 1; i -= 2) {
        a = (a ^ ((uint32_t)(unsigned char)query[i - 1] << ((n - i + 1) % 30))) & 0x7fffffff;
        b = (b ^ ((uint32_t)(unsigned char)query[i - 2] << ((i - 1) % 30))) & 0x7fffffff;
    }
    snprintf(out, 17, "%llx", (unsigned long long)a + b);
}
bool thingsos_weidu_body(
    const char* book, const char* chapter, const char* ps, uint32_t time, uint32_t random,
    char* out, size_t cap) {
    if (time < 1577836800 || !thingsos_weidu_token(ps, 128)) return false;
    char b[160], c[160], pc[160], stamp[16], sign[17];
    if (!thingsos_weidu_encode_id(book, b) || !thingsos_weidu_encode_id(chapter, c)) return false;
    snprintf(stamp, sizeof(stamp), "%u", (unsigned)time);
    if (!thingsos_weidu_encode_id(stamp, pc)) return false;
    if (!strcmp(pc, ps)) {
        ++time;
        snprintf(stamp, sizeof(stamp), "%u", (unsigned)time);
        thingsos_weidu_encode_id(stamp, pc);
    }
    uint32_t r = (random % 10000) * (random % 10000);
    int n = snprintf(
        out, cap, "b=%s&c=%s&ct=%u&pc=%s&prevChapter=false&ps=%s&r=%u&sc=1&st=0", b, c,
        (unsigned)time, pc, ps, (unsigned)r);
    if (n < 0 || (size_t)n >= cap) return false;
    thingsos_weidu_sign(out, sign);
    n = snprintf(
        out, cap,
        "{\"b\":\"%s\",\"c\":\"%s\",\"r\":%u,\"ct\":%u,\"ps\":\"%s\",\"pc\":\"%s\",\"sc\":1,"
        "\"prevChapter\":\"false\",\"st\":0,\"s\":\"%s\"}",
        b, c, (unsigned)r, (unsigned)time, ps, pc, sign);
    return n > 0 && (size_t)n < cap;
}
thingsos_weidu_error_t thingsos_weidu_decode_control(
    const char* const* paths, size_t count, const char* output,const thingsos_weidu_control_t* control) {
    if (count < 2 || count > 3) return WD_PROTOCOL;
    thingsos_weidu_error_t error = WD_PROTOCOL;
    unsigned char* joined = NULL;
    size_t used = 0;
    bool ok = false;
    for (size_t i = 0; i < count; ++i) {
        if(thingsos_weidu_stopped(control)){error=WD_CANCEL;goto done;}
        FILE* f = fopen(paths[i], "rb");
        if (!f) {
            error = WD_STORAGE;
            goto done;
        }
        if (fseek(f, 0, SEEK_END)) {
            error = WD_STORAGE;
            fclose(f);
            goto done;
        }
        long length = ftell(f);
        rewind(f);
        if (length <= 33 || length > 2 * 1024 * 1024 || used + (size_t)length > 2 * 1024 * 1024) {
            error = length > 2 * 1024 * 1024 || used + (size_t)length > 2 * 1024 * 1024
                        ? WD_LIMIT
                        : WD_PROTOCOL;
            fclose(f);
            goto done;
        }
        /* The wire prefix is opaque protocol framing. It is not specified as
           the MD5 of this encoded shard; content is still validated after
           decoding and before replacing an existing offline book. */
        unsigned char* next = thingsos_psram_realloc(joined, used + (size_t)length - 32);
        if (!next) {
            error = WD_MEMORY;
            fclose(f);
            goto done;
        }
        joined = next;
        size_t n = (size_t)length - 32;
        if (fseek(f,32,SEEK_SET)) {
            error = WD_STORAGE;
            fclose(f);
            goto done;
        }
        for(size_t at=0;at<n;) {
            if(thingsos_weidu_stopped(control)){error=WD_CANCEL;fclose(f);goto done;}
            size_t take=n-at<16384?n-at:16384;
            if(fread(joined+used+at,1,take,f)!=take){error=WD_STORAGE;fclose(f);goto done;}
            at+=take;
        }
        fclose(f);
        if (!i) {
            memmove(joined, joined + 1, n - 1);
            --n;
        }
        used += n;
    }
    if (used < 4) goto done;
    uint32_t swaps[10];
    size_t total = 0, tail = (used + 9) / 10;
    if (tail > 4) tail = 4;
    if (used < 11) {
        swaps[0] = 0;
        swaps[1] = 2;
        total = 2;
    } else {
        char decimal[64] = { 0 };
        size_t n = 0;
        for (size_t i = used; i > used - tail; --i) {
            unsigned v = 0;
            for (unsigned bit = 0; bit < 8; ++bit)
                if (joined[i - 1] & (1u << bit)) v += 1u << (2 * bit);
            n += (size_t)snprintf(decimal + n, sizeof(decimal) - n, "%u", v);
        }
        size_t mod = used - tail - 2, step = 1;
        for (size_t x = mod; x >= 10; x /= 10) ++step;
        for (size_t at = 0; total < 10 && at + step < n; at += step) {
            char digit[24];
            memcpy(digit, decimal + at, step);
            digit[step] = 0;
            swaps[total++] = (uint32_t)(strtoul(digit, NULL, 10) % mod);
            memcpy(digit, decimal + at + 1, step);
            digit[step] = 0;
            swaps[total++] = (uint32_t)(strtoul(digit, NULL, 10) % mod);
        }
    }
    if (!total) goto done;
    for (size_t i = total; i >= 2; i -= 2)
        for (int delta = 1; delta >= 0; --delta) {
            size_t a = swaps[i - 1] + delta, b = swaps[i - 2] + delta;
            if (a < used && b < used) {
                unsigned char tmp = joined[a];
                joined[a] = joined[b];
                joined[b] = tmp;
            }
        }
    FILE* f = fopen(output, "wb");
    if (!f) {
        error = WD_STORAGE;
        goto done;
    }
    uint32_t bits = 0;
    unsigned char* output_buffer=thingsos_psram_alloc(16384);
    if(!output_buffer){fclose(f);error=WD_MEMORY;goto done;}
    size_t buffered=0;
    unsigned available = 0;
    size_t bytes = 0;
    ok = true;
    for (size_t i = 0; i < used && ok; ++i) {
        if(!(i%2048) && thingsos_weidu_stopped(control)){error=WD_CANCEL;ok=false;break;}
        unsigned char ch = joined[i];
        int v = ch >= 'A' && ch <= 'Z'   ? ch - 'A'
                : ch >= 'a' && ch <= 'z' ? ch - 'a' + 26
                : ch >= '0' && ch <= '9' ? ch - '0' + 52
                : ch == '+' || ch == '-' ? 62
                : ch == '/' || ch == '_' ? 63
                                         : -1;
        if (v < 0) {
            if (ch == '=' || isspace(ch)) continue;
            ok = false;
            break;
        }
        bits = (bits << 6) | (unsigned)v;
        available += 6;
        if (available >= 8) {
            available -= 8;
            output_buffer[buffered++]=(unsigned char)((bits>>available)&255);
            if(buffered==16384){ok=fwrite(output_buffer,1,buffered,f)==buffered;buffered=0;}
            if (!ok) error = WD_STORAGE;
            ++bytes;
        }
    }
    ok = ok && bytes && available != 6;
    if(ok && buffered && fwrite(output_buffer,1,buffered,f)!=buffered){ok=false;error=WD_STORAGE;}
    if (ok && !thingsos_weidu_flush(f)) {
        error = WD_STORAGE;
        ok = false;
    }
    if (fclose(f)) {
        error = WD_STORAGE;
        ok = false;
    }
    free(output_buffer);
done:
    free(joined);
    if (!ok) remove(output);
    return ok ? WD_OK : error;
}
thingsos_weidu_error_t thingsos_weidu_decode_shards(const char* const* paths,size_t count,const char* output) {
    return thingsos_weidu_decode_control(paths,count,output,NULL);
}
