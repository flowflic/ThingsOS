// SPDX-License-Identifier: Apache-2.0
#include "thingsos/graphics.h"
#include "thingsos/reader.h"
#include "jpeg_decoder.h"
#include "webp_decoder.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#ifdef ESP_PLATFORM
#include "esp_log.h"
#endif
#include "reader_png.inc"
typedef struct {const unsigned char* data;size_t bytes,at;unsigned char* pixels;unsigned w,h,sw,sh;} thumbnail_jpeg_t;
static size_t thumbnail_input(JDEC* decoder,uint8_t* output,size_t n) {
    thumbnail_jpeg_t* s=decoder->device;
    if(!s || !s->data || s->at>s->bytes)return 0;
    if(n>s->bytes-s->at)n=s->bytes-s->at;
    if(output)memcpy(output,s->data+s->at,n);
    s->at+=n;return n;
}
static int thumbnail_output(JDEC* decoder,void* pixels,JRECT* r) {
    thumbnail_jpeg_t* s=decoder->device;
    if(r->right>=s->sw || r->bottom>=s->sh || r->left>r->right || r->top>r->bottom)return 0;
    unsigned n=r->right-r->left+1;
    const unsigned char* input=pixels;
    unsigned char clipped[256];
#if JD_FORMAT == 2 && JD_FASTDECODE >= 1
    /* Fast IDCT retains signed values beyond 0..255. Upstream's grayscale
       output casts them directly, wrapping white (256) to black (0). Rebuild
       only affected MCU output with saturation before averaging. Vendor code
       and its pinned license stay unchanged. */
    unsigned mx=decoder->msx*8,my=decoder->msy*8;bool outside=false;
    for(unsigned i=0;i<mx*my;++i)
        if(decoder->mcubuf[i]<0 || decoder->mcubuf[i]>255){outside=true;break;}
    if(outside) {
        unsigned side=1u<<decoder->scale;
        for(unsigned y=0;y<=r->bottom-r->top;++y)for(unsigned x=0;x<n;++x) {
            unsigned sum=0;
            for(unsigned yy=y*side;yy<(y+1)*side;++yy)for(unsigned xx=x*side;xx<(x+1)*side;++xx) {
                size_t at=((yy/8)*(mx/8)+xx/8)*64+(yy%8)*8+xx%8;
                int value=decoder->mcubuf[at];sum+=(unsigned)(value<0?0:value>255?255:value);
            }
            clipped[(size_t)y*n+x]=(unsigned char)(sum>>(2*decoder->scale));
        }
        input=clipped;
    }
#endif
    if(s->w==s->sw && s->h==s->sh) {
        for(unsigned y=r->top;y<=r->bottom;++y)
            memcpy(s->pixels+(size_t)y*s->w+r->left,input+(size_t)(y-r->top)*n,n);
    } else {
        /* Stream scaled MCU blocks directly into the bounded screen image;
           a tall map need not be halved again just to fit one allocation. */
        unsigned x0=((unsigned)r->left*s->w+s->sw-1)/s->sw;
        unsigned x1=((unsigned)(r->right+1)*s->w+s->sw-1)/s->sw;
        unsigned y0=((unsigned)r->top*s->h+s->sh-1)/s->sh;
        unsigned y1=((unsigned)(r->bottom+1)*s->h+s->sh-1)/s->sh;
        for(unsigned y=y0;y<y1;++y)for(unsigned x=x0;x<x1;++x) {
            unsigned sx=(unsigned)((uint64_t)x*s->sw/s->w),sy=(unsigned)((uint64_t)y*s->sh/s->h);
            s->pixels[(size_t)y*s->w+x]=input[(size_t)(sy-r->top)*n+sx-r->left];
        }
    }
    return 1;
}
static bool jpeg_scaled(const unsigned char* data,size_t bytes,unsigned bw,unsigned bh,
                        uint64_t source_limit,size_t output_limit,
                        unsigned char** gray,unsigned* width,unsigned* height) {
    if(gray)*gray=NULL;
    if(!data || !bytes || bytes>THINGSOS_MEMBER_BYTES || !bw || !bh || !gray || !width || !height)return false;
    if(bytes>=2 && data[0]==255 && data[1]==216) {
        void* pool=thingsos_image_buffer(16384);JDEC decoder;
        thumbnail_jpeg_t state={.data=data,.bytes=bytes};
        if(pool && thingsos_jpeg_prepare(&decoder,thumbnail_input,pool,16384,&state)==JDR_OK) {
            thingsos_size_t fit;
            if(decoder.width && decoder.height && decoder.width<=16384 && decoder.height<=16384 &&
               (uint64_t)decoder.width*decoder.height<=source_limit &&
               thingsos_image_fit(decoder.width,decoder.height,bw,bh,&fit)) {
                unsigned scale=0;
                while(scale<3 && (decoder.width>>(scale+1))>=fit.width && (decoder.height>>(scale+1))>=fit.height)++scale;
                state.sw=state.w=decoder.width>>scale;state.sh=state.h=decoder.height>>scale;
                if((uint64_t)state.w*state.h>output_limit){state.w=fit.width;state.h=fit.height;}
                if(!state.w || !state.h || (uint64_t)state.w*state.h>output_limit){free(pool);return false;}
                state.pixels=thingsos_image_buffer((size_t)state.w*state.h);
                if(state.pixels && thingsos_jpeg_decomp(&decoder,thumbnail_output,(uint8_t)scale)==JDR_OK) {
                    *gray=state.pixels;*width=state.w;*height=state.h;
#ifdef ESP_PLATFORM
                    ESP_LOGI("ThingsOS_cover","JPEG scaled 1/%u output=%ux%u scratch=16384 bytes",1u<<scale,state.w,state.h);
#endif
                    free(pool);return true;
                }
                free(state.pixels);
            }
        }
        free(pool);
    }
    return false;
}
bool thingsos_decode_thumbnail(const unsigned char* data,size_t bytes,unsigned bw,unsigned bh,
                              unsigned char** gray,unsigned* width,unsigned* height) {
    if(gray)*gray=NULL;
    if(!data || !bytes || bytes>THINGSOS_MEMBER_BYTES || !bw || !bh || !gray || !width || !height)return false;
    if(thingsos_is_webp(data,bytes))return thingsos_decode_webp(data,bytes,bw,bh,gray,width,height);
    if(jpeg_scaled(data,bytes,bw,bh,2u*1024u*1024u,2u*1024u*1024u,gray,width,height))return true;
    /* Progressive JPEG and other formats retain the existing decoder. */
    return thingsos_decode_image(data,bytes,gray,width,height);
}
bool thingsos_decode_reader_image(const unsigned char* data,size_t bytes,unsigned bw,unsigned bh,
                                 unsigned char** gray,unsigned* width,unsigned* height) {
    if(gray)*gray=NULL;
    if(!data || !bytes || bytes>THINGSOS_MEMBER_BYTES || !bw || !bh || !gray || !width || !height)return false;
    if(thingsos_is_webp(data,bytes))return thingsos_decode_webp(data,bytes,bw,bh,gray,width,height);
    if(bytes>=33 && !memcmp(data,"\x89PNG\r\n\x1a\n",8) && data[24]<=8 && data[28]==0)
        return reader_png_decode(data,bytes,gray,width,height);
    if(bytes>=10 && (!memcmp(data,"GIF87a",6)||!memcmp(data,"GIF89a",6)))
        return thingsos_decode_gif(data,bytes,1024u*1024u,gray,width,height);
    unsigned ordinary_w,ordinary_h;
    if(bytes>=2 && data[0]==255 && data[1]==216 &&
       thingsos_image_info(data,bytes,&ordinary_w,&ordinary_h) &&
       (uint64_t)ordinary_w*ordinary_h<=1024u*1024u &&
       thingsos_decode_image(data,bytes,gray,width,height))return true;
    if(jpeg_scaled(data,bytes,bw,bh,64u*1024u*1024u,1024u*1024u,gray,width,height))return true;
    return thingsos_decode_image(data,bytes,gray,width,height);
}
