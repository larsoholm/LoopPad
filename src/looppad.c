/* LoopPad v0.6.2-dev - Settings UI development fixed-length loops for Akai MPC Sample */
#define _GNU_SOURCE
#include <math.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tinyalsa/asoundlib.h>
#include "app.h"
#include "ui.h"
#include "input.h"
#include "widgets.h"
#include "menu.h"
#include "image.h"
#include "looppad_splash_data.h"
#include "version.h"

#define CH 2
#define SR 44100
#define PERIOD 128
#define PERIODS 3
#define XFADE (SR * 5 / 1000)
#define NCLIPS 8
#define WAVE_BINS 120
#define MAX_BARS 8
#define MIN_BPM 60
#define MAX_BPM 180
#define MAX_FRAMES ((size_t)MAX_BARS * 4 * 60 * SR / MIN_BPM + XFADE)

enum clip_state { EMPTY, ARMED, REC, PLAY, STOPPED, QUEUED_PLAY, QUEUED_STOP };
static const char *STATE_NAME[] = {"EMPTY","ARMED","REC","PLAY","STOP","START>","STOP>"};

struct clip {
    int32_t *audio;
    _Atomic int state;
    _Atomic size_t len, recpos;
    _Atomic uint64_t origin, arm_at, switch_at;
    _Atomic int volume; /* 0..127, controlled by the physical fader */
    _Atomic int bars;   /* per-pad recording length, 1..8 */
    unsigned char wave[WAVE_BINS]; /* mono peak overview, 0..127 */
    _Atomic int wave_ready;
};
static struct clip clips[NCLIPS];

/* Session v1C hand-off. 0=idle, 1=pending for audio thread, 2=committed. */
static _Atomic int g_load_commit_state=0;
static int32_t *g_load_new_audio[NCLIPS]={0};
static int32_t *g_load_old_audio[NCLIPS]={0};
static size_t g_load_frames[NCLIPS]={0};


/* command: 1..4 pad press, 100+pad clear */
enum quant_mode { Q_BAR, Q_BEAT, Q_OFF };
enum countin_mode { CI_NEXT, CI_1BAR, CI_NOW };
enum app_mode { MODE_SETUP, MODE_LIVE };
static _Atomic int g_mode=MODE_SETUP;
static _Atomic int g_quant=Q_BAR;
static _Atomic int g_countin=CI_NEXT;
static _Atomic int g_cmd=0, g_bpm=90, g_metro=1, g_transport=1;
static _Atomic int g_click_volume=90; /* SHIFT+K1, 0..127 */
static _Atomic int g_running=1, g_xruns=0, g_volume=32, g_input=0;
static _Atomic int g_active_clips=4; /* 4 or 8; PAD BANK toggles in SETUP */
static _Atomic int g_default_bars=1;  /* default for newly enabled EMPTY loop slots */
static _Atomic int g_peak_l=0, g_peak_r=0;
static _Atomic uint64_t g_clock=0;
static _Atomic uint64_t g_audio_frames=0; /* frames processed since audio thread start */
static struct pcm *g_cap,*g_pb;
static uint32_t *g_splash_px=NULL;
static int g_splash_w=0,g_splash_h=0;
static int click_left,click_hi;
static float click_phase;

static inline int32_t sat32(int64_t v){return v>INT32_MAX?INT32_MAX:v<INT32_MIN?INT32_MIN:(int32_t)v;}
static inline int32_t gain(int64_t x,int v){if(v<=0)return 0;if(v>127)v=127;int64_t q=((int64_t)v*v*8192)/(127*127);return sat32((x*q)>>15);}

static int setup_input_route(int rear){
    struct mixer *m=mixer_open(2); if(!m)return -1;
    struct mixer_ctl *mic=mixer_get_ctl_by_name(m,"Builtin Microphone Switch");
    struct mixer_ctl *trs=mixer_get_ctl_by_name(m,"TRS Switch");
    if(!mic||!trs){mixer_close(m);return -1;}
    int a=mixer_ctl_set_value(mic,0,rear?0:1), b=mixer_ctl_set_value(trs,0,rear?1:0);
    mixer_close(m); return (a||b)?-1:0;
}

static struct pcm *open_pcm(unsigned flags){
    struct pcm_config c={.channels=CH,.rate=SR,.format=PCM_FORMAT_S32_LE,.period_size=PERIOD,.period_count=PERIODS};
    struct pcm *p=pcm_open(2,0,flags,&c);
    if(!p||!pcm_is_ready(p)){fprintf(stderr,"LoopPad: hw:2,0 %s: %s\n",flags&PCM_IN?"capture":"playback",p?pcm_get_error(p):"open failed");if(p)pcm_close(p);return NULL;}
    return p;
}

static uint64_t next_quant_boundary(uint64_t t,uint64_t bar_len){
    int q=atomic_load(&g_quant);
    uint64_t unit=(q==Q_BEAT)?bar_len/4:bar_len;
    return ((t/unit)+1)*unit;
}

static int any_recording(void){int n=atomic_load(&g_active_clips);for(int i=0;i<n;i++){int s=atomic_load(&clips[i].state);if(s==ARMED||s==REC)return 1;}return 0;}

static void handle_command(int command,uint64_t t,uint64_t bar_len){
    if(command>=1&&command<=NCLIPS){
        int i=command-1, st=atomic_load(&clips[i].state);
        if(st==EMPTY){
            if(!any_recording()){
                atomic_store(&clips[i].wave_ready,0);memset(clips[i].wave,0,sizeof clips[i].wave);
                int ci=atomic_load(&g_countin);
                uint64_t at;
                if(ci==CI_NOW) at=t;
                else {uint64_t next=((t/bar_len)+1)*bar_len; at=(ci==CI_1BAR)?next+bar_len:next;}
                atomic_store(&clips[i].arm_at,at);
                atomic_store(&clips[i].state,ARMED);
            }
        } else if(st==ARMED){
            atomic_store(&clips[i].arm_at,0);
            atomic_store(&clips[i].state,EMPTY);
        } else if(st==PLAY){
            if(atomic_load(&g_quant)==Q_OFF) atomic_store(&clips[i].state,STOPPED);
            else {atomic_store(&clips[i].switch_at,next_quant_boundary(t,bar_len));atomic_store(&clips[i].state,QUEUED_STOP);}
        } else if(st==STOPPED){
            if(atomic_load(&g_quant)==Q_OFF){
                /* Phase lock: unmute at the position implied by permanent clip origin. */
                if(atomic_load(&clips[i].len)) atomic_store(&clips[i].state,PLAY);
            } else {atomic_store(&clips[i].switch_at,next_quant_boundary(t,bar_len));atomic_store(&clips[i].state,QUEUED_PLAY);}
        } else if(st==QUEUED_STOP){
            /* Latest request is PLAY at the already scheduled boundary. */
            atomic_store(&clips[i].state,QUEUED_PLAY);
        } else if(st==QUEUED_PLAY){
            /* Latest request is STOP at the already scheduled boundary. */
            atomic_store(&clips[i].state,QUEUED_STOP);
        }
    } else if(command>=101&&command<101+NCLIPS){
        int i=command-101; atomic_store(&clips[i].state,EMPTY);atomic_store(&clips[i].len,0);atomic_store(&clips[i].recpos,0);atomic_store(&clips[i].switch_at,0);atomic_store(&clips[i].wave_ready,0);memset(clips[i].wave,0,sizeof clips[i].wave);
    }
}

static void build_waveform(struct clip *c) {
    size_t len=atomic_load(&c->len);
    if(!c||!c->audio||!len)return;
    for(int x=0;x<WAVE_BINS;x++){
        size_t a=(len*(size_t)x)/WAVE_BINS, z=(len*(size_t)(x+1))/WAVE_BINS;
        if(z<=a)z=a+1;if(z>len)z=len;
        int64_t peak=0;
        for(size_t f=a;f<z;f++){
            int64_t l=c->audio[f*2],r=c->audio[f*2+1];if(l<0)l=-l;if(r<0)r=-r;
            int64_t v=l>r?l:r;if(v>peak)peak=v;
        }
        /* sqrt-ish compression: quiet material remains visible. */
        double norm=(double)peak/INT32_MAX;
        /* Display-only boost: make normal mic material visually useful. */
        norm*=8.0;
        if(norm>1.0)norm=1.0;
        int q=(int)(pow(norm,0.35)*127.0+0.5);if(q<0)q=0;if(q>127)q=127;
        c->wave[x]=(unsigned char)q;
    }
    atomic_store(&c->wave_ready,1);
}

static void process(const int32_t *in,int32_t *out,unsigned n){
    int bpm=atomic_load(&g_bpm),run=atomic_load(&g_transport);
    uint64_t bar_len=(uint64_t)(4.0*60.0*SR/bpm+0.5),beat_len=bar_len/4,t=atomic_load(&g_clock);
    int cmd=atomic_exchange(&g_cmd,0); if(cmd)handle_command(cmd,t,bar_len);
    int64_t pl=0,pr=0;
    for(unsigned j=0;j<n;j++){int64_t a=in[j*2],b=in[j*2+1];if(a<0)a=-a;if(b<0)b=-b;if(a>pl)pl=a;if(b>pr)pr=b;}
    atomic_store(&g_peak_l,(int)(pl*127/INT32_MAX));atomic_store(&g_peak_r,(int)(pr*127/INT32_MAX));

    for(unsigned f=0;f<n;f++){
        int64_t mix[2]={0,0}; const int32_t *src=in+f*2;
        if(run){
            for(int i=0;i<NCLIPS;i++){
                struct clip *c=&clips[i]; int st=atomic_load(&c->state);
                size_t len=atomic_load(&c->len),rp=atomic_load(&c->recpos);uint64_t origin=atomic_load(&c->origin);
                if(st==ARMED){uint64_t at=atomic_load(&c->arm_at);if(at&&t>=at){st=REC;origin=at;rp=0;len=(size_t)bar_len*atomic_load(&c->bars);atomic_store(&c->arm_at,0);}}
                if(st==QUEUED_STOP){
                    uint64_t at=atomic_load(&c->switch_at);
                    if(at&&t>=at){st=STOPPED;atomic_store(&c->switch_at,0);}
                } else if(st==QUEUED_PLAY){
                    uint64_t at=atomic_load(&c->switch_at);
                    if(at&&t>=at&&len){
                        /* Preserve original recording origin: stopped clips keep advancing silently. */
                        st=PLAY;
                        atomic_store(&c->switch_at,0);
                    }
                }
                if(st==REC){
                    if(rp<len)memcpy(c->audio+rp*2,src,sizeof(int32_t)*2);
                    else {size_t k=rp-len;if(k<XFADE){double h=(double)k/XFADE;for(int ch=0;ch<2;ch++){int32_t *x=&c->audio[k*2+ch];*x=sat32((int64_t)llrint(*x*h+src[ch]*(1.0-h)));}}}
                    rp++;if(rp>=len+XFADE){st=PLAY;atomic_store(&c->len,len);build_waveform(c);}
                }
                if((st==PLAY||st==QUEUED_STOP||st==REC)&&len&&t>=origin+len){
                    size_t p=(size_t)((t-origin)%len);
                    int cv=atomic_load(&c->volume);
                    mix[0]+=((int64_t)c->audio[p*2]*cv)/127;
                    mix[1]+=((int64_t)c->audio[p*2+1]*cv)/127;
                }
                atomic_store(&c->state,st);atomic_store(&c->origin,origin);atomic_store(&c->len,len);atomic_store(&c->recpos,rp);
            }
            if(atomic_load(&g_metro)&&t>0&&t%beat_len==0){click_left=SR/50;click_hi=t%bar_len==0;click_phase=0;}
            if(click_left>0){float e=(float)click_left/(SR/50);click_phase+=2*M_PI*(click_hi?1500.f:1000.f)/SR;int64_t z=(int64_t)(sinf(click_phase)*e*420000000.f);
                z=(z*atomic_load(&g_click_volume))/127;
                mix[0]+=z;mix[1]+=z;click_left--;}
            t++;
        }
        int v=atomic_load(&g_volume);
        int32_t ol=gain(mix[0],v), orr=gain(mix[1],v);
        uint64_t af=atomic_fetch_add(&g_audio_frames,1);
        const uint64_t mute_frames=(uint64_t)(SR*50/1000);
        const uint64_t fade_frames=(uint64_t)(SR*100/1000);
        if(af<mute_frames){ol=0;orr=0;}
        else if(af<mute_frames+fade_frames){
            uint64_t k=af-mute_frames;
            ol=(int32_t)(((int64_t)ol*k)/fade_frames);
            orr=(int32_t)(((int64_t)orr*k)/fade_frames);
        }
        out[f*2]=ol;out[f*2+1]=orr;
    }
    atomic_store(&g_clock,t);
}

static void session_commit_pending_load_audio_thread(void){
 if(atomic_load(&g_load_commit_state)!=1)return;
 uint64_t now=atomic_load(&g_clock);
 int bpm=atomic_load(&g_bpm);
 uint64_t bar_len=(uint64_t)(4.0*60.0*SR/bpm+0.5);
 uint64_t start=((now/bar_len)+1)*bar_len;
 for(int i=0;i<NCLIPS;i++){
  g_load_old_audio[i]=clips[i].audio;
  clips[i].audio=g_load_new_audio[i];
  g_load_new_audio[i]=NULL;
  atomic_store(&clips[i].state,g_load_frames[i]?QUEUED_PLAY:EMPTY);
  atomic_store(&clips[i].len,g_load_frames[i]);
  atomic_store(&clips[i].recpos,g_load_frames[i]);
  /* Loaded loops join the master grid together at the next bar boundary. */
  atomic_store(&clips[i].origin,g_load_frames[i]?start:0);
  atomic_store(&clips[i].arm_at,0);
  atomic_store(&clips[i].switch_at,g_load_frames[i]?start:0);
  atomic_store(&clips[i].wave_ready,0);
  memset(clips[i].wave,0,sizeof clips[i].wave);
 }
 atomic_store(&g_load_commit_state,2);
}

static void *audio_thread(void *x){(void)x;struct sched_param sp={.sched_priority=80};(void)pthread_setschedparam(pthread_self(),SCHED_FIFO,&sp);int32_t in[PERIOD*2],out[PERIOD*2]={0};
    for(int i=0;i<4;i++)(void)pcm_writei(g_pb,out,PERIOD);
    while(atomic_load(&g_running)){if(pcm_readi(g_cap,in,PERIOD)<0){atomic_fetch_add(&g_xruns,1);pcm_prepare(g_cap);continue;}session_commit_pending_load_audio_thread();process(in,out,PERIOD);if(pcm_writei(g_pb,out,PERIOD)<0){atomic_fetch_add(&g_xruns,1);pcm_prepare(g_pb);}}return NULL;}

static int arm_beats_left(const struct clip *c) {
    uint64_t at=atomic_load(&c->arm_at),t=atomic_load(&g_clock);
    int bpm=atomic_load(&g_bpm);
    uint64_t beat=(uint64_t)(60.0*SR/bpm+0.5);
    if(!at||at<=t||!beat)return 0;
    uint64_t left=at-t;
    return (int)((left+beat-1)/beat);
}
static int arm_flash_on(void) {
    int bpm=atomic_load(&g_bpm);uint64_t beat=(uint64_t)(60.0*SR/bpm+0.5);
    uint64_t t=atomic_load(&g_clock);if(!beat)return 1;
    return (t%(beat/2?beat/2:1)) < (beat/4?beat/4:1);
}

static int meter_mask(int v){static const int th[5]={1,4,13,32,64};int n=0;for(int i=0;i<5;i++)if(v>=th[i])n=i+1;return n?(1<<n)-1:0;}
static uint32_t state_color(int s){return s==ARMED?0xf59e0b:s==REC?0xef4444:(s==PLAY||s==QUEUED_STOP)?0x22c55e:(s==STOPPED||s==QUEUED_PLAY)?0x3b82f6:0x475569;}

static void draw_clip_wave(struct ui *u,struct clip *c,int x,int y,int w,int h,uint32_t col,int st) {
    if(!atomic_load(&c->wave_ready)) return;
    int mid=y+h/2;
    for(int px=0;px<w;px++){
        int bin=(px*WAVE_BINS)/w;if(bin>=WAVE_BINS)bin=WAVE_BINS-1;
        int amp=(c->wave[bin]*(h/2-1))/127;if(amp<1&&c->wave[bin])amp=1;
        ui_box(u,x+px,mid-amp,1,amp*2+1,(struct ui_paint){.fill_set=1,.fill=col});
    }
    size_t len=atomic_load(&c->len);uint64_t t=atomic_load(&g_clock),o=atomic_load(&c->origin);
    if(len && t>=o){size_t pos=(size_t)((t-o)%len);int ph=x+(int)((pos*(uint64_t)w)/len);if(ph>=x+w)ph=x+w-1;
        uint32_t pc=(st==STOPPED||st==QUEUED_PLAY)?0x93c5fd:0xffffff;
        ui_box(u,ph,y,1,h,(struct ui_paint){.fill_set=1,.fill=pc});
    }
}


/* v0.6.2 Settings UI. Audio/transport processing is intentionally untouched. */
static int g_settings_open=0;
static int g_settings_row=0;
static int g_settings_edit=0;
static int g_about_open=0;
static int g_session_open=0;
static int g_session_row=0;
static char g_session_msg[64]="";
static int g_session_msg_error=0;
static char g_settings_status[48]="";
static int g_settings_status_ticks=0;

static int settings_hidden_clips_empty(void){
    for(int i=4;i<8;i++){
        if(atomic_load(&clips[i].state)!=EMPTY || atomic_load(&clips[i].len)!=0) return 0;
    }
    return 1;
}

static void settings_change(int dir){
    if(!dir) return;
    g_settings_status[0]='\0';
    switch(g_settings_row){
        case 0: {
            int n=atomic_load(&g_active_clips);
            if(dir>0){ int def=atomic_load(&g_default_bars); for(int q=4;q<8;q++) if(atomic_load(&clips[q].state)==EMPTY && atomic_load(&clips[q].len)==0) atomic_store(&clips[q].bars,def); atomic_store(&g_active_clips,8); }
            else if(n==8){
                if(settings_hidden_clips_empty()) atomic_store(&g_active_clips,4);
                else {snprintf(g_settings_status,sizeof g_settings_status,"CLEAR P5-P8 FIRST");g_settings_status_ticks=60;}
            }
        } break;
        case 1: {
            int bpm=atomic_load(&g_bpm)+dir;
            if(bpm<40)bpm=40;if(bpm>240)bpm=240;
            atomic_store(&g_bpm,bpm);
        } break;
        case 2: {
            int q=atomic_load(&g_quant)+dir;
            if(q<0)q=Q_OFF;if(q>Q_OFF)q=Q_BAR;
            atomic_store(&g_quant,q);
        } break;
        case 3: {
            int ci=atomic_load(&g_countin)+dir;
            if(ci<0)ci=CI_NOW;if(ci>CI_NOW)ci=CI_NEXT;
            atomic_store(&g_countin,ci);
        } break;
        case 4: {
            int rear=dir>0?1:0;
            if(rear!=atomic_load(&g_input) && setup_input_route(rear)==0) atomic_store(&g_input,rear);
        } break;
        case 5: {
            int v=atomic_load(&g_click_volume)+dir*6;
            if(v<0)v=0;if(v>127)v=127;
            atomic_store(&g_click_volume,v);
        } break;
        case 6: {
            int v=atomic_load(&g_default_bars)+dir;
            if(v<1)v=1;if(v>8)v=8;
            atomic_store(&g_default_bars,v);
        } break;
        default: break;
    }
}


#define SESSION_FORMAT_VERSION 1
#define SESSION_DIR_NAME "MPC-Mods"
#define SESSION_SUBDIR "LoopPad/Sessions"
#define SESSION_NAME "Default"

struct session_meta {
    int format_version,bpm,layout,quantize,countin,input,click_level,default_bars;
    int bars[NCLIPS],volume[NCLIPS];
};

static int session_find_storage(char *out,size_t n){
    FILE *f=fopen("/proc/mounts","r");
    if(!f)return -1;
    char dev[256],mnt[256],type[64],opts[256];
    int ok=-1;
    while(fscanf(f,"%255s %255s %63s %255s %*d %*d",dev,mnt,type,opts)==4){
        if((strncmp(mnt,"/media/",7)==0 || strncmp(mnt,"/mnt/",5)==0) && strstr(opts,"rw")){
            snprintf(out,n,"%s",mnt);ok=0;break;
        }
    }
    fclose(f);return ok;
}

static int session_mkdir(const char *p){
    if(mkdir(p,0775)==0 || errno==EEXIST)return 0;
    return -1;
}

static int session_paths(char *dir,size_t dn,char *cfg,size_t cn,char *tmp,size_t tn){
 char mount[256],mods[512],app[512],sessions[512];
 if(session_find_storage(mount,sizeof mount)<0)return -1;

 snprintf(mods,sizeof mods,"%s/MPC-Mods",mount);
 snprintf(app,sizeof app,"%s/LoopPad",mods);
 snprintf(sessions,sizeof sessions,"%s/Sessions",app);
 snprintf(dir,dn,"%s/%s",sessions,SESSION_NAME);

 if(session_mkdir(mods)<0 ||
    session_mkdir(app)<0 ||
    session_mkdir(sessions)<0 ||
    session_mkdir(dir)<0) return -1;

 snprintf(cfg,cn,"%s/session.cfg",dir);
 snprintf(tmp,tn,"%s/session.cfg.tmp",dir);
 return 0;
}

static int session_write_atomic(const char *path,const void *data,size_t bytes){
 char tmp[704];
 snprintf(tmp,sizeof tmp,"%s.tmp",path);
 FILE *f=fopen(tmp,"wb");
 if(!f)return -1;
 if(bytes && fwrite(data,1,bytes,f)!=bytes){fclose(f);remove(tmp);return -1;}
 if(fflush(f)!=0){fclose(f);remove(tmp);return -1;}
 if(fsync(fileno(f))!=0){fclose(f);remove(tmp);return -1;}
 if(fclose(f)!=0){remove(tmp);return -1;}
 if(rename(tmp,path)!=0){remove(tmp);return -1;}
 return 0;
}

static int session_save_audio(char *msg,size_t mn){
 char dir[512],cfg[576],tmpcfg[576];
 if(session_paths(dir,sizeof dir,cfg,sizeof cfg,tmpcfg,sizeof tmpcfg)<0){snprintf(msg,mn,"NO WRITABLE SD/USB");return -1;}
 for(int i=0;i<NCLIPS;i++){
  size_t frames=(size_t)atomic_load(&clips[i].len);
  if(!frames)continue;
  if(!clips[i].audio){snprintf(msg,mn,"PAD %d NO BUFFER",i+1);return -1;}
  if(frames > SIZE_MAX/(2*sizeof(int32_t))){snprintf(msg,mn,"PAD %d TOO LARGE",i+1);return -1;}
  char path[640];
  snprintf(path,sizeof path,"%s/pad%d.raw",dir,i+1);
  size_t bytes=frames*2*sizeof(int32_t);
  if(session_write_atomic(path,clips[i].audio,bytes)<0){snprintf(msg,mn,"PAD %d SAVE FAILED",i+1);return -1;}
 }
 snprintf(msg,mn,"AUDIO SAVED");
 return 0;
}

static int session_save_metadata(char *msg,size_t mn){
    char dir[512],cfg[576],tmp[576];
    if(session_paths(dir,sizeof dir,cfg,sizeof cfg,tmp,sizeof tmp)<0){snprintf(msg,mn,"NO WRITABLE SD/USB");return -1;}
    FILE *f=fopen(tmp,"w");
    if(!f){snprintf(msg,mn,"SAVE OPEN FAILED");return -1;}
    fprintf(f,"format_version=%d\ncreated_with=%s\n",SESSION_FORMAT_VERSION,LOOPPAD_VERSION);
    fprintf(f,"bpm=%d\nlayout=%d\nquantize=%d\ncountin=%d\ninput=%d\nclick_level=%d\ndefault_bars=%d\n",
        atomic_load(&g_bpm),atomic_load(&g_active_clips),atomic_load(&g_quant),atomic_load(&g_countin),
        atomic_load(&g_input),atomic_load(&g_click_volume),atomic_load(&g_default_bars));
    for(int i=0;i<NCLIPS;i++)fprintf(f,"pad%d.bars=%d\npad%d.volume=%d\npad%d.frames=%zu\n",i+1,atomic_load(&clips[i].bars),i+1,atomic_load(&clips[i].volume),i+1,(size_t)atomic_load(&clips[i].len));
    if(fflush(f)!=0 || fsync(fileno(f))!=0 || fclose(f)!=0){remove(tmp);snprintf(msg,mn,"SAVE WRITE FAILED");return -1;}
    if(rename(tmp,cfg)!=0){remove(tmp);snprintf(msg,mn,"SAVE RENAME FAILED");return -1;}
    snprintf(msg,mn,"METADATA SAVED");return 0;
}

static int session_parse_int(const char *line,const char *key,int *v){
    size_t k=strlen(key);if(strncmp(line,key,k)||line[k]!='=')return 0;
    char *end=0;long x=strtol(line+k+1,&end,10);if(end==line+k+1)return -1;*v=(int)x;return 1;
}

static int session_read_frame_counts(const char *cfg,size_t frames[NCLIPS],char *msg,size_t mn){
 for(int i=0;i<NCLIPS;i++)frames[i]=0;
 FILE *f=fopen(cfg,"r");
 if(!f){snprintf(msg,mn,"NO SESSION METADATA");return -1;}
 char line[192];int fmt=-1,seen_fmt=0;
 while(fgets(line,sizeof line,f)){
  int v=0;
  if(session_parse_int(line,"format_version",&v)==1){fmt=v;seen_fmt=1;continue;}
  for(int i=0;i<NCLIPS;i++){
   char key[32];snprintf(key,sizeof key,"pad%d.frames",i+1);
   int r=session_parse_int(line,key,&v);
   if(r==1){if(v<0){fclose(f);snprintf(msg,mn,"BAD PAD %d FRAMES",i+1);return -1;}frames[i]=(size_t)v;break;}
   if(r<0){fclose(f);snprintf(msg,mn,"BAD METADATA");return -1;}
  }
 }
 fclose(f);
 if(!seen_fmt || fmt!=SESSION_FORMAT_VERSION){snprintf(msg,mn,"BAD SESSION FORMAT");return -1;}
 return 0;
}

static int session_load_audio(char *msg,size_t mn){
 char dir[512],cfg[576],tmpcfg[576];
 if(session_paths(dir,sizeof dir,cfg,sizeof cfg,tmpcfg,sizeof tmpcfg)<0){snprintf(msg,mn,"NO WRITABLE SD/USB");return -1;}
 size_t frames[NCLIPS]={0};
 if(session_read_frame_counts(cfg,frames,msg,mn)<0)return -1;
 int32_t *loaded[NCLIPS]={0};
 int rc=-1;
 for(int i=0;i<NCLIPS;i++){
  if(!frames[i])continue;
  if(frames[i]>MAX_FRAMES){snprintf(msg,mn,"PAD %d TOO LONG",i+1);goto fail;}
  if(frames[i]>SIZE_MAX/(2*sizeof(int32_t))){snprintf(msg,mn,"PAD %d TOO LARGE",i+1);goto fail;}
  size_t bytes=frames[i]*2*sizeof(int32_t);
  char path[640];snprintf(path,sizeof path,"%s/pad%d.raw",dir,i+1);
  struct stat st;
  if(stat(path,&st)!=0 || st.st_size<0 || (uint64_t)st.st_size!=(uint64_t)bytes){snprintf(msg,mn,"PAD %d SIZE ERROR",i+1);goto fail;}
  loaded[i]=calloc(MAX_FRAMES*2,sizeof(int32_t));
  if(!loaded[i]){snprintf(msg,mn,"PAD %d NO MEMORY",i+1);goto fail;}
  FILE *f=fopen(path,"rb");
  if(!f){snprintf(msg,mn,"PAD %d OPEN FAILED",i+1);goto fail;}
  size_t got=fread(loaded[i],1,bytes,f);int extra=fgetc(f);int ferr=ferror(f);fclose(f);
  if(got!=bytes || extra!=EOF || ferr){snprintf(msg,mn,"PAD %d READ FAILED",i+1);goto fail;}
 }
 /* All files are valid and in full-capacity temporary buffers. Publish them,
    then let the audio thread swap pointers between PCM blocks. */
 if(atomic_load(&g_load_commit_state)!=0){snprintf(msg,mn,"LOAD BUSY");goto fail;}
 for(int i=0;i<NCLIPS;i++){
  if(!loaded[i]){
   loaded[i]=calloc(MAX_FRAMES*2,sizeof(int32_t));
   if(!loaded[i]){snprintf(msg,mn,"PAD %d NO MEMORY",i+1);goto fail;}
  }
  g_load_new_audio[i]=loaded[i];
  loaded[i]=NULL;
  g_load_frames[i]=frames[i];
  g_load_old_audio[i]=NULL;
 }
 atomic_store(&g_load_commit_state,1);

 /* A PCM period is 128 frames (~2.9 ms at 44.1 kHz). Wait only for the
    block-boundary pointer swap, never for file I/O in the audio thread. */
 int waits=0;
 while(atomic_load(&g_load_commit_state)==1 && waits++<500)usleep(1000);
 if(atomic_load(&g_load_commit_state)!=2){
  /* Do not free published pointers: ownership may still be with audio thread. */
  snprintf(msg,mn,"LOAD COMMIT TIMEOUT");
  return -1;
 }

 /* Swap completed: old clip buffers are no longer referenced by process(). */
 for(int i=0;i<NCLIPS;i++){
  free(g_load_old_audio[i]);g_load_old_audio[i]=NULL;
  g_load_frames[i]=0;
 }
 atomic_store(&g_load_commit_state,0);

 /* Rebuild display waveforms outside the realtime thread. Audio is read-only now. */
 for(int i=0;i<NCLIPS;i++)if(atomic_load(&clips[i].len))build_waveform(&clips[i]);
 snprintf(msg,mn,"AUDIO LOADED");rc=0;
fail:
 for(int i=0;i<NCLIPS;i++)free(loaded[i]);
 return rc;
}

static int session_load_metadata(char *msg,size_t mn){
    char dir[512],cfg[576],tmp[576];
    if(session_paths(dir,sizeof dir,cfg,sizeof cfg,tmp,sizeof tmp)<0){snprintf(msg,mn,"NO WRITABLE SD/USB");return -1;}
    FILE *f=fopen(cfg,"r");if(!f){snprintf(msg,mn,"NO SAVED METADATA");return -1;}
    struct session_meta m={0};int seen=0;char line[256];
    while(fgets(line,sizeof line,f)){
        int r=0;
        if((r=session_parse_int(line,"format_version",&m.format_version)))seen|=1<<0;
        else if((r=session_parse_int(line,"bpm",&m.bpm)))seen|=1<<1;
        else if((r=session_parse_int(line,"layout",&m.layout)))seen|=1<<2;
        else if((r=session_parse_int(line,"quantize",&m.quantize)))seen|=1<<3;
        else if((r=session_parse_int(line,"countin",&m.countin)))seen|=1<<4;
        else if((r=session_parse_int(line,"input",&m.input)))seen|=1<<5;
        else if((r=session_parse_int(line,"click_level",&m.click_level)))seen|=1<<6;
        else if((r=session_parse_int(line,"default_bars",&m.default_bars)))seen|=1<<7;
        else {
            for(int i=0;i<NCLIPS;i++){
                char k[32];snprintf(k,sizeof k,"pad%d.bars",i+1);if((r=session_parse_int(line,k,&m.bars[i]))){seen|=1<<(8+i);break;}
                snprintf(k,sizeof k,"pad%d.volume",i+1);if((r=session_parse_int(line,k,&m.volume[i]))){seen|=1<<(16+i);break;}
            }
        }
        if(r<0){fclose(f);snprintf(msg,mn,"INVALID METADATA");return -1;}
    }
    fclose(f);
    if((seen&0x00ffffff)!=0x00ffffff || m.format_version!=SESSION_FORMAT_VERSION || m.bpm<40 || m.bpm>240 ||
       (m.layout!=4&&m.layout!=8) || m.quantize<Q_BAR || m.quantize>Q_OFF || m.countin<CI_NEXT || m.countin>CI_NOW ||
       m.input<0 || m.input>1 || m.click_level<0 || m.click_level>127 || m.default_bars<1 || m.default_bars>8){
        snprintf(msg,mn,"METADATA VALIDATION FAILED");return -1;
    }
    for(int i=0;i<NCLIPS;i++)if(m.bars[i]<1||m.bars[i]>8||m.volume[i]<0||m.volume[i]>127){snprintf(msg,mn,"PAD METADATA INVALID");return -1;}
    if(m.layout==4 && !settings_hidden_clips_empty()){snprintf(msg,mn,"CLEAR P5-P8 BEFORE LOAD");return -1;}
    if(m.input!=atomic_load(&g_input) && setup_input_route(m.input)<0){snprintf(msg,mn,"INPUT ROUTE FAILED");return -1;}
    atomic_store(&g_bpm,m.bpm);atomic_store(&g_active_clips,m.layout);atomic_store(&g_quant,m.quantize);
    atomic_store(&g_countin,m.countin);atomic_store(&g_input,m.input);atomic_store(&g_click_volume,m.click_level);
    atomic_store(&g_default_bars,m.default_bars);
    for(int i=0;i<NCLIPS;i++){atomic_store(&clips[i].bars,m.bars[i]);atomic_store(&clips[i].volume,m.volume[i]);}
    snprintf(msg,mn,"METADATA LOADED");return 0;
}

static void draw_session_page(struct ui *u){
    static const char *row[]={"SAVE METADATA","LOAD METADATA","SAVE AUDIO","LOAD AUDIO","FOLDER"};
    ui_clear(u,0x0b1018);ui_box(u,0,0,UI_W,30,(struct ui_paint){.fill_set=1,.fill=0x123047});
    ui_text(u,115,8,"SESSION v1C",0xffffff,1);
    for(int i=0;i<5;i++){int y=42+i*32;ui_box(u,30,y,260,26,(struct ui_paint){.fill_set=1,.fill=i==g_session_row?0x183b56:0x111827,.border_set=1,.border=i==g_session_row?0x22d3ee:0x263244,.border_px=i==g_session_row?2:1});ui_text(u,42,y+7,row[i],i==g_session_row?0x67e8f9:0xcbd5e1,1);}
    ui_text(u,42,192,"MPC-Mods/LoopPad/Sessions/Default",0x94a3b8,1);
    if(g_session_msg[0])ui_text(u,42,211,g_session_msg,g_session_msg_error?0xfca5a5:0x86efac,1);
    ui_text(u,15,226,"B1 BACK   ENCODER SELECT / PRESS RUN",0x94a3b8,1);
}

static void draw_about_page(struct ui *u){
    ui_clear(u,0x0b1018);
    ui_box(u,0,0,UI_W,30,(struct ui_paint){.fill_set=1,.fill=0x123047});
    ui_text(u,124,8,"ABOUT",0xffffff,1);
    ui_text(u,115,48,"LOOPPAD",0x67e8f9,2);
    ui_text(u,121,76,LOOPPAD_VERSION,0xf8fafc,1);
    ui_text(u,76,104,"MPC SAMPLE LIVE LOOPER",0xcbd5e1,1);
    ui_text(u,62,132,"ENGINE     8 CLIPS",0x94a3b8,1);
    ui_text(u,62,147,"AUDIO      44.1K / S32",0x94a3b8,1);
    ui_text(u,62,162,"TOOLKIT    0c162ff",0x94a3b8,1);
    ui_text(u,62,177,"LOOPPAD    MIT",0x94a3b8,1);
    ui_text(u,62,192,"UI TOOLKIT CC0-1.0",0x94a3b8,1);
    ui_text(u,15,226,"B1 BACK",0x94a3b8,1);
}

static void draw_settings_page(struct ui *u,const struct input *in){
    static const char *names[]={"LOOP LAYOUT","BPM","QUANTIZE","COUNT-IN","INPUT","CLICK LEVEL","DEFAULT BARS","SESSION","ABOUT"};
    char value[48];
    ui_clear(u,0x0b1018);
    ui_box(u,0,0,UI_W,30,(struct ui_paint){.fill_set=1,.fill=0x123047});
    ui_text(u,84,8,"LOOPPAD SETTINGS",0xffffff,1);
    for(int i=0;i<9;i++){
        int y=32+i*19;
        uint32_t fill=i==g_settings_row?0x183b56:0x111827;
        uint32_t border=i==g_settings_row?0x22d3ee:0x263244;
        ui_box(u,16,y,288,20,(struct ui_paint){.fill_set=1,.fill=fill,.border_set=1,.border=border,.border_px=i==g_settings_row?2:1});
        ui_text(u,24,y+4,names[i],i==g_settings_row?0x67e8f9:0xcbd5e1,1);
        value[0]='\0';
        if(i==0)snprintf(value,sizeof value,"%d LOOPS",atomic_load(&g_active_clips));
        else if(i==1)snprintf(value,sizeof value,"%d",atomic_load(&g_bpm));
        else if(i==2){int q=atomic_load(&g_quant);snprintf(value,sizeof value,"%s",q==Q_BAR?"BAR":q==Q_BEAT?"BEAT":"OFF");}
        else if(i==3){int ci=atomic_load(&g_countin);snprintf(value,sizeof value,"%s",ci==CI_NEXT?"NEXT":ci==CI_1BAR?"1BAR":"NOW");}
        else if(i==4)snprintf(value,sizeof value,"%s",atomic_load(&g_input)?"REAR":"MIC");
        else if(i==5)snprintf(value,sizeof value,"%d%%",(atomic_load(&g_click_volume)*100+63)/127);
        else if(i==6)snprintf(value,sizeof value,"%dB",atomic_load(&g_default_bars));
        else snprintf(value,sizeof value,">");
        int x=296-(int)strlen(value)*6;
        if(i==g_settings_row && g_settings_edit){
            ui_box(u,x-5,y+2,(int)strlen(value)*6+10,16,(struct ui_paint){.fill_set=1,.fill=0x164e63,.border_set=1,.border=0x67e8f9,.border_px=1});
        }
        ui_text(u,x,y+4,value,0xf8fafc,1);
    }
    if(g_settings_status_ticks>0){
        ui_box(u,56,202,208,18,(struct ui_paint){.fill_set=1,.fill=0x450a0a,.border_set=1,.border=0xef4444,.border_px=1});
        ui_text(u,74,205,g_settings_status,0xfca5a5,1);
    }
    ui_text(u,15,226,g_settings_edit?"B1 CANCEL   ENCODER CHANGE / PRESS DONE":"B1 BACK   ENCODER SELECT / PRESS EDIT",0x94a3b8,1);
    (void)in;
}

static int frame(struct ui *u,const struct input *in,void *x){
    (void)x;
    static int splash_frames=0;
    ui_wake(u,33);
    if(g_splash_px && splash_frames<75){
        splash_frames++;
        memcpy(u->px,g_splash_px,(size_t)UI_W*UI_H*sizeof(uint32_t));

        /* Version is drawn dynamically; it is not baked into the PNG. */
        ui_text(u, 122, 218, LOOPPAD_VERSION, 0xb8c2cc, 1);
        return 0;
    }
    /* Main Volume is global and must work on every interactive screen. */
    if(in){
        atomic_store(&g_volume,in->main_moved?in->main:in->main_pos);
    }

    if(in && in->shift && input_down(in,IN_SAMPLE_SELECT)){
        g_settings_open=!g_settings_open;
        g_settings_status[0]='\0';g_settings_status_ticks=0;
    }
    if(g_settings_open){
        if(g_session_open){
            if(in){
                if(input_down(in,IN_B1)){g_session_open=0;return 0;}
                if(in->encoder){g_session_row+=in->encoder;if(g_session_row<0)g_session_row=0;if(g_session_row>4)g_session_row=4;}
                if(input_down(in,IN_ENCODER)){
                    if(g_session_row==0)g_session_msg_error=session_save_metadata(g_session_msg,sizeof g_session_msg)<0;
                    else if(g_session_row==1)g_session_msg_error=session_load_metadata(g_session_msg,sizeof g_session_msg)<0;
                    else if(g_session_row==2){g_session_msg_error=session_save_audio(g_session_msg,sizeof g_session_msg)<0;}
                    else if(g_session_row==3){g_session_msg_error=session_load_audio(g_session_msg,sizeof g_session_msg)<0;}
                    else {snprintf(g_session_msg,sizeof g_session_msg,"AUTO-DETECTED SD/USB");g_session_msg_error=0;}
                }
            }
            draw_session_page(u);return 0;
        }
        if(g_about_open){
            if(in && input_down(in,IN_B1))g_about_open=0;
            draw_about_page(u);
            return 0;
        }
        if(in){
            if(input_down(in,IN_B1)){
                if(g_settings_edit) g_settings_edit=0;
                else {g_settings_open=0;return 0;}
            }
            if(input_down(in,IN_ENCODER)){
                if(g_settings_row==7){g_session_open=1;g_settings_edit=0;g_session_msg[0]='\0';}
                else if(g_settings_row==8){g_about_open=1;g_settings_edit=0;}
                else g_settings_edit=!g_settings_edit;
            }
            if(in->encoder){
                if(g_settings_edit){
                    int step=in->encoder>0?1:-1;
                    int n=in->encoder>0?in->encoder:-in->encoder;
                    while(n--) settings_change(step);
                } else {
                    g_settings_row+=in->encoder;
                    if(g_settings_row<0)g_settings_row=0;if(g_settings_row>8)g_settings_row=8;
                }
            }
            /* Keep +/- as an optional shortcut. */
            if(input_down(in,IN_PLUS))settings_change(1);
            if(input_down(in,IN_MINUS))settings_change(-1);
        }
        if(g_settings_status_ticks>0)g_settings_status_ticks--;
        draw_settings_page(u,in);
        return 0;
    }
    example_menu_prepare();(void)ui_menu_open(&example_menu,in);
    static int selected=0;
    static int fader_latched=0;
    static int fader_last=-1;
    static int fader_seen=0;
    static int clear_pad=-1;
    static int clear_ticks=0;
    static int layout_ticks=0;
    static char layout_msg[64]={0};
    if(clear_ticks>0 && --clear_ticks==0) clear_pad=-1;
    if(layout_ticks>0) --layout_ticks;
    if(in){

        if(in->knob_moved[0]){
            if(in->shift) atomic_store(&g_click_volume,in->knob[0]);
            else if(atomic_load(&g_mode)==MODE_SETUP && !any_recording() && atomic_load(&clips[selected].state)==EMPTY){
                int bars=1+(in->knob[0]*8)/128; if(bars<1)bars=1; if(bars>8)bars=8;
                atomic_store(&clips[selected].bars,bars);
            }
        }
        if(in->knob_moved[1]){
            if(in->shift){int v=in->knob[1];atomic_store(&g_quant,v<43?Q_BAR:v<86?Q_BEAT:Q_OFF);}
            else if(!any_recording())atomic_store(&g_bpm,MIN_BPM+in->knob[1]*(MAX_BPM-MIN_BPM)/127);
        }
        if(in->knob_moved[2]){
            if(in->shift){int v=in->knob[2];atomic_store(&g_countin,v<43?CI_NEXT:v<86?CI_1BAR:CI_NOW);}
            else {int rear=in->knob[2]>=64;if(rear!=atomic_load(&g_input)&&setup_input_route(rear)==0)atomic_store(&g_input,rear);}
        }
        if(input_down(in,IN_SAMPLE_SELECT)){
            int m=atomic_load(&g_mode);
            atomic_store(&g_mode,m==MODE_SETUP?MODE_LIVE:MODE_SETUP);
        }
        if(input_down(in,IN_PAD_BANK)){
            if(atomic_load(&g_mode)!=MODE_SETUP){
                snprintf(layout_msg,sizeof layout_msg,"SETUP REQUIRED"); layout_ticks=45;
            } else if(any_recording()){
                snprintf(layout_msg,sizeof layout_msg,"PLEASE WAIT - RECORDING"); layout_ticks=45;
            } else if(atomic_load(&g_active_clips)==4){
                int def=atomic_load(&g_default_bars); for(int q=4;q<8;q++) if(atomic_load(&clips[q].state)==EMPTY && atomic_load(&clips[q].len)==0) atomic_store(&clips[q].bars,def); atomic_store(&g_active_clips,8); snprintf(layout_msg,sizeof layout_msg,"8 LOOP MODE - PADS 1-8"); layout_ticks=45;
            } else {
                char used[32]={0}; int first=1;
                for(int q=4;q<8;q++) if(atomic_load(&clips[q].state)!=EMPTY || atomic_load(&clips[q].len)!=0){
                    char t[8]; snprintf(t,sizeof t,first?"P%d":" + P%d",q+1); strncat(used,t,sizeof used-strlen(used)-1); first=0;
                }
                if(!first){snprintf(layout_msg,sizeof layout_msg,"CLEAR %s FIRST",used);layout_ticks=60;}
                else {atomic_store(&g_active_clips,4);if(selected>=4)selected=0;snprintf(layout_msg,sizeof layout_msg,"4 LOOP MODE - PADS 1-4");layout_ticks=45;}
            }
        }
        for(int i=0;i<atomic_load(&g_active_clips);i++) if(in->pad_hit[i]>0){
            clear_pad=-1; clear_ticks=0;
            if(selected!=i){selected=i;fader_latched=0;fader_last=in->fader_pos;}
            int st=atomic_load(&clips[i].state);
            if(atomic_load(&g_mode)==MODE_SETUP && st==EMPTY){
                /* setup: tap only selects an empty slot */
            } else {
                atomic_store(&g_cmd,i+1);
            }
        }
        if(in->fader_moved){
            fader_seen=1;
            int pos=in->fader,target=atomic_load(&clips[selected].volume);
            if(!fader_latched){
                if(fader_last<0)fader_last=in->fader_pos;
                int crossed=(fader_last<=target&&pos>=target)||(fader_last>=target&&pos<=target);
                int d=pos-target;if(d<0)d=-d;
                if(crossed||d<=2)fader_latched=1;
            }
            if(fader_latched)atomic_store(&clips[selected].volume,pos);
            fader_last=pos;
        }
        if(input_down(in,IN_B1))atomic_fetch_xor(&g_transport,1);
        if(input_down(in,IN_B2))atomic_fetch_xor(&g_metro,1);
        if(input_down(in,IN_B3)){
            int st=atomic_load(&clips[selected].state);
            if(st==ARMED){ atomic_store(&g_cmd,selected+1); clear_pad=-1; clear_ticks=0; }
            else if(st!=EMPTY){
                if(clear_pad==selected && clear_ticks>0){ atomic_store(&g_cmd,101+selected); clear_pad=-1; clear_ticks=0; }
                else { clear_pad=selected; clear_ticks=60; }
            }
        }
    }
    ui_clear(u,0x0b1018);
    char b[96];
    int active=atomic_load(&g_active_clips);

    /* TOP: mirrors the three physical B buttons directly above the LCD. */
    ui_box(u,0,0,320,29,(struct ui_paint){.fill_set=1,.fill=0x162033,.border_set=1,.border=0x334155,.border_px=1});
    ui_text(u,10,4,atomic_load(&g_transport)?"B1 STOP":"B1 PLAY",0xe2e8f0,1);
    ui_text(u,116,4,atomic_load(&g_metro)?"B2 CLICK ON":"B2 CLICK OFF",atomic_load(&g_metro)?0xf59e0b:0x94a3b8,1);
    ui_text(u,244,4,clear_pad>=0?"B3 CONFIRM":"B3 CLEAR",0xef4444,1);
    if(atomic_load(&g_mode)==MODE_SETUP){
        ui_box(u,92,15,136,13,(struct ui_paint){.fill_set=1,.fill=0x0e7490});
        ui_text(u,111,17,active==4?"LOOPPAD SETUP 4":"LOOPPAD SETUP 8",0xffffff,1);
    } else {
        ui_box(u,92,15,136,13,(struct ui_paint){.fill_set=1,.fill=0x166534});
        ui_text(u,116,17,active==4?"LOOPPAD LIVE 4":"LOOPPAD LIVE 8",0xffffff,1);
    }

    /* Secondary status: side LEDs carry the live L/R input meter. */
    int pl=atomic_load(&g_peak_l),pr=atomic_load(&g_peak_r);
    ui_meter(u,meter_mask(pl),meter_mask(pr));
    {int q=atomic_load(&g_quant),ci=atomic_load(&g_countin);snprintf(b,sizeof b,"%s Q:%s CI:%s CLK:%d%%",atomic_load(&g_input)?"REAR":"MIC",q==Q_BAR?"BAR":q==Q_BEAT?"BEAT":"OFF",ci==CI_NEXT?"NEXT":ci==CI_1BAR?"1BAR":"NOW",(atomic_load(&g_click_volume)*100+63)/127);}
    ui_text(u,10,34,b,0x64748b,1);
    snprintf(b,sizeof b,"XRUN %d",atomic_load(&g_xruns));
    ui_text(u,262,34,b,0x64748b,1);

    /* CENTRE: detailed 4-loop view or compact 8-loop view over the same engine. */
    if(active==4){
        for(int i=0;i<4;i++){
            int col=i%2,row=i/2,x0=12+col*154,y0=50+row*58,st=atomic_load(&clips[i].state);uint32_t c=state_color(st);
            uint32_t border=(atomic_load(&g_mode)==MODE_SETUP && i==selected && st==EMPTY)?0x22d3ee:c;
            if(clear_pad==i) border=(clear_ticks&4)?0xef4444:0x7f1d1d;
            ui_box(u,x0,y0,142,50,(struct ui_paint){.fill_set=1,.fill=i==selected?0x182235:0x111827,.border_set=1,.border=border,.border_px=i==selected?3:1});
            if(st==ARMED){int n=arm_beats_left(&clips[i]);int ci=atomic_load(&g_countin);if(ci==CI_NOW)snprintf(b,sizeof b,"P%d REC NOW",i+1);else if(ci==CI_1BAR&&n>4)snprintf(b,sizeof b,"P%d COUNT %d",i+1,n);else snprintf(b,sizeof b,"P%d REC IN %d",i+1,n>0?n:1);}else snprintf(b,sizeof b,"P%d %s",i+1,STATE_NAME[st]);
            ui_text(u,x0+7,y0+5,b,c,1);snprintf(b,sizeof b,"%dB",atomic_load(&clips[i].bars));ui_text(u,x0+88,y0+5,b,0x93c5fd,1);snprintf(b,sizeof b,"%d%%",(atomic_load(&clips[i].volume)*100+63)/127);int px=x0+135-(int)strlen(b)*6;if(px<x0+108)px=x0+108;ui_text(u,px,y0+5,b,0xcbd5e1,1);
            size_t len=atomic_load(&clips[i].len);double q=0;uint64_t t=atomic_load(&g_clock),o=atomic_load(&clips[i].origin);if(st==REC&&len)q=(double)atomic_load(&clips[i].recpos)/(len+XFADE);else if((st==PLAY||st==QUEUED_STOP)&&len)q=(double)((t-o)%len)/len;if(q>1)q=1;
            ui_box(u,x0+7,y0+17,128,28,(struct ui_paint){.fill_set=1,.fill=0x0b1220});if(st==ARMED){int n=arm_beats_left(&clips[i]);char cb[16];snprintf(cb,sizeof cb,"%d",n>0?n:1);ui_text(u,x0+58,y0+21,cb,0xfbbf24,2);}else if(atomic_load(&clips[i].wave_ready))draw_clip_wave(u,&clips[i],x0+7,y0+17,128,28,c,st);else if(q>0)ui_box(u,x0+7,y0+28,(int)(128*q),5,(struct ui_paint){.fill_set=1,.fill=c});
            if(st==ARMED){int on=arm_flash_on();ui_led(u,i,on?127:25,on?70:12,0);}else if(st==REC)ui_led(u,i,127,0,0);else if(st==PLAY)ui_led(u,i,0,127,20);else if(st==STOPPED)ui_led(u,i,0,30,127);else if(st==QUEUED_STOP)ui_led(u,i,0,100,90);else if(st==QUEUED_PLAY)ui_led(u,i,30,70,127);else if(atomic_load(&g_mode)==MODE_SETUP&&i==selected)ui_led(u,i,20,110,127);else if(atomic_load(&g_mode)==MODE_SETUP)ui_led(u,i,12,18,22);else ui_led(u,i,8,8,12);
        }
    }else{
        for(int i=0;i<8;i++){
            int col=i%4,row=1-(i/4),x0=5+col*79,y0=52+row*57,st=atomic_load(&clips[i].state);uint32_t c=state_color(st),border=(atomic_load(&g_mode)==MODE_SETUP&&i==selected&&st==EMPTY)?0x22d3ee:c;if(clear_pad==i)border=(clear_ticks&4)?0xef4444:0x7f1d1d;
            ui_box(u,x0,y0,74,49,(struct ui_paint){.fill_set=1,.fill=i==selected?0x182235:0x111827,.border_set=1,.border=border,.border_px=i==selected?2:1});
            snprintf(b,sizeof b,"P%d %s",i+1,STATE_NAME[st]);ui_text(u,x0+3,y0+3,b,c,1);snprintf(b,sizeof b,"%dB %d%%",atomic_load(&clips[i].bars),(atomic_load(&clips[i].volume)*100+63)/127);ui_text(u,x0+3,y0+14,b,0xcbd5e1,1);
            if(atomic_load(&clips[i].wave_ready))draw_clip_wave(u,&clips[i],x0+3,y0+27,68,17,c,st);else if(st==ARMED){int n=arm_beats_left(&clips[i]);snprintf(b,sizeof b,"IN %d",n>0?n:1);ui_text(u,x0+24,y0+30,b,0xfbbf24,1);}
            if(st==ARMED){int on=arm_flash_on();ui_led(u,i,on?127:25,on?70:12,0);}else if(st==REC)ui_led(u,i,127,0,0);else if(st==PLAY)ui_led(u,i,0,127,20);else if(st==STOPPED)ui_led(u,i,0,30,127);else if(st==QUEUED_STOP)ui_led(u,i,0,100,90);else if(st==QUEUED_PLAY)ui_led(u,i,30,70,127);else if(atomic_load(&g_mode)==MODE_SETUP&&i==selected)ui_led(u,i,20,110,127);else if(atomic_load(&g_mode)==MODE_SETUP)ui_led(u,i,12,18,22);else ui_led(u,i,8,8,12);
        }
    }
    for(int i=active;i<16;i++)ui_led(u,i,0,0,0);

    /* Pickup message immediately above the physical K/fader controls. */
    if(!fader_seen){
        snprintf(b,sizeof b,"P%d LEVEL %d%%   FADER --   PICKUP %d%%",selected+1,(atomic_load(&clips[selected].volume)*100+63)/127,(atomic_load(&clips[selected].volume)*100+63)/127);
        ui_text(u,16,169,b,0xf59e0b,1);
    } else if(!fader_latched){
        snprintf(b,sizeof b,"P%d LEVEL %d%%   FADER %d%%   PICKUP %d%%",selected+1,(atomic_load(&clips[selected].volume)*100+63)/127,fader_last*100/127,(atomic_load(&clips[selected].volume)*100+63)/127);
        ui_text(u,10,169,b,0xf59e0b,1);
    } else {
        snprintf(b,sizeof b,"P%d LEVEL %d%%   FADER %d%%   CONTROL",selected+1,(atomic_load(&clips[selected].volume)*100+63)/127,fader_last*100/127);
        ui_text(u,20,169,b,0x22c55e,1);
    }

    if(layout_ticks>0){
        ui_box(u,38,137,244,38,(struct ui_paint){.fill_set=1,.fill=0x0f172a,.border_set=1,.border=0x38bdf8,.border_px=2});
        ui_text(u,72,151,layout_msg,0xffffff,1);
    }

    if(clear_pad>=0){
        int st=atomic_load(&clips[clear_pad].state);
        ui_box(u,54,151,212,30,(struct ui_paint){.fill_set=1,.fill=0x450a0a,.border_set=1,.border=0xef4444,.border_px=2});
        snprintf(b,sizeof b,st==REC?"CANCEL RECORDING P%d?":"CLEAR P%d?",clear_pad+1);
        ui_text(u,91,155,b,0xffffff,1);
        ui_text(u,92,168,"PRESS B3 AGAIN",0xfca5a5,1);
    }

    /* BOTTOM: mirrors K1, K2, K3 plus the physical fader below the display. */
    ui_box(u,0,186,320,54,(struct ui_paint){.fill_set=1,.fill=0x101827,.border_set=1,.border=0x334155,.border_px=1});
    if(in&&in->shift) snprintf(b,sizeof b,"K1 CLICK");
    else if(atomic_load(&g_mode)==MODE_SETUP) snprintf(b,sizeof b,"K1 P%d BARS",selected+1);
    else snprintf(b,sizeof b,"P%d LENGTH",selected+1);
    ui_text(u,10,191,b,0x93c5fd,1);
    if(in&&in->shift) snprintf(b,sizeof b,"%d%%",(atomic_load(&g_click_volume)*100+63)/127); else snprintf(b,sizeof b,"%d",atomic_load(&clips[selected].bars));
    ui_text(u,27,207,b,0xf8fafc,1);
    ui_text(u,88,191,in&&in->shift?"K2 QUANT":"K2 BPM",0x93c5fd,1);
    if(in&&in->shift){int q=atomic_load(&g_quant);snprintf(b,sizeof b,"%s",q==Q_BAR?"BAR":q==Q_BEAT?"BEAT":"OFF");}
    else snprintf(b,sizeof b,"%d",atomic_load(&g_bpm));
    ui_text(u,103,207,b,0xf8fafc,1);
    ui_text(u,163,191,in&&in->shift?"K3 COUNTIN":"K3 INPUT",0x93c5fd,1);
    if(in&&in->shift){int ci=atomic_load(&g_countin);ui_text(u,178,207,ci==CI_NEXT?"NEXT":ci==CI_1BAR?"1BAR":"NOW",0xf8fafc,1);}
    else ui_text(u,178,207,atomic_load(&g_input)?"REAR":"MIC",0xf8fafc,1);
    snprintf(b,sizeof b,"FADER P%d",selected+1); ui_text(u,247,191,b,0x93c5fd,1);
    if(!fader_seen) snprintf(b,sizeof b,"--"); else snprintf(b,sizeof b,"%d%%",fader_last*100/127); ui_text(u,265,207,b,0xf8fafc,1);
    if(atomic_load(&g_mode)==MODE_SETUP)
        ui_text(u,8,225,"SETUP: SELECT PAD + K1 LENGTH   SAMPLE SELECT > LIVE",0x22d3ee,1);
    else
        ui_text(u,8,225,"LIVE: TAP PAD TO PERFORM   SAMPLE SELECT > SETUP",0x4ade80,1);

    ui_button_led(u,IN_B1,atomic_load(&g_transport)?2:0);ui_button_led(u,IN_B2,atomic_load(&g_metro)?2:0);
    ui_button_led(u,IN_B3,clear_pad>=0?2:0);
    if(example_menu_draw(u,in))return 1;return 0;
}

static void sig(int x){(void)x;atomic_store(&g_running,0);}
int main(void){
    signal(SIGINT,sig);signal(SIGTERM,sig);
    for(int i=0;i<NCLIPS;i++){clips[i].audio=calloc(MAX_FRAMES*2,sizeof(int32_t));if(!clips[i].audio)return 1;atomic_store(&clips[i].state,EMPTY);atomic_store(&clips[i].volume,127);atomic_store(&clips[i].bars,1);atomic_store(&clips[i].wave_ready,0);memset(clips[i].wave,0,sizeof clips[i].wave);}
    if(setup_input_route(0)!=0)fprintf(stderr,"LoopPad: input route failed\n");
    g_cap=open_pcm(PCM_IN);if(!g_cap)return 2;g_pb=open_pcm(PCM_OUT);if(!g_pb)return 3;
    pthread_t th;if(pthread_create(&th,NULL,audio_thread,NULL)!=0)return 4;
    uint32_t *raw=NULL;int rw=0,rh=0;
    if(image_decode_mem(looppad_splash_png,(int)looppad_splash_png_len,&raw,&rw,&rh)==0){
        if(rw==UI_W && rh==UI_H){g_splash_px=raw;g_splash_w=rw;g_splash_h=rh;}
        else {g_splash_px=image_scale(raw,rw,rh,UI_W,UI_H);free(raw);if(g_splash_px){g_splash_w=UI_W;g_splash_h=UI_H;}}
    }
    int rc=app_run(frame,NULL);atomic_store(&g_running,0);pthread_join(th,NULL);pcm_close(g_cap);pcm_close(g_pb);free(g_splash_px);for(int i=0;i<NCLIPS;i++)free(clips[i].audio);return rc;
}
