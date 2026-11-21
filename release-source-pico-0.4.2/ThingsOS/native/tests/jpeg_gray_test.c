/* SPDX-License-Identifier: Apache-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "thingsos/graphics.h"
extern unsigned char* jpeg_reference(const unsigned char*,int,int*,int*);
#define CHECK(x) do {if(!(x)){fprintf(stderr,"JPEG gray line %d: %s\n",__LINE__,#x);exit(1);}} while(0)
int main(int argc,char** argv) {
    CHECK(argc>=2);
    const char* names[]={"progressive-420.jpg","progressive-422.jpg","progressive-444.jpg","progressive-gray.jpg","progressive-cmyk.jpg"};
    unsigned count=argc==2?5:(unsigned)argc-2;
    for(unsigned i=0;i<count;++i) {
        char path[1024];snprintf(path,sizeof(path),"%s/%s",argv[1],argc==2?names[i]:argv[i+2]);
        FILE* f=fopen(path,"rb");CHECK(f && !fseek(f,0,SEEK_END));long n=ftell(f);CHECK(n>0);rewind(f);
        unsigned char* bytes=malloc((size_t)n);CHECK(bytes && fread(bytes,1,(size_t)n,f)==(size_t)n);fclose(f);
        int rw,rh;unsigned w,h;unsigned char* expected=jpeg_reference(bytes,(int)n,&rw,&rh),*actual=NULL;
        CHECK(expected && thingsos_decode_image(bytes,(size_t)n,&actual,&w,&h));
        CHECK(w==(unsigned)rw && h==(unsigned)rh && !memcmp(actual,expected,(size_t)w*h));
        free(actual);actual=NULL;
        if(!strstr(path,"cmyk")) {
            CHECK(thingsos_decode_progressive_jpeg(bytes,(size_t)n,684,1216,&actual,&w,&h));
            for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)
                CHECK(actual[(size_t)y*w+x]==expected[(size_t)((uint64_t)y*rh/h)*rw+(unsigned)((uint64_t)x*rw/w)]);
        }
        free(actual);free(expected);free(bytes);printf("Progressive JPEG equals pinned decoder: %s\n",path);
    }
    return 0;
}
