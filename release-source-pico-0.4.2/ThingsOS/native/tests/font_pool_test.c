// SPDX-License-Identifier: Apache-2.0
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MODULE_H
#include FT_OUTLINE_H
#include FT_SYSTEM_H
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#undef NDEBUG
#include <assert.h>
extern const FT_Raster_Funcs thingsos_reference_grays_raster;
typedef struct {size_t live,pool_live,pool_allocations;int fail_pool;} counts_t;
typedef union {long double alignment;void* pointer;size_t size;unsigned char padding[16];} allocation_t;
/* The pool is rounded down to complete cells; host pointer size changes the
   cell padding. ESP32-S3 uses exactly 16384 bytes. */
static int is_pool(size_t n) {return n>=16320&&n<=16384;}
static void* allocate(FT_Memory m,long n) {
    counts_t* c=m->user;if(n<=0||(is_pool((size_t)n)&&c->fail_pool))return NULL;
    allocation_t* p=malloc(sizeof(*p)+(size_t)n);if(!p)return NULL;p->size=(size_t)n;++c->live;
    if(is_pool((size_t)n)){++c->pool_live;++c->pool_allocations;}return p+1;
}
static void release(FT_Memory m,void* b) {
    if(!b)return;counts_t* c=m->user;allocation_t* p=(allocation_t*)b-1;
    assert(c->live);--c->live;if(is_pool(p->size)){assert(c->pool_live);--c->pool_live;}free(p);
}
static void* resize(FT_Memory m,long old,long n,void* p) {
    if(!n){release(m,p);return NULL;}void* q=allocate(m,n);if(!q)return NULL;
    if(p)memcpy(q,p,(size_t)(old<n?old:n));release(m,p);return q;
}
static void compare(FT_Library library,FT_Face face,FT_Raster reference,unsigned cp,unsigned px) {
    assert(FT_Get_Char_Index(face,cp));assert(!FT_Set_Pixel_Sizes(face,0,px));assert(!FT_Load_Char(face,cp,FT_LOAD_DEFAULT));
    unsigned char before[128*128]={0},after[128*128]={0};
    FT_Bitmap bitmap={.rows=128,.width=128,.pitch=128,.buffer=before,.num_grays=256,.pixel_mode=FT_PIXEL_MODE_GRAY};
    FT_Raster_Params params={.target=&bitmap,.source=&face->glyph->outline,.flags=FT_RASTER_FLAG_AA};
    assert(!thingsos_reference_grays_raster.raster_render(reference,&params));
    bitmap.buffer=after;assert(!FT_Outline_Render(library,&face->glyph->outline,&params));
    assert(!memcmp(before,after,sizeof(before)));
    unsigned nonzero=0;for(size_t i=0;i<sizeof(after);++i)nonzero+=after[i]!=0;assert(nonzero);
}
int main(int argc,char** argv) {
    assert(argc==2);counts_t counts[2]={{0}};
    struct FT_MemoryRec_ memory[2];FT_Library library[2];FT_Face face[2];FT_Raster reference[2];
    for(unsigned i=0;i<2;++i) {
        memory[i]=(struct FT_MemoryRec_){.user=&counts[i],.alloc=allocate,.free=release,.realloc=resize};
        assert(!FT_New_Library(&memory[i],&library[i]));FT_Add_Default_Modules(library[i]);
        assert(!FT_New_Face(library[i],argv[1],0,&face[i]));
        assert(!thingsos_reference_grays_raster.raster_new(&memory[i],&reference[i]));
        assert(!FT_Set_Pixel_Sizes(face[i],0,46));assert(!FT_Load_Char(face[i],'W',FT_LOAD_DEFAULT));
        unsigned char output[128*128]={0};FT_Bitmap bitmap={.rows=128,.width=128,.pitch=128,.buffer=output,.num_grays=256,.pixel_mode=FT_PIXEL_MODE_GRAY};
        FT_Raster_Params params={.target=&bitmap,.source=&face[i]->glyph->outline,.flags=FT_RASTER_FLAG_AA};
        counts[i].fail_pool=1;assert(FT_Outline_Render(library[i],&face[i]->glyph->outline,&params));counts[i].fail_pool=0;
        size_t previous=counts[i].pool_allocations;
        assert(!FT_Outline_Render(library[i],&face[i]->glyph->outline,&params));
        assert(counts[i].pool_allocations==previous+1);
    }
    static const unsigned glyphs[]={'A','W','g','0','9','@','#','?',0x4e2d,0x6587,0x4e66,0x3002,0xff0c};
    static const unsigned sizes[]={18,30,46,72};
    size_t allocations[2]={counts[0].pool_allocations,counts[1].pool_allocations};
    for(unsigned size=0;size<4;++size)for(unsigned g=0;g<sizeof(glyphs)/sizeof(glyphs[0]);++g)
        for(unsigned i=0;i<2;++i)compare(library[i],face[i],reference[i],glyphs[g],sizes[size]);
    /* Interleaved libraries must own independent reusable workspaces. */
    assert(counts[0].pool_allocations==allocations[0]&&counts[1].pool_allocations==allocations[1]);
    for(unsigned i=0;i<2;++i){thingsos_reference_grays_raster.raster_done(reference[i]);FT_Done_Face(face[i]);FT_Done_Library(library[i]);assert(!counts[i].live&&!counts[i].pool_live);}
    puts("Gray pool: 104 pixel-identical raster comparisons, independent reuse, OOM recovery and full cleanup passed");
    return 0;
}
