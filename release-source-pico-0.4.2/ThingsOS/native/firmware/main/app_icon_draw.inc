/* SPDX-License-Identifier: Apache-2.0 */
static void app_outline(int x,int y,int w,int h){
    thingsos_rect(&app.canvas,x,y,w,2,0);thingsos_rect(&app.canvas,x,y+h-2,w,2,0);
    thingsos_rect(&app.canvas,x,y,2,h,0);thingsos_rect(&app.canvas,x+w-2,y,2,h,0);
}
static void app_circle(int x,int y,int radius,int thickness){
    for(int dy=-radius;dy<=radius;++dy)for(int dx=-radius;dx<=radius;++dx)
        if(dx*dx+dy*dy<=radius*radius&&dx*dx+dy*dy>=(radius-thickness)*(radius-thickness))thingsos_rect(&app.canvas,x+dx,y+dy,1,1,0);
}
#include "app_icons.inc"
static void app_icon_bitmap(const uint8_t* packed,unsigned size,int left,int top){
    for(unsigned y=0;y<size;++y)for(unsigned x=0;x<size;++x){size_t at=(size_t)y*size+x;
        unsigned gray=(packed[at/2]>>((at&1)?0:4))&15;
        int dx=left+(int)x,dy=top+(int)y;
        if(gray<15&&dx>=0&&dy>=0&&dx<app.canvas.width&&dy<app.canvas.height)
            app.canvas.pixels[(size_t)dy*app.canvas.width+dx]=(uint8_t)(gray*17);
    }
}
static void app_icon(unsigned type,int left,int top){
    if(type==10){
        /* flomo mark: a ring whose top runs into a slanted bar, with a second
           slanted bar above. Filled per pixel in a 307x242 design space
           (ring centre 80,160, outer radius 80, inner 38), scaled to 70 px. */
        const int w=70,h=55,ox=left+54-w/2,oy=top+54-h/2;
        for(int iy=0;iy<h;++iy)for(int ix=0;ix<w;++ix){
            int X=ix*307/w,Y=iy*242/h,dx=X-80,dy=Y-160,d2=dx*dx+dy*dy;
            bool ring=d2<=80*80&&d2>=38*38;
            bool low=Y>=73&&Y<=120&&X>=80&&X<=265-(Y-73)*28/47;
            bool up=Y<=46&&X>=157-Y*27/46&&X<=307-Y*27/46;
            if(ring||low||up)thingsos_rect(&app.canvas,ox+ix,oy+iy,1,1,0);
        }
        return;
    }
    if(type==9){
        /* Isometric cube outline representing Cubox. */
        int cx=left+54,cy=top+54;
        for(int i=0;i<=20;++i){
            int dy=(i*11)/20;
            thingsos_rect(&app.canvas,cx-i,cy-22+dy,3,3,0);
            thingsos_rect(&app.canvas,cx+i,cy-22+dy,3,3,0);
            thingsos_rect(&app.canvas,cx-i,cy+dy,3,3,0);
            thingsos_rect(&app.canvas,cx+i,cy+dy,3,3,0);
            thingsos_rect(&app.canvas,cx-20+i,cy+11+dy,3,3,0);
            thingsos_rect(&app.canvas,cx+20-i,cy+11+dy,3,3,0);
        }
        thingsos_rect(&app.canvas,cx-20,cy-11,3,23,0);
        thingsos_rect(&app.canvas,cx,cy-1,3,23,0);
        thingsos_rect(&app.canvas,cx+20,cy-11,3,23,0);
        return;
    }
    if(type==8){
        app_outline(left+28,top+31,52,46);app_circle(left+65,top+43,5,5);
        for(int i=0;i<16;++i){thingsos_rect(&app.canvas,left+32+i,top+70-i,3,3,0);thingsos_rect(&app.canvas,left+47+i,top+55+i,3,3,0);}
        for(int i=0;i<9;++i){thingsos_rect(&app.canvas,left+60+i,top+68-i,3,3,0);thingsos_rect(&app.canvas,left+68+i,top+60+i,3,3,0);}
        return;
    }
    if(type==7){
        app_outline(left+35,top+35,38,38);app_outline(left+44,top+44,20,20);
        for(int i=0;i<3;++i){int p=43+i*11;
            thingsos_rect(&app.canvas,left+p,top+25,3,10,0);thingsos_rect(&app.canvas,left+p,top+73,3,10,0);
            thingsos_rect(&app.canvas,left+25,top+p,10,3,0);thingsos_rect(&app.canvas,left+73,top+p,10,3,0);}
        return;
    }
    if(type<APP_ICON_COUNT)app_icon_bitmap(app_icon_glyphs[type],APP_ICON_GLYPH_SIZE,left+25,top+25);
}
static void app_icon_tile(unsigned type,int left,int top){app_icon_bitmap(app_icon_border,APP_ICON_TILE_SIZE,left,top);app_icon(type,left,top);}
