// SPDX-License-Identifier: Apache-2.0
#include "thingsos/audio_denoise.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"Denoise line %d: %s\n",__LINE__,#x); exit(1); } } while (0)

/* Delayed, non-identity backend: test framing independently of the opaque
 * target-only network. It models the pinned STFT's one-hop output delay. */
typedef struct { int16_t previous[256]; unsigned calls, fail_at; } backend_t;
static bool infer(void* context, int16_t* in, int16_t* out) {
    backend_t* b = context;
    if (++b->calls == b->fail_at) return false;
    for (unsigned i=0;i<256;++i) { out[i] = b->previous[i]; b->previous[i] = in[i]/2; }
    return true;
}

static void segment(size_t length, size_t stride) {
    int16_t input[8000], result[8000], block[514];
    for (size_t i=0;i<8000;++i) input[i] = (int16_t)((i*113u)%50000-25000);
    backend_t b={0}; thingsos_audio_denoise_t s;
    thingsos_audio_denoise_init(&s,infer,&b);
    size_t total=0;
    for (size_t at=0;at<length;) {
        size_t n=length-at; if(n>stride)n=stride;
        size_t produced=999; block[512]=12345; block[513]=-23456;
        CHECK(thingsos_audio_denoise_feed(&s,input+at,n,block,512,&produced));
        CHECK(produced<=512 && block[512]==12345 && block[513]==-23456);
        memcpy(result+total,block,produced*sizeof(*block)); total+=produced; at+=n;
    }
    size_t produced=999;
    CHECK(thingsos_audio_denoise_finish(&s,block,512,&produced));
    memcpy(result+total,block,produced*sizeof(*block)); total+=produced;
    CHECK(total==length && s.accepted==s.emitted && s.finished);
    for(size_t i=0;i<length;++i) CHECK(result[i]==input[i]/2);
    CHECK(thingsos_audio_denoise_finish(&s,block,512,&produced) && produced==0);
    CHECK(!thingsos_audio_denoise_feed(&s,input,1,block,512,&produced));
}

int main(void) {
    size_t lengths[]={0,1,7,255,256,257,511,512,513,767,768,1023,7001};
    size_t strides[]={1,17,97,255,256,257,511,512};
    for(size_t i=0;i<sizeof(lengths)/sizeof(lengths[0]);++i)
        for(size_t j=0;j<sizeof(strides)/sizeof(strides[0]);++j) segment(lengths[i],strides[j]);
    /* Pause/resume = finish and a fresh backend. Partial short segments must
     * neither disappear nor retain the prior segment's overlap. */
    segment(37,17); segment(9,1); segment(513,97);
    int16_t in[512]={0},out[512];size_t n;
    backend_t b={0};thingsos_audio_denoise_t s;
    thingsos_audio_denoise_init(&s,infer,&b);
    CHECK(!thingsos_audio_denoise_feed(&s,in,512,out,255,&n));
    CHECK(!s.accepted && !b.calls && !n);
    CHECK(thingsos_audio_denoise_feed(&s,in,1,out,512,&n));
    CHECK(!thingsos_audio_denoise_finish(&s,out,0,&n));
    CHECK(!s.failed && !s.finished && s.accepted==1);
    CHECK(thingsos_audio_denoise_finish(&s,out,512,&n) && n==1);
    memset(&b,0,sizeof(b));b.fail_at=2;
    thingsos_audio_denoise_init(&s,infer,&b);
    CHECK(!thingsos_audio_denoise_feed(&s,in,512,out,512,&n) && s.failed && n==0);
    CHECK(!thingsos_audio_denoise_finish(&s,out,512,&n));
    memset(&b,0,sizeof(b));b.fail_at=2;
    thingsos_audio_denoise_init(&s,infer,&b);
    CHECK(thingsos_audio_denoise_feed(&s,in,1,out,512,&n));
    CHECK(!thingsos_audio_denoise_finish(&s,out,512,&n) && s.failed && n==0);
    CHECK(!thingsos_audio_denoise_feed(NULL,in,1,out,512,&n));
    puts("Recording denoise: chunking, delay/tail, pause segments, capacity and inference failures passed");
    return 0;
}
