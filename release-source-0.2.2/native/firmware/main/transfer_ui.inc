// SPDX-License-Identifier: Apache-2.0
/* User-approved book/music transfer UE, native canvas implementation. */
static void transfer_tab(const char* text,int x,bool selected){
 control_round(x,216,297,70,10,2,85,255);centered_label(text,x+10,261,28,277);
 if(selected)for(int yy=0;yy<70;++yy)for(int xx=0;xx<297;++xx)
  if(control_inside(xx*4+2,yy*4+2,297,70,10)){size_t at=(size_t)(216+yy)*app.canvas.width+x+xx;app.canvas.pixels[at]=255-app.canvas.pixels[at];}
}
static void app_transfer_page_draw(void){
 header("传输");transfer_tab("书籍",36,!app.transfer_music);transfer_tab("音乐",351,app.transfer_music);
 control_round(36,306,612,116,14,2,170,255);char line[768],address[96],link[128];
 bool connected=thingsos_network_connected();
 snprintf(line,sizeof(line),"%s　%s",app.hotspot?"设备热点":"Wi-Fi",app.hotspot?thingsos_network_ssid():thingsos_network_saved_name()[0]?thingsos_network_saved_name():"尚未选择网络");
 label(line,58,348,25,568);
 if(app.hotspot)snprintf(line,sizeof(line),"热点密码：%s",thingsos_network_password());
 else strlcpy(line,connected?"手机与设备连接同一个 Wi-Fi":"请从网络应用或控制中心连接 Wi-Fi",sizeof(line));
 label(line,58,394,24,568);
 thingsos_network_address(app.hotspot,address,sizeof(address));
 if(!strncmp(address,"http://",7))snprintf(link,sizeof(link),"%s%ckind=%s",address,strchr(address,'?')?'&':'?',app.transfer_music?"music":"books");else strlcpy(link,address,sizeof(link));
 if(app.service_on&&(app.hotspot||connected)&&!strncmp(address,"http://",7))qr_address_box(link,440,200);
 else centered_label(app.service_on?"连接网络后显示传输二维码":"传输服务已结束",36,548,27,612);
 centered_label(link,36,680,26,612);
 label("扫码或在浏览器打开设备地址",36,733,24,612);label("手机页面默认选择当前分类",36,775,23,612);
 snprintf(line,sizeof(line),"保存至 %s",app.transfer_music?"/sdcard/music":app.records.library_path[0]?app.records.library_path:"/sdcard/books");label(line,36,830,27,612);
 label(app.transfer_music?"支持 MP3 / WAV · 上传后在音乐应用中查看":"支持 EPUB / TXT / Markdown · 上传后刷新书库",36,876,22,612);
 thingsos_transfer_state_t transfer;thingsos_network_transfer(&transfer);
 if(transfer.active){snprintf(line,sizeof(line),"接收 %u%% · %s",transfer.percent,transfer.name);label(line,36,910,22,612);thingsos_rect(&app.canvas,36,920,612,6,215);thingsos_rect(&app.canvas,36,920,(int)(612*transfer.percent/100),6,0);}
 else if(app.message[0])label(app.message,36,910,22,612);
 control_round(36,936,612,70,10,2,85,255);centered_label(app.service_on?"结束传输":"开启传输",46,981,28,592);
 label(!media()?"SD 卡未挂载，无法接收文件":app.transfer_music?"音乐固定保存到 /sdcard/music":"字体 /fonts · 壁纸 /wallpapers",36,1065,22,612);
 nav();
}
