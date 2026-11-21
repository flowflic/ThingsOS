// SPDX-License-Identifier: Apache-2.0
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include "thingsos/graphics.h"
#include "thingsos/reader.h"
#include "webp_decoder.h"
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_log.h"
#endif
/* Image decoding belongs to the UI owner, never the text pre-render worker.
   Track stb's temporary buffers as well as its output: a pixel-count check
   alone does not bound PNG inflate/conversion or JPEG component buffers. */
static struct { size_t limit, live, peak; void* ptr[64]; size_t bytes[64]; } decode_heap;
/* Long readings leave PSRAM fragmented: several megabytes may be free while the
   largest contiguous block is under one. stb needs a single ~1.5 MB request for
   a phone screenshot, so an MP article reserves a contiguous arena up front and
   every decode restarts at its base. */
static unsigned char* decode_arena;
static size_t decode_arena_size, decode_arena_used;
static struct { void* ptr[64]; size_t bytes[64]; } arena_slots;
bool thingsos_decode_reserve(size_t bytes) {
    if (decode_arena) return decode_arena_size >= bytes;
    if (!bytes) return false;
#ifdef ESP_PLATFORM
    decode_arena = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    decode_arena = malloc(bytes);
#endif
    if (!decode_arena) return false;
    decode_arena_size = bytes;
    decode_arena_used = 0;
    memset(&arena_slots, 0, sizeof(arena_slots));
    return true;
}
void thingsos_decode_release(void) {
    if (!decode_arena) return;
    free(decode_arena);
    decode_arena = NULL;
    decode_arena_size = decode_arena_used = 0;
    memset(&arena_slots, 0, sizeof(arena_slots));
}
static void arena_forget(void* p) {
    for (unsigned i = 0; i < 64; ++i)
        if (arena_slots.ptr[i] == p) { arena_slots.ptr[i] = NULL; return; }
}
static void* arena_alloc(size_t n) {
    if (n > decode_arena_size - decode_arena_used) {
#ifdef ESP_PLATFORM
        ESP_LOGW("ThingsOS_img","arena full need=%u used=%u size=%u",(unsigned)n,
            (unsigned)decode_arena_used,(unsigned)decode_arena_size);
#endif
        return NULL;
    }
    unsigned slot = 0;
    while (slot < 64 && arena_slots.ptr[slot]) ++slot;
    if (slot == 64) return NULL;
    void* p = decode_arena + decode_arena_used;
    decode_arena_used += n;
    arena_slots.ptr[slot] = p;
    arena_slots.bytes[slot] = n;
    return p;
}
static void* arena_realloc(void* p, size_t n) {
    if (!p) return arena_alloc(n);
    void* next = arena_alloc(n);
    if (next) {
        size_t old = 0;
        for (unsigned i = 0; i < 64; ++i) if (arena_slots.ptr[i] == p) old = arena_slots.bytes[i];
        memcpy(next, p, old < n ? old : n);
        arena_forget(p);
    }
    return next;
}
size_t thingsos_wallpaper_decode_peak(void) {return decode_heap.peak;}
void* thingsos_image_buffer(size_t n) {
#ifdef ESP_PLATFORM
    return heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
    return malloc(n);
#endif
}
static void* image_alloc(size_t n) {
    if(!decode_heap.limit) {
        if(decode_arena)return arena_alloc(n);
        void* plain=thingsos_image_buffer(n);
#ifdef ESP_PLATFORM
        if(!plain)ESP_LOGW("ThingsOS_img","alloc %u failed free=%u largest=%u limit=%u",(unsigned)n,
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
            (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
            (unsigned)decode_heap.limit);
#endif
        return plain;
    }
    if(n>decode_heap.limit-decode_heap.live)return NULL;
    unsigned slot=0;while(slot<64 && decode_heap.ptr[slot])++slot;
    if(slot==64)return NULL;
#ifdef ESP_PLATFORM
    size_t available=heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if(available<384u*1024u || n>available-384u*1024u)return NULL;
    void* p=heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
    void* p=malloc(n);
#endif
    if(p){decode_heap.ptr[slot]=p;decode_heap.bytes[slot]=n;decode_heap.live+=n;
        if(decode_heap.live>decode_heap.peak)decode_heap.peak=decode_heap.live;}
    return p;
}
static void image_detach(void* p) {
    for(unsigned i=0;i<64;++i)if(p && decode_heap.ptr[i]==p) {
        decode_heap.live-=decode_heap.bytes[i];decode_heap.ptr[i]=NULL;break;
    }
}
static void image_free(void* p) {
    if(decode_arena){arena_forget(p);return;}
    image_detach(p);free(p);
}
static void* image_realloc(void* p,size_t n) {
    if(decode_arena && !decode_heap.limit) return arena_realloc(p,n);
    if(!decode_heap.limit) {
#ifdef ESP_PLATFORM
        return heap_caps_realloc(p,n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
        return realloc(p,n);
#endif
    }
    if(!p)return image_alloc(n);
    for(unsigned i=0;i<64;++i)if(decode_heap.ptr[i]==p) {
        /* Allocate before freeing so the bound also covers realloc's peak. */
        void* next=image_alloc(n);
        if(next){memcpy(next,p,n<decode_heap.bytes[i]?n:decode_heap.bytes[i]);image_free(p);}
        return next;
    }
    return NULL;
}
#define STBI_MALLOC(n) image_alloc(n)
#define STBI_REALLOC(p,n) image_realloc(p,n)
#define STBI_FREE(p) image_free(p)
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_MAX_DIMENSIONS 4096
#define STB_IMAGE_IMPLEMENTATION
#include "thingsos_stb_image.h"
#include "reader_jpeg.inc"
#include "reader_gif.inc"
bool thingsos_decode_gif(const unsigned char* data,size_t bytes,size_t max_pixels,
                         unsigned char** gray,unsigned* width,unsigned* height) {
    if(gray)*gray=NULL;
    if(!data || !bytes || bytes>THINGSOS_MEMBER_BYTES || bytes>INT_MAX)return false;
    return reader_gif_decode(data,bytes,max_pixels,false,gray,width,height);
}
bool thingsos_image_info(const unsigned char* data,size_t bytes,unsigned* width,unsigned* height) {
    if(thingsos_is_webp(data,bytes))return thingsos_webp_info(data,bytes,width,height);
    if(reader_gif_is_gif(data,bytes)) {
        if(!data || !bytes || bytes>THINGSOS_MEMBER_BYTES || bytes>INT_MAX)return false;
        return reader_gif_info(data,bytes,width,height);
    }
    int w=0,h=0,channels=0;
    if(!data || !bytes || bytes>THINGSOS_MEMBER_BYTES || bytes>INT_MAX || !width || !height ||
       !stbi_info_from_memory(data,(int)bytes,&w,&h,&channels) || w<=0 || h<=0 ||
       (uint64_t)w*h>2u*1024u*1024u)return false;
    *width=(unsigned)w;*height=(unsigned)h;return true;
}
static bool decode_image(
    const unsigned char* data, size_t bytes, unsigned char** gray, unsigned* width,
    unsigned* height) {
    if (gray) *gray = NULL;
    if (!data || !gray || !width || !height || !bytes || bytes > THINGSOS_MEMBER_BYTES ||
        bytes > INT_MAX)
        return false;
    if(reader_gif_is_gif(data,bytes))
        return reader_gif_decode(data,bytes,2u*1024u*1024u,true,gray,width,height);
    int w=0, h=0, channels=0;
    if (!stbi_info_from_memory(data, (int)bytes, &w, &h, &channels) || w <= 0 || h <= 0 ||
        (uint64_t)w * h > 2u * 1024u * 1024u) {
#ifdef ESP_PLATFORM
        ESP_LOGW("ThingsOS_img","probe failed %dx%d ch=%d bytes=%u reason=%s psram=%u",w,h,channels,
            (unsigned)bytes,stbi_failure_reason(),(unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
#endif
        return false;
    }
    /* JPEG and opaque PNG covers need one byte/pixel. Expanding them to
       gray+alpha doubled the peak allocation merely to discard opaque alpha. */
    int output_channels=(channels==2 || channels==4)?2:1;
    if(decode_arena)decode_arena_used=0;
    unsigned char* pixels = stbi_load_from_memory(data, (int)bytes, &w, &h, &channels, output_channels);
    if (!pixels) {
#ifdef ESP_PLATFORM
        ESP_LOGW("ThingsOS_img","decode failed %dx%d ch=%d out=%d bytes=%u reason=%s psram=%u",w,h,channels,
            output_channels,(unsigned)bytes,stbi_failure_reason(),(unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
#endif
        return false;
    }
    for (size_t i = 0; output_channels==2 && i < (size_t)w * h; i++) {
        unsigned gray_value = pixels[i * 2], alpha = pixels[i * 2 + 1];
        pixels[i] = (unsigned char)((gray_value * alpha + 255 * (255 - alpha)) / 255);
    }
    if (decode_arena) {
        /* The caller owns and frees the result, so move the one-byte-per-pixel
           image out of the bump arena and recycle the scratch space. */
        unsigned char* copy=thingsos_image_buffer((size_t)w*h);
        if(!copy)return false;
        memcpy(copy,pixels,(size_t)w*h);
        decode_arena_used=0;
        memset(&arena_slots,0,sizeof(arena_slots));
        pixels=copy;
    } else image_detach(pixels);
    *gray = pixels;
    *width = (unsigned)w;
    *height = (unsigned)h;
    return true;
}
bool thingsos_decode_image(const unsigned char* data,size_t bytes,unsigned char** gray,
                          unsigned* width,unsigned* height) {
    if(thingsos_is_webp(data,bytes))return thingsos_decode_webp(data,bytes,0,0,gray,width,height);
    return decode_image(data,bytes,gray,width,height);
}
bool thingsos_decode_wallpaper(const unsigned char* data,size_t bytes,unsigned char** gray,
                              unsigned* width,unsigned* height) {
    if(gray)*gray=NULL;
    if(bytes>1024u*1024u)return false;
    if(thingsos_is_webp(data,bytes))return thingsos_decode_webp(data,bytes,1216,1216,gray,width,height);
    decode_heap.limit=3u*1024u*1024u;
    decode_heap.peak=0;
    bool ok=decode_image(data,bytes,gray,width,height);
    /* Failed stb paths must release all scratch buffers too. */
    for(unsigned i=0;i<64;++i)if(decode_heap.ptr[i])image_free(decode_heap.ptr[i]);
    decode_heap.limit=0;
    return ok;
}
