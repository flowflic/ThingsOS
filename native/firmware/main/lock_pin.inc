// SPDX-License-Identifier: Apache-2.0
/* Main-task-owned screen lock. The host executes this same controller/storage
   path; Windows CNG supplies PBKDF2 instead of the device's PSA implementation. */
#ifdef THINGSOS_HOST_UI
#include <bcrypt.h>
static bool host_pin_crypto_fail,host_pin_random_fail;
#else
#include "psa/crypto.h"
#endif
#define LOCK_PIN_MAGIC UINT32_C(0x50494e31)
/* The label, switch and help text form one settings row. Share its bounds
   between contact activation and ordinary tap dispatch, including when off. */
static bool power_pin_hit(int x,int y) {
    return x>=POWER_PIN_LEFT && x<POWER_PIN_LEFT+POWER_PIN_WIDTH &&
        y>POWER_PIN_TOP && y<POWER_PIN_TOP+POWER_PIN_HEIGHT;
}
static void pin_wipe(void* data,size_t size) {
    volatile unsigned char* p=data;while(size--)*p++=0;
}
static bool pin_digits(const char* pin) {
    if(!pin)return false;
    for(unsigned i=0;i<LOCK_PIN_DIGITS;++i)if(pin[i]<'0'||pin[i]>'9')return false;
    return pin[LOCK_PIN_DIGITS]==0;
}
static bool pin_derive(const char* pin,const uint8_t salt[16],uint8_t out[32]) {
    if(!pin_digits(pin))return false;
#ifdef THINGSOS_HOST_UI
    if(host_pin_crypto_fail)return false;
    BCRYPT_ALG_HANDLE algorithm=NULL;
    bool ok=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,NULL,BCRYPT_ALG_HANDLE_HMAC_FLAG)>=0;
    if(ok)ok=BCryptDeriveKeyPBKDF2(algorithm,(PUCHAR)pin,LOCK_PIN_DIGITS,(PUCHAR)salt,16,
        LOCK_PIN_ITERATIONS,out,32,0)>=0;
    if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);
    return ok;
#else
    if(psa_crypto_init()!=PSA_SUCCESS)return false;
    psa_key_derivation_operation_t operation=PSA_KEY_DERIVATION_OPERATION_INIT;
    bool ok=psa_key_derivation_setup(&operation,PSA_ALG_PBKDF2_HMAC(PSA_ALG_SHA_256))==PSA_SUCCESS &&
        psa_key_derivation_input_integer(&operation,PSA_KEY_DERIVATION_INPUT_COST,LOCK_PIN_ITERATIONS)==PSA_SUCCESS &&
        psa_key_derivation_input_bytes(&operation,PSA_KEY_DERIVATION_INPUT_SALT,salt,16)==PSA_SUCCESS &&
        psa_key_derivation_input_bytes(&operation,PSA_KEY_DERIVATION_INPUT_PASSWORD,(const uint8_t*)pin,LOCK_PIN_DIGITS)==PSA_SUCCESS &&
        psa_key_derivation_output_bytes(&operation,out,32)==PSA_SUCCESS;
    psa_key_derivation_abort(&operation);
    return ok;
#endif
}
static bool pin_random(uint8_t salt[16]) {
#ifdef THINGSOS_HOST_UI
    return !host_pin_random_fail && BCryptGenRandom(NULL,salt,16,BCRYPT_USE_SYSTEM_PREFERRED_RNG)>=0;
#else
    return psa_crypto_init()==PSA_SUCCESS && psa_generate_random(salt,16)==PSA_SUCCESS;
#endif
}
static void pin_clear_entry(void) {
    pin_wipe(app.pin.entry,sizeof(app.pin.entry));app.pin.length=0;
}
static void pin_clear_input(void) {
    pin_clear_entry();pin_wipe(app.pin.draft,sizeof(app.pin.draft));
    app.pin.verified=false;app.pin.notice[0]=0;
}
static bool pin_record_valid(const lock_pin_record_t* record) {
    if(record->magic!=LOCK_PIN_MAGIC || record->version!=1 ||
       record->iterations!=LOCK_PIN_ITERATIONS || record->enabled>1 ||
       record->reserved[0] || record->reserved[1] || record->reserved[2])return false;
    if(!record->enabled){
        unsigned nonzero=0;
        for(unsigned i=0;i<16;++i)nonzero|=record->salt[i];
        for(unsigned i=0;i<32;++i)nonzero|=record->verifier[i];
        if(nonzero)return false;
    }
    return true;
}
static void pin_load(void) {
    pin_wipe(&app.pin,sizeof(app.pin));
    nvs_handle_t handle;
    esp_err_t result=nvs_flash_init();
    if(result==ESP_OK)result=nvs_open("thingsos-ui",NVS_READONLY,&handle);
    if(result==ESP_ERR_NVS_NOT_FOUND)return; /* Upgrade / no password configured. */
    if(result!=ESP_OK){app.pin.fault=app.pin.required=true;return;}
    lock_pin_record_t record={0};size_t size=sizeof(record);
    result=nvs_get_blob(handle,"lock-pin-v1",&record,&size);nvs_close(handle);
    if(result==ESP_ERR_NVS_NOT_FOUND)return;
    if(result!=ESP_OK || size!=sizeof(record) || !pin_record_valid(&record)) {
        app.pin.fault=app.pin.required=true;
    }else{
        app.pin.record=record;app.pin.enabled=record.enabled!=0;
        app.pin.required=app.pin.enabled;
    }
    pin_wipe(&record,sizeof(record));
}
static bool pin_store(const lock_pin_record_t* candidate) {
    nvs_handle_t handle;
    if(!pin_record_valid(candidate) || nvs_open("thingsos-ui",NVS_READWRITE,&handle)!=ESP_OK)return false;
    bool ok=nvs_set_blob(handle,"lock-pin-v1",candidate,sizeof(*candidate))==ESP_OK && nvs_commit(handle)==ESP_OK;
    nvs_close(handle);
    if(ok){app.pin.record=*candidate;app.pin.enabled=candidate->enabled!=0;}
    return ok;
}
/* Entry buffers never survive sleep, a cancelled edit or leaving the page. */
static void pin_lock(void) {
    pin_clear_input();app.pin.required=app.pin.enabled||app.pin.fault;
    if(app.pin.required){app.pin.purpose=PIN_UNLOCK;app.pin.stage=PIN_VERIFY;}
}
static void pin_boot(void) {
    pin_load();
    if(app.pin.required){
        app.pin.purpose=PIN_UNLOCK;app.pin.stage=PIN_VERIFY;
        app.page=THINGSOS_UI_LOCK_PIN;app.menu_root=THINGSOS_UI_LIBRARY;
    }
    thingsos_board_private_input(app.pin.required);
}
static void pin_open(lock_pin_purpose_t purpose) {
    pin_clear_input();app.pin.purpose=purpose;
    app.pin.stage=purpose==PIN_SETUP?PIN_NEW:PIN_VERIFY;
    app.confirm_action=0;app.controls_open=false;
    if(app.image_view.open){
        thingsos_image_close(&app.image_view,&app.runtime.location);
        app.start=app.runtime.location.byte_offset;
        free(app.image_pixels);app.image_pixels=NULL;
        thingsos_runtime_image_overlay(&app.runtime,false,now());
    }
    app.footnote_open=false;
    app.volume_overlay_until=0;app.volume_overlay_pending=false;
    navigate(THINGSOS_UI_LOCK_PIN);
    app.message[0]=0;app.activity_at=now();
}
static void pin_wake(void) {
    app.pin.required=true;pin_open(PIN_UNLOCK);
    app.lock_wave_pending=true;app.lock_wave_forward=false;app.force_refresh=true;
}
static void pin_cancel(void) {
    bool locked=app.pin.required;
    pin_clear_input();
    if(locked)power_action(1);
    else navigate(THINGSOS_UI_POWER);
}
static void pin_error(const char* message) {
    pin_clear_entry();strlcpy(app.pin.notice,message,sizeof(app.pin.notice));
    app.input_refresh=app.dirty=true;
}
static void pin_submit(void) {
    if(app.pin.fault || app.pin.length!=LOCK_PIN_DIGITS || !pin_digits(app.pin.entry))return;
    /* A new setup/verification stage may change the header as well. Ordinary
       digits and validation errors use the narrow input path instead. */
    app.input_refresh=false;
    if(app.pin.required && app.pin.purpose!=PIN_UNLOCK){pin_wake();return;}
    if(app.pin.stage==PIN_VERIFY) {
        uint8_t verifier[32]={0};
        bool ok=app.pin.enabled && pin_derive(app.pin.entry,app.pin.record.salt,verifier);
        unsigned difference=0;
        for(unsigned i=0;i<sizeof(verifier);++i)difference|=verifier[i]^app.pin.record.verifier[i];
        pin_wipe(verifier,sizeof(verifier));
        if(!ok){pin_error("密码验证失败，请重试");return;}
        if(difference){pin_error("密码错误，请重新输入");return;}
        pin_clear_entry();app.pin.notice[0]=0;
        if(app.pin.purpose==PIN_UNLOCK) {
            app.pin.required=false;pin_clear_input();
            navigate(THINGSOS_UI_LIBRARY);app.activity_at=now();app.force_refresh=true;
            app.lock_wave_pending=true;app.lock_wave_forward=false;
        }else if(app.pin.purpose==PIN_DISABLE) {
            lock_pin_record_t candidate={.magic=LOCK_PIN_MAGIC,.version=1,.iterations=LOCK_PIN_ITERATIONS};
            if(!pin_store(&candidate)){pin_error("保存失败，密码仍开启");return;}
            pin_clear_input();navigate(THINGSOS_UI_POWER);say("解锁密码已关闭");
        }else if(app.pin.purpose==PIN_CHANGE) {
            app.pin.verified=true;app.pin.stage=PIN_NEW;app.dirty=true;
        }
        return;
    }
    if((app.pin.enabled && (app.pin.purpose!=PIN_CHANGE || !app.pin.verified)) ||
       (!app.pin.enabled && app.pin.purpose!=PIN_SETUP))return;
    if(app.pin.stage==PIN_NEW) {
        memcpy(app.pin.draft,app.pin.entry,sizeof(app.pin.draft));pin_clear_entry();
        app.pin.notice[0]=0;app.pin.stage=PIN_CONFIRM;app.dirty=true;return;
    }
    if(memcmp(app.pin.entry,app.pin.draft,LOCK_PIN_DIGITS)) {
        pin_error("两次密码不一致，请重新输入");return;
    }
    lock_pin_record_t candidate={.magic=LOCK_PIN_MAGIC,.version=1,.iterations=LOCK_PIN_ITERATIONS,.enabled=1};
    bool ok=pin_random(candidate.salt) && pin_derive(app.pin.entry,candidate.salt,candidate.verifier) && pin_store(&candidate);
    pin_wipe(&candidate,sizeof(candidate));
    if(!ok){pin_error("密码保存失败，请重试");return;}
    bool changed=app.pin.purpose==PIN_CHANGE;
    pin_clear_input();navigate(THINGSOS_UI_POWER);say(changed?"解锁密码已修改":"解锁密码已开启");
}
static void pin_tap(int x,int y) {
    app.activity_at=now();thingsos_runtime_activity(&app.runtime,app.activity_at);
    if(x<90 && y>=74 && y<200){if(!app.pin.required)pin_cancel();return;}
    if(y>=1090 && y<1166 && x>=44 && x<640){pin_submit();return;}
    if(x<44||x>=640||y<546||y>=1042)return;
    unsigned column=(unsigned)(x-44)/204,row=(unsigned)(y-546)/128;
    if(column>2 || row>3 || x>=44+(int)column*204+188 || y>=546+(int)row*128+112)return;
    if(row==3 && column==0){pin_cancel();return;}
    if(app.pin.fault)return;
    unsigned previous_length=app.pin.length;
    if(row==3 && column==2) {
        if(app.pin.length)app.pin.entry[--app.pin.length]=0;
    }else if(app.pin.length<LOCK_PIN_DIGITS){
        app.pin.entry[app.pin.length++]=(char)(row==3?'0':'1'+row*3+column);
        app.pin.entry[app.pin.length]=0;
    }
    if(previous_length==app.pin.length&&!app.pin.notice[0])return;
    app.pin.notice[0]=0;app.input_refresh=app.dirty=true;
}
static bool pin_input_frame(void) {
    return app.page==THINGSOS_UI_LOCK_PIN && app.input_refresh &&
        !app.force_refresh && !app.transition_refresh && !app.lock_wave_pending;
}
static void pin_input_draw(void) {
    bool locked=app.pin.required,verify=app.pin.stage==PIN_VERIFY;
    app.font.ui_size_percent=100;app.font.ui_weight_override=1;
    /* Match the board's two exact bands. Never repaint the static number keys
       or header for a digit, delete, or validation error. */
    thingsos_rect(&app.canvas,0,320,684,158,255);
    thingsos_rect(&app.canvas,0,1090,684,76,255);
    const char* title=locked?"输入解锁密码":verify?"验证原密码":app.pin.stage==PIN_CONFIRM?"再次输入密码":app.pin.purpose==PIN_CHANGE?"设置新密码":"设置解锁密码";
    centered_label(title,36,351,30,612);
    for(unsigned i=0;i<LOCK_PIN_DIGITS;++i)app_circle(187+(int)i*62,384,12,i<app.pin.length?12:2);
    const char* subtitle=app.pin.fault?"密码记录不可用，请重启或恢复备份":app.pin.notice[0]?app.pin.notice:
        locked?"验证后进入首页":verify?(app.pin.purpose==PIN_DISABLE?"验证后关闭解锁密码":"验证后可设置新密码"):
        app.pin.stage==PIN_CONFIRM?"请再次输入刚才设置的 6 位数字":"请输入 6 位数字";
    centered_label(subtitle,36,448,22,612);
    const char* submit=locked?"解锁":verify?"验证":app.pin.stage==PIN_CONFIRM?(app.pin.purpose==PIN_CHANGE?"保存新密码":"确认并开启"):"下一步";
    bool active=app.pin.length==LOCK_PIN_DIGITS&&!app.pin.fault;
    /* DU has black/white endpoints: outline versus solid distinguishes the
       disabled and enabled button without losing a gray border on deletion. */
    thingsos_rect(&app.canvas,44,1090,596,76,0);
    if(!active)thingsos_rect(&app.canvas,46,1092,592,72,255);
    /* Labels are drawn black first; invert only the solid submit button. */
    if(active)thingsos_rect(&app.canvas,44,1090,596,76,255);
    centered_label(submit,44,1138,29,596);
    if(active)for(int y=1090;y<1166;++y)for(int x=44;x<640;++x)
        app.canvas.pixels[(size_t)y*app.canvas.width+x]^=255;
    app.font.ui_size_percent=app.font.ui_weight_override=0;
}
static void pin_draw(void) {
    bool locked=app.pin.required,verify=app.pin.stage==PIN_VERIFY;
    /* Fixed typography keeps the approved keypad geometry valid at every UI size. */
    app.font.ui_size_percent=100;app.font.ui_weight_override=1;
    header(locked?"屏幕已锁定":verify?"验证密码":"解锁密码");
    if(locked)thingsos_rect(&app.canvas,24,80,64,112,255); /* No back navigation. */
    thingsos_rect(&app.canvas,326,251,32,31,0);thingsos_rect(&app.canvas,330,255,24,27,255);
    thingsos_rect(&app.canvas,316,275,52,39,0);thingsos_rect(&app.canvas,319,278,46,33,255);
    thingsos_rect(&app.canvas,339,287,6,15,0);
    for(unsigned row=0;row<4;++row)for(unsigned column=0;column<3;++column) {
        int x=44+(int)column*204,y=546+(int)row*128;
        thingsos_rect(&app.canvas,x,y,188,112,0);thingsos_rect(&app.canvas,x+2,y+2,184,108,255);
        char digit[2]={(char)(row==3?'0':'1'+row*3+column),0};
        const char* value=row==3&&column!=1?(column==0?"取消":"删除"):digit;
        centered_label(value,x,y+(strlen(value)>1?68:78),strlen(value)>1?27:58,188);
    }
    pin_input_draw();
}
