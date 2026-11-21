// SPDX-License-Identifier: Apache-2.0
#include <string.h>
#include "thingsos/graphics.h"
#include "thingsos/reader.h"
#include "src/webp/decode.h"
#include "src/dec/vp8i_dec.h"
#include "src/dec/webpi_dec.h"
#include "webp_decoder.h"

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
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

typedef struct {
    unsigned char* pixels;
    uint32_t* sums;
    unsigned width,height,source_w,source_h,rows;
} webp_gray_rows_t;

static int webp_gray_setup(VP8Io* io) {
    webp_gray_rows_t* s=io->opaque;
    return io->width==(int)s->source_w && io->height==(int)s->source_h &&
        WebPIoInitFromOptions(NULL,io,MODE_YUVA);
}

static unsigned webp_bin_edge(unsigned i,unsigned source,unsigned output) {
    return (unsigned)(((uint64_t)i*source+output-1)/output);
}

/* Consume the decoder's filtered rows directly. The screen needs grayscale,
   so retaining complete Y/U/V/A output planes wastes most of Pico's PSRAM.
   Average source pixels into output bins, compositing alpha on white first. */
static int webp_gray_put(const VP8Io* io) {
    webp_gray_rows_t* s=io->opaque;
    if(io->mb_y<0 || io->mb_h<=0 || io->mb_w!=(int)s->source_w ||
       (unsigned)(io->mb_y+io->mb_h)>s->source_h)return 0;
    for(int row=0;row<io->mb_h;++row) {
        unsigned sy=(unsigned)(io->mb_y+row),dy=(unsigned)((uint64_t)sy*s->height/s->source_h);
        const unsigned char* luma=io->y+(size_t)row*io->y_stride;
        const unsigned char* alpha=io->a?io->a+(size_t)row*s->source_w:NULL;
        for(unsigned x=0;x<s->source_w;++x) {
            unsigned value=luma[x]<=16?0:luma[x]>=235?255:((luma[x]-16)*255+109)/219;
            if(alpha)value=(value*alpha[x]+255*(255-alpha[x])+127)/255;
            unsigned dx=(unsigned)((uint64_t)x*s->width/s->source_w);
            s->sums[dx]+=value;
        }
        if(sy+1==webp_bin_edge(dy+1,s->source_h,s->height)) {
            unsigned rows=sy+1-webp_bin_edge(dy,s->source_h,s->height);
            for(unsigned x=0;x<s->width;++x) {
                unsigned count=rows*(webp_bin_edge(x+1,s->source_w,s->width)-webp_bin_edge(x,s->source_w,s->width));
                s->pixels[(size_t)dy*s->width+x]=(unsigned char)((s->sums[x]+count/2)/count);
                s->sums[x]=0;
            }
            ++s->rows;
        }
    }
    return 1;
}

static bool webp_lossy_gray(const unsigned char* data,size_t bytes,
                            const WebPBitstreamFeatures* features,thingsos_size_t size,
                            unsigned char** gray,unsigned* width,unsigned* height,bool* memory_limited) {
    *memory_limited=false;
    WebPHeaderStructure headers={0};headers.data=data;headers.data_size=bytes;headers.have_all_data=1;
    if(WebPParseHeaders(&headers)!=VP8_STATUS_OK || headers.is_lossless)return false;
    size_t output_bytes=(size_t)size.width*size.height;
#ifdef ESP_PLATFORM
    size_t available=heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if(available<512u*1024u || output_bytes>available-512u*1024u){*memory_limited=true;return false;}
#endif
    webp_gray_rows_t state={0};
    state.width=size.width;state.height=size.height;
    state.source_w=(unsigned)features->width;state.source_h=(unsigned)features->height;
    state.pixels=thingsos_image_buffer(output_bytes);
    state.sums=(uint32_t*)thingsos_image_buffer(size.width*sizeof(*state.sums));
    VP8Decoder* decoder=VP8New();VP8Io io;
    bool ok=state.pixels && state.sums && decoder && VP8InitIo(&io);
    if(!ok)*memory_limited=true;
    if(ok) {
        memset(state.sums,0,size.width*sizeof(*state.sums));
        decoder->alpha_data=headers.alpha_data;decoder->alpha_data_size=headers.alpha_data_size;
        io.data=headers.data+headers.offset;io.data_size=headers.data_size-headers.offset;
        io.opaque=&state;io.setup=webp_gray_setup;io.put=webp_gray_put;
        ok=VP8Decode(decoder,&io) && state.rows==size.height;
        if(!ok)*memory_limited=VP8Status(decoder)==VP8_STATUS_OUT_OF_MEMORY;
    }
    VP8Delete(decoder);free(state.sums);
    if(!ok){free(state.pixels);return false;}
    *gray=state.pixels;*width=size.width;*height=size.height;return true;
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
    if(config.input.format==1) {
        for(unsigned attempt=0;attempt<4;++attempt) {
            bool memory_limited;
            if(webp_lossy_gray(data,bytes,&config.input,size,gray,width,height,&memory_limited))return true;
            if(!memory_limited || !bound_w || !bound_h || size.width<32 || size.height<32)break;
            size.width=(size.width+1)/2;size.height=(size.height+1)/2;
        }
        return false;
    }
    config.output.colorspace=config.input.has_alpha?MODE_YUVA:MODE_YUV;
    bool ok=WebPDecode(data,bytes,&config)==VP8_STATUS_OK &&
        config.output.width==(int)size.width && config.output.height==(int)size.height;
    size_t output_bytes=(size_t)size.width*size.height;
#ifdef ESP_PLATFORM
    size_t available=heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if(available<512u*1024u || output_bytes>available-512u*1024u)ok=false;
#endif
    unsigned char* pixels=ok?thingsos_image_buffer(output_bytes):NULL;
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
