// SPDX-License-Identifier: Apache-2.0
/* Borrow the exact paragraph/Markdown/image/UTF-8 cursor engine on transposed
 * flow axes. Glyph rasters and images stay upright; no full-page scratch plane
 * or alternate byte index is allocated. Output hits are always physical. */
typedef struct {
    thingsos_picture_fn draw;
    void* context;
    thingsos_canvas_t* canvas;
    thingsos_font_t* font;
} vertical_picture_t;
static bool vertical_picture(void* context,const thingsos_inline_t* image,
                             thingsos_canvas_t* flow,int x,int y,int width,int height) {
    vertical_picture_t* p=context;
    /* Image adapters may draw their own horizontal hint/placeholder labels. */
    bool old=p->font->vertical_text;p->font->vertical_text=false;
    bool ok=p->draw(p->context,image,p->canvas,flow->height-y-height,x,height,width);
    p->font->vertical_text=old;
    return ok;
}
static void vertical_hit(thingsos_text_hit_t* h,int right,bool undo) {
    int x=h->x,y=h->y,w=h->w,height=h->h;
    h->x=undo?y:right-y-height;h->y=undo?right-x-w:x;
    h->w=height;h->h=w;
}
static void vertical_hits(thingsos_text_hits_t* hits,int right,bool undo) {
    if(!hits)return;
    for(size_t i=0;i<hits->count;++i)vertical_hit(&hits->items[i],right,undo);
    for(size_t i=0;i<hits->line_count;++i)vertical_hit(&hits->lines[i],right,undo);
    for(size_t i=0;i<hits->glyph_count;++i) {
        thingsos_glyph_hit_t* g=&hits->glyphs[i];
        int x=g->x,y=g->y,w=g->w,h=g->h;
        g->x=(int16_t)(undo?y:right-y-h);g->y=(int16_t)(undo?right-x-w:x);
        g->w=(int16_t)h;g->h=(int16_t)w;
    }
}
static size_t vertical_text_page(thingsos_font_t* font,thingsos_canvas_t* canvas,
    const text_view_t* text,size_t start,int left,int top,int width,int height,
    unsigned spacing,unsigned indent,bool paragraphs,thingsos_picture_fn picture,void* context,
    thingsos_text_hits_t* hits,thingsos_layout_continue_fn proceed,void* proceed_context,
    bool* completed,thingsos_text_cursor_t* cursor) {
    /* A stable virtual origin works for measuring without a canvas as well as
       portrait, landscape and immersive rendering. Only the body's bounds enter
       pagination; screen dimensions are not inferred from a nullable canvas. */
    int right=left+width;
    thingsos_canvas_t flow={0};
    if(canvas) {
        flow=*canvas;flow.width=canvas->height;flow.height=canvas->width;flow.vertical_text=true;
        right=canvas->width;
    }
    int virtual_top=right-left-width;
    if(cursor && cursor->active)vertical_hits(hits,right,true);
    vertical_picture_t p={picture,context,canvas,font};
    bool previous=font->vertical_text;font->vertical_text=true;
    size_t end=text_view_page(font,canvas?&flow:NULL,text,start,top,virtual_top,height,width,
        spacing,indent,paragraphs,picture?vertical_picture:NULL,&p,hits,proceed,proceed_context,completed,cursor);
    font->vertical_text=previous;
    vertical_hits(hits,right,false);
    return end;
}
