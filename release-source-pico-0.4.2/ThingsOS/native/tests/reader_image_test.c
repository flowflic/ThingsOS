// SPDX-License-Identifier: Apache-2.0
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "thingsos/graphics.h"
extern size_t image_test_limit,image_test_rejected,image_test_max_request;
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static unsigned char* read_image(const char* root,const char* name,size_t* bytes) {
    char path[1024];snprintf(path,sizeof(path),"%s/%s",root,name);
    FILE* f=fopen(path,"rb");CHECK(f && !fseek(f,0,SEEK_END));long n=ftell(f);CHECK(n>0);rewind(f);
    unsigned char* b=malloc((size_t)n);CHECK(b && fread(b,1,n,f)==(size_t)n);fclose(f);*bytes=(size_t)n;return b;
}
int main(int argc,char** argv) {
    CHECK(argc==2 || argc==3);
    size_t bytes;unsigned char *b=read_image(argv[1],"rgba.png",&bytes),*gray=NULL,*reference=NULL;unsigned w,h,rw,rh;
    CHECK(!thingsos_decode_image(b,bytes,&gray,&w,&h) && !gray && image_test_rejected);
    CHECK(image_test_max_request>=3024000); /* Reproduces the actual screen failure. */
    image_test_rejected=image_test_max_request=0;
    CHECK(thingsos_decode_reader_image(b,bytes,684,1216,&gray,&w,&h) && w==1080 && h==700);
    CHECK(gray[0]==255 && gray[1]==197 && !image_test_rejected && image_test_max_request<=1024u*1024u);
    image_test_limit=SIZE_MAX;
    CHECK(thingsos_decode_image(b,bytes,&reference,&rw,&rh) && rw==w && rh==h && !memcmp(gray,reference,(size_t)w*h));
    free(gray);free(reference);image_test_limit=1736692;
    b[bytes-1]^=1;gray=NULL;
    CHECK(!thingsos_decode_reader_image(b,bytes,684,1216,&gray,&w,&h) && !gray);b[bytes-1]^=1;
    CHECK(!thingsos_decode_reader_image(b,bytes-5,684,1216,&gray,&w,&h));free(b);
    static const char* names[]={"palette.png","gray.png","gray-alpha.png","rgb.png","world-map-size.jpg","white-table.jpg","small.jpg"};
    for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);++i) {
        b=read_image(argv[1],names[i],&bytes);gray=NULL;image_test_max_request=image_test_rejected=0;
        CHECK(thingsos_decode_reader_image(b,bytes,684,1216,&gray,&w,&h));
        CHECK(w && h && (uint64_t)w*h<=1024u*1024u && !image_test_rejected);
        if(i==4)CHECK(w==624 && h==1216 && gray[0]>=62 && gray[0]<=66 && gray[(size_t)w*h-1]>=62 && gray[(size_t)w*h-1]<=66);
        if(i==0)CHECK(w==960 && h==514 && gray[0]==255 && gray[1]==255);
        if(i==5)for(size_t j=0;j<(size_t)w*h;++j)CHECK(gray[j]>=250);
        if(i==6){
            CHECK(thingsos_decode_image(b,bytes,&reference,&rw,&rh) && w==rw && h==rh && !memcmp(gray,reference,(size_t)w*h));free(reference);
        }
        free(b);free(gray);
    }
    b=read_image(argv[1],"cover.webp",&bytes);
    CHECK(thingsos_image_info(b,bytes,&w,&h) && w==1080 && h==1440);
    CHECK(thingsos_decode_thumbnail(b,bytes,148,188,&gray,&w,&h) && w==141 && h==188);
    CHECK(gray[0]>=66 && gray[0]<=74);free(gray);
    CHECK(thingsos_decode_reader_image(b,bytes,684,1216,&gray,&w,&h) && w==684 && h==912);
    free(gray);
    CHECK(thingsos_decode_wallpaper(b,bytes,&gray,&w,&h) && w==912 && h==1216);
    free(gray);
    CHECK(!thingsos_decode_wallpaper(b,bytes/2,&gray,&w,&h) && !gray);
    free(b);
    b=read_image(argv[1],"alpha.webp",&bytes);
    CHECK(thingsos_decode_wallpaper(b,bytes,&gray,&w,&h) && w==16 && h==12);
    CHECK(gray[0]==255 && gray[1]==127);free(gray);free(b);
    if(argc==3) {
        thingsos_zip_t zip;CHECK(thingsos_zip_open(&zip,argv[2]));unsigned count=0;
        for(size_t i=0;i<zip.count;++i) {
            const char* name=zip.entries[i].name;
            if(!strstr(name,".jpg") && !strstr(name,".png"))continue;
            unsigned source_w=0,source_h=0;
            CHECK(thingsos_zip_image_info(&zip,name,&source_w,&source_h));
            uint64_t zip_size=zip.size;zip.size=0;unsigned cached_w=0,cached_h=0;
            CHECK(thingsos_zip_image_info(&zip,name,&cached_w,&cached_h) && source_w==cached_w && source_h==cached_h);zip.size=zip_size;
            if(!strcmp(name,"OEBPS/Images/a2.jpg"))CHECK(source_w==3800 && source_h==7400);
            CHECK(thingsos_zip_read(&zip,name,THINGSOS_MEMBER_BYTES,&b,&bytes));
            gray=NULL;image_test_rejected=image_test_max_request=0;
            bool ok=thingsos_decode_reader_image(b,bytes,684,1216,&gray,&w,&h);
            printf("%s %s %ux%u max_request=%zu rejected=%zu\n",ok?"PASS":"FAIL",name,w,h,image_test_max_request,image_test_rejected);
            CHECK(ok && !image_test_rejected && (uint64_t)w*h<=1024u*1024u);
            if(!strcmp(name,"OEBPS/Images/a5.jpg") || !strcmp(name,"OEBPS/Images/a6.jpg") || !strcmp(name,"OEBPS/Images/a7.jpg")) {
                size_t white=0;for(size_t j=0;j<(size_t)w*h;++j)white+=gray[j]>240;
                CHECK(white*2>(size_t)w*h); /* Actual white-background tables must not invert. */
            }
            if(strstr(name,"/a2.jpg")) {
                char path[1024];snprintf(path,sizeof(path),"%s/world-map-decoded.pgm",argv[1]);FILE* f=fopen(path,"wb");CHECK(f);
                fprintf(f,"P5\n%u %u\n255\n",w,h);CHECK(fwrite(gray,1,(size_t)w*h,f)==(size_t)w*h);fclose(f);
            }
            free(b);free(gray);++count;
        }
        CHECK(count==114);printf("Actual EPUB: %u/114 images passed under measured fragmentation\n",count);thingsos_zip_close(&zip);
    }
    puts("Reader image memory regression passed");return 0;
}
