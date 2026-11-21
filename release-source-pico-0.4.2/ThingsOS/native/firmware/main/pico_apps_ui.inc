/* SPDX-License-Identifier: Apache-2.0 */
static const struct {
    const char* name;
    thingsos_ui_page_t page;
    unsigned icon;
} pico_apps[] = {
    {"传输", THINGSOS_UI_TRANSFER, 0},
    {"文件夹", THINGSOS_UI_FILES, 2},
    {"Cubox", THINGSOS_UI_CUBOX, 9},
    {"系统", THINGSOS_UI_SYSTEM, 7},
    {"壁纸", THINGSOS_UI_WALLPAPER, 8},
    {"网络", THINGSOS_UI_WIFI, 1},
    {"flomo", THINGSOS_UI_FLOMO, 10},
};

static int pico_tile_x(unsigned index) { return 36 + 210 * (int)(index % 3); }
static int pico_tile_y(unsigned index) { return 150 + 200 * (int)(index / 3); }

static void pico_apps_draw(void) {
    for (unsigned i=0; i<sizeof(pico_apps)/sizeof(*pico_apps); ++i) {
        int x=pico_tile_x(i), y=pico_tile_y(i);
        app_icon_tile(pico_apps[i].icon,x+42,y);
        centered_label(pico_apps[i].name,x,y+161,29,192);
    }
    nav();
}

static void pico_transfer_open(void) {
    if(thingsos_network_start(false)==ESP_OK)navigate(THINGSOS_UI_TRANSFER);
    else say("联网启动失败");
}

static bool pico_apps_tap(int x,int y) {
    if(app.page!=THINGSOS_UI_APPS)return false;
    for(unsigned i=0; i<sizeof(pico_apps)/sizeof(*pico_apps); ++i) {
        int left=pico_tile_x(i), top=pico_tile_y(i);
        if(x<left || x>=left+192 || y<top || y>=top+180)continue;
        switch(pico_apps[i].page) {
        case THINGSOS_UI_TRANSFER:
            pico_transfer_open();
            break;
        case THINGSOS_UI_WIFI:
            if(thingsos_network_start(false)==ESP_OK)navigate(THINGSOS_UI_WIFI);
            else say("联网启动失败");
            break;
        case THINGSOS_UI_FILES:
            enter_files();
            break;
        case THINGSOS_UI_WALLPAPER:
            app.wallpaper_step=app.wallpaper_start=0;
            navigate(THINGSOS_UI_WALLPAPER);
            break;
#ifndef THINGSOS_HOST_UI
        case THINGSOS_UI_CUBOX:
            cubox_enter();
            break;
        case THINGSOS_UI_FLOMO:
            flomo_enter();
            break;
#endif
        default:
            navigate(pico_apps[i].page);
            break;
        }
        break;
    }
    return true;
}
