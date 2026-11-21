// SPDX-License-Identifier: Apache-2.0
/* One device list; transport-specific workers retain ownership of both radios. */
typedef struct {thingsos_ble_device_t device;bool ble;} bt_ui_found_t;
typedef struct {thingsos_bt_status_t status;thingsos_ble_status_t ble;thingsos_bt_device_t selected;thingsos_ble_device_t ble_selected;
 unsigned serial,ble_serial,list_start,view,ble_digits;uint32_t ble_pair_generation;
 bool confirm_pair,confirm_forget,list_saved,ble_confirm,ble_forget;char ble_pin[7];
 bt_ui_found_t found[THINGSOS_BT_FOUND+THINGSOS_BLE_FOUND];unsigned found_count,search_audio_serial,search_ble_serial;
 uint64_t next_redraw,ble_notice_until;unsigned ble_notice_serial;
 bool wait_audio,wait_ble,pending,painted,painted_controls;} bluetooth_ui_t;
static bluetooth_ui_t* bluetooth_ui;
static bool bt_ui_ready(void){if(!bluetooth_ui)bluetooth_ui=thingsos_psram_calloc(1,sizeof(*bluetooth_ui));return bluetooth_ui!=NULL;}
static bool bt_ui_unsaved_current(void){thingsos_bt_status_t* s=&bluetooth_ui->status;if(!s->connected)return false;for(unsigned i=0;i<s->saved_count;++i)if(!strcmp(s->saved[i].address,s->current.address))return false;return true;}
static unsigned bt_ui_saved_count(void){return bluetooth_ui->status.saved_count+(bt_ui_unsaved_current()?1u:0u);}
static thingsos_bt_device_t* bt_ui_saved_at(unsigned i){if(bt_ui_unsaved_current()){if(!i)return &bluetooth_ui->status.current;--i;}return bluetooth_ui->status.saved+i;}
static bool bt_ui_ble_unsaved(void){thingsos_ble_status_t* b=&bluetooth_ui->ble;if(!b->connected)return false;for(unsigned i=0;i<b->saved_count;++i)if(thingsos_ble_same(b->saved+i,&b->current)||thingsos_ble_same(b->saved+i,&b->identity))return false;return true;}
static unsigned bt_ui_count(void){return bluetooth_ui->list_saved?bt_ui_saved_count()+bluetooth_ui->ble.saved_count+(bt_ui_ble_unsaved()?1u:0u):bluetooth_ui->found_count;}
static bool bt_ui_found_add(const thingsos_ble_device_t* d,bool ble){
 unsigned i=0;
 for(;i<bluetooth_ui->found_count;++i){bt_ui_found_t* r=bluetooth_ui->found+i;
  if(r->ble==ble&&(!ble||r->device.address_type==d->address_type)&&!strcmp(r->device.device.address,d->device.address))break;
 }
 if(i==bluetooth_ui->found_count){
  if(i==THINGSOS_BT_FOUND+THINGSOS_BLE_FOUND)return false;
  /* Audio can arrive after many BLE advertisements: keep it at the front,
     retaining discovery order within each transport. */
  if(!ble){i=0;while(i<bluetooth_ui->found_count&&!bluetooth_ui->found[i].ble)++i;
   memmove(bluetooth_ui->found+i+1,bluetooth_ui->found+i,(bluetooth_ui->found_count-i)*sizeof(bluetooth_ui->found[0]));}
  ++bluetooth_ui->found_count;bluetooth_ui->found[i]=(bt_ui_found_t){.device=*d,.ble=ble};return true;
 }
 bt_ui_found_t* r=bluetooth_ui->found+i;bool changed=strcmp(r->device.device.name,d->device.name)||r->device.connectable!=d->connectable;r->device=*d;return changed;
}
static bool bt_ui_merge(void){
 thingsos_bt_status_t* s=&bluetooth_ui->status;thingsos_ble_status_t* b=&bluetooth_ui->ble;bool changed=false;
 if(bluetooth_ui->wait_audio&&s->serial!=bluetooth_ui->search_audio_serial&&
    (s->phase==THINGSOS_BT_SCANNING||(s->phase==THINGSOS_BT_READY&&s->serial-bluetooth_ui->search_audio_serial>=3)))bluetooth_ui->wait_audio=false;
 if(bluetooth_ui->wait_ble&&b->serial!=bluetooth_ui->search_ble_serial&&
    (b->scanning||(b->phase!=THINGSOS_BLE_ERROR&&b->serial-bluetooth_ui->search_ble_serial>=2)))bluetooth_ui->wait_ble=false;
 if(!bluetooth_ui->wait_audio)for(unsigned i=0;i<s->found_count;++i){thingsos_ble_device_t d={.device=s->found[i],.connectable=true};changed|=bt_ui_found_add(&d,false);}
 /* Address-only advertisers (phones, beacons, broadcasts) cannot be identified
    or used as input devices; list them only once a name or kind is known. */
 if(!bluetooth_ui->wait_ble)for(unsigned i=0;i<b->count;++i)if(thingsos_ble_named(b->devices+i))changed|=bt_ui_found_add(b->devices+i,true);
 return changed;
}
/* References into the PSRAM snapshot avoid arrays on the UI stack. Keep typed
   BLE addresses intact: equal MACs do not prove equal transport identities. */
static const thingsos_bt_device_t* bt_ui_at(unsigned i,const thingsos_ble_device_t** ble){
 *ble=NULL;unsigned audio=bluetooth_ui->list_saved?bt_ui_saved_count():bluetooth_ui->status.found_count;
 if(!bluetooth_ui->list_saved){bt_ui_found_t* r=bluetooth_ui->found+i;if(r->ble)*ble=&r->device;return &r->device.device;}
 if(i<audio)return bluetooth_ui->list_saved?bt_ui_saved_at(i):bluetooth_ui->status.found+i;
 i-=audio;if(bluetooth_ui->list_saved&&bt_ui_ble_unsaved()){if(!i){*ble=&bluetooth_ui->ble.current;return &(*ble)->device;}--i;}
 *ble=(bluetooth_ui->list_saved?bluetooth_ui->ble.saved:bluetooth_ui->ble.devices)+i;return &(*ble)->device;
}
static void bt_ui_notice_state(void){
 if(bluetooth_ui->ble.serial==bluetooth_ui->ble_notice_serial)return;
 bluetooth_ui->ble_notice_serial=bluetooth_ui->ble.serial;
 bluetooth_ui->ble_notice_until=app.page==THINGSOS_UI_BLUETOOTH&&bluetooth_ui->ble.phase==THINGSOS_BLE_ERROR?now()+8000:0;
}
static bool bt_ui_ble_error_visible(void){return bluetooth_ui->ble.phase==THINGSOS_BLE_ERROR&&bluetooth_ui->ble_notice_until&&now()<bluetooth_ui->ble_notice_until;}
static void bt_ui_state(void){if(bt_ui_ready()){thingsos_bluetooth_status(&bluetooth_ui->status);thingsos_ble_status(&bluetooth_ui->ble);bt_ui_notice_state();bt_ui_merge();if(bluetooth_ui->list_start>=bt_ui_count())bluetooth_ui->list_start=0;}}
static const char* bt_ui_summary(void){bt_ui_state();return !bluetooth_ui?"未连接":bluetooth_ui->status.connected?bluetooth_ui->status.current.name:bluetooth_ui->ble.connected?bluetooth_ui->ble.current.device.name:"未连接";}
static bool bt_ui_idle(void){if(thingsos_audio_suspend_locked())return true;say("请先保存录音或结束音频初始化，再修改蓝牙连接");return false;}
static void bt_ui_result(bool ok){if(!ok)say("蓝牙任务处理中，请稍后重试");app.dirty=true;}
static void bt_ui_enter(void){if(!bt_ui_ready()){say("界面内存不足");return;}bluetooth_ui->view=0;bluetooth_ui->painted=false;bluetooth_ui->confirm_pair=bluetooth_ui->confirm_forget=bluetooth_ui->ble_confirm=false;bt_ui_state();bluetooth_ui->ble_notice_until=0;navigate(THINGSOS_UI_BLUETOOTH);}
static bool bt_ui_scanning(void){return bluetooth_ui->ble.scanning||bluetooth_ui->status.phase==THINGSOS_BT_SCANNING||bluetooth_ui->status.phase==THINGSOS_BT_STARTING;}
static bool bt_ui_audio_busy(void){return bluetooth_ui->status.busy&&bluetooth_ui->status.phase!=THINGSOS_BT_SCANNING;}
static void bt_ui_search(bool start){
 /* Release I2S DMA before the BLE initializer checks the same internal heap. */
 bool audio=!start||bt_ui_idle(),ble=thingsos_ble_scan(start);
 if(start){audio=audio&&thingsos_bluetooth_scan(true);bluetooth_ui->ble_notice_until=0;bluetooth_ui->list_saved=false;bluetooth_ui->list_start=0;
  bluetooth_ui->found_count=0;bluetooth_ui->wait_audio=audio;bluetooth_ui->wait_ble=ble;
  bluetooth_ui->search_audio_serial=bluetooth_ui->status.serial;bluetooth_ui->search_ble_serial=bluetooth_ui->ble.serial;}
 else if(bluetooth_ui->status.phase==THINGSOS_BT_SCANNING||bluetooth_ui->status.phase==THINGSOS_BT_STARTING)audio=thingsos_bluetooth_scan(false);
 if(!ble&&!audio)say("蓝牙搜索请求失败，请稍后重试");
 else if(!ble)say("低功耗搜索请求失败；音频设备搜索继续");
 else if(!audio)say("音频搜索请求失败；低功耗设备搜索继续");
 app.dirty=true;
}
static uint64_t bt_ui_audio_key(void){
 thingsos_bt_status_t* s=&bluetooth_ui->status;unsigned flags[]={s->connected,s->enabled,s->phase==THINGSOS_BT_SCANNING||s->phase==THINGSOS_BT_STARTING,s->phase==THINGSOS_BT_ERROR};
 uint64_t key=cache_hash(14695981039346656037ULL,flags,sizeof(flags));
 if(s->connected)key=cache_hash(key,&s->current,sizeof(s->current));
 if(bluetooth_ui->view==1){unsigned detail[]={s->phase,s->automatic,s->busy};key=cache_hash(key,detail,sizeof(detail));}
 if(bluetooth_ui->view==1||s->phase==THINGSOS_BT_ERROR)key=cache_hash(key,s->message,strlen(s->message));
 if(bluetooth_ui->list_saved){key=cache_hash(key,&s->saved_count,sizeof(s->saved_count));key=cache_hash(key,s->saved,s->saved_count*sizeof(s->saved[0]));}
 return key;
}
static uint64_t bt_ui_ble_key(void){
 thingsos_ble_status_t* b=&bluetooth_ui->ble;unsigned flags[]={b->connected,b->scanning,b->phase==THINGSOS_BLE_ERROR};
 uint64_t key=cache_hash(14695981039346656037ULL,flags,sizeof(flags));
 if(b->connected){key=cache_hash(key,&b->current.device,sizeof(b->current.device));key=cache_hash(key,&b->identity.device,sizeof(b->identity.device));}
 if(bluetooth_ui->view==3){unsigned detail[]={b->phase,b->pairing,b->passkey,b->generation,b->automatic,b->hid_ready,b->bonded};key=cache_hash(key,detail,sizeof(detail));}
 if(bluetooth_ui->view==3||b->phase==THINGSOS_BLE_ERROR)key=cache_hash(key,b->message,strlen(b->message));
 if(bluetooth_ui->list_saved){key=cache_hash(key,&b->saved_count,sizeof(b->saved_count));for(unsigned i=0;i<b->saved_count;++i){key=cache_hash(key,&b->saved[i].device,sizeof(b->saved[i].device));key=cache_hash(key,&b->saved[i].address_type,sizeof(b->saved[i].address_type));}}
 return key;
}
static uint64_t bt_ui_summary_key(void){
 const char* text=bluetooth_ui->status.connected?bluetooth_ui->status.current.name:bluetooth_ui->ble.connected?bluetooth_ui->ble.current.device.name:"未连接";
 return cache_hash(14695981039346656037ULL,text,strlen(text));
}
static bool bt_ui_region(void){return bluetooth_ui&&app.page==THINGSOS_UI_BLUETOOTH&&bluetooth_ui->painted&&!bluetooth_ui->painted_controls&&!app.controls_open&&!app.controls_refresh&&!app.feedback_refresh&&!app.force_refresh&&!app.transition_refresh&&!app.lock_wave_pending&&!app.message[0];}
static esp_err_t bt_ui_present(void){return thingsos_board_present_metrics(&app.canvas);}
static void bt_ui_presented(bool ok){
 if(!bluetooth_ui)return;
 bluetooth_ui->painted=ok&&app.page==THINGSOS_UI_BLUETOOTH&&!app.controls_open;bluetooth_ui->painted_controls=app.controls_open;
 if(bluetooth_ui->painted){bluetooth_ui->pending=false;bluetooth_ui->next_redraw=now()+2000;bluetooth_ui->serial=bluetooth_ui->status.serial;bluetooth_ui->ble_serial=bluetooth_ui->ble.serial;}
}
static void bt_ui_poll(void){
 if(!bluetooth_ui)return;
 uint64_t audio_before=bt_ui_audio_key(),ble_before=bt_ui_ble_key(),summary_before=bt_ui_summary_key();bool connected_audio=bluetooth_ui->status.connected,connected_ble=bluetooth_ui->ble.connected;
 thingsos_bluetooth_status(&bluetooth_ui->status);thingsos_ble_status(&bluetooth_ui->ble);
 bt_ui_notice_state();
 if(app.page!=THINGSOS_UI_BLUETOOTH){if(bluetooth_ui->ble.scanning)thingsos_ble_scan(false);if(bluetooth_ui->status.phase==THINGSOS_BT_SCANNING)thingsos_bluetooth_scan(false);}
 bool changed_ble=bluetooth_ui->ble.serial!=bluetooth_ui->ble_serial,changed_audio=bluetooth_ui->status.serial!=bluetooth_ui->serial;
 bluetooth_ui->ble_serial=bluetooth_ui->ble.serial;bluetooth_ui->serial=bluetooth_ui->status.serial;
 if(bluetooth_ui->ble_pair_generation!=bluetooth_ui->ble.generation){bluetooth_ui->ble_pair_generation=bluetooth_ui->ble.generation;bluetooth_ui->ble_digits=0;bluetooth_ui->ble_pin[0]=0;}
 bool rows=(changed_ble||changed_audio)&&bt_ui_merge();
 if(app.page==THINGSOS_UI_SETTINGS){if(summary_before!=bt_ui_summary_key())app.dirty=true;return;}
 if(app.page!=THINGSOS_UI_BLUETOOTH){bluetooth_ui->painted=false;return;}
 if(bluetooth_ui->ble_notice_until&&now()>=bluetooth_ui->ble_notice_until){bluetooth_ui->ble_notice_until=0;bluetooth_ui->pending=true;app.dirty=true;}
 bool audio=audio_before!=bt_ui_audio_key(),ble=ble_before!=bt_ui_ble_key(),urgent=false;
 if(changed_ble&&bluetooth_ui->ble.pairing!=THINGSOS_BLE_PAIR_NONE){bluetooth_ui->view=3;bluetooth_ui->ble_selected=bluetooth_ui->ble.current;bluetooth_ui->confirm_pair=bluetooth_ui->confirm_forget=bluetooth_ui->ble_confirm=false;urgent=true;}
 bool relevant=!bluetooth_ui->view?(audio||ble||(!bluetooth_ui->list_saved&&rows)):bluetooth_ui->view==1?audio:ble;
 if(relevant||urgent)bluetooth_ui->pending=true;
 urgent|=connected_audio!=bluetooth_ui->status.connected||connected_ble!=bluetooth_ui->ble.connected||
   (bluetooth_ui->status.phase==THINGSOS_BT_ERROR&&audio)||(bluetooth_ui->ble.phase==THINGSOS_BLE_ERROR&&ble)||
   (bluetooth_ui->view==1&&audio)||(bluetooth_ui->view==3&&ble);
 if(bluetooth_ui->pending&&(urgent||now()>=bluetooth_ui->next_redraw))app.dirty=true;
}
static void bt_ui_switch(int x,int y,bool on){static const char* const names[]={"关","开"};reader_refresh_segments(names,x,y,176,2,on?1:0);}
#include "ble_connection_ui.inc"
static bool bt_ui_draw(void){
 if(app.page!=THINGSOS_UI_BLUETOOTH||!bt_ui_ready())return false;
 bt_ui_state();thingsos_bt_status_t* s=&bluetooth_ui->status;header(bluetooth_ui->view?"设备详情":"蓝牙");
 if(ble_ui_draw())return true;
 if(bluetooth_ui->view){
  thingsos_bt_device_t* d=&bluetooth_ui->selected;bool connected=s->connected&&!strcmp(s->current.address,d->address);
  char address[18];thingsos_bt_display_address(d,address);
  label(thingsos_bt_display_name(d),36,285,32,612);label(address,36,338,24,612);label("音频设备",36,385,23,612);
  label(bt_ui_audio_busy()?"正在处理…":connected?"已连接 · 音频输出":"未连接",36,435,26,612);
  reader_control(connected?"断开连接":"连接",36,490,612,!bt_ui_audio_busy());reader_control("忘记此设备",36,590,612,false);
  label("自动重连",36,750,24,420);bt_ui_switch(472,704,s->automatic);
  label("断开保留设备信息，忘记后需重新配对",36,850,23,612);
  if(s->enabled)reader_control("关闭音频蓝牙",36,938,612,false);
  if(s->message[0])label(s->message,36,1175,22,612);
 }else{
  label("耳机、音箱、键盘和翻页器",36,270,26,612);
  char text[256];snprintf(text,sizeof(text),"音频：%s   外设：%s",s->connected?s->current.name:"未连接",bluetooth_ui->ble.connected?bluetooth_ui->ble.current.device.name:"未连接");label(text,36,318,22,612);
  reader_control(bt_ui_scanning()?"停止搜索":"搜索设备",36,370,285,false);reader_control(bluetooth_ui->list_saved?"查看附近":"查看已保存",363,370,285,false);
  unsigned count=bt_ui_count();if(!count)label(bluetooth_ui->list_saved?"还没有保存的设备":bt_ui_scanning()?"正在搜索，请将设备设为配对模式":"点击“搜索设备”查找附近设备",36,520,24,612);
  for(unsigned i=0;i<5&&bluetooth_ui->list_start+i<count;++i){
   const thingsos_ble_device_t* ble;const thingsos_bt_device_t* d=bt_ui_at(bluetooth_ui->list_start+i,&ble);int top=470+(int)i*108;
   bool active=ble?ble_ui_current(ble):s->connected&&!strcmp(s->current.address,d->address);
   label(ble?d->name:thingsos_bt_display_name(d),36,top+28,26,500);label("详情",550,top+28,22,98);
   char address[18];if(!ble)thingsos_bt_display_address(d,address);
   snprintf(text,sizeof(text),"%s · %s · %s",ble?"低功耗":"音频",active?"已连接":bluetooth_ui->list_saved?"已保存":ble&&!ble->connectable?"仅广播":"可连接",ble?d->address:address);
   label(text,36,top+70,21,612);thingsos_rect(&app.canvas,36,top+94,612,1,170);
  }
  if(count>5){reader_control("上一组",36,1040,285,false);reader_control("下一组",363,1040,285,false);}
  snprintf(text,sizeof(text),"%s %u · %s",bluetooth_ui->list_saved?"已保存":"已发现",count,bt_ui_scanning()?"搜索中":"请将设备设为配对模式");
  label(s->phase==THINGSOS_BT_ERROR?s->message:bt_ui_ble_error_visible()?bluetooth_ui->ble.message:text,36,1175,22,612);
 }
 if(bluetooth_ui->confirm_pair||bluetooth_ui->confirm_forget){
  bool pair=bluetooth_ui->confirm_pair;thingsos_rect(&app.canvas,36,355,612,475,0);thingsos_rect(&app.canvas,38,357,608,471,255);
  label(pair?"配对并连接？":"忘记此设备？",66,420,34,552);label(thingsos_bt_display_name(&bluetooth_ui->selected),66,490,28,552);
  label(pair?"成功后保存设备信息，下次可直接连接":"移除本机连接记录，录音和音乐保留",66,550,24,552);
  reader_control(pair?"配对并连接":"忘记设备",66,610,552,true);reader_control("取消",66,710,552,false);
 }
 return true;
}
static bool bt_ui_key(thingsos_key_action_t action){
 if(app.page!=THINGSOS_UI_BLUETOOTH)return false;
 if(action==THINGSOS_KEY_HOME)return false;
 if(action!=THINGSOS_KEY_PREVIOUS&&action!=THINGSOS_KEY_MENU)return true;
 bt_ui_state();
 if(bluetooth_ui->confirm_pair||bluetooth_ui->confirm_forget)bluetooth_ui->confirm_pair=bluetooth_ui->confirm_forget=false;
 else if(bluetooth_ui->ble_confirm)bluetooth_ui->ble_confirm=false;
 else if(bluetooth_ui->view){if(bluetooth_ui->view==3&&bluetooth_ui->ble.pairing!=THINGSOS_BLE_PAIR_NONE)thingsos_ble_pair_reply(false,0);bluetooth_ui->view=0;}
 else{bt_ui_search(false);
  /* Opened from the control center: return there, like Wi-Fi details. */
  if(app_controls_bt_return){thingsos_ui_page_t back=app_controls_bt_page;navigate(back);app_controls_set(true);}
  else navigate(THINGSOS_UI_SETTINGS);}
 app.dirty=true;return true;
}
static bool bt_ui_tap(int x,int y){
 if(app.page!=THINGSOS_UI_BLUETOOTH||!bt_ui_ready())return false;
 if(app.controls_open)return false;
 bt_ui_state();thingsos_bt_status_t* s=&bluetooth_ui->status;
 if(bluetooth_ui->confirm_pair||bluetooth_ui->confirm_forget){
  if(x>=66&&x<618&&y>=710&&y<778)bluetooth_ui->confirm_pair=bluetooth_ui->confirm_forget=false;
  else if(x>=66&&x<618&&y>=610&&y<678&&bt_ui_idle()){
   bool ok=bluetooth_ui->confirm_pair?thingsos_bluetooth_connect(&bluetooth_ui->selected):thingsos_bluetooth_forget(bluetooth_ui->selected.address);
   if(ok){bluetooth_ui->view=bluetooth_ui->confirm_pair?1:0;bluetooth_ui->confirm_pair=bluetooth_ui->confirm_forget=false;}bt_ui_result(ok);
  }app.dirty=true;return true;
 }
 if(x<100&&y>=74&&y<200){bt_ui_key(THINGSOS_KEY_PREVIOUS);return true;}
 if(ble_ui_tap(x,y))return true;
 if(bluetooth_ui->view){
  bool connected=s->connected&&!strcmp(s->current.address,bluetooth_ui->selected.address);
  if(y>=490&&y<558&&!bt_ui_audio_busy()&&bt_ui_idle()){if(connected)bt_ui_result(thingsos_bluetooth_disconnect());else bluetooth_ui->confirm_pair=true;}
  else if(y>=590&&y<658&&!s->busy)bluetooth_ui->confirm_forget=true;
  else if(y>=704&&y<790)bt_ui_result(thingsos_bluetooth_automatic(!s->automatic));
  else if(y>=938&&y<1006&&!s->busy&&bt_ui_idle())bt_ui_result(thingsos_bluetooth_power(false));
 }else if(y>=370&&y<438){if(x<342)bt_ui_search(!bt_ui_scanning());else{bluetooth_ui->list_saved=!bluetooth_ui->list_saved;bluetooth_ui->list_start=0;}}
 else if(y>=470&&y<1010){unsigned i=bluetooth_ui->list_start+(unsigned)(y-470)/108;if(i<bt_ui_count()){
  const thingsos_ble_device_t* ble;const thingsos_bt_device_t* d=bt_ui_at(i,&ble);
  if(ble){bluetooth_ui->ble_selected=*ble;bluetooth_ui->view=3;}else{bluetooth_ui->selected=*d;bluetooth_ui->view=1;}
 }}else if(y>=1040&&y<1108&&bt_ui_count()>5){if(x<342)bluetooth_ui->list_start=bluetooth_ui->list_start>=5?bluetooth_ui->list_start-5:0;else if(bluetooth_ui->list_start+5<bt_ui_count())bluetooth_ui->list_start+=5;}
 app.dirty=true;return true;
}
/* Control-center Bluetooth tile. "On" mirrors a phone switch: BLE auto-reconnect
   enabled, or the audio module powered, or any link up. Both states persist in
   their services (BLE automatic flag, audio module enabled state). */
static bool bt_cc_state(bool* connected){
 *connected=false;if(!bt_ui_ready())return false;
 thingsos_bluetooth_status(&bluetooth_ui->status);thingsos_ble_status(&bluetooth_ui->ble);
 const thingsos_bt_status_t* s=&bluetooth_ui->status;const thingsos_ble_status_t* b=&bluetooth_ui->ble;
 *connected=(s->enabled&&s->connected)||b->connected;
 return s->enabled||b->automatic||b->connected;
}
static void bt_cc_toggle(void){
 bool connected,on=bt_cc_state(&connected);
 if(!bluetooth_ui){say("界面内存不足");return;}
 const thingsos_bt_status_t* s=&bluetooth_ui->status;const thingsos_ble_status_t* b=&bluetooth_ui->ble;
 bool ok=true;
 if(on){
  /* Never cut a headset that is carrying (or holding) the current audio. */
  if(s->enabled&&s->connected&&thingsos_audio_active()){say("蓝牙耳机正在使用，请先停止播放");return;}
  if(s->enabled&&!bt_ui_idle())return;
  ok=thingsos_ble_automatic(false);
  if(b->scanning)thingsos_ble_scan(false);
  if(b->connected||b->phase==THINGSOS_BLE_CONNECTING||b->phase==THINGSOS_BLE_PAIRING||b->phase==THINGSOS_BLE_DISCOVERING)
   ok=thingsos_ble_disconnect()&&ok;
  if(s->enabled)ok=thingsos_bluetooth_power(false)&&ok;
  say(ok?"蓝牙已关闭":"蓝牙任务处理中，请稍后重试");
 }else{
  ok=thingsos_ble_automatic(true);
  if(!b->connected&&b->last.device.address[0])ok=thingsos_ble_connect(&b->last)&&ok;
  /* Headsets reconnect only when the user kept audio auto-reconnect on. */
  if(!s->enabled&&s->saved_count&&s->automatic)ok=thingsos_bluetooth_power(true)&&ok;
  say(ok?"蓝牙已开启":"蓝牙任务处理中，请稍后重试");
 }
 app.dirty=true;
}
/* Long press / chevron: the Bluetooth page, searching; Back returns here. */
static void bt_cc_open(void){
 thingsos_ui_page_t from=app.page;
 app.controls_open=false;bt_ui_enter();
 if(app.page!=THINGSOS_UI_BLUETOOTH)return;
 app_controls_bt_return=true;app_controls_bt_page=from;
 if(!bt_ui_scanning())bt_ui_search(true);
 app.dirty=true;
}
