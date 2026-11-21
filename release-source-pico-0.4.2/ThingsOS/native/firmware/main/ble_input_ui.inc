/* SPDX-License-Identifier: Apache-2.0 */
/* UI ownership stays here. The BLE task only queues decoded HID presses. */
static void ble_editor_action(bool confirm){
 if(input_uses_qwerty()||app.editing==4){feature_tap(confirm?500:100,app.editing==4?1030:580);return;}
 /* The compact rename/date editor retains the original 10-column key grid. */
 unsigned key=confirm?48:47;
 feature_tap(36+(int)(key%10)*61+27,600+(int)(key/10)*74+34);
}
static void ble_key_dispatch(const thingsos_hid_key_t* key){
 if(!app.ble_enabled||app.asleep||app.pin.required||app.page==THINGSOS_UI_LOCK||app.page==THINGSOS_UI_LOCK_PIN)return;
 app.activity_at=now();thingsos_runtime_activity(&app.runtime,app.activity_at);
 if(app.page==THINGSOS_UI_PASSWORD && key->page==7){
  bool qwerty=input_uses_qwerty();
  if(key->usage==0x28||key->usage==0x58){
   if(app.input.composition[0]){thingsos_ime_commit(&app.input,app.input.candidate_page*THINGSOS_IME_PAGE_SIZE);app.input_refresh=qwerty;app.dirty=true;}
   else if(app.editing==6&&!(key->modifiers&0x11)){thingsos_input_insert(&app.input,"\n");app.input_refresh=qwerty;app.dirty=true;}
   else ble_editor_action(true);
   return;
  }
  if(key->usage==0x29){
   if(app.input.composition[0]){app.input.composition[0]=0;app.input.candidate_page=0;app.input_refresh=qwerty;app.dirty=true;}
   else ble_editor_action(false);
   return;
  }
  if(key->usage==0x2c && (key->modifiers&0x11)){
   if(app.input.composition[0])say("请先选择拼音候选");else{app.input.chinese=!app.input.chinese;app.transition_refresh=true;app.dirty=true;}return;
  }
  bool changed=false;
  if(key->usage==0x2a){thingsos_ime_backspace(&app.input);changed=true;}
  else if(key->usage==0x4c && app.input.cursor<app.input.length){thingsos_input_move(&app.input,1);thingsos_input_backspace(&app.input);changed=true;}
  else if(key->usage==0x50||key->usage==0x4f){thingsos_input_move(&app.input,key->usage==0x50?-1:1);changed=true;}
  else if(app.input.composition[0]&&(key->usage==0x4b||key->usage==0x4e)){thingsos_ime_page(&app.input,key->usage==0x4b?-1:1);changed=true;}
  else{
   char value[2]={thingsos_hid_ascii(key),0};
   if(!value[0])return;
   if(app.editing==4 && (value[0]<'0'||value[0]>'9'))return;
   if(value[0]==' '&&app.input.composition[0])changed=thingsos_ime_commit(&app.input,app.input.candidate_page*THINGSOS_IME_PAGE_SIZE);
   else if(app.input.chinese&&((value[0]>='a'&&value[0]<='z')||(value[0]=='\''&&app.input.composition[0])))changed=thingsos_ime_letter(&app.input,value[0]);
   else changed=thingsos_input_insert(&app.input,value);
   if(!changed)say("输入已达到字节上限");
  }
  if(changed){app.input_refresh|=qwerty;app.dirty=true;}return;
 }
 thingsos_key_action_t action=thingsos_hid_navigation(key);
 if(key->page==7&&key->usage==0x29&&(app.page!=THINGSOS_UI_READING||app.menu||app.jump))action=THINGSOS_KEY_PREVIOUS;
 if(action!=THINGSOS_KEY_NONE)capacitive_key(action);
}
static void ble_input_poll(void){
 static bool blocked;bool unavailable=!app.ble_enabled||app.asleep||app.pin.required||
    app.page==THINGSOS_UI_LOCK||app.page==THINGSOS_UI_LOCK_PIN;
 thingsos_hid_key_t key;
 if(unavailable||blocked){while(thingsos_ble_input(&key)){}blocked=unavailable;return;}
 for(unsigned i=0;i<8&&thingsos_ble_input(&key);++i)ble_key_dispatch(&key);
}
