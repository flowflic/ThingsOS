// SPDX-License-Identifier: Apache-2.0
#ifndef THINGSOS_READER_UI_H
#define THINGSOS_READER_UI_H
#include "thingsos/graphics.h"
typedef enum {
    THINGSOS_MENU_NONE, THINGSOS_MENU_CLOSE, THINGSOS_MENU_SMALLER, THINGSOS_MENU_LARGER,
    THINGSOS_MENU_LINE, THINGSOS_MENU_MARGIN, THINGSOS_MENU_CHAPTERS,
    THINGSOS_MENU_BOOKMARKS, THINGSOS_MENU_JUMP, THINGSOS_MENU_FONTS, THINGSOS_MENU_STATS,
    THINGSOS_MENU_WEIGHT, THINGSOS_MENU_RIPPLE, THINGSOS_MENU_NOTES, THINGSOS_MENU_INDENT, THINGSOS_MENU_MORE
} thingsos_menu_action_t;
/* indent is the first-line indent in character widths; zero keeps the source layout. */
typedef struct { unsigned px, line, margin, weight, indent; bool ripple, weidu; } thingsos_reader_style_t;
enum { THINGSOS_READER_SIZE_DEFAULT = 46,
       THINGSOS_READER_SIZE_MIN = 30, THINGSOS_READER_SIZE_MAX = 70,
       THINGSOS_READER_SIZE_STEP = 4 };
/* First-line indent choices. Off is the default; the enabled width is two
   character widths, so one indent step is always two body font sizes. */
enum { THINGSOS_READER_INDENT_OFF = 0, THINGSOS_READER_INDENT_CHARS = 2 };
unsigned thingsos_reader_indent_px(unsigned px, unsigned chars);
unsigned thingsos_reader_size_step(unsigned current, bool increase);
void thingsos_draw_nav(thingsos_font_t*, thingsos_canvas_t*, unsigned active);
void thingsos_draw_reader_menu(thingsos_font_t*, thingsos_canvas_t*, thingsos_reader_style_t);
thingsos_menu_action_t thingsos_reader_menu_hit_weidu(int x,int y,unsigned* choice);
thingsos_menu_action_t thingsos_reader_menu_hit(int x, int y, unsigned* choice);
thingsos_menu_action_t thingsos_reader_menu_hit_landscape(int x,int y,unsigned* choice,bool weidu);

#endif
