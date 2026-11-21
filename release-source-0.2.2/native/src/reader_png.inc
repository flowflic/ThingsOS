// SPDX-License-Identifier: Apache-2.0
/* Non-interlaced PNG reader: two filtered rows plus bounded grayscale output.
   No full-size RGBA or inflated-image allocation, even on fragmented PSRAM. */
#include "zlib.h"
static uint32_t reader_png_u32(const unsigned char* p) {
    return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3];
}
static voidpf reader_png_alloc(voidpf opaque,uInt n,uInt size) {
    (void)opaque;
    if(size && n>SIZE_MAX/size)return NULL;
    return thingsos_image_buffer((size_t)n*size);
}
static void reader_png_free(voidpf opaque,voidpf p) {(void)opaque;free(p);}
static unsigned reader_png_paeth(int a,int b,int c) {
    int p=a+b-c,pa=abs(p-a),pb=abs(p-b),pc=abs(p-c);
    return (unsigned)(pa<=pb && pa<=pc?a:pb<=pc?b:c);
}
static bool reader_png_decode(const unsigned char* data,size_t bytes,
                              unsigned char** gray,unsigned* width,unsigned* height) {
    static const unsigned char signature[]={137,80,78,71,13,10,26,10};
    if(bytes<45 || memcmp(data,signature,8))return false;
    unsigned w=0,h=0,depth=0,type=0,channels=0,palette_count=0;
    unsigned char palette[256][4];
    for(unsigned i=0;i<256;++i){palette[i][0]=palette[i][1]=palette[i][2]=0;palette[i][3]=255;}
    uint16_t transparent[3]={0};bool have_transparent=false;
    bool header=false,idat=false,idat_closed=false,end=false;
    size_t offset=8;
    /* Validate all chunk bounds/CRCs and ordering before producing pixels. */
    while(offset<=bytes-12) {
        uint32_t n=reader_png_u32(data+offset);
        if(n>bytes-offset-12)return false;
        const unsigned char* tag=data+offset+4,*p=tag+4;
        uint32_t crc=(uint32_t)crc32(0,tag,(uInt)n+4);
        if(crc!=reader_png_u32(p+n))return false;
        if(!header && memcmp(tag,"IHDR",4))return false;
        if(!memcmp(tag,"IHDR",4)) {
            if(header || n!=13)return false;
            w=reader_png_u32(p);h=reader_png_u32(p+4);depth=p[8];type=p[9];
            if(!w||!h||w>16384||h>16384||(uint64_t)w*h>32u*1024u*1024u || p[10]||p[11]||p[12])return false;
            channels=type==0?1:type==2?3:type==3?1:type==4?2:type==6?4:0;
            if(!channels || (depth!=8 && !((type==0||type==3)&&(depth==1||depth==2||depth==4))))return false;
            header=true;
        } else if(!memcmp(tag,"PLTE",4)) {
            if(idat || palette_count || !n || n>768 || n%3)return false;
            palette_count=n/3;
            for(unsigned i=0;i<palette_count;++i)memcpy(palette[i],p+3*i,3);
        } else if(!memcmp(tag,"tRNS",4)) {
            if(idat || have_transparent)return false;
            if(type==3) {
                if(!palette_count || n>palette_count)return false;
                for(unsigned i=0;i<n;++i)palette[i][3]=p[i];
            } else if(type==0 && n==2)transparent[0]=(uint16_t)(p[0]*256+p[1]);
            else if(type==2 && n==6)for(unsigned i=0;i<3;++i)transparent[i]=(uint16_t)(p[i*2]*256+p[i*2+1]);
            else return false;
            have_transparent=true;
        } else if(!memcmp(tag,"IDAT",4)) {
            if(idat_closed || (type==3 && !palette_count))return false;
            idat=true;
        } else {
            if(idat)idat_closed=true;
            if(!memcmp(tag,"IEND",4)) {if(n || !idat || offset+12!=bytes)return false;end=true;break;}
            if(!(tag[0]&32))return false; /* Unknown critical chunk. */
        }
        offset+=(size_t)n+12;
    }
    if(!end)return false;
    unsigned step=1;
    while((uint64_t)((w+step-1)/step)*((h+step-1)/step)>1024u*1024u)step*=2;
    unsigned ow=(w+step-1)/step,oh=(h+step-1)/step;
    size_t row_bytes=((size_t)w*channels*depth+7)/8;
    unsigned bpp=(channels*depth+7)/8;
    unsigned char* output=thingsos_image_buffer((size_t)ow*oh);
    unsigned char* rows=thingsos_image_buffer((row_bytes+1)*2);
    if(!output||!rows){free(output);free(rows);return false;}
    unsigned char *row=rows,*previous=rows+row_bytes+1;
    memset(previous,0,row_bytes+1);
    z_stream stream={0};stream.zalloc=reader_png_alloc;stream.zfree=reader_png_free;
    if(inflateInit(&stream)!=Z_OK){free(output);free(rows);return false;}
    unsigned y=0;size_t used=0;bool ok=true,finished=false;
    for(offset=8;ok && offset<=bytes-12;) {
        uint32_t n=reader_png_u32(data+offset);const unsigned char* tag=data+offset+4;
        if(!memcmp(tag,"IDAT",4) && n) {
            if(finished){ok=false;break;}
            stream.next_in=(Bytef*)(tag+4);stream.avail_in=n;
            while(ok) {
                unsigned char extra;
                stream.next_out=y<h?row+used:&extra;
                stream.avail_out=y<h?(uInt)(row_bytes+1-used):1;
                uInt before_in=stream.avail_in,before_out=stream.avail_out;
                int status=inflate(&stream,Z_NO_FLUSH);
                size_t made=before_out-stream.avail_out;
                if(y>=h && made){ok=false;break;}
                used+=made;
                if(y<h && used==row_bytes+1) {
                    unsigned filter=row[0];if(filter>4){ok=false;break;}
                    for(size_t x=0;x<row_bytes;++x) {
                        unsigned a=x>=bpp?row[1+x-bpp]:0,b=previous[1+x],c=x>=bpp?previous[1+x-bpp]:0;
                        unsigned delta=filter==0?0:filter==1?a:filter==2?b:filter==3?(a+b)/2:reader_png_paeth(a,b,c);
                        row[1+x]=(unsigned char)(row[1+x]+delta);
                    }
                    if(y%step==0)for(unsigned x=0;x<w;x+=step) {
                        const unsigned char* p=row+1+(depth==8?(size_t)x*channels:0);
                        unsigned r,g,b,a=255;
                        if(type==3 || type==0) {
                            unsigned raw=depth==8?row[1+x]:(row[1+(size_t)x*depth/8]>>(8-depth-(x*depth%8)))&((1u<<depth)-1);
                            if(type==3) {
                                if(raw>=palette_count){ok=false;break;}
                                r=palette[raw][0];g=palette[raw][1];b=palette[raw][2];a=palette[raw][3];
                            } else {r=g=b=raw*255/((1u<<depth)-1);if(have_transparent && raw==transparent[0])a=0;}
                        } else if(type==2 || type==6) {
                            r=p[0];g=p[1];b=p[2];
                            if(type==6)a=p[3];
                            else if(have_transparent && r==transparent[0] && g==transparent[1] && b==transparent[2])a=0;
                        } else {r=g=b=p[0];a=p[1];}
                        unsigned value=(r*77+g*150+b*29)>>8;
                        output[(size_t)(y/step)*ow+x/step]=(unsigned char)((value*a+255*(255-a))/255);
                    }
                    unsigned char* swap=previous;previous=row;row=swap;used=0;++y;
                }
                if(status==Z_STREAM_END){finished=true;if(stream.avail_in)ok=false;break;}
                if(status==Z_BUF_ERROR && !stream.avail_in && !made)break;
                if(status!=Z_OK || (before_in==stream.avail_in && !made)){ok=false;break;}
                if(!stream.avail_in && stream.avail_out)break;
            }
        }
        offset+=(size_t)n+12;
    }
    ok=ok && finished && y==h && used==0;
    inflateEnd(&stream);free(rows);
    if(!ok){free(output);return false;}
    *gray=output;*width=ow;*height=oh;
    return true;
}
