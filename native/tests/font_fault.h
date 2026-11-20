/* SPDX-License-Identifier: Apache-2.0
 * Fail the real FreeType allocator, rather than bypassing the glyph renderer. */
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_SYSTEM_H
static FT_Alloc_Func font_saved_alloc;
static FT_Realloc_Func font_saved_realloc;
static void* font_fail_alloc(FT_Memory memory,long bytes) {
    (void)memory;(void)bytes;return NULL;
}
static void* font_fail_realloc(FT_Memory memory,long old,long bytes,void* block) {
    (void)memory;(void)old;(void)bytes;(void)block;return NULL;
}
static void font_fault_begin(thingsos_font_t* font) {
    FT_Memory memory=((FT_Face)font->face)->memory;
    font_saved_alloc=memory->alloc;font_saved_realloc=memory->realloc;
    memory->alloc=font_fail_alloc;memory->realloc=font_fail_realloc;
}
static void font_fault_end(thingsos_font_t* font) {
    FT_Memory memory=((FT_Face)font->face)->memory;
    memory->alloc=font_saved_alloc;memory->realloc=font_saved_realloc;
}
