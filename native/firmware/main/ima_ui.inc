// SPDX-License-Identifier: Apache-2.0
/* User-approved recording upload flow. Everything here runs on the UI task. */
typedef struct {thingsos_ima_status_t status;unsigned view,return_view,start,serial,shown,pair_generation;uint64_t tick;bool pick_default,waiting_save,pending_send,keep,pair_retry;char path[512];} ima_ui_t;
static ima_ui_t* ima_ui;
static bool ima_ready(void){if(!ima_ui)ima_ui=thingsos_psram_calloc(1,sizeof(*ima_ui));return ima_ui!=NULL;}
static void ima_view(unsigned view){if(!ima_ready())return;if(view==2){ima_ui->pair_generation=ima_ui->status.connection_serial;ima_ui->pair_retry=false;}ima_ui->view=view;app.audio_progress=false;app.transition_refresh=true;app.dirty=true;}
static void ima_prepare(const char* path){
    if(!ima_ready())return;
    app_media.preview=false;
    if(!thingsos_ima_recording_path(path)){say("只能上传 audio 目录内已保存的 WAV 录音");return;}
    if(app.audio.state==THINGSOS_AUDIO_RECORDING||app.audio.state==THINGSOS_AUDIO_RECORD_PAUSED){say("请先结束并保存当前录音");return;}
    strlcpy(ima_ui->path,path,sizeof(ima_ui->path));thingsos_ima_status(&ima_ui->status);ima_ui->shown=0;
    ima_view(ima_ui->status.configured?4:7);
}
static void ima_recording_upload(size_t index){if(index>=app_media.record_count)return;char path[512];snprintf(path,sizeof(path),"/sdcard/audio/%s",app_media.records[index].name);ima_prepare(path);}
static void ima_save_pending(void){if(ima_ready())ima_ui->waiting_save=true;}
static void ima_open_settings(void){if(!ima_ready())return;thingsos_ima_status(&ima_ui->status);ima_view(1);}
static void ima_libraries(bool default_target){if(!ima_ready())return;ima_ui->pick_default=default_target;ima_ui->return_view=default_target?1:4;ima_ui->start=0;if(!thingsos_ima_refresh(false))say("请先连接 ima，并结束音频或传输任务");ima_view(3);}
static void ima_send(bool keep){
    if(!ima_ready())return;
    if(!thingsos_audio_quiesce_locked()){say("请先完成音频保存");return;}
    if(ima_ui->status.pairing){thingsos_ima_pair(false);ima_ui->pending_send=true;ima_ui->keep=keep;return;}
    if(!thingsos_ima_upload(ima_ui->path,keep)){thingsos_ima_status(&ima_ui->status);say(ima_ui->status.message[0]?ima_ui->status.message:"请先连接网络、选择知识库并结束其他任务");}
    ima_ui->shown=1;ima_view(4);
}
static bool ima_key(thingsos_key_action_t key){
    if(!ima_ui||!ima_ui->view)return false;
    if(key==THINGSOS_KEY_HOME){ima_ui->view=0;return false;}
    if(key!=THINGSOS_KEY_MENU&&key!=THINGSOS_KEY_PREVIOUS)return false;
    if(ima_ui->view==2){thingsos_ima_pair(false);ima_view(1);}
    else if(ima_ui->view==3)ima_view(ima_ui->return_view);
    else if(ima_ui->view==1||ima_ui->view==4||ima_ui->view>=5)ima_view(0);
    return true;
}
/* Phone pairing entry: the LAN address is what the user scans, so it is shown
   with its QR code instead of being typed from a shortened URL line. */
static void ima_pair_link(char* link,size_t capacity){
    char address[96];thingsos_network_address(false,address,sizeof(address));
    const char* host=strstr(address,"://");
    if(strncmp(address,"http://",7)){link[0]=0;return;}
    snprintf(link,capacity,"http://%s:8081/",host?host+3:address);
}
static bool ima_draw(void){
    if(!ima_ui||!ima_ui->view||ima_ui->view>=5)return false;
    unsigned view=ima_ui->view;thingsos_ima_status_t* s=&ima_ui->status;app.font.dither=true;
    header(view==1?"ima 知识库":view==2?"连接 ima":view==3?"选择知识库":"上传到 ima");
    char line[256];
    if(view==1){
        centered_label(s->configured?"已连接 ima":"连接你的 ima",36,310,36,612);
        label(s->configured?"选择默认知识库，手动上传录音":"扫码配置，重启后自动保持连接",36,380,26,612);
        setting_row("连接状态",s->configured?"已连接":"未连接",425);
        setting_row("默认知识库",s->kb_name[0]?s->kb_name:"连接后选择",535);
        setting_row("上传方式","手动上传 · 本地文件保留",645);
        reader_control(s->configured?"更新连接配置":"手机配置",36,790,612,true);
        if(s->configured)reader_control("断开 ima",36,890,612,false);
        label(s->message,36,1035,23,612);
    }else if(view==2){
        centered_label("手机与设备连接同一 WiFi",36,265,26,612);
        char link[128];ima_pair_link(link,sizeof(link));
        if(s->config_phase==THINGSOS_IMA_CONFIG_VERIFYING||(s->config_phase==THINGSOS_IMA_CONFIG_FAILED&&!ima_ui->pair_retry)){
            bool verifying=s->config_phase==THINGSOS_IMA_CONFIG_VERIFYING;
            centered_label(verifying?"正在验证 ima…":"ima 连接未完成",36,370,34,612);
            thingsos_font_size(ui_font(),26);
            thingsos_text_string_page(ui_font(),&app.canvas,s->message,strlen(s->message),0,48,430,588,210,150);
            label(verifying?"正在检查凭证并获取可写知识库":"请在手机修正信息后重新提交",36,735,25,612);
            label("凭证保存到设备；重启后继续使用",36,790,24,612);
            if(!verifying)reader_control("返回配对二维码",36,878,612,false);
            reader_control("关闭手机配对",36,984,612,false);
        }else if(!s->pairing || !link[0]){
            centered_label("手机配对尚未就绪",36,380,30,612);
            label(link[0]?"服务启动后将显示配对二维码":"请先连接 WiFi",36,440,24,612);
            label("若配对已超时，请点击下方按钮重新打开",36,486,23,612);
            label(s->message,36,560,23,612);
            reader_control("重新打开手机配对",36,878,612,false);
            reader_control("关闭手机配对",36,984,612,false);
        }else{
            qr_address_box(link,300,300);
            centered_label(link,36,640,26,612);
            centered_label("扫码直接配置 ima，无需解锁密码",36,690,24,612);
            snprintf(line,sizeof(line),"配对码：%s",s->code);centered_label(line,36,765,38,612);
            snprintf(line,sizeof(line),"剩余 %u 秒",s->pair_seconds);centered_label(line,36,810,24,612);
            reader_control("刷新配对码",36,878,612,false);
            reader_control("关闭手机配对",36,984,612,false);
        }
    }else if(view==3){
        label("仅显示可添加内容的知识库 · 根目录",36,265,24,612);
        for(unsigned row=0;row<4&&ima_ui->start+row<s->count;++row){thingsos_ima_library_t library;if(!thingsos_ima_library(ima_ui->start+row,&library))continue;int y=320+(int)row*145;label(library.name,48,y+47,30,540);label(">",600,y+47,26,40);thingsos_rect(&app.canvas,36,y+125,612,1,0);}
        if(!s->count)centered_label(s->busy?"正在获取知识库…":"没有可添加内容的知识库",36,480,28,612);
        reader_control("首页 / 刷新",36,950,280,false);reader_control("下一页",368,950,280,false);label(s->message,36,1060,22,612);
    }else{
        thingsos_ima_job_t* j=&s->job;bool same=!strcmp(j->path,ima_ui->path)&&ima_ui->shown;bool busy=same&&s->busy;
        label(app_basename(ima_ui->path),36,280,28,612);label("WAV · 本地录音保留",36,335,24,612);
        label("目标知识库",36,420,26,240);label(same?j->kb_name:s->kb_name,275,420,26,373);label("根目录",36,465,23,612);
        if(same&&(busy||j->phase==IMA_UPLOADED||j->phase==IMA_UNCERTAIN||j->phase==IMA_FAILED||j->phase==IMA_CANCELLED)){
            centered_label(j->message[0]?j->message:s->message,36,560,27,612);
            if(busy){snprintf(line,sizeof(line),"%u%%",j->percent);centered_label(line,36,650,64,612);reader_control("取消上传",36,800,612,false);}
            else if(j->phase==IMA_UPLOADED){reader_control("返回录音",36,800,612,true);label("云端内容处理中，请在 ima 中查看",36,730,25,612);}
            else {reader_control(j->phase==IMA_UNCERTAIN?"检查上传结果":"重试",36,790,612,true);reader_control("返回录音",36,890,612,false);}
        }else{
            reader_control("选择知识库",36,540,612,false);reader_control("确认上传",36,770,612,true);reader_control("返回录音",36,870,612,false);
            label("上传后不会删除或重命名本地文件",36,700,24,612);
        }
    }
    nav();return true;
}
static void ima_overlay(void){
    if(!ima_ui||ima_ui->view<5)return;
    unsigned view=ima_ui->view;
    thingsos_rect(&app.canvas,36,360,612,410,0);thingsos_rect(&app.canvas,38,362,608,406,255);
    label(view==5?"录音已保存":view==6?"知识库已有同名录音":"连接 ima",66,425,34,552);
    label(view==5?app_basename(ima_ui->path):view==6?"保留两份：云端副本名添加时间":"请先用手机配置 ima",66,495,25,552);
    label(view==6?"本地不更名，不覆盖云端文件":"本地录音保存在 /sdcard/audio",66,545,24,552);
    reader_control(view==5?"完成":"取消",66,640,250,false);reader_control(view==5?"上传到 ima":view==6?"保留两份":"去配置",366,640,250,true);
}
static bool ima_tap(int x,int y){
    if(app.controls_open)return false;
    if(!ima_ui||!ima_ui->view){if(app.page==THINGSOS_UI_RECORDER&&x>=470&&y>=90&&y<190){ima_open_settings();return true;}return false;}
    unsigned view=ima_ui->view;thingsos_ima_status_t* s=&ima_ui->status;
    if(view>=5){if(y>=640&&y<708){if(x<342)ima_view(0);else if(view==5)ima_prepare(ima_ui->path);else if(view==6)ima_send(true);else {ima_view(2);if(!thingsos_ima_pair(true))say("请先连接 WiFi 并结束传输");}}return true;}
    if(y>=1098){ima_ui->view=0;return false;}
    if(x<100&&y>=74&&y<200){ima_key(THINGSOS_KEY_PREVIOUS);return true;}
    if(view==1){
        if(y>=535&&y<635&&s->configured)ima_libraries(true);
        else if(y>=790&&y<858){ima_view(2);if(!thingsos_ima_pair(true))say("请先连接 WiFi，并结束音频或传输");}
        else if(y>=890&&y<958){if(thingsos_ima_disconnect())say("已断开 ima，本地录音保留");else say("请先结束上传");}
    }else if(view==2){if(y>=984&&y<1052){thingsos_ima_pair(false);ima_view(1);}else if(y>=878&&y<946){if(s->busy){say("正在验证，请稍候");}else if(s->config_phase==THINGSOS_IMA_CONFIG_FAILED&&s->pairing&&!ima_ui->pair_retry){ima_ui->pair_retry=true;}else{thingsos_ima_pair(true);ima_ui->pair_retry=false;}app.dirty=true;}}
    else if(view==3){
        if(y>=320&&y<900&&!s->busy){unsigned index=ima_ui->start+(unsigned)(y-320)/145;if(thingsos_ima_choose(index,ima_ui->pick_default))ima_view(ima_ui->return_view);else say("知识库选择失败，请重试");}
        else if(y>=950&&y<1018&&!s->busy){if(x<342){ima_ui->start=0;thingsos_ima_refresh(false);}else if(ima_ui->start+4<s->count)ima_ui->start+=4;else if(s->more){ima_ui->start=0;thingsos_ima_refresh(true);}app.dirty=true;}
    }else if(view==4){
        bool same=ima_ui->shown&&!strcmp(s->job.path,ima_ui->path);
        if(y>=790&&y<858&&s->busy){thingsos_ima_cancel();say("正在取消，本地录音保留");}
        else if(same&&!s->busy&&s->job.phase==IMA_UPLOADED&&y>=800&&y<868)ima_view(0);
        else if(same&&!s->busy&&(s->job.phase==IMA_UNCERTAIN||s->job.phase==IMA_FAILED||s->job.phase==IMA_CANCELLED)){
            if(y>=790&&y<858){if(s->job.phase==IMA_UNCERTAIN)thingsos_ima_check();else ima_send(false);}else if(y>=890&&y<958)ima_view(0);
        }else if(!s->busy){if(y>=540&&y<608)ima_libraries(false);else if(y>=770&&y<838)ima_send(false);else if(y>=870&&y<938)ima_view(0);}
    }
    return true;
}
static void ima_poll(void){
    if(!ima_ready())return;
    thingsos_ima_status_t* previous=&ima_ui->status;
    thingsos_ima_status_t current;thingsos_ima_status(&current);bool changed=current.serial!=ima_ui->serial;
    bool progress=current.job.percent/10!=previous->job.percent/10&&now()-ima_ui->tick>2000;
    bool pair_tick=ima_ui->view==2&&current.pair_seconds/15!=previous->pair_seconds/15;
    if(current.config_phase==THINGSOS_IMA_CONFIG_VERIFYING)ima_ui->pair_retry=false;
    ima_ui->status=current;
    if(ima_ui->waiting_save&&(app.audio.state==THINGSOS_AUDIO_IDLE||app.audio.state==THINGSOS_AUDIO_ERROR)){
        ima_ui->waiting_save=false;if(app.audio.state==THINGSOS_AUDIO_IDLE&&thingsos_ima_recording_path(app.audio.path)){strlcpy(ima_ui->path,app.audio.path,sizeof(ima_ui->path));ima_view(5);}}
    if(ima_ui->view==2&&current.connection_serial!=ima_ui->pair_generation&&current.configured&&!current.busy){ima_ui->pick_default=true;ima_ui->return_view=1;ima_ui->start=0;ima_view(3);}
    if(ima_ui->pending_send&&!current.pairing&&!current.busy){ima_ui->pending_send=false;ima_send(ima_ui->keep);}
    if(changed){ima_ui->serial=current.serial;if(current.job.phase==IMA_DUPLICATE&&ima_ui->view==4)ima_view(6);}
    if((changed||progress||pair_tick)&&ima_ui->view){bool partial=progress&&!changed&&ima_ui->view==4&&!app.dirty&&!app.controls_open;app.dirty=true;app.audio_progress=partial;ima_ui->tick=now();}
}
