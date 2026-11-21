/* SPDX-License-Identifier: Apache-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "src/dec/alphai_dec.h"
#include "src/dec/vp8li_dec.h"
#define CHECK(x) do { if(!(x)){fprintf(stderr,"alpha test line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
static unsigned char* read_file(const char* path,size_t* bytes) {
    FILE* f=fopen(path,"rb");CHECK(f && !fseek(f,0,SEEK_END));long n=ftell(f);CHECK(n>0);rewind(f);
    unsigned char* p=malloc((size_t)n);CHECK(p && fread(p,1,n,f)==(size_t)n);fclose(f);*bytes=(size_t)n;return p;
}
static void alpha_test(const char* root,const char* name) {
    char path[1024];snprintf(path,sizeof(path),"%s/%s.webp",root,name);
    size_t bytes,expected_bytes;unsigned char* b=read_file(path,&bytes);
    WebPBitstreamFeatures features;CHECK(WebPGetFeatures(b,bytes,&features)==VP8_STATUS_OK && features.has_alpha);
    snprintf(path,sizeof(path),"%s/%s.alpha",root,name);unsigned char* expected=read_file(path,&expected_bytes);
    CHECK(expected_bytes==(size_t)features.width*features.height);
    WebPHeaderStructure h={0};h.data=b;h.data_size=bytes;h.have_all_data=1;
    CHECK(WebPParseHeaders(&h)==VP8_STATUS_OK && h.alpha_data && (h.alpha_data[0]&3)==1);
    ALPHDecoder a={0};a.width=features.width;a.height=features.height;
    a.filter=(WEBP_FILTER_TYPE)((h.alpha_data[0]>>2)&3);a.method=1;
    CHECK(VP8InitIo(&a.io));a.io.width=a.width;a.io.height=a.height;a.io.crop_bottom=a.height;
    a.output=calloc(expected_bytes,1);CHECK(a.output);VP8FiltersInit();
    CHECK(VP8LDecodeAlphaHeader(&a,h.alpha_data+1,h.alpha_data_size-1));
    for(int y=1;y<a.height;y+=13)CHECK(VP8LDecodeAlphaImageStream(&a,y));
    CHECK(VP8LDecodeAlphaImageStream(&a,a.height));
    CHECK(!memcmp(expected,a.output,expected_bytes));
    VP8LDelete(a.vp8l_dec);free(a.output);
    /* The public decoder exercises the bounded alpha row window as well as
       the history codec. Its external output avoids counting the host-side
       comparison image against the device's decoder scratch budget. */
    unsigned char* rgba=malloc(expected_bytes*4);CHECK(rgba);
    CHECK(WebPDecodeRGBAInto(b,bytes,rgba,expected_bytes*4,features.width*4));
    for(size_t i=0;i<expected_bytes;++i)CHECK(rgba[i*4+3]==expected[i]);
    CHECK(!WebPDecodeRGBAInto(b,bytes/2,rgba,expected_bytes*4,features.width*4));
    free(rgba);free(expected);free(b);
    printf("Alpha exactly matches independent reference: %s\n",name);
}
int main(int argc,char** argv) {
    CHECK(argc>=2);
    if(argc==2){alpha_test(argv[1],"alpha-complex");alpha_test(argv[1],"alpha-filtered");}
    else for(int i=2;i<argc;++i)alpha_test(argv[1],argv[i]);
    return 0;
}
