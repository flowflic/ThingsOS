// SPDX-License-Identifier: Apache-2.0
/* Independent local audio service. The worker never draws. File/PA operations
   use the UI's existing SD mutex; audio buffers span a normal panel scan.
   An external-operation claim excludes upload/OTA/USB mutations while a file
   is open. Radio on/off remains independent of that storage claim. */
#include "thingsos/audio.h"
#include "thingsos/audio_format.h"
#include "record_denoise.h"
#include "thingsos/bluetooth.h"
#include "thingsos/memory_trace.h"
#include "thingsos/heap_diag.h"
#include "metalio_bsp.h"
#include "network.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_intr_alloc.h"
#include "esp_attr.h"
#include "soc/interrupts.h"
#include "soc/soc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/idf_additions.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <time.h>
#include <limits.h>
#include <stdatomic.h>
/* Scratch belongs exclusively to the single audio worker. */
static void* thingsos_mp3_scratch;
#define MINIMP3_IMPLEMENTATION
#include "minimp3_psram.h"
#define TAG "ThingsOS_audio"
#define INPUT_BYTES (512u*1024)
#define RECORD_BYTES (128u*1024)
#define FRAME_SAMPLES 512u
typedef enum {CMD_PLAY,CMD_RECORD,CMD_PAUSE,CMD_RESUME,CMD_STOP,CMD_SEEK,CMD_SUSPEND} command_kind_t;
typedef struct {command_kind_t kind;uint32_t offset;char path[512];
    /* Resume of a parked playback: exact data offset (MP3), output sample
       count and any pending seek target, restored instead of re-decoding. */
    bool resume,resume_mp3;uint64_t resume_byte,resume_pcm,resume_seek;} command_t;
/* A paused playback is parked: file closed, decoder/I2S/buffers freed, the
   network claim released and the worker exited, so a long pause no longer
   holds ~40 KiB of main SRAM or blocks WeRead/BLE. status keeps PLAY_PAUSED
   with path and position; resume reopens the file at the saved point. */
typedef struct {bool valid,mp3;uint64_t byte,pcm,seek;uint32_t position_ms;} parked_t;
static parked_t parked;
/* One-shot heap diagnosis of the first audio session of a boot: which
   allocation survives the session and splits the largest main-SRAM block.
   0 idle, 1 S0 (before play), 2 S1 (after I2S open), 3 closed, 4 reported. */
static thingsos_heap_diag_snapshot_t *diag_s0, *diag_s1, *diag_s2;
static atomic_int diag_state;
static int64_t diag_closed_at;
static QueueHandle_t commands;
static SemaphoreHandle_t lifecycle;
static TaskHandle_t task;
static portMUX_TYPE guard=portMUX_INITIALIZER_UNLOCKED;
static thingsos_audio_status_t status={.volume=40};
static unsigned volume=40;
static FILE* file;
static bool recording,paused,is_mp3,eof,hardware,wireless_playback;
static atomic_bool claimed;
static atomic_bool output_suspended;
static uint8_t* input;
static size_t input_used,input_count,input_capacity;
static uint64_t unread,source_size,pcm_samples,seek_sample;
static uint32_t written;
static unsigned source_rate,source_channels,stalls;
static int16_t *decoded,*output;
static int32_t* slots;
static mp3dec_t* decoder;
/* Persistent PSRAM arena for the per-frame MP3 working set, reserved once at
   boot. Measured: when WeRead ran before the first playback, the separately
   allocated decoder buffers landed at addresses that decoded ~15x slower
   (40 ms vs 2.5 ms per frame) for the rest of that boot. One contiguous,
   cache-line aligned block keeps the placement identical every time. */
typedef struct {
    mp3dec_t dec;
    mp3dec_scratch_t scratch;
    mp3d_sample_t decoded[MINIMP3_MAX_SAMPLES_PER_FRAME];
    int16_t output[4610];
} mp3_arena_t;
static mp3_arena_t* mp3_arena;
static bool arena_used;
static uint32_t task_stack; /* stack size of the live worker */
static thingsos_audio_resampler_t resampler;
static uint64_t sync_at;
static uint64_t record_samples,record_clipped;
static unsigned record_clip_peak;
/* Fairness for the shared core. A healthy speaker path blocks in the I2S write
   once the DMA ring is full. When decoding falls behind real time (underrun,
   heard as warble) or a malformed stream yields no PCM, the worker would spin
   at priority 4 and starve the CPU0-pinned UI task. Cap continuous runs. */
#define RUN_LIMIT_US 20000
static uint64_t run_from_us;
static unsigned forced_yields,underrun_writes;
static void ran_blocked(void){run_from_us=(uint64_t)esp_timer_get_time();}
static void fair_yield(void){
    uint64_t now=(uint64_t)esp_timer_get_time();
    if(!run_from_us||now<run_from_us){run_from_us=now;return;}
    if(now-run_from_us<RUN_LIMIT_US)return;
    ++forced_yields;vTaskDelay(1);ran_blocked();
}
/* Playback diagnostics, one line per 5 s while playing. The window is re-armed
   at the first write after start/resume so DMA warm-up and pauses (I2S keeps
   clocking cleared buffers) are not counted as underruns. */
static bool stats_armed;
static uint64_t stats_from_us,last_write_end_us;
static unsigned stats_ovf_base;
static uint32_t gap_max_us,decode_max_us,refill_max_us,write_max_us,frames_decoded;
static uint64_t decode_sum_us;
static void stats_disarm(void){stats_armed=false;}
static void stats_reset(uint64_t now){
    stats_from_us=now;stats_ovf_base=metalio_audio_tx_underruns();
    gap_max_us=decode_max_us=refill_max_us=write_max_us=frames_decoded=0;decode_sum_us=0;forced_yields=underrun_writes=0;
}
static void stats_note(uint32_t* max,uint64_t from){uint32_t d=(uint32_t)((uint64_t)esp_timer_get_time()-from);if(d>*max)*max=d;}
static void stats_write(uint64_t start,uint64_t end){
    if(!stats_armed){stats_armed=true;last_write_end_us=end;stats_reset(end);return;}
    if(start>last_write_end_us&&start-last_write_end_us>gap_max_us)gap_max_us=(uint32_t)(start-last_write_end_us);
    if(end-start>write_max_us)write_max_us=(uint32_t)(end-start);
    last_write_end_us=end;
    if(end-stats_from_us<5000000)return;
    /* Release builds report a 5 s window only when the I2S queue ran dry. */
    unsigned underruns=metalio_audio_tx_underruns()-stats_ovf_base;
    if(underruns)ESP_LOGW(TAG,"Playback %llu ms: underruns=%u gap_max=%u ms write_max=%u ms decode_max=%u avg=%u us refill_max=%u ms frames=%u nonblocking=%u yields=%u dma_free=%u largest=%u",
        (unsigned long long)((end-stats_from_us)/1000),underruns,
        (unsigned)(gap_max_us/1000),(unsigned)(write_max_us/1000),(unsigned)(decode_max_us/1000),(unsigned)(frames_decoded?decode_sum_us/frames_decoded:0),(unsigned)(refill_max_us/1000),
        (unsigned)frames_decoded,underrun_writes,forced_yields,
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA|MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA|MALLOC_CAP_8BIT));
    stats_reset(end);
}
static bool io_take(unsigned timeout){return !thingsos_io_lock||xSemaphoreTake(thingsos_io_lock,timeout==UINT_MAX?portMAX_DELAY:pdMS_TO_TICKS(timeout))==pdTRUE;}
static void io_give(void){if(thingsos_io_lock)xSemaphoreGive(thingsos_io_lock);}
static void pa(bool on){if(io_take(UINT_MAX)){metalio_pa(on&&!thingsos_bluetooth_connected());io_give();}}
void thingsos_audio_status(thingsos_audio_status_t* out){if(!out)return;portENTER_CRITICAL(&guard);*out=status;portEXIT_CRITICAL(&guard);}
static bool parked_valid(void){portENTER_CRITICAL(&guard);bool v=parked.valid;portEXIT_CRITICAL(&guard);return v;}
static void parked_set(const parked_t* p){portENTER_CRITICAL(&guard);if(p)parked=*p;else parked.valid=false;portEXIT_CRITICAL(&guard);}
static bool parked_take(parked_t* out){portENTER_CRITICAL(&guard);bool v=parked.valid;if(v){*out=parked;parked.valid=false;}portEXIT_CRITICAL(&guard);return v;}
bool thingsos_audio_claimed(void){return atomic_load(&claimed);}
bool thingsos_audio_active(void){thingsos_audio_status_t s;thingsos_audio_status(&s);return atomic_load(&claimed)||(s.state!=THINGSOS_AUDIO_IDLE&&s.state!=THINGSOS_AUDIO_ERROR);}
void thingsos_audio_reserve(void){
    if(mp3_arena)return;
    mp3_arena=heap_caps_aligned_calloc(64,1,sizeof(*mp3_arena),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    ESP_LOGI(TAG,"MP3 arena %p bytes=%u",mp3_arena,(unsigned)sizeof(*mp3_arena));
}
/* Interrupt descriptors. esp_intr_alloc() creates one permanent 32-byte
   vector_desc_t per (CPU, interrupt line) the first time that line is used,
   in internal RAM, and never frees it. The I2S and GDMA interrupts of the
   first playback/recording each needed one on the audio core (lines 7 and 9
   measured, 2026-11-22). Created mid-session they landed wherever first-fit
   put them, once in the middle of the largest main-SRAM block (40,948 B ->
   20,468 B for the rest of the boot). The allocator always takes the lowest
   free usable line, so briefly holding the lowest INTR_PREPARE_LINES lines on
   the audio core at boot creates those descriptors while the heap is still
   compact; later I2S/GDMA allocations reuse them and allocate nothing that
   persists. Sources are peripherals ThingsOS never uses; the lines are
   allocated disabled, so they are never routed live. */
/* Audio task core; the task itself is created below. */
#if CONFIG_FREERTOS_NUMBER_OF_CORES>1
#define AUDIO_CORE 1
#else
#define AUDIO_CORE 0
#endif
#define INTR_PREPARE_LINES 6
typedef struct {TaskHandle_t waiter;int lines[INTR_PREPARE_LINES];} intr_prepare_t;
static void IRAM_ATTR intr_prepare_isr(void* unused){(void)unused;}
static void intr_prepare_task(void* context){
    intr_prepare_t* job=context;
    static const int sources[INTR_PREPARE_LINES]={ETS_TWAI0_INTR_SOURCE,ETS_TWAI1_INTR_SOURCE,
        ETS_PCNT0_INTR_SOURCE,ETS_PCNT1_INTR_SOURCE,ETS_CORDIC_INTR_SOURCE,ETS_ZERO_DET_INTR_SOURCE};
    intr_handle_t handle[INTR_PREPARE_LINES]={0};
    for(unsigned i=0;i<INTR_PREPARE_LINES;++i){
        job->lines[i]=-1;
        if(esp_intr_alloc(sources[i],ESP_INTR_FLAG_LOWMED|ESP_INTR_FLAG_INTRDISABLED,intr_prepare_isr,NULL,&handle[i])==ESP_OK)
            job->lines[i]=esp_intr_get_intno(handle[i]);
    }
    for(unsigned i=0;i<INTR_PREPARE_LINES;++i)if(handle[i])esp_intr_free(handle[i]);
    xTaskNotifyGive(job->waiter);
    vTaskDelete(NULL);
}
void thingsos_audio_prepare_interrupts(void){
    static bool done;
    if(done)return;
    done=true;
    intr_prepare_t job={.waiter=xTaskGetCurrentTaskHandle()};
    for(unsigned i=0;i<INTR_PREPARE_LINES;++i)job.lines[i]=-1;
    size_t before=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA);
    /* Interrupts belong to the CPU that allocates them: run on the audio core. */
    if(xTaskCreatePinnedToCore(intr_prepare_task,"audio_intr",3072,&job,configMAX_PRIORITIES-2,NULL,AUDIO_CORE)!=pdPASS){
        ESP_LOGW(TAG,"Interrupt descriptor preparation skipped: no task memory");
        return;
    }
    ulTaskNotifyTake(pdTRUE,portMAX_DELAY); /* job lives on this stack */
    vTaskDelay(1); /* let the idle task reclaim the helper's stack */
    ESP_LOGI(TAG,"Prepared interrupt descriptors core=%d lines=%d,%d,%d,%d,%d,%d main_free %u -> %u",
        AUDIO_CORE,job.lines[0],job.lines[1],job.lines[2],job.lines[3],job.lines[4],job.lines[5],
        (unsigned)before,(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA));
}
void thingsos_audio_volume(unsigned percent){portENTER_CRITICAL(&guard);volume=percent>100?100:percent;status.volume=volume;portEXIT_CRITICAL(&guard);}
static void publish(thingsos_audio_state_t state,const char* message,int error){
    portENTER_CRITICAL(&guard);status.state=state;status.error=error;status.ended=false;++status.serial;
    strlcpy(status.message,message?message:"",sizeof(status.message));portEXIT_CRITICAL(&guard);
    ESP_LOGI(TAG,"State=%u error=%d message=%s",state,error,message?message:"");
}
static bool flush_record(bool final){
    if(!file)return true;
    if(!io_take(final?UINT_MAX:0))return !final;
    bool ok=true;
    if(input_count){size_t n=fwrite(input,1,input_count,file);written+=(uint32_t)n;
        if(n<input_count){memmove(input,input+n,input_count-n);input_count-=n;ok=false;}else input_count=0;}
    uint64_t now=esp_timer_get_time()/1000;
    if(ok&&(final||now-sync_at>=1000)){
        ok=thingsos_wav_header(file,16000,1,written)&&fflush(file)==0;
        if(ok&&final)ok=fsync(fileno(file))==0;sync_at=now;
    }
    io_give();return ok;
}
static bool append_record(const int16_t* samples,size_t frames){
    if(frames>(input_capacity-input_count)/2||written>UINT32_MAX-44-input_count-frames*2)return false;
    memcpy(input+input_count,samples,frames*2);input_count+=frames*2;return true;
}
static bool finish_record_segment(void){
    size_t frames=0;
    bool ok=thingsos_record_denoise_finish(output,FRAME_SAMPLES,&frames);
    if(ok&&frames){
        if(input_capacity-input_count<frames*2&&!flush_record(true))ok=false;
        if(ok)ok=append_record(output,frames);
    }
    thingsos_record_denoise_close();
    return ok;
}
static bool finish(void){
    bool ok=true;
    if(hardware){metalio_audio_close();hardware=false;}
    pa(false);
    if(recording&&file){
        if(!finish_record_segment())ok=false;
        if(!flush_record(true))ok=false;
        ESP_LOGI(TAG,"Recording input samples=%llu clipped=%llu preclip_peak=%u (I2S shift=12)",
          (unsigned long long)record_samples,(unsigned long long)record_clipped,record_clip_peak);
    }else thingsos_record_denoise_close();
    if(file){if(io_take(UINT_MAX)){if(fclose(file))ok=false;file=NULL;io_give();}else ok=false;}
    if(file&&task&&xTaskGetCurrentTaskHandle()==task)
        ESP_LOGI(TAG,"Audio session end %s stack=%u min_free=%u",recording?"record":"play",(unsigned)task_stack,(unsigned)uxTaskGetStackHighWaterMark(NULL));
    if(arena_used){decoder=NULL;thingsos_mp3_scratch=NULL;decoded=NULL;output=NULL;arena_used=false;}
    free(input);input=NULL;free(decoded);decoded=NULL;free(output);output=NULL;free(slots);slots=NULL;free(decoder);decoder=NULL;free(thingsos_mp3_scratch);thingsos_mp3_scratch=NULL;
    input_count=input_used=0;recording=paused=false;
    atomic_store(&output_suspended,false);
    if(claimed){thingsos_network_external_end();claimed=false;}
    {int expect=2;if(atomic_compare_exchange_strong(&diag_state,&expect,3))diag_closed_at=esp_timer_get_time();}
    return ok;
}
static bool refill(bool blocking){
    if(eof||(input_capacity-(input_count-input_used)<65536u&&!blocking))return true;
    if(!io_take(blocking?3000:0))return true;
    if(input_used){memmove(input,input+input_used,input_count-input_used);input_count-=input_used;input_used=0;}
    size_t wanted=input_capacity-input_count;if(unread<wanted)wanted=(size_t)unread;
    size_t got=wanted?fread(input+input_count,1,wanted,file):0;
    input_count+=got;unread-=got;eof=unread==0;
    bool ok=!ferror(file)&&(got||!wanted);io_give();return ok;
}
static bool buffers(bool record){
    input_capacity=record?RECORD_BYTES:INPUT_BYTES;
    input=heap_caps_malloc(input_capacity,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    slots=heap_caps_malloc(2*FRAME_SAMPLES*sizeof(*slots),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    const char* where="PSRAM";
    if(!record){
        /* The synthesis/IMDCT state is touched for every sample. Internal RAM
           avoids PSRAM cache misses, but only with a margin left for Wi-Fi,
           TLS and the SD/LCD DMA pool; otherwise the boot-time PSRAM arena. */
        const unsigned internal=MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT;
        size_t need=sizeof(*decoder)+sizeof(mp3dec_scratch_t);
        bool fast=heap_caps_get_free_size(internal)>=need+40960&&heap_caps_get_largest_free_block(internal)>=sizeof(mp3dec_scratch_t)+16384;
        if(fast){
            decoder=heap_caps_calloc(1,sizeof(*decoder),internal);
            thingsos_mp3_scratch=decoder?heap_caps_malloc(sizeof(mp3dec_scratch_t),internal):NULL;
            if(!thingsos_mp3_scratch){free(decoder);decoder=NULL;fast=false;}
            else where="internal";
        }
        if(!fast){
            thingsos_audio_reserve();
            if(mp3_arena){
                memset(&mp3_arena->dec,0,sizeof(mp3_arena->dec));
                decoder=&mp3_arena->dec;thingsos_mp3_scratch=&mp3_arena->scratch;
                decoded=mp3_arena->decoded;output=mp3_arena->output;arena_used=true;where="arena";
            }else{
                decoder=heap_caps_calloc(1,sizeof(*decoder),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
                thingsos_mp3_scratch=heap_caps_malloc(sizeof(mp3dec_scratch_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
            }
        }
    }
    if(!decoded)decoded=heap_caps_malloc(MINIMP3_MAX_SAMPLES_PER_FRAME*sizeof(*decoded),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!output)output=heap_caps_malloc(4610*sizeof(*output),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!record)ESP_LOGI(TAG,"MP3 state %s decoder=%p scratch=%p decoded=%p output=%p input=%p internal_free=%u largest=%u",
        where,decoder,thingsos_mp3_scratch,decoded,output,input,
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
    return input&&decoded&&output&&slots&&(record||(decoder&&thingsos_mp3_scratch));
}
static bool start(const command_t* command){
    if(!finish()){publish(THINGSOS_AUDIO_ERROR,"音频文件保存失败，请检查 SD 卡",EIO);return false;}
    if(!command->resume)parked_set(NULL);
    publish(THINGSOS_AUDIO_STARTING,"准备音频…",0);
    if(!thingsos_network_external_begin()){
        if(command->resume){
            /* WeRead/transfer took the claim while parked: stay paused. */
            parked_t p={.valid=true,.mp3=command->resume_mp3,.byte=command->resume_byte,.pcm=command->resume_pcm,
                        .seek=command->resume_seek,.position_ms=command->offset};
            parked_set(&p);publish(THINGSOS_AUDIO_PLAY_PAUSED,"传输或微读进行中，完成后再继续播放",EBUSY);return false;
        }
        publish(THINGSOS_AUDIO_ERROR,"传输处理中，请稍后使用音频",EBUSY);return false;
    }
    claimed=true;recording=command->kind==CMD_RECORD;written=0;pcm_samples=0;seek_sample=0;stalls=0;eof=false;input_used=input_count=0;
    if(!thingsos_bluetooth_audio_ready(recording)){publish(THINGSOS_AUDIO_ERROR,recording?"蓝牙音频占用中，请断开耳机或结束搜索后再录音":"音频输出尚未就绪，请稍后重试",EBUSY);finish();return false;}
    wireless_playback=!recording&&thingsos_bluetooth_connected();
    memset(&resampler,0,sizeof(resampler));sync_at=0;
    if(!buffers(recording)){publish(THINGSOS_AUDIO_ERROR,"音频缓冲内存不足",ENOMEM);finish();return false;}
    record_samples=record_clipped=0;record_clip_peak=0;
    if(recording&&!thingsos_record_denoise_open()){publish(THINGSOS_AUDIO_ERROR,"录音降噪初始化失败，请释放内存后重试",ENOMEM);finish();return false;}
    if(!io_take(3000)){publish(THINGSOS_AUDIO_ERROR,"SD 卡繁忙",EBUSY);finish();return false;}
    metalio_sd_info_t sd={0};bool valid=metalio_sd_get_info(&sd)==ESP_OK&&sd.mounted;
    char path[512];strlcpy(path,command->path,sizeof(path));
    if(valid&&recording){
        valid=(!mkdir("/sdcard/audio",0775)||errno==EEXIST)&&sd.free_bytes>1024*1024;
        time_t now=time(NULL);struct tm when={0};localtime_r(&now,&when);char stamp[40];strftime(stamp,sizeof(stamp),"%Y%m%d-%H%M%S",&when);
        for(unsigned copy=0;valid&&copy<1000;++copy){snprintf(path,sizeof(path),"/sdcard/audio/REC-%s-%03u.wav",stamp,copy);
            int fd=open(path,O_CREAT|O_EXCL|O_RDWR,0666);if(fd>=0){file=fdopen(fd,"wb+");if(!file)close(fd);break;}if(errno!=EEXIST){valid=false;break;}}
        valid=valid&&file&&thingsos_wav_header(file,16000,1,0)&&fflush(file)==0;
    }else if(valid){
        if(strncmp(path,"/sdcard/",8)||strstr(path,"/../"))valid=false;
        if(valid)file=fopen(path,"rb");valid=valid&&file;
        if(valid){
            const char* ext=strrchr(path,'.');is_mp3=ext&&!strcasecmp(ext,".mp3");
            if(is_mp3){
                valid=!fseek(file,0,SEEK_END);long size=ftell(file);valid=valid&&size>0;
                uint8_t h[10];long skip=0;rewind(file);
                if(valid&&fread(h,1,10,file)==10&&!memcmp(h,"ID3",3)){
                    valid=!(h[6]&128)&&!(h[7]&128)&&!(h[8]&128)&&!(h[9]&128);
                    skip=10+((long)h[6]<<21)+((long)h[7]<<14)+((long)h[8]<<7)+h[9]+((h[5]&16)?10:0);
                }
                valid=valid&&size>skip&&fseek(file,skip,SEEK_SET)==0;
                unread=source_size=valid?(uint64_t)(size-skip):0;mp3dec_init(decoder);source_rate=0;source_channels=0;
                seek_sample=(uint64_t)command->offset*16;
                if(valid&&command->resume&&command->resume_mp3){
                    valid=command->resume_byte<source_size&&fseek(file,skip+(long)command->resume_byte,SEEK_SET)==0;
                    unread=source_size-command->resume_byte;pcm_samples=command->resume_pcm;
                    seek_sample=command->resume_seek>pcm_samples?command->resume_seek:0;
                }
            }else{
                thingsos_wav_t wave={0};valid=ext&&!strcasecmp(ext,".wav")&&thingsos_wav_read(file,&wave);
                if(valid){source_rate=wave.rate;source_channels=wave.channels;source_size=wave.bytes;
                    uint64_t frame=(uint64_t)command->offset*wave.rate/1000,total=wave.bytes/(wave.channels*2);
                    if(frame>=total)frame=total?total-1:0;
                    uint32_t byte=(uint32_t)frame*wave.channels*2;unread=wave.bytes-byte;
                    valid=fseek(file,(long)(wave.offset+byte),SEEK_SET)==0;
                    pcm_samples=frame*16000/wave.rate;resampler.rate=wave.rate;
                    portENTER_CRITICAL(&guard);status.duration_ms=(uint32_t)((uint64_t)wave.bytes*1000/(wave.rate*wave.channels*2));portEXIT_CRITICAL(&guard);
                }
            }
        }
    }
    io_give();
    if(!valid){publish(THINGSOS_AUDIO_ERROR,recording?"无法创建录音，请检查 SD 卡空间":"文件无法播放，支持 PCM16 WAV / MP3",EIO);finish();return false;}
    portENTER_CRITICAL(&guard);strlcpy(status.path,path,sizeof(status.path));status.position_ms=(uint32_t)(pcm_samples/16);if(recording||is_mp3)status.duration_ms=0;portEXIT_CRITICAL(&guard);
    if(!recording&&!refill(true)){publish(THINGSOS_AUDIO_ERROR,"读取音频失败",EIO);finish();return false;}
    pa(false);esp_err_t error=metalio_audio_open(recording);
    if(error!=ESP_OK){publish(THINGSOS_AUDIO_ERROR,"音频硬件启动失败",error);finish();return false;}hardware=true;
    {int expect=1;if(diag_s1&&atomic_compare_exchange_strong(&diag_state,&expect,2))thingsos_heap_diag_take(diag_s1);}
    if(!recording){pa(true);vTaskDelay(pdMS_TO_TICKS(10));}
    stats_disarm();
    publish(recording?THINGSOS_AUDIO_RECORDING:THINGSOS_AUDIO_PLAYING,"",0);
    ESP_LOGI(TAG,"Started %s path=%s buffer=%u stack_remaining=%u",recording?"record":"play",path,(unsigned)input_capacity,(unsigned)uxTaskGetStackHighWaterMark(NULL));return true;
}
static bool record_step(void){
    size_t bytes=0;uint64_t read_at=(uint64_t)esp_timer_get_time();esp_err_t error=metalio_audio_read(slots,FRAME_SAMPLES*8,&bytes);
    if((uint64_t)esp_timer_get_time()-read_at>=1000)ran_blocked();
    if(!bytes){if(++stalls>=8){publish(THINGSOS_AUDIO_ERROR,"麦克风无采样，请检查音频时钟",error?error:EIO);return false;}return true;}
    if(bytes>FRAME_SAMPLES*8||bytes%8){publish(THINGSOS_AUDIO_ERROR,"麦克风采样格式异常",EIO);return false;}
    stalls=0;size_t frames=bytes/8;unsigned peak=0;
    for(size_t i=0;i<frames;++i){int32_t v=slots[i*2]>>12;
        unsigned raw=(unsigned)(v<0?-v:v);if(raw>record_clip_peak)record_clip_peak=raw;
        if(v>32767||v< -32768)++record_clipped;
        if(v>32767)v=32767;if(v< -32768)v=-32768;decoded[i]=(int16_t)v;}
    record_samples+=frames;
    if(!paused){
        size_t processed=0;
        if(!thingsos_record_denoise_feed(decoded,frames,output,FRAME_SAMPLES,&processed)){publish(THINGSOS_AUDIO_ERROR,"录音降噪处理失败",EIO);return false;}
        if(!append_record(output,processed)){publish(THINGSOS_AUDIO_ERROR,"录音缓冲已满，已停止采样",ENOSPC);return false;}
        for(size_t i=0;i<processed;++i){int v=output[i];unsigned p=(unsigned)(v<0?-v:v);if(p>peak)peak=p;}
        pcm_samples+=frames;
    }
    if(!flush_record(false)){publish(THINGSOS_AUDIO_ERROR,"录音写入失败，请检查 SD 卡",EIO);return false;}
    portENTER_CRITICAL(&guard);status.peak=peak;status.position_ms=(uint32_t)(pcm_samples/16);portEXIT_CRITICAL(&guard);return true;
}
static bool play_step(void){
    uint64_t refill_at=(uint64_t)esp_timer_get_time();
    if(!refill(false)){publish(THINGSOS_AUDIO_ERROR,"音频读取失败",EIO);return false;}
    stats_note(&refill_max_us,refill_at);
    size_t available=input_count-input_used;
    if(!available){if(eof)return false;vTaskDelay(pdMS_TO_TICKS(5));return true;}
    size_t frames=0;unsigned channels=source_channels;
    if(is_mp3){
        if(available<4096&&!eof){uint64_t at=(uint64_t)esp_timer_get_time();if(!refill(true)){publish(THINGSOS_AUDIO_ERROR,"音频读取失败",EIO);return false;}stats_note(&refill_max_us,at);available=input_count-input_used;}
        mp3dec_frame_info_t info={0};uint64_t decode_at=(uint64_t)esp_timer_get_time();int got=mp3dec_decode_frame(decoder,input+input_used,(int)available,decoded,&info);
        stats_note(&decode_max_us,decode_at);decode_sum_us+=(uint64_t)esp_timer_get_time()-decode_at;++frames_decoded;
        if(info.frame_bytes>0)input_used+=(unsigned)info.frame_bytes;
        else {input_used+=available>4096?4096:available;return true;}
        if(got<=0)return true;
        if((info.channels!=1&&info.channels!=2)||info.hz<8000||info.hz>48000){publish(THINGSOS_AUDIO_ERROR,"MP3 采样格式不支持",EINVAL);return false;}
        if(source_rate&&source_rate!=(unsigned)info.hz){publish(THINGSOS_AUDIO_ERROR,"MP3 采样率中途变化",EINVAL);return false;}
        source_rate=(unsigned)info.hz;channels=(unsigned)info.channels;frames=(size_t)got;resampler.rate=source_rate;
        portENTER_CRITICAL(&guard);if(!status.duration_ms&&info.bitrate_kbps>0)status.duration_ms=(uint32_t)(source_size*8/(unsigned)info.bitrate_kbps);portEXIT_CRITICAL(&guard);
    }else{
        frames=available/(channels*2);if(frames>FRAME_SAMPLES)frames=FRAME_SAMPLES;
        if(!frames){if(eof)return false;vTaskDelay(pdMS_TO_TICKS(5));return true;}
        memcpy(decoded,input+input_used,frames*channels*2);input_used+=frames*channels*2;
    }
    size_t count=thingsos_audio_resample(&resampler,decoded,frames,channels,output,4610);
    if(!count){publish(THINGSOS_AUDIO_ERROR,"音频重采样失败",EINVAL);return false;}
    size_t skip=0;if(pcm_samples<seek_sample){uint64_t rest=seek_sample-pcm_samples;skip=rest<count?(size_t)rest:count;}
    pcm_samples+=count;
    for(size_t at=skip;at<count;){
        if(wireless_playback&&!thingsos_bluetooth_connected()){paused=true;pa(false);publish(THINGSOS_AUDIO_PLAY_PAUSED,"蓝牙连接断开，播放已暂停",0);return true;}
        size_t n=count-at;if(n>FRAME_SAMPLES)n=FRAME_SAMPLES;
        portENTER_CRITICAL(&guard);unsigned level=volume;portEXIT_CRITICAL(&guard);
        for(size_t i=0;i<n;++i){int32_t v=(int32_t)((int64_t)output[at+i]*65536*level*level/10000);slots[i*2]=slots[i*2+1]=v;}
        size_t sent=0,total=n*8;unsigned tries=0;
        while(sent<total){if(wireless_playback&&!thingsos_bluetooth_connected()){paused=true;pa(false);publish(THINGSOS_AUDIO_PLAY_PAUSED,"蓝牙连接断开，播放已暂停",0);return true;}size_t done=0;uint64_t write_at=(uint64_t)esp_timer_get_time();esp_err_t error=metalio_audio_write((const int32_t*)((const uint8_t*)slots+sent),total-sent,&done);sent+=done;
            /* >=1 ms inside the write means the DMA ring was full and the task slept. */
            uint64_t write_end=(uint64_t)esp_timer_get_time();
            if(write_end-write_at>=1000)ran_blocked();else ++underrun_writes;
            if(done)stats_write(write_at,write_end);
            if(error!=ESP_OK||!done){if(++tries>=3){
                /* The module is the I2S master. No write progress means it stopped
                   clocking, which is exactly what a headset powered off in its case
                   looks like; the module never reports that link loss itself. */
                if(wireless_playback&&thingsos_bluetooth_connected()){thingsos_bluetooth_link_lost();paused=true;pa(false);publish(THINGSOS_AUDIO_PLAY_PAUSED,"蓝牙连接断开，播放已暂停",0);return true;}
                publish(THINGSOS_AUDIO_ERROR,"扬声器输出超时，请检查音频时钟",error?error:EIO);return false;}}}
        at+=n;
    }
    portENTER_CRITICAL(&guard);status.position_ms=(uint32_t)(pcm_samples/16);portEXIT_CRITICAL(&guard);return true;
}
/* Worker stack placement. MALLOC_CAP_INTERNAL also matches the 32 KiB LP RAM
   (0x2E000000), which is far slower for the CPU. When WeRead has fragmented
   main SRAM, a 16 KiB internal stack lands there and every stack access in the
   float-heavy decoder (and NSNet recording) runs ~15x slower: measured
   36-42 ms per MP3 frame vs ~2 ms, heard as stutter. DMA-capable internal
   memory is main SRAM only, so try that first. Playback uses ~3.5 KiB of
   stack (16384 - 12.9 KiB free at start); 10 KiB keeps a wide margin and fits
   fragmented SRAM. Recording keeps 16 KiB. */
#define PLAY_STACK 10240u
#define RECORD_STACK 16384u
static void worker(void* unused);
static bool audio_task_create(bool record){
    const uint32_t size=record?RECORD_STACK:PLAY_STACK;
    const uint32_t main_caps=MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA|MALLOC_CAP_8BIT;
    size_t main_free=heap_caps_get_free_size(main_caps),main_largest=heap_caps_get_largest_free_block(main_caps);
    TaskHandle_t created=NULL;bool fast=true;
    if(xTaskCreatePinnedToCoreWithCaps(worker,"local_audio",size,NULL,4,&created,AUDIO_CORE,main_caps)!=pdPASS){
        fast=false;created=NULL;
        if(xTaskCreatePinnedToCoreWithCaps(worker,"local_audio",size,NULL,4,&created,AUDIO_CORE,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)!=pdPASS){
            ESP_LOGE(TAG,"Task allocation failed: stack=%u main_free=%u main_largest=%u internal_free=%u largest=%u",(unsigned)size,
                (unsigned)main_free,(unsigned)main_largest,
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
            return false;
        }
    }
    uintptr_t at=(uintptr_t)xTaskGetStackStart(created);
    const char* region=at>=SOC_LP_RAM_LOW&&at<SOC_LP_RAM_HIGH?"LP-RAM(slow)":at>=SOC_DIRAM_DRAM_LOW&&at<SOC_DIRAM_DRAM_HIGH?"main-SRAM":"other";
    task=created;task_stack=size;
    if(fast)ESP_LOGI(TAG,"Audio task stack=%u at %p %s main_free=%u main_largest=%u",(unsigned)size,(void*)at,region,(unsigned)main_free,(unsigned)main_largest);
    else ESP_LOGW(TAG,"Audio task stack=%u at %p %s main_free=%u main_largest=%u",(unsigned)size,(void*)at,region,(unsigned)main_free,(unsigned)main_largest);
    return true;
}
static void park(void){
    parked_t p={.valid=true,.mp3=is_mp3,.pcm=pcm_samples,.seek=seek_sample};
    if(is_mp3)p.byte=thingsos_audio_resume_byte(source_size,unread,input_count-input_used);
    portENTER_CRITICAL(&guard);p.position_ms=status.position_ms;portEXIT_CRITICAL(&guard);
    if(!finish()){publish(THINGSOS_AUDIO_ERROR,"音频文件关闭失败，请检查 SD 卡",EIO);return;}
    parked_set(&p);
    ESP_LOGI(TAG,"Playback parked: position=%u ms byte=%llu; file, buffers, I2S and network claim released",
        (unsigned)p.position_ms,(unsigned long long)p.byte);
    thingsos_memory_trace("audio-parked",0,(int)p.position_ms);
}
static void worker(void* unused){
    (void)unused;command_t command;
    for(;;){
        if(!file){
            xSemaphoreTake(lifecycle,portMAX_DELAY);
            if(!uxQueueMessagesWaiting(commands)){
                task=NULL;xSemaphoreGive(lifecycle);vTaskDeleteWithCaps(NULL);return;
            }
            xSemaphoreGive(lifecycle);
        }
        bool has=xQueueReceive(commands,&command,file?0:portMAX_DELAY)==pdTRUE;
        if(has){
            if(command.kind==CMD_RECORD&&task_stack<RECORD_STACK){
                /* A playback-sized worker hands a record request to a fresh
                   16 KiB worker: close playback, requeue, start successor, exit. */
                finish();
                xSemaphoreTake(lifecycle,portMAX_DELAY);
                xQueueSendToFront(commands,&command,0);
                if(!audio_task_create(true)){xQueueReset(commands);task=NULL;publish(THINGSOS_AUDIO_ERROR,"音频任务内存不足",ENOMEM);}
                xSemaphoreGive(lifecycle);vTaskDeleteWithCaps(NULL);return;
            }
            if(command.kind==CMD_PLAY||command.kind==CMD_RECORD){start(&command);continue;}
            if(!file&&(command.kind==CMD_RESUME||command.kind==CMD_SEEK)){
                parked_t p;
                if(parked_take(&p)){
                    thingsos_audio_status_t s;thingsos_audio_status(&s);
                    command_t resume={.kind=CMD_PLAY,.offset=command.kind==CMD_SEEK?command.offset:p.position_ms};
                    strlcpy(resume.path,s.path,sizeof(resume.path));
                    if(command.kind==CMD_RESUME){resume.resume=true;resume.resume_mp3=p.mp3;resume.resume_byte=p.byte;resume.resume_pcm=p.pcm;resume.resume_seek=p.seek;}
                    start(&resume);continue;
                }
            }
            if(command.kind==CMD_STOP){parked_set(NULL);bool ok=finish();publish(ok?THINGSOS_AUDIO_IDLE:THINGSOS_AUDIO_ERROR,ok?"":"音频保存失败，请检查 SD 卡",ok?0:EIO);continue;}
            if(command.kind==CMD_SUSPEND&&file&&!recording){paused=true;if(hardware){metalio_audio_close();hardware=false;}pa(false);atomic_store(&output_suspended,true);publish(THINGSOS_AUDIO_PLAY_PAUSED,"播放已暂停",0);continue;}
            if(command.kind==CMD_SEEK&&file&&!recording){thingsos_audio_status_t s;thingsos_audio_status(&s);command.kind=CMD_PLAY;strlcpy(command.path,s.path,sizeof(command.path));start(&command);continue;}
            if(file&&(command.kind==CMD_PAUSE||command.kind==CMD_RESUME)){
                if(recording&&command.kind==CMD_PAUSE&&!paused){
                    if(hardware){metalio_audio_close();hardware=false;}
                    if(!finish_record_segment()||!flush_record(true)){
                        publish(THINGSOS_AUDIO_ERROR,"暂停时保存降噪录音失败",EIO);finish();continue;
                    }
                }
                if(recording&&command.kind==CMD_RESUME&&paused){
                    if(!thingsos_record_denoise_open()){
                        publish(THINGSOS_AUDIO_ERROR,"录音降噪初始化失败，请释放内存后重试",ENOMEM);finish();continue;
                    }
                    esp_err_t error=metalio_audio_open(true);
                    if(error!=ESP_OK){publish(THINGSOS_AUDIO_ERROR,"麦克风恢复失败",error);finish();continue;}
                    hardware=true;
                }
                if(command.kind==CMD_RESUME&&!recording&&!hardware){
                    if(!thingsos_bluetooth_audio_ready(false)||metalio_audio_open(false)!=ESP_OK){publish(THINGSOS_AUDIO_PLAY_PAUSED,"音频输出尚未就绪，请稍后重试",0);continue;}
                    hardware=true;wireless_playback=thingsos_bluetooth_connected();atomic_store(&output_suspended,false);
                }
                paused=command.kind==CMD_PAUSE;if(!recording)pa(!paused);
                stats_disarm();
                publish(recording?(paused?THINGSOS_AUDIO_RECORD_PAUSED:THINGSOS_AUDIO_RECORDING):(paused?THINGSOS_AUDIO_PLAY_PAUSED:THINGSOS_AUDIO_PLAYING),"",0);
            }
        }
        if(file){
            if(!recording&&!paused&&wireless_playback&&!thingsos_bluetooth_connected()){paused=true;pa(false);publish(THINGSOS_AUDIO_PLAY_PAUSED,"蓝牙连接断开，播放已暂停",0);}
            /* Playback pause (user, Bluetooth loss or pre-sleep suspend) parks the
               session; a recording pause keeps its WAV file open. */
            if(paused&&!recording){park();continue;}
            if(paused){vTaskDelay(pdMS_TO_TICKS(10));ran_blocked();continue;}
            bool ok=recording?record_step():play_step();
            fair_yield();
            if(!ok){thingsos_audio_status_t s;thingsos_audio_status(&s);bool ended=s.state!=THINGSOS_AUDIO_ERROR&&!recording;
                bool closed=finish();if(s.state!=THINGSOS_AUDIO_ERROR)publish(closed?THINGSOS_AUDIO_IDLE:THINGSOS_AUDIO_ERROR,closed?"":"音频保存失败",closed?0:EIO);
                portENTER_CRITICAL(&guard);status.ended=ended&&closed;portEXIT_CRITICAL(&guard);}
        }
    }
}
static bool submit(command_kind_t kind,const char* path,uint32_t offset){
    /* Created on the first play and kept for the boot. In internal RAM these
       ~0.8 KiB split the largest main-SRAM block (63.5 -> 38.9 KiB measured
       2026-11-22). Neither is used from an ISR or a flash-write window. */
    if(!commands){commands=xQueueCreateWithCaps(1,sizeof(command_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!commands)return false;}
    if(!lifecycle){lifecycle=xSemaphoreCreateMutexWithCaps(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!lifecycle)return false;}
    command_t command={.kind=kind,.offset=offset};if(path){if(strlen(path)>=sizeof(command.path))return false;strlcpy(command.path,path,sizeof(command.path));}
    if(kind==CMD_PLAY||kind==CMD_RECORD){if(thingsos_network_busy()&&!thingsos_audio_active())return false;publish(THINGSOS_AUDIO_STARTING,"准备音频…",0);}
    if(kind==CMD_STOP)publish(THINGSOS_AUDIO_STOPPING,"正在结束音频…",0);
    xSemaphoreTake(lifecycle,portMAX_DELAY);
    bool ok=xQueueOverwrite(commands,&command)==pdPASS;
    /* Internal stack is intentional: controls persist NVS while audio runs.
       A PSRAM decoder stack is unsafe across flash-cache disable windows.
       MP3 scratch is separately allocated in PSRAM.
       The worker releases this stack immediately once its file is closed.
       Pinned to CPU1: CPU0 carries main (pinned), Wi-Fi, NimBLE and esp_timer.
       An unpinned priority-4 decoder settled on CPU0 and starved the UI after
       Weidu, while CPU1 idled. The epd_prep feed on CPU1 stays at the maximum
       priority, so the panel scan still preempts audio. The I2S/GDMA interrupt
       is allocated on CPU1 because the worker opens the channel. */
    if(ok&&!task&&!audio_task_create(kind==CMD_RECORD)){
        xQueueReset(commands);publish(THINGSOS_AUDIO_ERROR,"音频任务内存不足",ENOMEM);ok=false;
    }
    xSemaphoreGive(lifecycle);return ok;
}
bool thingsos_audio_play(const char* path,uint32_t offset){
    if(path&&atomic_load(&diag_state)==0) {
        diag_s0=heap_caps_calloc(1,sizeof(*diag_s0),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        diag_s1=heap_caps_calloc(1,sizeof(*diag_s1),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        diag_s2=heap_caps_calloc(1,sizeof(*diag_s2),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(diag_s0&&diag_s1&&diag_s2){thingsos_heap_diag_take(diag_s0);atomic_store(&diag_state,1);}
        else{free(diag_s0);free(diag_s1);free(diag_s2);diag_s0=diag_s1=diag_s2=NULL;atomic_store(&diag_state,4);}
    }
    return path&&thingsos_bluetooth_audio_ready(false)&&submit(CMD_PLAY,path,offset);
}
/* Main task, every ~2 s: report once the first session closed and its worker
   (and 10 KiB stack) had time to be deleted. */
void thingsos_audio_heap_diag_poll(void){
    if(atomic_load(&diag_state)!=3||esp_timer_get_time()-diag_closed_at<2000000)return;
    if(task)return; /* worker still alive: its stack would show as "new" */
    thingsos_heap_diag_take(diag_s2);
    thingsos_heap_diag_report("audio-first-session",diag_s0,diag_s1,diag_s2);
    free(diag_s0);free(diag_s1);free(diag_s2);diag_s0=diag_s1=diag_s2=NULL;
    atomic_store(&diag_state,4);
}
bool thingsos_audio_record(void){return thingsos_bluetooth_audio_ready(true)&&submit(CMD_RECORD,NULL,0);}
bool thingsos_audio_pause(bool paused){thingsos_audio_status_t s;thingsos_audio_status(&s);bool mic=s.state==THINGSOS_AUDIO_RECORDING||s.state==THINGSOS_AUDIO_RECORD_PAUSED;
    if(paused&&parked_valid())return true; /* already parked */
    return commands&&(paused||thingsos_bluetooth_audio_ready(mic))&&submit(paused?CMD_PAUSE:CMD_RESUME,NULL,0);}
bool thingsos_audio_stop(void){
    /* A parked session has no worker or open file: end it without a task. */
    if(lifecycle){
        xSemaphoreTake(lifecycle,portMAX_DELAY);
        bool idle=!task&&parked_valid();
        if(idle){parked_set(NULL);publish(THINGSOS_AUDIO_IDLE,"",0);}
        xSemaphoreGive(lifecycle);
        if(idle)return true;
    }
    if(!commands)return true;
    return submit(CMD_STOP,NULL,0);
}
bool thingsos_audio_seek(uint32_t offset){return commands&&submit(CMD_SEEK,NULL,offset);}
bool thingsos_audio_route_idle(void){return !thingsos_audio_active()||atomic_load(&output_suspended)||parked_valid();}
bool thingsos_audio_suspend_locked(void){
    if(thingsos_audio_route_idle())return true;
    thingsos_audio_status_t s;thingsos_audio_status(&s);
    if(s.state!=THINGSOS_AUDIO_PLAYING&&s.state!=THINGSOS_AUDIO_PLAY_PAUSED)return false;
    if(!submit(CMD_SUSPEND,NULL,0))return false;
    io_give();uint64_t until=esp_timer_get_time()/1000+3000;
    while(!thingsos_audio_route_idle()&&(uint64_t)esp_timer_get_time()/1000<until)vTaskDelay(pdMS_TO_TICKS(10));
    if(thingsos_io_lock)xSemaphoreTake(thingsos_io_lock,portMAX_DELAY);
    return thingsos_audio_route_idle();
}
bool thingsos_audio_quiesce_locked(void){
    if(!thingsos_audio_active())return true;
    if(!thingsos_audio_stop())return false;
    io_give();uint64_t until=esp_timer_get_time()/1000+6000;
    while(thingsos_audio_active()&&(uint64_t)esp_timer_get_time()/1000<until)vTaskDelay(pdMS_TO_TICKS(10));
    if(thingsos_io_lock)xSemaphoreTake(thingsos_io_lock,portMAX_DELAY);
    thingsos_audio_status_t s;thingsos_audio_status(&s);return s.state==THINGSOS_AUDIO_IDLE;
}
