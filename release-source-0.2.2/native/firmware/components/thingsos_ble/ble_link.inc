/* SPDX-License-Identifier: Apache-2.0 */
/* Bounded, asynchronous HID-over-GATT discovery. All callbacks run on the host
   event queue; connection generation rejects late completions after cancel. */
enum {HID_SERVICES=4,HID_CHARACTERISTICS=32,HID_REPORTS=16};
typedef enum {OP_NONE,OP_SERVICE,OP_CHARACTERISTIC,OP_MAP,OP_PROTOCOL,OP_DESCRIPTOR,OP_REFERENCE,OP_SUBSCRIBE} hid_operation_t;
typedef struct {uint16_t start,end;thingsos_hid_layout_t layout;thingsos_hid_decoder_t decoder;bool valid;} hid_service_t;
typedef struct {uint16_t definition,value,uuid;uint8_t properties;} hid_characteristic_t;
typedef struct {uint16_t value,end,ccc,reference;uint8_t service,id,type,properties;bool enabled;} hid_report_t;
typedef struct {hid_service_t service[HID_SERVICES];hid_characteristic_t chr[HID_CHARACTERISTICS];hid_report_t report[HID_REPORTS];
 unsigned services,service_index,characteristics,reports,report_index,first_report,enabled;uint16_t map,protocol;uint8_t protocol_properties;
 uint8_t raw[2048];size_t raw_size;hid_operation_t operation;bool warnings,boot;} hid_session_t;
static hid_session_t* hid;
static void forget_device(const thingsos_ble_device_t*);
static void next_service(void);
static void next_report(void);
static void discover_descriptors(void);
static void read_reference(void);
static void subscribe_report(void);
static void hid_reset(void){if(hid)memset(hid,0,sizeof(*hid));}
static bool operation_current(uint16_t conn,void* arg,hid_operation_t op){return current(conn,arg)&&hid&&hid->operation==op&&state.phase==THINGSOS_BLE_DISCOVERING;}
static void discovery_failed(const char* text){if(hid)hid->operation=OP_NONE;close_link(text);}
static void discovery_finished(void){
 state.phase=THINGSOS_BLE_CONNECTED;state.connected=true;state.hid_ready=hid&&hid->enabled>0;state.pairing=THINGSOS_BLE_PAIR_NONE;reconnect_at=0;reconnect_attempts=0;
 snprintf(state.message,sizeof(state.message),"%s",state.hid_ready?(hid->warnings?"已连接，部分按键报告不可用":"已连接，可使用键盘或翻页器"):"已连接，此设备暂无可用的标准输入服务");
 save_connected();publish(NULL);
}
static bool report_supported(const hid_report_t* report){
 if(report->type!=1 || !hid->service[report->service].valid)return false;
 const thingsos_hid_layout_t* layout=&hid->service[report->service].layout;
 for(unsigned i=0;i<layout->reports;++i)if(layout->report[i].id==report->id)return true;
 return false;
}
static int subscribed(uint16_t conn,const struct ble_gatt_error* error,struct ble_gatt_attr* attr,void* arg){
 (void)attr;if(!operation_current(conn,arg,OP_SUBSCRIBE))return 0;
 if(!error->status){hid->report[hid->report_index].enabled=true;++hid->enabled;}
 else{hid->warnings=true;ESP_LOGW("ThingsOS_ble","HID subscription rc=%d",error->status);}
 next_report();return 0;
}
static void subscribe_report(void){
 hid_report_t* report=hid->report+hid->report_index;
 if(!report->ccc||!report_supported(report)){hid->warnings=true;next_report();return;}
 uint8_t value[2]={(report->properties&0x10)?1:2,0};hid->operation=OP_SUBSCRIBE;deadline=clock_ms()+10000;
 if(ble_gattc_write_flat(connection,report->ccc,value,2,subscribed,token())){hid->warnings=true;next_report();}
}
static int referenced(uint16_t conn,const struct ble_gatt_error* error,struct ble_gatt_attr* attr,void* arg){
 if(!operation_current(conn,arg,OP_REFERENCE))return 0;
 if(error->status || !attr || OS_MBUF_PKTLEN(attr->om)!=2){hid->warnings=true;next_report();return 0;}
 uint8_t value[2];if(ble_hs_mbuf_to_flat(attr->om,value,2,NULL)){hid->warnings=true;next_report();return 0;}
 hid_report_t* report=hid->report+hid->report_index;report->id=value[0];report->type=value[1];subscribe_report();return 0;
}
static void read_reference(void){
 hid_report_t* report=hid->report+hid->report_index;
 if(!report->reference){subscribe_report();return;}
 hid->operation=OP_REFERENCE;deadline=clock_ms()+10000;
 if(ble_gattc_read(connection,report->reference,referenced,token())){hid->warnings=true;next_report();}
}
static int descriptor(uint16_t conn,const struct ble_gatt_error* error,uint16_t value,const struct ble_gatt_dsc* dsc,void* arg){
 (void)value;if(!operation_current(conn,arg,OP_DESCRIPTOR))return 0;
 if(!error->status && dsc){uint16_t uuid=ble_uuid_u16(&dsc->uuid.u);hid_report_t* report=hid->report+hid->report_index;
  if(uuid==0x2902)report->ccc=dsc->handle;else if(uuid==0x2908)report->reference=dsc->handle;
 }else if(error->status==BLE_HS_EDONE)read_reference();else{hid->warnings=true;next_report();}
 return 0;
}
static void discover_descriptors(void){
 hid_report_t* report=hid->report+hid->report_index;
 if(report->end<=report->value){hid->warnings=true;next_report();return;}
 hid->operation=OP_DESCRIPTOR;deadline=clock_ms()+10000;
 if(ble_gattc_disc_all_dscs(connection,report->value,report->end,descriptor,token())){hid->warnings=true;next_report();}
}
static void next_report(void){if(++hid->report_index<hid->reports)discover_descriptors();else next_service();}
static void start_reports(void){
 hid->report_index=hid->first_report;if(hid->report_index<hid->reports)discover_descriptors();else next_service();
}
static int protocol_written(uint16_t conn,const struct ble_gatt_error* error,struct ble_gatt_attr* attr,void* arg){
 (void)attr;if(!operation_current(conn,arg,OP_PROTOCOL))return 0;
 if(error->status){hid->warnings=true;next_service();}else start_reports();return 0;
}
static void set_protocol(void){
 if(!hid->service[hid->service_index].valid){next_service();return;}
 if(!hid->protocol){start_reports();return;}uint8_t mode=hid->boot?0:1;
 if(hid->protocol_properties&0x04){
  if(ble_gattc_write_no_rsp_flat(connection,hid->protocol,&mode,1)){hid->warnings=true;next_service();}else start_reports();
 }else if(hid->protocol_properties&0x08){hid->operation=OP_PROTOCOL;deadline=clock_ms()+10000;
  if(ble_gattc_write_flat(connection,hid->protocol,&mode,1,protocol_written,token())){hid->warnings=true;next_service();}
 }else if(hid->boot){hid->warnings=true;next_service();}else start_reports();
}
static int report_map(uint16_t conn,const struct ble_gatt_error* error,struct ble_gatt_attr* attr,void* arg){
 if(!operation_current(conn,arg,OP_MAP))return 0;
 if(!error->status && attr){size_t bytes=OS_MBUF_PKTLEN(attr->om);
  if(attr->offset!=hid->raw_size||bytes>sizeof(hid->raw)-hid->raw_size||ble_hs_mbuf_to_flat(attr->om,hid->raw+hid->raw_size,(uint16_t)bytes,NULL)){
   discovery_failed("设备输入报告过大或格式异常");return BLE_HS_EAPP;
  }hid->raw_size+=bytes;deadline=clock_ms()+10000;
 }else if(error->status==BLE_HS_EDONE){
  hid_service_t* svc=hid->service+hid->service_index;svc->valid=thingsos_hid_parse(&svc->layout,hid->raw,hid->raw_size);
  if(!svc->valid)hid->warnings=true;set_protocol();
 }else{hid->warnings=true;next_service();}
 return 0;
}
static void characteristics_done(void){
 hid->map=hid->protocol=0;hid->raw_size=0;hid->boot=false;hid->first_report=hid->reports;
 for(unsigned i=0;i<hid->characteristics;++i){hid_characteristic_t* c=hid->chr+i;
  if(c->uuid==0x2a4b)hid->map=c->value;
  else if(c->uuid==0x2a4e){hid->protocol=c->value;hid->protocol_properties=c->properties;}
 }
 hid->boot=!hid->map;
 for(unsigned i=0;i<hid->characteristics;++i){hid_characteristic_t* c=hid->chr+i;
  if(c->uuid!=(hid->boot?0x2a22:0x2a4d)||!(c->properties&0x30))continue;
  if(hid->reports==HID_REPORTS){discovery_failed("设备输入通道过多，暂不支持");return;}
  hid_report_t* r=hid->report+hid->reports++;memset(r,0,sizeof(*r));r->value=c->value;r->end=i+1<hid->characteristics?hid->chr[i+1].definition-1:hid->service[hid->service_index].end;
  r->service=(uint8_t)hid->service_index;r->type=1;r->properties=c->properties;
 }
 if(hid->boot){
  static const uint8_t boot_keyboard[]={0x05,7,0x19,0xe0,0x29,0xe7,0x15,0,0x25,1,0x75,1,0x95,8,0x81,2,0x75,8,0x95,1,0x81,1,0x19,0,0x29,0x65,0x15,0,0x25,0x65,0x75,8,0x95,6,0x81,0};
  hid->service[hid->service_index].valid=thingsos_hid_parse(&hid->service[hid->service_index].layout,boot_keyboard,sizeof(boot_keyboard));set_protocol();
 }else{hid->operation=OP_MAP;deadline=clock_ms()+10000;
  if(ble_gattc_read_long(connection,hid->map,0,report_map,token())){hid->warnings=true;next_service();}
 }
}
static int characteristic(uint16_t conn,const struct ble_gatt_error* error,const struct ble_gatt_chr* chr,void* arg){
 if(!operation_current(conn,arg,OP_CHARACTERISTIC))return 0;
 if(!error->status && chr){
  if(hid->characteristics==HID_CHARACTERISTICS){discovery_failed("设备输入特征过多，暂不支持");return BLE_HS_EAPP;}
  if(chr->val_handle<=chr->def_handle){discovery_failed("设备输入特征格式异常");return BLE_HS_EAPP;}
  hid->chr[hid->characteristics++]=(hid_characteristic_t){chr->def_handle,chr->val_handle,ble_uuid_u16(&chr->uuid.u),chr->properties};
 }else if(error->status==BLE_HS_EDONE)characteristics_done();else discovery_failed("输入服务读取失败，请重试");return 0;
}
static void discover_characteristics(void){
 hid->characteristics=0;hid->operation=OP_CHARACTERISTIC;deadline=clock_ms()+10000;hid_service_t* svc=hid->service+hid->service_index;
 if(ble_gattc_disc_all_chrs(connection,svc->start,svc->end,characteristic,token()))discovery_failed("输入服务发现启动失败");
}
static void next_service(void){
 if(++hid->service_index<hid->services)discover_characteristics();else{hid->operation=OP_NONE;discovery_finished();}
}
static int service(uint16_t conn,const struct ble_gatt_error* error,const struct ble_gatt_svc* svc,void* arg){
 if(!operation_current(conn,arg,OP_SERVICE))return 0;
 if(!error->status && svc){
  if(hid->services==HID_SERVICES){discovery_failed("设备输入服务过多，暂不支持");return BLE_HS_EAPP;}
  hid_service_t* target=hid->service+hid->services++;target->start=svc->start_handle;target->end=svc->end_handle;
 }else if(error->status==BLE_HS_EDONE){if(hid->services)discover_characteristics();else discovery_finished();}
 else discovery_failed("设备服务发现失败，请重试");return 0;
}
static void hid_discover(void){
 if(state.phase==THINGSOS_BLE_DISCOVERING || state.phase==THINGSOS_BLE_CONNECTED)return;
 if(!hid){hid=heap_caps_calloc(1,sizeof(*hid),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!hid)hid=calloc(1,sizeof(*hid));}
 if(!hid){discovery_finished();return;}hid_reset();state.phase=THINGSOS_BLE_DISCOVERING;publish("配对成功，正在准备输入服务…");
 static const ble_uuid16_t uuid=BLE_UUID16_INIT(0x1812);hid->operation=OP_SERVICE;deadline=clock_ms()+10000;hard_deadline=clock_ms()+60000;
 if(ble_gattc_disc_svc_by_uuid(connection,&uuid.u,service,token()))discovery_failed("设备服务发现启动失败");
}
static void hid_notify(uint16_t handle,const struct os_mbuf* om){
 if(!hid || !om)return;size_t bytes=OS_MBUF_PKTLEN(om);if(bytes>257)return;uint8_t data[257];
 if(ble_hs_mbuf_to_flat(om,data,(uint16_t)bytes,NULL))return;
 for(unsigned i=0;i<hid->reports;++i){hid_report_t* report=hid->report+i;if(!report->enabled||report->value!=handle)continue;
  hid_service_t* svc=hid->service+report->service;thingsos_hid_decode(&svc->layout,&svc->decoder,report->id,data,bytes,emit_key,NULL);return;
 }
}
