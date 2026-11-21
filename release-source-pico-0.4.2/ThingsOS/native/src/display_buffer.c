// SPDX-License-Identifier: Apache-2.0
#include "thingsos/display_buffer.h"
#if defined(__GNUC__)
__attribute__((optimize("O2")))
#endif
bool thingsos_panel_pack_rows(const thingsos_canvas_t* c, unsigned char* fb, size_t bytes,
                              bool monochrome, unsigned first, unsigned rows) {
    if(c && c->pixels && c->width==1216 && c->height==684 && fb &&
       bytes>=THINGSOS_PANEL_BYTES && first<=684 && rows && rows<=684-first) {
        /* Landscape is the native panel order. Reuse the same 8-bit canvas;
           no rotated full-page allocation or in-place permutation. */
        size_t begin=(size_t)first*608,end=(size_t)(first+rows)*608;
        for(size_t i=begin;i<end;++i) {
            unsigned a=c->pixels[i*2],b=c->pixels[i*2+1];
            fb[i]=monochrome?(unsigned char)((a<128?0:15)|(b<128?0:240))
                            :(unsigned char)((a>>4)|(b&240));
        }
        return true;
    }
    if (!c || !c->pixels || c->width != 684 || c->height != 1216 || !fb ||
        bytes < THINGSOS_PANEL_BYTES || first > 1216 || !rows || rows > 1216-first ||
        (first & 1) || (rows & 1)) return false;
    /* Tile the transpose: nearby input rows and output bytes stay in cache.
       Each physical byte is written once; even x is the LOW nibble in epdiy. */
    unsigned last = first + rows;
    for (unsigned by = first; by < last; by += 16) {
        unsigned pairs = last-by < 16 ? (last-by)/2 : 8;
        for (unsigned bx = 0; bx < 684; bx += 16) {
            unsigned end = bx + 16 < 684 ? bx + 16 : 684;
            for (unsigned x = bx; x < end; ++x) {
                const unsigned char* src = c->pixels + (size_t)by * 684 + x;
                unsigned char* dst = fb + (size_t)(683 - x) * 608 + by / 2;
                for (unsigned pair = 0; pair < pairs; ++pair) {
                    unsigned a = src[(size_t)pair*1368], b = src[(size_t)pair*1368+684];
                    dst[pair] = monochrome
                        ? (unsigned char)((a < 128 ? 0 : 15) | (b < 128 ? 0 : 240))
                        : (unsigned char)((a >> 4) | (b & 240));
                }
            }
        }
    }
    return true;
}
bool thingsos_panel_pack(const thingsos_canvas_t* c, unsigned char* fb, size_t bytes,
                         bool monochrome) {
    return thingsos_panel_pack_rows(c,fb,bytes,monochrome,0,c && c->width==1216?684:1216);
}
#if defined(__GNUC__)
__attribute__((optimize("O2")))
#endif
bool thingsos_panel_pack_diff(const thingsos_canvas_t* c, unsigned char* fb, size_t bytes,
                             bool monochrome, unsigned* first, unsigned* rows) {
    if (!c || !c->pixels || c->width!=684 || c->height!=1216 || !fb ||
        bytes<THINGSOS_PANEL_BYTES || !first || !rows) return false;
    unsigned low=1216, high=0;
    for(unsigned by=0;by<1216;by+=16) for(unsigned bx=0;bx<684;bx+=16) {
        unsigned end=bx+16<684?bx+16:684;
        for(unsigned x=bx;x<end;++x) {
            const unsigned char* src=c->pixels+(size_t)by*684+x;
            unsigned char* dst=fb+(size_t)(683-x)*608+by/2;
            for(unsigned pair=0;pair<8;++pair) {
                unsigned a=src[pair*1368],b=src[pair*1368+684];
                unsigned char value=monochrome?(unsigned char)((a<128?0:15)|(b<128?0:240))
                                             :(unsigned char)((a>>4)|(b&240));
                if(dst[pair]!=value) {
                    unsigned y=by+pair*2;
                    if(y<low)low=y;
                    if(y+2>high)high=y+2;
                    dst[pair]=value;
                }
            }
        }
    }
    *first=high?low:0;*rows=high?high-low:0;
    return true;
}
