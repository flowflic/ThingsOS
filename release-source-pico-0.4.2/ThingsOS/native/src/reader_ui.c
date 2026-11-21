// SPDX-License-Identifier: Apache-2.0
#include "thingsos/reader_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
unsigned thingsos_reader_indent_px(unsigned px, unsigned chars) {
    return chars * px;
}
unsigned thingsos_reader_size_step(unsigned current, bool increase) {
    if (current < THINGSOS_READER_SIZE_MIN) current = THINGSOS_READER_SIZE_MIN;
    if (current > THINGSOS_READER_SIZE_MAX) current = THINGSOS_READER_SIZE_MAX;
    current = THINGSOS_READER_SIZE_MIN +
        (current - THINGSOS_READER_SIZE_MIN) / THINGSOS_READER_SIZE_STEP * THINGSOS_READER_SIZE_STEP;
    if (increase) return current < THINGSOS_READER_SIZE_MAX ?
        current + THINGSOS_READER_SIZE_STEP : THINGSOS_READER_SIZE_MAX;
    return current > THINGSOS_READER_SIZE_MIN ?
        current - THINGSOS_READER_SIZE_STEP : THINGSOS_READER_SIZE_MIN;
}
static void line(thingsos_canvas_t* c, int x, int y, int xx, int yy) {
    int dx = abs(xx-x), sx = x < xx ? 1 : -1, dy = -abs(yy-y), sy = y < yy ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        thingsos_rect(c, x-1, y-1, 3, 3, 0);
        if (x == xx && y == yy) break;
        int e = err*2;
        if (e >= dy) { err += dy; x += sx; }
        if (e <= dx) { err += dx; y += sy; }
    }
}
static void outline(thingsos_canvas_t* c, int x, int y, int w, int h) {
    thingsos_rect(c,x,y,w,2,0); thingsos_rect(c,x,y+h-2,w,2,0);
    thingsos_rect(c,x,y,2,h,0); thingsos_rect(c,x+w-2,y,2,h,0);
}
static void text(thingsos_font_t* f, thingsos_canvas_t* c, const char* s, int x, int y,
                 unsigned size, int width) {
    thingsos_font_size(f,size); thingsos_label(f,c,s,x,y,width);
}
static void centered(thingsos_font_t* f, thingsos_canvas_t* c, const char* s,
                     int x, int y, int w, unsigned size) {
    thingsos_font_size(f,size);
    thingsos_label_centered(f,c,s,x,y,w);
}
void thingsos_draw_nav(thingsos_font_t* f, thingsos_canvas_t* c, unsigned active) {
    static const char* names[] = {"书库","微读","应用","设置"};
    thingsos_rect(c,0,1098,684,1,188);
    for (unsigned i=0;i<4;++i) {
        unsigned item=i;
        int x=(int)i*171+68, y=1117;
        if (i==0) {
            outline(c,x+6,y+4,24,29); line(c,x+11,y+4,x+11,y+32);
            line(c,x+16,y+11,x+26,y+11); line(c,x+16,y+17,x+26,y+17);
        } else if(item==1) {
            outline(c,x+2,y+7,32,26);line(c,x+18,y+7,x+18,y+32);
            line(c,x+6,y+13,x+14,y+13);line(c,x+23,y+13,x+30,y+13);
        } else if(item==2) {
            for (int row=0;row<2;++row)
                for (int col=0;col<2;++col)
                    outline(c,x+4+16*col,y+4+16*row,12,12);
        } else {
            for(int j=0;j<3;++j) {
                int yy=y+9+j*9, knob=x+(j==0?12:j==1?24:15);
                line(c,x+6,yy,x+30,yy); line(c,knob,yy-4,knob,yy+4);
            }
        }
        centered(f,c,names[i],(int)i*171,1181,171,24);
        if(item==active) thingsos_rect(c,(int)i*171+52,1201,66,5,0);
    }
}
static void choice(thingsos_font_t* f, thingsos_canvas_t* c, const char* s,
                   int x, int y, int w, bool selected) {
    outline(c,x,y,w,68);
    if ((s[0]=='-' || s[0]=='+') && !s[1]) {
        line(c,x+w/2-10,y+34,x+w/2+10,y+34);
        if(s[0]=='+') line(c,x+w/2,y+24,x+w/2,y+44);
    } else centered(f,c,s,x,y+45,w,28);
    if(selected) for(int yy=y;yy<y+68;++yy) for(int xx=x;xx<x+w;++xx)
        c->pixels[(size_t)yy*c->width+xx] = 255-c->pixels[(size_t)yy*c->width+xx];
}
void thingsos_draw_reader_menu(thingsos_font_t* f, thingsos_canvas_t* c,
                               thingsos_reader_style_t style) {
    if(c->width==1216 && c->height==684) {
        thingsos_rect(c,0,74,1216,610,255);
        text(f,c,"阅读设置",24,126,32,900);
        line(c,1150,94,1172,116);line(c,1172,94,1150,116);
        static const char* names[]={"字号","缩进","行距","页边距","字体粗细","翻页效果"};
        static const char* options[][3]={{"-","","+"},{"关闭","开启",""},{"紧凑","标准","宽松"},
            {"窄","标准","宽"},{"常规","中粗","加粗"},{"关闭","水波纹",""}};
        char size[16];snprintf(size,sizeof(size),"%u",style.px);
        for(unsigned row=0;row<6;++row) {
            int base=row<3?24:632,y=156+(int)(row%3)*104;
            text(f,c,names[row],base,y+43,26,138);
            for(unsigned col=0;col<3;++col) {
                if((row==1 || row==5) && col==2)continue;
                unsigned selected=row==1?(style.indent!=0):row==2?style.line:row==3?style.margin:row==4?style.weight:row==5?style.ripple:9;
                int x=base+144+(int)col*140;
                if(row==0 && col==1)centered(f,c,size,x,y+45,128,30);
                else choice(f,c,options[row][col],x,y,128,selected==col);
            }
        }
        choice(f,c,"目录",24,488,376,false);choice(f,c,"书签",420,488,376,false);choice(f,c,"跳转",816,488,376,false);
        if(style.weidu){choice(f,c,"字体",24,586,376,false);choice(f,c,"划线",420,586,376,false);choice(f,c,"更多",816,586,376,false);}
        else {choice(f,c,"字体",24,586,574,false);choice(f,c,"更多",618,586,574,false);}
        return;
    }
    /* Soften the reading page above the sheet without an extra panel refresh. */
    for(int y=74;y<450;++y) for(int x=0;x<c->width;++x)
        c->pixels[(size_t)y*c->width+x]=(unsigned char)(128+c->pixels[(size_t)y*c->width+x]/2);
    thingsos_rect(c,0,450,684,766,255); thingsos_rect(c,0,450,684,3,0);
    text(f,c,"阅读设置",36,510,34,540);
    line(c,615,478,637,500); line(c,637,478,615,500);
    static const char* labels[]={"字号","缩进","行距","页边距","字体粗细","翻页效果"};
    static const char* indents[]={"关闭","开启",""};
    static const char* lines[]={"紧凑","标准","宽松"};
    static const char* margins[]={"窄","标准","宽"};
    static const char* weights[]={"常规","中粗","加粗"};
    static const char* effects[]={"关闭","水波纹",""};
    char size[16]; snprintf(size,sizeof(size),"%u",style.px);
    /* Six rows share the sheet above the chapter/bookmark row: 537 + 5*80 + 68
       ends at 1005, which keeps the 12 px control gap and clears 目录 at 1044. */
    for(unsigned row=0;row<6;++row) {
        int y=537+(int)row*80;
        text(f,c,labels[row],36,y+45,28,162);
        for(unsigned col=0;col<3;++col) {
            if((row==1 || row==5) && col==2) continue;
            const char* s=row==0?(col==0?"-":col==1?size:"+"):row==1?indents[col]:row==2?lines[col]:
                row==3?margins[col]:row==4?weights[col]:effects[col];
            bool selected=row==1?(style.indent?col==1:col==0):row==2?style.line==col:
                row==3?style.margin==col:row==4?style.weight==col:row==5?(unsigned)style.ripple==col:false;
            if(row==0 && col==1) centered(f,c,s,212+(int)col*149,y+45,137,30);
            else choice(f,c,s,212+(int)col*149,y,137,selected);
        }
        thingsos_rect(c,36,y+74,612,1,204);
    }
    choice(f,c,"目录",36,1044,194,false); choice(f,c,"书签",245,1044,194,false);
    choice(f,c,"跳转",454,1044,194,false);
    if(style.weidu){choice(f,c,"字体",36,1132,194,false);choice(f,c,"划线",245,1132,194,false);choice(f,c,"更多",454,1132,194,false);}
    else {choice(f,c,"字体",36,1132,292,false);choice(f,c,"更多",356,1132,292,false);}
}
thingsos_menu_action_t thingsos_reader_menu_hit(int x,int y,unsigned* value) {
    if(x<0||x>=684||y<0||y>=1216) return THINGSOS_MENU_NONE;
    if(y<450 || (x>=592 && y>=464 && y<524)) return THINGSOS_MENU_CLOSE;
    if(x>=212 && x<647 && y>=537 && y<1005) {
        unsigned col=(unsigned)(x-212)/149;
        if((x-212)%149>=137) return THINGSOS_MENU_NONE;
        int row=(y-537)/80;
        if((y-537)%80>=68) return THINGSOS_MENU_NONE;
        if(value) *value=col;
        if(row==0) return col==0?THINGSOS_MENU_SMALLER:col==2?THINGSOS_MENU_LARGER:THINGSOS_MENU_NONE;
        if(row==1) return col<2?THINGSOS_MENU_INDENT:THINGSOS_MENU_NONE;
        return row==2?THINGSOS_MENU_LINE:row==3?THINGSOS_MENU_MARGIN:
            row==4?THINGSOS_MENU_WEIGHT:col<2?THINGSOS_MENU_RIPPLE:THINGSOS_MENU_NONE;
    }
    if(y>=1044 && y<1112) {
        if(x>=36&&x<230) return THINGSOS_MENU_CHAPTERS;
        if(x>=245&&x<439) return THINGSOS_MENU_BOOKMARKS;
        if(x>=454&&x<648) return THINGSOS_MENU_JUMP;
    }
    if(y>=1132) {
        if(x>=36&&x<328) return THINGSOS_MENU_FONTS;
        if(x>=356&&x<648) return THINGSOS_MENU_MORE;
    }
    return THINGSOS_MENU_NONE;
}

thingsos_menu_action_t thingsos_reader_menu_hit_weidu(int x,int y,unsigned* value){
    if(x<0 || x>=684 || y<0 || y>=1216)return THINGSOS_MENU_NONE;
    if(y>=1132){if(x>=36&&x<230)return THINGSOS_MENU_FONTS;if(x>=245&&x<439)return THINGSOS_MENU_NOTES;if(x>=454&&x<648)return THINGSOS_MENU_MORE;return THINGSOS_MENU_NONE;}
    return thingsos_reader_menu_hit(x,y,value);
}
thingsos_menu_action_t thingsos_reader_menu_hit_landscape(int x,int y,unsigned* value,bool weidu) {
    if(x<0 || x>=1216 || y<0 || y>=684)return THINGSOS_MENU_NONE;
    if(y<74 || (x>=1128 && y>=82 && y<138))return THINGSOS_MENU_CLOSE;
    if(y>=156 && y<432) {
        unsigned group=x>=632,base=group?632:24;
        if(x<(int)base+144 || x>=(int)base+144+408)return THINGSOS_MENU_NONE;
        unsigned col=(unsigned)(x-base-144)/140,row=(unsigned)(y-156)/104+group*3;
        if((x-base-144)%140>=128 || (y-156)%104>=68)return THINGSOS_MENU_NONE;
        if(value)*value=col;
        if(row==0)return col==0?THINGSOS_MENU_SMALLER:col==2?THINGSOS_MENU_LARGER:THINGSOS_MENU_NONE;
        if(row==1)return col<2?THINGSOS_MENU_INDENT:THINGSOS_MENU_NONE;
        if(row==2)return THINGSOS_MENU_LINE;
        if(row==3)return THINGSOS_MENU_MARGIN;
        if(row==4)return THINGSOS_MENU_WEIGHT;
        return col<2?THINGSOS_MENU_RIPPLE:THINGSOS_MENU_NONE;
    }
    if(y>=488 && y<556) {
        if(x>=24 && x<400)return THINGSOS_MENU_CHAPTERS;
        if(x>=420 && x<796)return THINGSOS_MENU_BOOKMARKS;
        if(x>=816 && x<1192)return THINGSOS_MENU_JUMP;
    }
    if(y>=586) {
        if(weidu) {
            if(x>=24 && x<400)return THINGSOS_MENU_FONTS;
            if(x>=420 && x<796)return THINGSOS_MENU_NOTES;
            if(x>=816 && x<1192)return THINGSOS_MENU_MORE;
        }else {
            if(x>=24 && x<598)return THINGSOS_MENU_FONTS;
            if(x>=618 && x<1192)return THINGSOS_MENU_MORE;
        }
    }
    return THINGSOS_MENU_NONE;
}
