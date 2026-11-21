// SPDX-License-Identifier: Apache-2.0
/* Read only the PNG/GIF/JPEG header prefix needed for deterministic pagination.
   The later full image read still verifies the complete ZIP member CRC. */
static bool zip_picture_header(const unsigned char* b,size_t n,unsigned* w,unsigned* h) {
    if(thingsos_is_webp(b,n))return thingsos_webp_info(b,n,w,h);
    unsigned x=0,y=0;
    if(n>=24 && !memcmp(b,"\x89PNG\r\n\x1a\n",8) && !memcmp(b+12,"IHDR",4)) {
        x=(unsigned)b[16]<<24|(unsigned)b[17]<<16|(unsigned)b[18]<<8|b[19];
        y=(unsigned)b[20]<<24|(unsigned)b[21]<<16|(unsigned)b[22]<<8|b[23];
    } else if(n>=10 && (!memcmp(b,"GIF87a",6)||!memcmp(b,"GIF89a",6))) {
        x=u16(b+6);y=u16(b+8);
    } else if(n>=4 && b[0]==255 && b[1]==216) {
        size_t p=2;
        while(p<n) {
            if(b[p++]!=255)return false;
            while(p<n && b[p]==255)++p;
            if(p>=n)return false;
            unsigned marker=b[p++];
            if(marker==0xd9 || marker==0xda)return false;
            if(marker==0x01 || (marker>=0xd0 && marker<=0xd7))continue;
            if(n-p<2)return false;
            unsigned len=(unsigned)b[p]*256+b[p+1];
            if(len<2 || len>n-p)return false;
            if(marker==0xc0 || marker==0xc1 || marker==0xc2) {
                if(len<8 || b[p+2]!=8)return false;
                y=(unsigned)b[p+3]*256+b[p+4];x=(unsigned)b[p+5]*256+b[p+6];break;
            }
            p+=len;
        }
    }
    if(!x || !y || x>16384 || y>16384 || (uint64_t)x*y>64u*1024u*1024u)return false;
    *w=x;*h=y;return true;
}
bool thingsos_file_image_info(const char* file,unsigned* w,unsigned* h) {
    FILE* f=fopen(file,"rb");
    if(!f)return false;
    long size=fseek(f,0,SEEK_END)?-1:ftell(f);
    size_t capacity=size>0 && size<=THINGSOS_MEMBER_BYTES?(size_t)size:0;
    if(capacity>65536)capacity=65536;
    unsigned char* prefix=capacity?zip_buffer(capacity):NULL;
    bool ok=prefix && !fseek(f,0,SEEK_SET) && fread(prefix,1,capacity,f)==capacity &&
        zip_picture_header(prefix,capacity,w,h);
    free(prefix);
    fclose(f);
    return ok;
}
bool thingsos_zip_image_info(thingsos_zip_t* z,const char* name,unsigned* w,unsigned* h) {
    if(!z||!name||!w||!h)return false;
    thingsos_zip_entry_t* v=NULL;
    for(size_t i=0;i<z->count;++i)if(!strcmp(name,z->entries[i].name)){v=z->entries+i;break;}
    if(!v)return false;
    if(v->image_probed){*w=v->image_width;*h=v->image_height;return *w && *h;}
    if(!v->bytes || v->bytes>THINGSOS_MEMBER_BYTES || v->compressed>THINGSOS_MEMBER_BYTES || (v->method!=0 && v->method!=8))return false;
    unsigned char header[30];
    if(!at(z,v->offset,header,30) || u32(header)!=0x04034b50 || u16(header+8)!=v->method || u16(header+6)!=v->flags)return false;
    unsigned nl=u16(header+26);char local[256];
    if(nl!=strlen(name) || nl>=sizeof(local) || !at(z,(uint64_t)v->offset+30,local,nl) || memcmp(local,name,nl))return false;
    uint64_t pos=(uint64_t)v->offset+30+nl+u16(header+28);
    if(pos>z->size || v->compressed>z->size-pos)return false;
    size_t capacity=v->bytes<65536?v->bytes:65536;
    unsigned char* prefix=zip_buffer(capacity);if(!prefix)return false;
    bool ok=false;
    if(v->method==0) {
        ok=v->bytes==v->compressed && at(z,pos,prefix,capacity) && zip_picture_header(prefix,capacity,w,h);
    } else {
        z_stream stream={0};
#ifdef ESP_PLATFORM
        stream.zalloc=zip_inflate_alloc;stream.zfree=zip_inflate_free;
#endif
        if(inflateInit2(&stream,-MAX_WBITS)==Z_OK) {
            unsigned char compressed[1024];uint32_t consumed=0;size_t made=0;int status=Z_OK;
            while(status==Z_OK && consumed<v->compressed && made<capacity) {
                unsigned n=v->compressed-consumed;if(n>sizeof(compressed))n=sizeof(compressed);
                if(!at(z,pos+consumed,compressed,n))break;
                consumed+=n;stream.next_in=compressed;stream.avail_in=n;
                stream.next_out=prefix+made;stream.avail_out=(uInt)(capacity-made);
                status=inflate(&stream,Z_NO_FLUSH);made=capacity-stream.avail_out;
                if(zip_picture_header(prefix,made,w,h)){ok=true;break;}
            }
            inflateEnd(&stream);
        }
    }
    free(prefix);
    if(ok){v->image_width=(uint16_t)*w;v->image_height=(uint16_t)*h;v->image_probed=true;}
    return ok;
}
