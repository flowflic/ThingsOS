// SPDX-License-Identifier: Apache-2.0
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include "thingsos/content.h"
#include "thingsos/mem.h"
#include "webp_decoder.h"
#include "zlib.h"
static uint16_t u16(const unsigned char* p) {
    return (uint16_t)(p[0] | p[1] << 8);
}
static uint32_t u32(const unsigned char* p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static bool at(thingsos_zip_t* z, uint64_t offset, void* out, size_t n) {
    return offset <= z->size && n <= z->size - offset && offset <= LONG_MAX &&
           fseek(z->file, (long)offset, SEEK_SET) == 0 && fread(out, 1, n, z->file) == n;
}
bool thingsos_archive_path(const char* base, const char* ref, char out[256]) {
    if (!base || !ref || !out || !ref[0] || ref[0] == '/' || strchr(ref, '\\'))
        return false;
    /* Only an ASCII URI scheme can introduce an external reference.
       Other colons are literal ZIP member characters, not filesystem paths. */
    size_t scheme = strcspn(ref, ":/?#");
    if (ref[scheme] == ':' &&
        ((ref[0] >= 'A' && ref[0] <= 'Z') || (ref[0] >= 'a' && ref[0] <= 'z'))) {
        size_t i = 1;
        for (; i < scheme; ++i) {
            char c = ref[i];
            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.')) break;
        }
        if (i == scheme) return false;
    }
    char joined[512];
    const char* slash = strrchr(base, '/');
    size_t prefix = slash ? (size_t)(slash - base + 1) : 0;
    size_t len = strcspn(ref, "#?");
    if (prefix + len >= sizeof(joined)) return false;
    memcpy(joined, base, prefix);
    memcpy(joined + prefix, ref, len);
    joined[prefix + len] = 0;
    size_t used = 0;
    const char* p = joined;
    while (*p) {
        const char* end = strchr(p, '/');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        if (n == 1 && p[0] == '.') {
        } else if (n == 2 && p[0] == '.' && p[1] == '.') {
            if (!used) return false;
            while (used && out[used - 1] != '/') --used;
            if (used) --used;
        } else {
            if (!n || used + n + (used ? 1 : 0) >= 256) return false;
            for (size_t i = 0; i < n; i++)
                if ((unsigned char)p[i] < 32 || p[i] == '%') return false;
            if (used) out[used++] = '/';
            memcpy(out + used, p, n);
            used += n;
        }
        if (!end) break;
        p = end + 1;
    }
    out[used] = 0;
    return used > 0;
}
void thingsos_zip_close(thingsos_zip_t* z) {
    if (!z) return;
    if (z->file) fclose(z->file);
    free(z->entries);
    memset(z, 0, sizeof(*z));
}
static int compare_entry_names(const void* a, const void* b) {
    const thingsos_zip_entry_t* const* left = a;
    const thingsos_zip_entry_t* const* right = b;
    return strcmp((*left)->name, (*right)->name);
}
static bool unique_entry_names(const thingsos_zip_t* z) {
    const thingsos_zip_entry_t** names = thingsos_psram_alloc(z->count * sizeof(*names));
    if (!names) return false;
    for (size_t i = 0; i < z->count; ++i) names[i] = &z->entries[i];
    /* Preserve central-directory order for existing reader cache keys. */
    qsort(names, z->count, sizeof(*names), compare_entry_names);
    bool unique = true;
    for (size_t i = 1; i < z->count; ++i) {
        if (!strcmp(names[i - 1]->name, names[i]->name)) {
            unique = false;
            break;
        }
    }
    free(names);
    return unique;
}
bool thingsos_zip_open(thingsos_zip_t* z, const char* path) {
    if (!z || !path) return false;
    memset(z, 0, sizeof(*z));
    unsigned char* buf = NULL;
    z->file = fopen(path, "rb");
    if (!z->file) return false;
    if (fseek(z->file, 0, SEEK_END) || ftell(z->file) < 22) goto fail;
    z->size = (uint64_t)ftell(z->file);
    size_t tail = (size_t)(z->size > 65557 ? 65557 : z->size);
    buf = thingsos_psram_alloc(tail);
    if (!buf) goto fail;
    uint64_t buffered_from = z->size - tail, buffered_end = z->size;
    if (!at(z, buffered_from, buf, tail)) goto fail;
    size_t e = tail - 22;
    bool found = false;
    for (;;) {
        if (u32(buf + e) == 0x06054b50 && e + 22 + u16(buf + e + 20) == tail) {
            found = true;
            break;
        }
        if (!e) break;
        --e;
    }
    if (!found || u16(buf + e + 4) || u16(buf + e + 6) || u16(buf + e + 8) != u16(buf + e + 10))
        goto fail;
    z->count = u16(buf + e + 10);
    uint64_t cursor = u32(buf + e + 16), end = cursor + u32(buf + e + 12);
    uint64_t eocd = z->size - tail + e;
    if (!z->count || z->count > THINGSOS_ZIP_ENTRIES || end > eocd) goto fail;
    z->entries = thingsos_psram_calloc(z->count, sizeof(*z->entries));
    if (!z->entries) goto fail;
    for (size_t i = 0; i < z->count; i++) {
        if (cursor + 46 > end) goto fail;
        /* Reuse the EOCD window and refill in blocks. The fixed header and
           any supported filename fit together in one window. */
        uint64_t needed = end - cursor < 46 + 255 ? end : cursor + 46 + 255;
        if (cursor < buffered_from || needed > buffered_end) {
            size_t bytes = end - cursor < tail ? (size_t)(end - cursor) : tail;
            if (!at(z, cursor, buf, bytes)) goto fail;
            buffered_from = cursor;
            buffered_end = cursor + bytes;
        }
        const unsigned char* h = buf + (size_t)(cursor - buffered_from);
        if (u32(h) != 0x02014b50) goto fail;
        unsigned n = u16(h + 28), extra = u16(h + 30), comment = u16(h + 32);
        if (!n || n >= 256 || cursor + 46 + n + extra + comment > end) goto fail;
        thingsos_zip_entry_t* v = z->entries + i;
        memcpy(v->name, h + 46, n);
        if (memchr(v->name, 0, n)) goto fail;
        v->name[n] = 0;
        v->flags = u16(h + 8);
        v->method = u16(h + 10);
        v->crc = u32(h + 16);
        v->compressed = u32(h + 20);
        v->bytes = u32(h + 24);
        v->offset = u32(h + 42);
        if ((v->flags & 1) || u16(h + 34) || v->offset >= cursor) goto fail;
        cursor += 46 + n + extra + comment;
    }
    if (cursor != end) goto fail;
    free(buf);
    buf = NULL;
    if (unique_entry_names(z)) return true;
fail:
    free(buf);
    thingsos_zip_close(z);
    return false;
}
#ifdef ESP_PLATFORM
/* EPUB image extraction releases these before the decoder starts. Keep both
   compressed/output bytes and DEFLATE scratch off scarce internal/DMA RAM. */
#include "esp_heap_caps.h"
static void* zip_buffer(size_t bytes) {
    return heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
}
static voidpf zip_inflate_alloc(voidpf opaque,uInt items,uInt size) {
    (void)opaque;
    return heap_caps_calloc(items,size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
}
static void zip_inflate_free(voidpf opaque,voidpf address) {(void)opaque;free(address);}
#else
#define zip_buffer malloc
#endif
bool thingsos_zip_read(
    thingsos_zip_t* z, const char* name, size_t limit, unsigned char** out, size_t* bytes) {
    if (out) *out = NULL;
    if (bytes) *bytes = 0;
    if (!z || !out || !bytes || !name) return false;
    thingsos_zip_entry_t* v = NULL;
    for (size_t i = 0; i < z->count; i++)
        if (!strcmp(name, z->entries[i].name)) {
            v = z->entries + i;
            break;
        }
    if (!v || v->bytes > limit || v->bytes > THINGSOS_MEMBER_BYTES ||
        v->compressed > THINGSOS_MEMBER_BYTES || (v->method != 0 && v->method != 8))
        return false;
    unsigned char h[30];
    if (!at(z, v->offset, h, 30) || u32(h) != 0x04034b50 || u16(h + 8) != v->method ||
        u16(h + 6) != v->flags)
        return false;
    unsigned nl = u16(h + 26);
    if (nl != strlen(name) || nl >= 256) return false;
    char local[256];
    if (!at(z, (uint64_t)v->offset + 30, local, nl) || memcmp(local, name, nl)) return false;
    uint64_t pos = (uint64_t)v->offset + 30 + nl + u16(h + 28);
    if (pos > z->size || v->compressed > z->size - pos) return false;
    unsigned char *raw = zip_buffer(v->compressed ? v->compressed : 1),
                  *plain = zip_buffer((size_t)v->bytes + 1);
    if (!raw || !plain) {
        free(raw);
        free(plain);
        return false;
    }
    bool ok = at(z, pos, raw, v->compressed);
    if (ok && v->method == 0) {
        ok = v->bytes == v->compressed;
        if (ok) memcpy(plain, raw, v->bytes);
    } else if (ok) {
        z_stream stream = { 0 };
#ifdef ESP_PLATFORM
        stream.zalloc=zip_inflate_alloc;stream.zfree=zip_inflate_free;
#endif
        stream.next_in = raw;
        stream.avail_in = v->compressed;
        stream.next_out = plain;
        stream.avail_out = v->bytes + 1;
        int status = inflateInit2(&stream, -MAX_WBITS);
        if (status == Z_OK) {
            status = inflate(&stream, Z_FINISH);
            ok = status == Z_STREAM_END && stream.total_out == v->bytes &&
                 stream.total_in == v->compressed;
            inflateEnd(&stream);
        } else
            ok = false;
    }
    free(raw);
    if (ok) ok = (uint32_t)crc32(0, plain, v->bytes) == v->crc;
    if (!ok) {
        free(plain);
        return false;
    }
    plain[v->bytes] = 0;
    *out = plain;
    *bytes = v->bytes;
    return true;
}
#include "archive_image_info.inc"
