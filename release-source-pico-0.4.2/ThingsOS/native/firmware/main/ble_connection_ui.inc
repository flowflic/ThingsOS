/* SPDX-License-Identifier: Apache-2.0 */
static bool ble_ui_current(const thingsos_ble_device_t* d){
 return bluetooth_ui->ble.connected && (thingsos_ble_same(d,&bluetooth_ui->ble.current)||thingsos_ble_same(d,&bluetooth_ui->ble.identity));
}
static bool ble_ui_busy(void){thingsos_ble_phase_t p=bluetooth_ui->ble.phase;return p==THINGSOS_BLE_CONNECTING||p==THINGSOS_BLE_PAIRING||p==THINGSOS_BLE_DISCOVERING||p==THINGSOS_BLE_DISCONNECTING;}
static void ble_ui_result(bool ok){if(!ok)say("低功耗任务未接受，请稍后重试");app.dirty=true;}
static void ble_ui_pair_draw(void){
 thingsos_ble_status_t* b=&bluetooth_ui->ble;char text[16];
 label(b->current.device.name,36,280,28,612);
 if(b->pairing==THINGSOS_BLE_PAIR_INPUT){
  label("输入外设显示的六位验证码",36,390,24,612);
  snprintf(text,sizeof(text),"%s",bluetooth_ui->ble_pin);for(unsigned i=bluetooth_ui->ble_digits;i<6;++i)text[i]='_';text[6]=0;
  centered_label(text,36,500,50,612);
  static const char* const keys[]={"1","2","3","4","5","6","7","8","9","删除","0","清空"};
  for(unsigned i=0;i<12;++i)reader_control(keys[i],84+(int)(i%3)*176,560+(int)(i/3)*86,164,false);
  reader_control("取消",36,950,285,false);reader_control("确认",363,950,285,bluetooth_ui->ble_digits==6);
 }else{
  label(b->pairing==THINGSOS_BLE_PAIR_DISPLAY?"在外设输入验证码并按回车":"核对两端验证码是否一致",36,390,24,612);
  snprintf(text,sizeof(text),"%06u",(unsigned)b->passkey);centered_label(text,36,530,56,612);
  if(b->pairing==THINGSOS_BLE_PAIR_CONFIRM)reader_control("一致，确认配对",36,650,612,true);
  else centered_label("正在等待外设完成配对…",36,650,24,612);
  reader_control("取消配对",36,770,612,false);
 }
}
static bool ble_ui_draw(void){
 if(bluetooth_ui->view!=3)return false;
 thingsos_ble_status_t* b=&bluetooth_ui->ble;
 if(bluetooth_ui->view==3){
  if(b->pairing!=THINGSOS_BLE_PAIR_NONE){ble_ui_pair_draw();return true;}
  thingsos_ble_device_t* d=&bluetooth_ui->ble_selected;bool active=ble_ui_current(d);
  label(d->device.name,36,285,32,612);label(d->device.address,36,340,24,612);
  label(ble_ui_busy()?b->message:active?(b->hid_ready?"已连接 · 键盘 / 翻页器可用":"已连接 · 标准输入服务不可用"):"未连接",36,425,24,612);
  reader_control(ble_ui_busy()?"取消连接":active?"断开连接":"连接",36,490,612,!ble_ui_busy());
  reader_control("忘记此设备",36,590,612,false);
  label("自动重连",36,750,23,420);bt_ui_switch(472,704,b->automatic);
  label("方向键、翻页键和音量键可翻页",36,850,22,612);
  if(b->message[0])label(b->message,36,1175,22,612);
 }
 if(bluetooth_ui->ble_confirm){
  thingsos_rect(&app.canvas,36,355,612,475,0);thingsos_rect(&app.canvas,38,357,608,471,255);
  label(bluetooth_ui->ble_forget?"忘记此低功耗设备？":"配对并连接此设备？",66,420,30,552);
  label(bluetooth_ui->ble_selected.device.name,66,490,26,552);
  label(bluetooth_ui->ble_forget?"同时移除设备记录和配对密钥":"连接后可接收标准键盘和翻页按键",66,550,22,552);
  reader_control(bluetooth_ui->ble_forget?"忘记":"连接",66,610,552,true);reader_control("取消",66,710,552,false);
 }
 return true;
}
static bool ble_ui_tap(int x,int y){
 if(bluetooth_ui->view!=3)return false;
 thingsos_ble_status_t* b=&bluetooth_ui->ble;
 if(bluetooth_ui->ble_confirm){
  if(x>=66&&x<618&&y>=710&&y<778)bluetooth_ui->ble_confirm=false;
  else if(x>=66&&x<618&&y>=610&&y<678){bool ok=bluetooth_ui->ble_forget?thingsos_ble_forget(&bluetooth_ui->ble_selected):thingsos_ble_connect(&bluetooth_ui->ble_selected);if(ok)bluetooth_ui->ble_confirm=false;ble_ui_result(ok);}
 }else if(bluetooth_ui->view==3 && b->pairing!=THINGSOS_BLE_PAIR_NONE){
  if(b->pairing==THINGSOS_BLE_PAIR_INPUT){
   if(x>=84&&x<600&&y>=560&&y<904&&(x-84)%176<164&&(y-560)%86<68){unsigned k=(unsigned)(y-560)/86*3+(unsigned)(x-84)/176;
    if(k==9){if(bluetooth_ui->ble_digits)bluetooth_ui->ble_pin[--bluetooth_ui->ble_digits]=0;}
    else if(k==11){bluetooth_ui->ble_digits=0;bluetooth_ui->ble_pin[0]=0;}
    else if(bluetooth_ui->ble_digits<6){bluetooth_ui->ble_pin[bluetooth_ui->ble_digits++]=(char)(k==10?'0':'1'+k);bluetooth_ui->ble_pin[bluetooth_ui->ble_digits]=0;}
   }else if(y>=950&&y<1018){if(x<342)ble_ui_result(thingsos_ble_pair_reply(false,0));else if(bluetooth_ui->ble_digits==6)ble_ui_result(thingsos_ble_pair_reply(true,(uint32_t)strtoul(bluetooth_ui->ble_pin,NULL,10)));}
  }else if(b->pairing==THINGSOS_BLE_PAIR_CONFIRM&&y>=650&&y<718)ble_ui_result(thingsos_ble_pair_reply(true,0));
  else if(y>=770&&y<838)ble_ui_result(thingsos_ble_pair_reply(false,0));
 }else if(bluetooth_ui->view==3){
  if(y>=490&&y<558){
   if(ble_ui_busy()||ble_ui_current(&bluetooth_ui->ble_selected))ble_ui_result(thingsos_ble_disconnect());
   else if(b->connected)say("请先断开当前低功耗设备");
   else if(!bluetooth_ui->ble_selected.connectable&&!bluetooth_ui->list_saved)say("此设备仅广播，不接受连接");
   else{bluetooth_ui->ble_confirm=true;bluetooth_ui->ble_forget=false;}
  }else if(y>=590&&y<658){bluetooth_ui->ble_confirm=bluetooth_ui->ble_forget=true;}
  else if(y>=704&&y<790)ble_ui_result(thingsos_ble_automatic(!b->automatic));
 }
 app.dirty=true;return true;
}
