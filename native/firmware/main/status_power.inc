// SPDX-License-Identifier: Apache-2.0
/* Approved 2026-11-02 prototype: 49x23 battery, 14x17 internal bolt.
   Coverage is inverted against the fill so the bolt survives every SOC. */
static bool status_charge_ink(float x,float y) {
    static const float points[6][2]={{9,0},{0,10},{6,10},{3,17},{14,6},{8,6}};
    bool inside=false;
    for(int i=0,j=5;i<6;j=i++) {
        float yi=points[i][1],yj=points[j][1];
        if((yi>y)!=(yj>y) && x<(points[j][0]-points[i][0])*(y-yi)/(yj-yi)+points[i][0])
            inside=!inside;
    }
    return inside;
}
static void status_battery_draw(int left,int top,unsigned percent,bool charging) {
    if(percent>100)percent=100;
    thingsos_rect(&app.canvas,left,top,49,2,0);
    thingsos_rect(&app.canvas,left,top+21,49,2,0);
    thingsos_rect(&app.canvas,left,top,2,23,0);
    thingsos_rect(&app.canvas,left+47,top,2,23,0);
    thingsos_rect(&app.canvas,left+50,top+6,4,11,0);
    thingsos_rect(&app.canvas,left+4,top+4,(int)(41*percent/100),15,0);
    if(!charging)return;
    for(int y=0;y<17;++y)for(int x=0;x<14;++x) {
        int dx=left+18+x,dy=top+3+y;
        if(dx<0||dy<0||dx>=app.canvas.width||dy>=app.canvas.height)continue;
        unsigned ink=0;
        for(unsigned sy=0;sy<4;++sy)for(unsigned sx=0;sx<4;++sx)
            ink+=status_charge_ink(x+(sx+0.5f)*0.25f,y+(sy+0.5f)*0.25f);
        if(!ink)continue;
        size_t at=(size_t)dy*app.canvas.width+dx;
        unsigned base=app.canvas.pixels[at];
        app.canvas.pixels[at]=(uint8_t)((base*(16-ink)+(255-base)*ink+8)/16);
    }
}
/* A charge transition is its own refresh reason, including the first minute
   after boot. It must not be encoded as a zero minute-refresh timestamp. */
static bool status_refresh_due(uint64_t time) {
    bool changed=false;
    if(time-app.charge_checked>=5000) {
        app.charge_checked=time;
        bool charging=thingsos_board_charging();
        changed=charging!=app.charging;
        app.charging=charging;
    }
    if(!changed && time-app.clock_refreshed<60000)return false;
    app.battery=thingsos_board_battery();
    app.clock_refreshed=time;
    return true;
}
