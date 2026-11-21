// SPDX-License-Identifier: Apache-2.0
#include <string.h>
#include "thingsos/graphics.h"
#include "thingsos/reader.h"
#include "src/webp/decode.h"
#include "webp_decoder.h"

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
/* The upstream decoder checks multiplication overflow before these calls. */
void* thingsos_webp_malloc(size_t bytes) {
    return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
void* thingsos_webp_calloc(size_t count, size_t bytes) {
    return heap_caps_calloc(count, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
#endif

bool thingsos_is_webp(const unsigned char* data, size_t bytes) {
    return data && bytes >= 12 && !memcmp(data,"RIFF",4) && !memcmp(data+8,"WEBP",4);
}

static bool webp_features(const unsigned char* data, size_t bytes, WebPBitstreamFeatures* features) {
    if(!thingsos_is_webp(data,bytes) || bytes>THINGSOS_MEMBER_BYTES ||
       WebPGetFeatures(data,bytes,features)!=VP8_STATUS_OK || features->has_animation ||
       features->width<=0 || features->height<=0)return false;
    /* Opaque VP8 streams through row buffers. Lossless and alpha decoding
       may retain full-resolution planes and use the smaller source bound. */
    unsigned limit=features->format==2 || features->has_alpha?2u:16u;
    return (uint64_t)features->width*features->height<=limit*1024u*1024u;
}

bool thingsos_webp_info(const unsigned char* data,size_t bytes,unsigned* width,unsigned* height) {
    WebPBitstreamFeatures features;
    if(!width || !height || !webp_features(data,bytes,&features))return false;
    *width=(unsigned)features.width;
    *height=(unsigned)features.height;
    return true;
}

bool thingsos_decode_webp(const unsigned char* data,size_t bytes,unsigned bound_w,unsigned bound_h,
                          unsigned char** gray,unsigned* width,unsigned* height) {
    if(gray)*gray=NULL;
    WebPDecoderConfig config;
    if(!gray || !width || !height || !WebPInitDecoderConfig(&config) ||
       !webp_features(data,bytes,&config.input))return false;
    thingsos_size_t size={(unsigned)config.input.width,(unsigned)config.input.height};
    if(bound_w && bound_h && (size.width>bound_w || size.height>bound_h)) {
        if(!thingsos_image_fit(size.width,size.height,bound_w,bound_h,&size))return false;
        config.options.use_scaling=1;
        config.options.scaled_width=(int)size.width;
        config.options.scaled_height=(int)size.height;
    }
    if((uint64_t)size.width*size.height>2u*1024u*1024u)return false;
    config.output.colorspace=config.input.has_alpha?MODE_YUVA:MODE_YUV;
    bool ok=WebPDecode(data,bytes,&config)==VP8_STATUS_OK &&
        config.output.width==(int)size.width && config.output.height==(int)size.height;
    unsigned char* pixels=ok?thingsos_image_buffer((size_t)size.width*size.height):NULL;
    if(pixels) {
        const WebPYUVABuffer* planes=&config.output.u.YUVA;
        for(unsigned y=0;y<size.height;++y) {
            const unsigned char* luma=planes->y+(size_t)y*planes->y_stride;
            const unsigned char* alpha=config.input.has_alpha?planes->a+(size_t)y*planes->a_stride:NULL;
            for(unsigned x=0;x<size.width;++x) {
                /* WebP's Y plane uses video range. Composite transparency on white. */
                unsigned value=luma[x]<=16?0:luma[x]>=235?255:((luma[x]-16)*255+109)/219;
                if(alpha)value=(value*alpha[x]+255*(255-alpha[x])+127)/255;
                pixels[(size_t)y*size.width+x]=(unsigned char)value;
            }
        }
        *gray=pixels;
        *width=size.width;
        *height=size.height;
    }
    WebPFreeDecBuffer(&config.output);
    return pixels!=NULL;
}
