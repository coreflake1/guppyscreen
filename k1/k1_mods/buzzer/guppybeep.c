/* guppybeep — hardware-PWM buzzer player for the Ender-3 V3 KE (Ingenic X2000-V12).
 * Zero-CPU, print-safe: the PWM counter runs in hardware; this process sleeps.
 *
 * Buzzer = passive piezo on PC03 = PWM ch3. The signal only reaches the pad if
 * PC03 is muxed to its PWM device-function (func0) — the vendor /dev/jz_pwm driver
 * fails to do this, so we program the GPIO port-C controller directly. Original
 * pin state is saved and restored on exit (incl. signals) so /usr/bin/beep still works.
 *
 *   guppybeep tone <freq_hz> <ms>          play one tone
 *   guppybeep m300 S<freq> P<ms>           Klipper M300 (S=freq Hz, P=ms); defaults S1000 P100
 *   guppybeep rtttl "<rtttl-string>"       play an RTTTL song
 *
 * Build: docker run --rm -v /tmp:/t -w /t ghcr.io/coreflake1/guppydev:latest \
 *          mipsel-linux-gcc -O2 -static -o guppybeep guppybeep.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdint.h>
#include <signal.h>
#include <sys/mman.h>

#define PWM_BASE 0x134c0000u
#define GPC_PAGE 0x10010000u
#define GPC_OFF  0x200u            /* GPIO port C within the page */
#define MAP_LEN  0x1000
#define CH 3

/* PWM regs (offsets) */
#define PWMCCFG0  0x000            /* PRESCALEn: 4 bits/ch at ch*4; clk = 300MHz>>PRESCALE */
#define PWMENS    0x010            /* enable: write 1<<ch */
#define PWMENC    0x014            /* disable: write 1<<ch */
#define PWMUPDATE 0x020            /* load shadow->active: write 1<<ch */
#define PWMBUSYR  0x024            /* update in progress: bit ch */
#define PWMLVL    0x030            /* init_lvl bit ch, idle_lvl bit ch+16 */
#define PWMWCFG(n) (0x0B0 + (n)*4) /* [31:16]=HIGH cycles, [15:0]=LOW cycles (each <65536) */
#define PWMOEN    0x300            /* IO output enable, bit ch (X2000-V12) */

/* GPIO port-C regs (relative to port base) — set/clear pairs, write 1<<bit */
#define G_INTC  0x18
#define G_MSKS  0x24
#define G_MSKC  0x28
#define G_PAT1S 0x34
#define G_PAT1C 0x38
#define G_PAT0S 0x44
#define G_PAT0C 0x48

static volatile uint32_t *pwm, *gpc;
static uint32_t rd(int o){ return pwm[o/4]; }
static void wr(int o,uint32_t v){ pwm[o/4]=v; }
static void gset(int o){ gpc[o/4]=(1u<<CH); }

/* saved original PC03 GPIO bits, restored on exit so /usr/bin/beep keeps working */
static int saved=0, s_msk, s_pat1, s_pat0;
static int gpio_done=0;

static void gpio_save(void){
  s_msk =(gpc[0x20/4]>>CH)&1; s_pat1=(gpc[0x30/4]>>CH)&1; s_pat0=(gpc[0x40/4]>>CH)&1; saved=1;
}
static void gpio_mux_pwm(void){            /* PC03 -> device func0 (INT=0 MSK=0 PAT1=0 PAT0=0) */
  gset(G_INTC); gset(G_MSKC); gset(G_PAT1C); gset(G_PAT0C);
}
static void gpio_restore(void){
  if(!saved||gpio_done) return;
  gpc[(s_msk ?G_MSKS :G_MSKC )/4]=(1u<<CH);
  gpc[(s_pat1?G_PAT1S:G_PAT1C)/4]=(1u<<CH);
  gpc[(s_pat0?G_PAT0S:G_PAT0C)/4]=(1u<<CH);
  gpio_done=1;
}

static void pwm_off(void){ wr(PWMENC,1u<<CH); wr(PWMOEN, rd(PWMOEN)&~(1u<<CH)); }

static void cleanup(void){ pwm_off(); gpio_restore(); }
static void on_sig(int s){ (void)s; cleanup(); _exit(1); }

/* smallest PRESCALE (0..15) whose whole period fits one 16-bit field. We keep
 * the full period < 65536 (not just each half) so any duty cycle is safe: a low
 * duty makes the LOW count approach the whole period. */
static unsigned pick_prescale(unsigned freq, unsigned *total_out){
  for(unsigned P=0;P<=15;P++){
    unsigned total=(300000000u>>P)/freq;
    if(total>0 && total<65500){ *total_out=total; return P; }
  }
  *total_out=(300000000u>>15)/freq; return 15;
}

/* duty cycle in percent for the PWM high phase. 50 = square wave (loudest);
 * a low duty drives the piezo with narrow pulses -> much softer/quieter. */
static unsigned g_duty = 50;

static void pwm_note(unsigned freq){
  if(freq<20){ pwm_off(); return; }            /* treat as rest */
  unsigned total, P=pick_prescale(freq,&total);
  unsigned hi=(unsigned)((unsigned long long)total*g_duty/100);
  if(hi<1) hi=1; if(hi>=total) hi=total-1;
  unsigned lo=total-hi;
  wr(PWMENC,1u<<CH);                            /* stop before reconfig */
  uint32_t c=rd(PWMCCFG0); c&=~(0xFu<<(CH*4)); c|=(P<<(CH*4)); wr(PWMCCFG0,c);
  wr(PWMWCFG(CH),(hi<<16)|(lo&0xFFFF));
  uint32_t l=rd(PWMLVL); l&=~((1u<<CH)|(1u<<(CH+16))); wr(PWMLVL,l);   /* init/idle low */
  wr(PWMOEN, rd(PWMOEN)|(1u<<CH));
  wr(PWMENS,1u<<CH);
  wr(PWMUPDATE,1u<<CH);
  int sp=0; while((rd(PWMBUSYR)&(1u<<CH)) && sp<200000) sp++;
}

/* play tone for ms with a short articulation gap so consecutive notes separate */
static void play(unsigned freq, unsigned ms){
  if(freq<20){ pwm_off(); usleep(ms*1000); return; }
  unsigned gap = ms>40 ? 12 : 0;
  pwm_note(freq); usleep((ms-gap)*1000);
  if(gap){ pwm_off(); usleep(gap*1000); }
}

static int map_all(void){
  int fd=open("/dev/mem",O_RDWR|O_SYNC);
  if(fd<0){ perror("open /dev/mem"); return -1; }
  pwm=mmap(0,MAP_LEN,PROT_READ|PROT_WRITE,MAP_SHARED,fd,PWM_BASE);
  volatile uint32_t *pg=mmap(0,MAP_LEN,PROT_READ|PROT_WRITE,MAP_SHARED,fd,GPC_PAGE);
  if(pwm==MAP_FAILED||pg==MAP_FAILED){ perror("mmap"); return -1; }
  gpc=pg+GPC_OFF/4;
  return 0;
}

static void engine_start(void){
  signal(SIGINT,on_sig); signal(SIGTERM,on_sig);
  gpio_save(); gpio_mux_pwm();
}

/* ---- RTTTL ---------------------------------------------------------------- */
/* integer freqs for octave 4 (c4..b4); octaves scale by exact power-of-two shift */
static const unsigned OCT4[12]={262,277,294,311,330,349,370,392,415,440,466,494};
/* note letter -> index into OCT4 (c=0): c,c#,d,d#,e,f,f#,g,g#,a,a#,b */
static int note_idx(char c){
  switch(c){case 'c':return 0;case 'd':return 2;case 'e':return 4;case 'f':return 5;
            case 'g':return 7;case 'a':return 9;case 'b':return 11;default:return -1;}
}
static unsigned note_freq(int idx,int oct){
  if(idx<0) return 0;                                  /* pause */
  unsigned f=OCT4[idx];
  if(oct>=4) f<<=(oct-4); else f>>=(4-oct);
  return f;
}

static void play_rtttl(const char *s){
  int d_def=4, o_def=5, bpm=63;
  const char *p=strchr(s,':'); if(p) p++; else p=s;     /* skip name */
  const char *body=strchr(p,':');                       /* defaults are between p..body */
  if(body){
    for(const char *q=p;q<body;q++){
      if((q[0]=='d'||q[0]=='o'||q[0]=='b')&&q[1]=='='){
        int v=atoi(q+2);
        if(q[0]=='d'&&v) d_def=v; else if(q[0]=='o'&&v) o_def=v; else if(q[0]=='b'&&v) bpm=v;
      }
    }
    p=body+1;
  }
  unsigned whole = 4u*60000u/(unsigned)bpm;             /* whole-note ms = 4 * quarter */
  while(*p){
    while(*p==',' || isspace((unsigned char)*p)) p++;
    if(!*p) break;
    int dur=0; while(isdigit((unsigned char)*p)){ dur=dur*10+(*p-'0'); p++; }
    if(!dur) dur=d_def;
    char nc=tolower((unsigned char)*p); int idx;
    if(nc=='p'){ idx=-1; p++; } else { idx=note_idx(nc); if(idx>=0) p++; }
    /* sharp: '#' is standard RTTTL, but Klipper's gcode parser eats '#' as a
     * comment, so 's' (as/cs/fs) is accepted as an equivalent for macro use. */
    if(idx>=0 && (*p=='#'||*p=='s'||*p=='S')){ idx++; p++; }
    int dotted=0; if(*p=='.'){ dotted=1; p++; }
    int oct=o_def; if(isdigit((unsigned char)*p)){ oct=*p-'0'; p++; }
    if(*p=='.'){ dotted=1; p++; }                       /* dot may follow octave */
    unsigned ms = whole/(unsigned)dur;
    if(dotted) ms += ms/2;
    play(note_freq(idx,oct), ms);
  }
  pwm_off();
}

/* Play a named song from a songs file. Each line is  name = <rtttl> ; lines
 * starting with '#' or ';' are comments. Read directly here (never via Klipper),
 * so RTTTL in the file may use standard '#' sharps. Returns 0 if played. */
static int play_song(const char *name, const char *path){
  FILE *f=fopen(path,"r");
  if(!f){ fprintf(stderr,"guppybeep: cannot open %s\n",path); return -1; }
  char line[1024];
  while(fgets(line,sizeof line,f)){
    char *p=line; while(*p==' '||*p=='\t') p++;
    if(*p=='#'||*p==';'||*p=='\n'||*p=='\r'||*p=='\0') continue;
    char *eq=strchr(p,'='); if(!eq) continue;
    char *ne=eq; while(ne>p && (ne[-1]==' '||ne[-1]=='\t')) ne--; *ne='\0';   /* trim name */
    char *val=eq+1; while(*val==' '||*val=='\t') val++;                       /* skip lead ws */
    char *ve=val+strlen(val);
    while(ve>val && (ve[-1]=='\n'||ve[-1]=='\r'||ve[-1]==' '||ve[-1]=='\t')) ve--; *ve='\0';
    if(strcasecmp(p,name)==0){ fclose(f); play_rtttl(val); return 0; }
  }
  fclose(f);
  fprintf(stderr,"guppybeep: song '%s' not found in %s\n",name,path);
  return -1;
}

int main(int argc,char**argv){
  const char *mode = argc>1?argv[1]:"";
  if(map_all()) return 1;
  engine_start();

  if(!strcmp(mode,"tone")){
    unsigned f=argc>2?strtoul(argv[2],0,0):1000;
    unsigned ms=argc>3?strtoul(argv[3],0,0):200;
    play(f,ms);
  } else if(!strcmp(mode,"m300")){
    unsigned f=1000, ms=100;                            /* M300 defaults */
    for(int i=2;i<argc;i++){
      char c=toupper((unsigned char)argv[i][0]);
      const char *num = argv[i][1]?argv[i]+1 : (i+1<argc?argv[++i]:"");
      if(c=='S') f=strtoul(num,0,0);
      else if(c=='P') ms=strtoul(num,0,0);
    }
    if(f>=20) play(f,ms); else { pwm_off(); usleep(ms*1000); }
  } else if(!strcmp(mode,"rtttl")){
    if(argc>2) play_rtttl(argv[2]);
  } else if(!strcmp(mode,"song")){
    const char *name = argc>2?argv[2]:"";
    const char *path = argc>3?argv[3]:"/usr/data/printer_data/config/songs.conf";
    if(*name) play_song(name,path);
  } else if(!strcmp(mode,"click")){
    /* Soft phone-style touch tick: short, low duty, low pitch (well off the
     * ~2.3kHz resonance so it stays faint). Args optional: click [freq] [ms] [duty%]. */
    unsigned f    = argc>2?strtoul(argv[2],0,0):260;
    unsigned ms   = argc>3?strtoul(argv[3],0,0):4;
    g_duty        = argc>4?strtoul(argv[4],0,0):2;
    play(f,ms);
  } else {
    fprintf(stderr,"usage: %s tone <freq> <ms> | m300 S<f> P<ms> | rtttl <str> | song <name> [file] | click [f] [ms] [duty]\n",argv[0]);
    cleanup(); return 2;
  }
  cleanup();
  return 0;
}
