/* SPDX-License-Identifier: Apache-2.0 */
typedef struct {
    thingsos_ble_status_t ble;
    thingsos_ble_device_t ble_selected;
    bool list_saved,ble_confirm,ble_forget,scan_pending;
    unsigned view,ble_digits,first;
    char ble_pin[7];
} pico_ble_ui_t;
static pico_ble_ui_t* bluetooth_ui;

static void bt_ui_switch(int x,int y,bool on) {
    reader_control(on?"开":"关",x,y,132,on);
}
#include "ble_connection_ui.inc"

static void ble_resume_later(void) {
    app.ble_reconnect_due=app.ble_enabled;
}
static void pico_ble_open(void) {
    if(!bluetooth_ui)bluetooth_ui=thingsos_psram_calloc(1,sizeof(*bluetooth_ui));
    if(!bluetooth_ui){say("内存不足，无法打开蓝牙");return;}
    bluetooth_ui->view=bluetooth_ui->first=bluetooth_ui->ble_digits=0;
    bluetooth_ui->ble_pin[0]=0;
    bluetooth_ui->list_saved=bluetooth_ui->ble_confirm=false;
    /* Defer BLE active scanning until the BLE page finishes its first GC16
       transition, avoiding radio preemption during the 18 MHz panel scan. */
    bluetooth_ui->scan_pending=app.ble_enabled;
    navigate(THINGSOS_UI_BLE);
}
static void ble_ui_poll(bool idle) {
    if(!app.ble_enabled || app.pin.required) {
        if(bluetooth_ui)bluetooth_ui->scan_pending=false;
        if(!thingsos_ble_quiet())thingsos_ble_stop();
    } else if(idle && !app.dirty && !app.controls_open && app.ble_reconnect_due && thingsos_ble_quiet()) {
        if(thingsos_ble_reconnect())app.ble_reconnect_due=false;
    }
    if(app.page!=THINGSOS_UI_BLE || !bluetooth_ui) {
        if(bluetooth_ui)bluetooth_ui->scan_pending=false;
        thingsos_board_pclk_allow_recovery(true);
        return;
    }
    unsigned serial=bluetooth_ui->ble.serial;
    uint32_t generation=bluetooth_ui->ble.generation;
    thingsos_ble_status(&bluetooth_ui->ble);
    thingsos_board_pclk_allow_recovery(!bluetooth_ui->ble.scanning && !bluetooth_ui->scan_pending);
    if(idle && !app.dirty && !app.controls_open && bluetooth_ui->scan_pending) {
        bluetooth_ui->scan_pending=false;
        if(app.ble_enabled)ble_ui_result(thingsos_ble_scan(true));
        thingsos_ble_status(&bluetooth_ui->ble);
        thingsos_board_pclk_allow_recovery(!bluetooth_ui->ble.scanning);
    }
    if(generation!=bluetooth_ui->ble.generation) {
        bluetooth_ui->ble_digits=0;
        bluetooth_ui->ble_pin[0]=0;
    }
    if(serial!=bluetooth_ui->ble.serial)app.dirty=true;
}
static bool pico_ble_draw(void) {
    if(app.page!=THINGSOS_UI_BLE || !bluetooth_ui)return false;
    header("蓝牙设备");
    if(ble_ui_draw())return true;
    if(!app.ble_enabled) {
        reader_control("开启蓝牙并搜索",36,260,612,true);
        return true;
    }
    thingsos_ble_status_t* status=&bluetooth_ui->ble;
    bool saved=bluetooth_ui->list_saved;
    reader_control(saved?"附近设备":"已保存设备",36,240,292,false);
    reader_control(status->scanning?"停止搜索":"开始搜索",356,240,292,true);
    label(saved?"已保存设备":"附近的低功耗设备",36,355,25,612);
    unsigned count=saved?status->saved_count:status->count;
    for(unsigned row=0;row<6 && bluetooth_ui->first+row<count;++row) {
        unsigned i=bluetooth_ui->first+row;
        const thingsos_ble_device_t* d=saved?status->saved+i:status->devices+i;
        reader_control(d->device.name,36,380+(int)row*88,612,ble_ui_current(d));
    }
    if(count>6) {
        reader_control("上一组",36,930,292,false);
        reader_control("下一组",356,930,292,false);
    }
    label(status->message,36,1070,22,612);
    return true;
}
static bool pico_ble_tap(int x,int y) {
    if(app.page!=THINGSOS_UI_BLE || !bluetooth_ui)return false;
    if(y>=88 && y<152 && x<110) {
        if(bluetooth_ui->ble_confirm)bluetooth_ui->ble_confirm=false;
        else if(bluetooth_ui->view==3) {
            if(bluetooth_ui->ble.pairing!=THINGSOS_BLE_PAIR_NONE)thingsos_ble_pair_reply(false,0);
            bluetooth_ui->view=0;
        } else if(!pico_controls_return(THINGSOS_UI_BLE))navigate(THINGSOS_UI_SETTINGS);
        app.dirty=true;
        return true;
    }
    if(!app.ble_enabled) {
        if(x>=36 && x<648 && y>=260 && y<328) {
            app.ble_enabled=true;
            preferences_save();
            bluetooth_ui->scan_pending=true;
            app.dirty=true;
        }
        return true;
    }
    if(ble_ui_tap(x,y))return true;
    if(x<36 || x>=648)return true;
    thingsos_ble_status_t* status=&bluetooth_ui->ble;
    unsigned count=bluetooth_ui->list_saved?status->saved_count:status->count;
    if(y>=240 && y<308) {
        if(x<328){bluetooth_ui->list_saved=!bluetooth_ui->list_saved;bluetooth_ui->first=0;}
        else if(x>=356){bluetooth_ui->list_saved=false;bluetooth_ui->first=0;bluetooth_ui->scan_pending=false;ble_ui_result(thingsos_ble_scan(!status->scanning));}
    } else if(y>=380 && y<908 && (y-380)%88<68) {
        unsigned i=bluetooth_ui->first+(unsigned)(y-380)/88;
        if(i<count) {
            bluetooth_ui->ble_selected=bluetooth_ui->list_saved?status->saved[i]:status->devices[i];
            bluetooth_ui->view=3;
        }
    } else if(y>=930 && y<998) {
        if(x<328 && bluetooth_ui->first>=6)bluetooth_ui->first-=6;
        if(x>=356 && bluetooth_ui->first+6<count)bluetooth_ui->first+=6;
    }
    app.dirty=true;return true;
}
