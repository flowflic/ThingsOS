// SPDX-License-Identifier: Apache-2.0
#ifndef THINGSOS_GRAPHICS_H
#define THINGSOS_GRAPHICS_H
#include "thingsos/content.h"
typedef struct {
    unsigned char* pixels;
    int width, height;
    bool inverted; /* Panel output polarity. UI drawing uses normal grayscale. */
    bool vertical_text; /* Internal transposed text view; never passed to the panel. */
} thingsos_canvas_t;
typedef struct {
    void *library, *face;
    unsigned size, weight;
    /* Temporary UI draw context; zero keeps the caller's reading typography. */
    unsigned ui_size_percent, ui_weight_override;
    void* metrics;
    void* bitmaps;
    uint64_t signature;
    unsigned bitmap_hits, glyph_loads;
    unsigned errors; /* Monotonic: a frame with new font errors must not be published. */
    bool italic; /* Shear at blit time; shares upright glyph cache. */
    bool monochrome; /* Native hinted 1-bit rasterization; metrics/bitmaps distinguish this mode. */
    /* Quantize gray coverage to 1 bit with glyph-local error diffusion for
       DU edges. Hinting/advances and the cached gray raster stay unchanged. */
    bool dither;
    bool vertical_text; /* Scoped by the explicit body-flow API; cached rasters stay upright. */
} thingsos_font_t;
bool thingsos_font_memory(thingsos_font_t* font, const unsigned char* data, size_t bytes);
bool thingsos_font_file(thingsos_font_t* font, const char* path);
typedef struct {
    uint64_t requests, disk_reads, disk_bytes, cache_hits;
    unsigned cache_bytes;
} thingsos_font_io_t;
bool thingsos_font_io(const thingsos_font_t*, thingsos_font_io_t*);
void thingsos_font_close(thingsos_font_t* font);
bool thingsos_font_size(thingsos_font_t* font, unsigned size);
bool thingsos_font_weight(thingsos_font_t* font, unsigned weight);
void thingsos_rect(thingsos_canvas_t* canvas, int x, int y, int w, int h, unsigned char shade);
int thingsos_glyph(
    thingsos_font_t* font, thingsos_canvas_t* canvas, uint32_t cp, int x, int baseline);
void thingsos_label(
    thingsos_font_t* font, thingsos_canvas_t* canvas, const char* text, int x, int baseline,
    int max_width);
void thingsos_label_centered(
    thingsos_font_t* font, thingsos_canvas_t* canvas, const char* text, int x, int baseline,
    int width);
void thingsos_label_right(
    thingsos_font_t* font, thingsos_canvas_t* canvas, const char* text, int x, int baseline,
    int width);
typedef bool (*thingsos_picture_fn)(
    void* context, const thingsos_inline_t* image, thingsos_canvas_t* canvas, int x, int y, int w,
    int h);
/* Called between rendered rows; false hands font ownership back to the UI. */
typedef bool (*thingsos_layout_continue_fn)(void* context);
#define THINGSOS_TEXT_HIT_LIMIT 32
#define THINGSOS_LINE_HIT_LIMIT 64
#define THINGSOS_GLYPH_HIT_LIMIT 4096
typedef struct {
    uint32_t offset;
    uint16_t bytes;
    int16_t x,y,w,h;
} thingsos_glyph_hit_t;
/* Screen box of one interactive text range (currently footnote markers) on the
   page being laid out. Layout fills it only when a caller passes a sink, so the
   pre-render worker never publishes boxes for a page nobody is touching. */
typedef struct {
    size_t begin, end;
    int x, y, w, h;
} thingsos_text_hit_t;
typedef struct {
    thingsos_text_hit_t items[THINGSOS_TEXT_HIT_LIMIT];
    size_t count;
    thingsos_text_hit_t lines[THINGSOS_LINE_HIT_LIMIT];
    size_t line_count;
    thingsos_glyph_hit_t glyphs[THINGSOS_GLYPH_HIT_LIMIT];
    size_t glyph_count;
} thingsos_text_hits_t;
/* indent_px indents the first line of every paragraph; zero keeps the source
   layout. Page breaks in the middle of a paragraph are never indented. */
/* Caller retains pixels/hits and resets this cursor whenever content or layout
   changes. A paused page resumes without drawing its completed rows again. */
typedef struct {
    size_t offset;
    int x, y, line;
    unsigned size;
    bool active, heading, paragraph_ink, paragraph_head;
} thingsos_text_cursor_t;
size_t thingsos_text_page_resume(
    thingsos_font_t*, thingsos_canvas_t*, const thingsos_chapter_t*, size_t,
    int, int, int, int, unsigned, unsigned, bool, thingsos_picture_fn, void*,
    thingsos_text_hits_t*, thingsos_layout_continue_fn, void*, bool*, thingsos_text_cursor_t*);
/* Traditional vertical body flow: upright cells top-to-bottom, columns
 * right-to-left. Pagination, pictures and touch boxes share physical geometry.
 * Existing APIs and note/UI string layout retain horizontal flow. */
size_t thingsos_text_page_resume_flow(
    thingsos_font_t*, thingsos_canvas_t*, const thingsos_chapter_t*, size_t,
    int, int, int, int, unsigned, unsigned, bool, thingsos_picture_fn, void*,
    thingsos_text_hits_t*, thingsos_layout_continue_fn, void*, bool*, thingsos_text_cursor_t*, bool);
size_t thingsos_text_page_layout_flow(
    thingsos_font_t*, thingsos_canvas_t*, const thingsos_chapter_t*, size_t,
    int, int, int, int, unsigned, unsigned, bool, thingsos_picture_fn, void*, thingsos_text_hits_t*, bool);
size_t thingsos_text_page_interruptible(
    thingsos_font_t* font, thingsos_canvas_t* canvas, const thingsos_chapter_t* chapter,
    size_t start, int left, int top, int width, int height, unsigned line_percent,
    unsigned indent_px, bool epub_paragraphs, thingsos_picture_fn image, void* context,
    thingsos_text_hits_t* hits, thingsos_layout_continue_fn proceed, void* proceed_context,
    bool* completed);
size_t thingsos_text_page(
    thingsos_font_t* font, thingsos_canvas_t* canvas, const thingsos_chapter_t* chapter,
    size_t start, int left, int top, int width, int height, thingsos_picture_fn image,
    void* context);
/* EPUB blank lines collapse; paragraphs share the left gutter and body baseline
   grid without changing source offsets. */
size_t thingsos_text_page_layout(
    thingsos_font_t* font, thingsos_canvas_t* canvas, const thingsos_chapter_t* chapter,
    size_t start, int left, int top, int width, int height, unsigned line_percent,
    unsigned indent_px, bool epub_paragraphs, thingsos_picture_fn image, void* context,
    thingsos_text_hits_t* hits);
/* Note/string pagination borrows text and allocates no chapter/image workspace. */
size_t thingsos_text_string_page(thingsos_font_t*,thingsos_canvas_t*,const char*,size_t,size_t,int,int,int,int,unsigned);
bool thingsos_decode_wallpaper(const unsigned char*,size_t,unsigned char**,unsigned*,unsigned*);
bool thingsos_decode_thumbnail(const unsigned char*,size_t,unsigned,unsigned,unsigned char**,unsigned*,unsigned*);
/* PSRAM only on ESP; callers release with free(). Never spills into DMA RAM. */
void* thingsos_image_buffer(size_t);
size_t thingsos_wallpaper_decode_peak(void);
bool thingsos_decode_image(
    const unsigned char* data, size_t bytes, unsigned char** gray, unsigned* width,
    unsigned* height);
/* Reader-only bounded PNG rows / baseline JPEG scaling; caller frees gray. */
bool thingsos_decode_reader_image(const unsigned char*,size_t,unsigned,unsigned,unsigned char**,unsigned*,unsigned*);
bool thingsos_decode_gif(const unsigned char*,size_t,size_t,unsigned char**,unsigned*,unsigned*);
bool thingsos_image_info(const unsigned char*,size_t,unsigned*,unsigned*);
/* Reserve a contiguous scratch arena before a long reading fragments PSRAM.
   Decoding restarts at its base; the returned image is a normal allocation. */
bool thingsos_decode_reserve(size_t bytes);
void thingsos_decode_release(void);
unsigned char thingsos_image_tone_epd(unsigned char gray);
void thingsos_image_paint_clipped(
    thingsos_canvas_t* canvas, const unsigned char* pixels, unsigned w, unsigned h, int x, int y,
    unsigned ow, unsigned oh, int clip_x0, int clip_y0, int clip_x1, int clip_y1, bool smooth,
    bool reader_tone);
#endif
