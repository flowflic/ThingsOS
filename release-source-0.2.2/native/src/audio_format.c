// SPDX-License-Identifier: Apache-2.0
#include "thingsos/audio_format.h"
#include <string.h>
#include <limits.h>
static uint32_t le32(const uint8_t* p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static unsigned le16(const uint8_t* p){return p[0]|(unsigned)p[1]<<8;}
static void put32(uint8_t* p,uint32_t n){for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(n>>(i*8));}
static void put16(uint8_t* p,unsigned n){p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);}
bool thingsos_wav_read(FILE* f,thingsos_wav_t* result){
    if(!f||!result)return false;
    if(fseek(f,0,SEEK_END))return false;long length=ftell(f);
    if(length<44||fseek(f,0,SEEK_SET))return false;
    uint8_t h[16];if(fread(h,1,12,f)!=12||memcmp(h,"RIFF",4)||memcmp(h+8,"WAVE",4))return false;
    uint64_t end=(uint64_t)le32(h+4)+8;if(end>(uint64_t)length||end<44)return false;
    thingsos_wav_t wav={0};bool format=false,data=false;uint64_t at=12;
    for(unsigned chunks=0;at+8<=end&&chunks<1024;++chunks){
        if(at>LONG_MAX||fseek(f,(long)at,SEEK_SET)||fread(h,1,8,f)!=8)return false;
        uint32_t n=le32(h+4);uint64_t payload=at+8,next=payload+n+(n&1u);
        if(next>end)return false;
        if(!memcmp(h,"fmt ",4)){
            if(format||n<16||fread(h,1,16,f)!=16||le16(h)!=1||le16(h+14)!=16)return false;
            wav.channels=le16(h+2);wav.rate=le32(h+4);
            if((wav.channels!=1&&wav.channels!=2)||wav.rate<8000||wav.rate>48000||
               le16(h+12)!=wav.channels*2||le32(h+8)!=wav.rate*wav.channels*2)return false;
            format=true;
        }else if(!memcmp(h,"data",4)){
            if(data||payload>UINT32_MAX)return false;wav.offset=(uint32_t)payload;wav.bytes=n;data=true;
        }
        at=next;
    }
    if(!format||!data||!wav.bytes||wav.bytes%(wav.channels*2))return false;
    *result=wav;return fseek(f,(long)wav.offset,SEEK_SET)==0;
}
bool thingsos_wav_header(FILE* f,uint32_t rate,unsigned channels,uint32_t bytes){
    if(!f||rate<8000||rate>48000||(channels!=1&&channels!=2)||bytes>UINT32_MAX-36||bytes%(channels*2))return false;
    uint8_t h[44]={0};memcpy(h,"RIFF",4);put32(h+4,bytes+36);memcpy(h+8,"WAVEfmt ",8);put32(h+16,16);
    put16(h+20,1);put16(h+22,channels);put32(h+24,rate);put32(h+28,rate*channels*2);
    put16(h+32,channels*2);put16(h+34,16);memcpy(h+36,"data",4);put32(h+40,bytes);
    long before=ftell(f);if(before<0||fseek(f,0,SEEK_SET)||fwrite(h,1,44,f)!=44)return false;
    return fseek(f,before<44?44:before,SEEK_SET)==0;
}
size_t thingsos_audio_resample(thingsos_audio_resampler_t* r,const int16_t* input,size_t frames,unsigned channels,int16_t* out,size_t capacity){
    if(!r||!input||!out||(channels!=1&&channels!=2)||r->rate<8000||r->rate>48000||
       frames>SIZE_MAX/16000||capacity<frames*16000/r->rate+2)return 0;
    size_t n=0;
    for(size_t i=0;i<frames;++i){
        int32_t current=input[i*channels];if(channels==2)current=(current+(int32_t)input[i*2+1])/2;
        if(!r->ready){out[n++]=(int16_t)current;r->previous=current;r->until=r->rate;r->ready=true;continue;}
        while(r->until<=16000){out[n++]=(int16_t)(r->previous+(current-r->previous)*(int64_t)r->until/16000);r->until+=r->rate;}
        r->until-=16000;r->previous=current;
    }
    return n;
}
