// SPDX-License-Identifier: Apache-2.0
/* Approved V3 feedback. Canvas geometry follows portrait/landscape dimensions;
   all drawing and expiration remain on the main UI task. */
static void volume_overlay_draw(void) {
    app.volume_overlay_drawn=true;
    const int w=84,h=312,left=app.canvas.width-w-28,top=(app.canvas.height-h)/2;
    unsigned volume=app.audio_volume>100?100:app.audio_volume;
    float fill_top=h*(100-volume)/100.0f;
    for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        int dx=left+x,dy=top+y;
        if(dx<0||dy<0||dx>=app.canvas.width||dy>=app.canvas.height)continue;
        size_t at=(size_t)dy*app.canvas.width+dx;unsigned sum=0;
        for(int sy=0;sy<4;++sy)for(int sx=0;sx<4;++sx) {
            float px=x+(sx+0.5f)/4,py=y+(sy+0.5f)/4;
            if(!feedback_round_inside(px,py,w,h,42)){sum+=app.canvas.pixels[at];continue;}
            if(!feedback_round_inside(px-2,py-2,w-4,h-4,40))continue;
            bool filled=py>=fill_top;
            unsigned shade=filled?0:238;
            if(feedback_speaker_ink(px-24,py-258))shade=filled?255:0;
            if(!volume&&feedback_segment(px,py,22,255,64,297,1.75f))shade=0;
            sum+=shade;
        }
        app.canvas.pixels[at]=(uint8_t)((sum+8)/16);
    }
    unsigned scale=app.font.ui_size_percent;app.font.ui_size_percent=0;
    char text[16];snprintf(text,sizeof(text),"%u%%",volume);
    centered_label(text,left,top-18,24,w);app.font.ui_size_percent=scale;
}
static void record_feedback_draw(void) {
    if(app.record_notice==RECORD_NOTICE_NONE)return;
    int w=456,h=240,x=(app.canvas.width-w)/2,y=(app.canvas.height-h)/2;
    unsigned scale=app.font.ui_size_percent,weight=app.font.ui_weight_override;
    app.font.ui_size_percent=app.font.ui_weight_override=0;
    control_round(x,y,w,h,12,3,0,255);
    const char* title="正在准备录音";
    const char* subtitle="请稍候";
    const char* detail="录音开始后显示确认";
    if(app.record_notice==RECORD_NOTICE_STARTED) {
        title="开始录音";subtitle="后台录音中 · 再按 AI 键结束";detail="提示将自动收起";
    } else if(app.record_notice==RECORD_NOTICE_SAVING) {
        title="正在保存录音";detail="保存完成后显示结果";
    } else if(app.record_notice==RECORD_NOTICE_SAVED) {
        title="录音已保存";subtitle="可在「应用 → 录音」中查看";detail="提示将自动收起";
    } else if(app.record_notice>=RECORD_NOTICE_START_ERROR) {
        title=app.record_notice==RECORD_NOTICE_SAVE_ERROR?"录音保存失败":
            app.record_notice==RECORD_NOTICE_START_ERROR?"无法开始录音":"录音中断";
        subtitle=app.record_error[0]?app.record_error:"请检查 SD 卡后重试";detail="轻触卡片关闭";
    }
    if(app.record_notice==RECORD_NOTICE_SAVED) {
        /* Rounded success tick, without relying on a font symbol. */
        for(int yy=0;yy<52;++yy)for(int xx=0;xx<44;++xx) {
            unsigned ink=0;
            for(int sy=0;sy<4;++sy)for(int sx=0;sx<4;++sx) {
                float px=xx+(sx+0.5f)/4,py=yy+(sy+0.5f)/4;
                ink+=feedback_segment(px,py,7,26,17,37,3)||feedback_segment(px,py,17,37,38,14,3);
            }
            thingsos_rect(&app.canvas,x+206+xx,y+28+yy,1,1,(uint8_t)(255-ink*255/16));
        }
    } else feedback_mic_draw(x+206,y+28,44,52);
    centered_label(title,x+12,y+130,36,w-24);
    centered_label(subtitle,x+18,y+180,24,w-36);
    centered_label(detail,x+18,y+214,19,w-36);
    app.font.ui_size_percent=scale;app.font.ui_weight_override=weight;
}
