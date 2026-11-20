// SPDX-License-Identifier: Apache-2.0
/* Independently drawn V3 geometry. Coverage is sampled on the UI task, without
   a font glyph, bitmap allocation, animation, or board/voltage changes. */
static bool feedback_segment(float x,float y,float ax,float ay,float bx,float by,float radius) {
    float vx=bx-ax,vy=by-ay,len=vx*vx+vy*vy;
    float t=len?((x-ax)*vx+(y-ay)*vy)/len:0;
    if(t<0)t=0;
    if(t>1)t=1;
    float dx=x-ax-t*vx,dy=y-ay-t*vy;
    return dx*dx+dy*dy<=radius*radius;
}
static bool feedback_round_inside(float x,float y,float w,float h,float radius) {
    if(x<0||y<0||x>=w||y>=h)return false;
    float cx=x<radius?radius:x>w-radius?w-radius:x;
    float cy=y<radius?radius:y>h-radius?h-radius:y;
    float dx=x-cx,dy=y-cy;return dx*dx+dy*dy<=radius*radius;
}
static bool feedback_mic_ink(float x,float y) {
    /* Head x=8,y=3,w=14,h=22,r=7: 3 px lower than the initial soft mic. */
    float dx=x-15,dy=y<10?y-10:y>18?y-18:0;
    if(dx*dx+dy*dy<=49)return true;
    dx=x-15;dy=y-19;float rr=dx*dx+dy*dy;
    return (y>=19&&rr>=10.25f*10.25f&&rr<=13.75f*13.75f)||
        feedback_segment(x,y,3,16,3,19,1.75f)||
        feedback_segment(x,y,27,16,27,19,1.75f)||
        feedback_segment(x,y,15,31,15,34,1.75f)||
        feedback_segment(x,y,9,34,21,34,1.75f);
}
static void feedback_mic_draw(int left,int top,int w,int h) {
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        int dx=left+x,dy=top+y;
        if(dx<0||dy<0||dx>=app.canvas.width||dy>=app.canvas.height)continue;
        unsigned ink=0;
        for(int sy=0;sy<4;++sy)for(int sx=0;sx<4;++sx)
            ink+=feedback_mic_ink((x+(sx+0.5f)/4)*30/w,(y+(sy+0.5f)/4)*36/h);
        size_t at=(size_t)dy*app.canvas.width+dx;
        app.canvas.pixels[at]=(uint8_t)((app.canvas.pixels[at]*(16-ink)+8)/16);
    }
}
static bool feedback_speaker_ink(float x,float y) {
    if(x<2||x>36||y<4||y>32)return false;
    if((x>=5&&x<=12&&y>=14&&y<=22)||
       (x>=12&&x<=21&&y>=14-(x-12)*7/9&&y<=22+(x-12)*7/9))return true;
    static const float points[][2]={{5,14},{12,14},{21,7},{21,29},{12,22},{5,22},{5,14}};
    for(unsigned i=0;i<6;++i)
        if(feedback_segment(x,y,points[i][0],points[i][1],points[i+1][0],points[i+1][1],1.25f))return true;
    /* Round open arcs, matching the two speaker waves in the approved pill. */
    float dx=x-20.7085f,dy=y-18,rr=dx*dx+dy*dy;
    if(x>=26&&y>=12&&y<=24&&rr>=6.75f*6.75f&&rr<=9.25f*9.25f)return true;
    dx=x-19.802f;rr=dx*dx+dy*dy;
    if(x>=30&&y>=7&&y<=29&&rr>=13.75f*13.75f&&rr<=16.25f*16.25f)return true;
    return feedback_segment(x,y,26,12,26,12,1.25f)||feedback_segment(x,y,26,24,26,24,1.25f)||
        feedback_segment(x,y,30,7,30,7,1.25f)||feedback_segment(x,y,30,29,30,29,1.25f);
}
