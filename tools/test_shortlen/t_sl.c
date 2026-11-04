/* t_sl.c - off-game test of BodyHook's shortlen.inc against the real NBA 2K14 code.
 * The decrypted nba2k14 image (BodyHook_image.bin) is mapped at 0x400000; shortlen.inc is compiled for 32-bit
 * Linux with a tiny Win32 shim. Tests:
 *   1. install_shortlen(): Body and Head list copy (order, end record), page pointer, text lookup detour
 *   2. the GAME's text lookup 0x65FEC0 answers our label / help keys, other keys go to the original code
 *   3. the GAME's row visibility 0x4F8E90 on our record: regular player shown (0), created player hidden (3)
 *   4. value text through the GAME's formatter -> STRING handler, left/right clamps 0..10, ini writes
 *   5. cloth: the GAME's ClothMesh fix-up 0x45C630, instance creation 0x21651B0, then sl_after_build(); the
 *      GAME's cloth reset 0x45B8C0 + frame step 0x45B9B0 run N frames (standing / moving legs); positions out.
 * build: see run.sh */
#define __fastcall __attribute__((fastcall))
#define __cdecl __attribute__((cdecl))
#define __declspec(x) __attribute__((x))
#define WINAPI __attribute__((stdcall))
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32;
typedef int BOOL; typedef unsigned int DWORD, UINT, SIZE_T; typedef void *LPVOID, *HANDLE; typedef const char *LPCSTR; typedef char *LPSTR;
typedef struct { int x; } CRITICAL_SECTION;
#define MAX_PATH 260
#define MEM_COMMIT 0x1000
#define MEM_RESERVE 0x2000
#define MEM_RELEASE 0x8000
#define PAGE_READWRITE 4
#define NULL ((void*)0)
static int sys6(int n,int a,int b,int c,int d,int e,int f){int r;__asm__ volatile("push %%ebp; mov %7,%%ebp; int $0x80; pop %%ebp":"=a"(r):"a"(n),"b"(a),"c"(b),"d"(c),"S"(d),"D"(e),"m"(f):"memory");return r;}
#define sys3(n,a,b,c) sys6(n,a,b,c,0,0,0)
static void out(const char*s){int n=0;while(s[n])n++;sys3(4,1,(int)s,n);}
void *memset(void*d,int c,unsigned n){u8*p=d;while(n--)*p++=(u8)c;return d;}
void *memcpy(void*d,const void*s,unsigned n){u8*p=d;const u8*q=s;while(n--)*p++=*q++;return d;}
static void outx(const char *a, u32 v){ char b[16]; int i; out(a); for(i=0;i<8;i++){u32 d=(v>>(28-4*i))&15; b[i]=(char)(d<10?'0'+d:'A'+d-10);} b[8]='\n'; b[9]=0; out(b); }
static void outd(const char *a, int v){ char b[16]; int n=0,i; u32 u=v<0?-v:v; char t[12]; out(a); if(v<0)b[n++]='-'; do{t[0]=0; t[11]=0;}while(0); {int k=0; char r[12]; do{r[k++]=(char)('0'+u%10);u/=10;}while(u); for(i=k-1;i>=0;i--)b[n++]=r[i];} b[n++]='\n'; b[n]=0; out(b); }
static int fails = 0;
static void check(int ok, const char *what){ out(ok?"  ok   ":"  FAIL "); out(what); out("\n"); if(!ok) fails++; }

/* ---- Win32 shim ---- */
LPVOID WINAPI VirtualAlloc(LPVOID a, SIZE_T n, DWORD t, DWORD p){ int r; (void)a;(void)t;(void)p; r=sys6(192,0,(int)((n+4095)&~4095u),7,0x22,-1,0); return (r<0&&r>-4096)?0:(LPVOID)r; }
BOOL WINAPI VirtualFree(LPVOID a, SIZE_T n, DWORD t){ (void)a;(void)n;(void)t; return 1; }
typedef DWORD (WINAPI *LPTHREAD_START_ROUTINE)(LPVOID);
HANDLE WINAPI CreateThread(LPVOID a, SIZE_T b, LPTHREAD_START_ROUTINE f, LPVOID c, DWORD d, DWORD *e){ (void)a;(void)b;(void)f;(void)c;(void)d;(void)e; return (HANDLE)1; }
void WINAPI Sleep(DWORD ms){ (void)ms; }
static DWORD g_tick = 1000; DWORD WINAPI GetTickCount(void){ return g_tick; }
void WINAPI InitializeCriticalSection(CRITICAL_SECTION*c){(void)c;}
void WINAPI EnterCriticalSection(CRITICAL_SECTION*c){(void)c;}
void WINAPI LeaveCriticalSection(CRITICAL_SECTION*c){(void)c;}
static char g_iniSec[4096] = "1612=7\0" "195=12\0" "abc=3\0" "0=4\0" "411=5\0";   /* saved choices (+ junk lines) */
DWORD WINAPI GetPrivateProfileSectionA(LPCSTR s, LPSTR o, DWORD n, LPCSTR f){ (void)s;(void)f; memcpy(o,g_iniSec,n<sizeof g_iniSec?n:sizeof g_iniSec); return 30; }
static char g_lastKey[16], g_lastVal[8]; static int g_writes=0;
BOOL WINAPI WritePrivateProfileStringA(LPCSTR s, LPCSTR k, LPCSTR v, LPCSTR f){ int i; (void)s;(void)f; for(i=0;k[i]&&i<15;i++)g_lastKey[i]=k[i]; g_lastKey[i]=0; if(v){for(i=0;v[i]&&i<7;i++)g_lastVal[i]=v[i]; g_lastVal[i]=0;} else { g_lastVal[0]='-'; g_lastVal[1]=0; } g_writes++; return 1; }

/* ---- the BodyHook helpers shortlen.inc uses (same behaviour, no Windows) ---- */
static int cfg_diagnostics = 1; static int g_is2k12 = 0;
static char g_dir[MAX_PATH] = "./";
static u32 g_imageBase = 0x400000, g_exeImageSize = 0x1D7C000;
static u8 *g_textBase = (u8*)0x401000; static u32 g_textSize = 0xAC0000;
static int s_len(const char *s){int n=0;while(s[n])n++;return n;}
static void s_cpy(char *d,const char *s,int n){int i=0;while(s[i]&&i<n-1){d[i]=s[i];i++;}d[i]=0;}
static void s_cat(char *d,const char *s,int n){int l=s_len(d);s_cpy(d+l,s,n-l);}
static int readable(const void *p, u32 n){ u32 a=(u32)p; (void)n; return a>=0x10000; }
static int read32(u32 a, u32 *o){ if(a<0x10000) return 0; *o=*(volatile u32*)a; return 1; }
static void u32_to_dec(u32 v, char *o){ char t[12]; int i=0,j=0; do{t[i++]=(char)('0'+v%10);v/=10;}while(v); while(i)o[j++]=t[--i]; o[j]=0; }
static int ini_int2(const char *s, const char *k, int d){ (void)s;(void)k; return d; }
#include <stdarg.h>
static void blog(const char *fmt, ...){ char b[600]; int n=0; va_list ap; va_start(ap,fmt); out("    log: ");
  while(*fmt && n<580){ if(*fmt!='%'){ b[n++]=*fmt++; continue; } fmt++; { int w=0,hex=0; char t[16]; int k=0; u32 v; if(*fmt=='%'){b[n++]='%';fmt++;continue;}
    while(*fmt>='0'&&*fmt<='9'){ w=w*10+(*fmt-'0'); fmt++; }
    if(*fmt=='s'){ const char*q=va_arg(ap,const char*); while(*q&&n<580) b[n++]=*q++; fmt++; continue; }
    v=va_arg(ap,u32); if(*fmt=='X'||*fmt=='x'){hex=1;} if(*fmt=='d'&&(int)v<0){ b[n++]='-'; v=-(int)v; }
    do{ u32 d=hex?v%16:v%10; t[k++]=(char)(d<10?'0'+d:'A'+d-10); v=hex?v/16:v/10; }while(v);
    while(k<w) t[k++]='0'; while(k) b[n++]=t[--k]; fmt++; } }
  b[n]=0; va_end(ap); out(b); out("\n"); }
static u32 call_target_of(u32 site){ return site+5+*(int*)(site+1); }
static int in_text(u32 a){ return a>=(u32)g_textBase && a<(u32)g_textBase+g_textSize; }
static int patch_bytes(u32 a, const void *s, u32 n, int code){ (void)code; memcpy((void*)a,s,n); return 1; }
static int match(const u8 *p,const u8 *q,u32 n){u32 i;for(i=0;i<n;i++)if(p[i]!=q[i])return 0;return 1;}
static u8 *find_text(const u8 *pat,u32 n,u8 *from){ u8 *p=from?from:g_textBase,*end=g_textBase+g_textSize-n; for(;p<end;p++) if(p[0]==pat[0]&&match(p,pat,n)) return p; return 0; }
typedef int (*PageCb)(u32 lo, u32 hi, void *ctx);
static void for_data_pages(PageCb cb, void *ctx){ cb(0xEC6000, 0x0109F000, ctx) || cb(0x0109F000, 0x1D00000, ctx); }
static u32 player_cfid(u8 *rec){ short c; if(!rec) return 0; if(rec[0x9B]&1) return 0; c=*(short*)(rec+0x3C); return c>0?(u32)c:0; }
/* hairstyles.inc parts shortlen.inc uses (hair itself not installed here, like Enabled=0) */
#define HAIR_ITEM_HASH   0xABED212Bu
#define STRING_FMT_KEY   0x159D2468u
typedef int  (__cdecl    *HairVis_t)(void);
typedef void (__fastcall *HairText_t)(u8 *rec, void *sink, u32 unused);
typedef void (__fastcall *Fmt_t)(void *sink, u32 key, void *args);
static HairVis_t g_hairOrigVis = 0; static HairText_t g_hairOrigText = 0; static u32 g_hairStrHandler = 0;
int __cdecl HairVisible(void){ return 0; }
void __fastcall HairText(u8 *rec, void *sink, u32 unused){ (void)rec;(void)sink;(void)unused; }
#include "hair_extract.inc"
#include "../../src/shortlen.inc"

/* ---- game calls ---- */
static u32 savesp;
#define CLOB "memory","st","st(1)","st(2)","st(3)","st(4)","st(5)","st(6)","st(7)","xmm0","xmm1","xmm2","xmm3","xmm4","xmm5","xmm6","xmm7"
static u32 ga1, ga2, gr;   /* globals: the asm below changes esp, so no stack operands */
static u32 game_text(u32 obj, u32 key){ ga1=obj; ga2=key; __asm__ volatile("pushal; mov %%esp,%0; mov %1,%%ecx; mov %2,%%edx; mov $0x65fec0,%%eax; call *%%eax; mov %0,%%esp; mov %%eax,%3; popal"::"m"(savesp),"m"(ga1),"m"(ga2),"m"(gr):CLOB); return gr; }
static u32 game_vis(u32 item){ ga1=item; __asm__ volatile("pushal; mov %%esp,%0; mov %1,%%ecx; mov $0x4f8e90,%%eax; call *%%eax; mov %0,%%esp; mov %%eax,%2; popal"::"m"(savesp),"m"(ga1),"m"(gr):CLOB); return gr; }
static u8 row[1<<17] __attribute__((aligned(128)));
static u8 instbuf[1<<16] __attribute__((aligned(128)));
static float mats[256*12] __attribute__((aligned(16)));
static float gdir[4] __attribute__((aligned(16)))={0,-1,0,0};
static u32 g_inst;
static void call_fix(void){ __asm__ volatile("pushal; mov %%esp,%0; lea %1,%%ecx; mov $0x45c630,%%eax; call *%%eax; mov %0,%%esp; popal"::"m"(savesp),"m"(row):CLOB); }
static u32 call_create(void){ __asm__ volatile("pushal; mov %%esp,%0; lea %1,%%ecx; lea %2,%%edx; pushl $0; mov $0x21651b0,%%eax; call *%%eax; mov %0,%%esp; mov %%eax,%3; popal"::"m"(savesp),"m"(row),"m"(instbuf),"m"(gr):CLOB); return gr; }
static void call_reset(void){ __asm__ volatile("pushal; mov %%esp,%0; mov %1,%%esi; lea %2,%%eax; pushl $0x3f800000; mov $0x45b8c0,%%edx; call *%%edx; mov %0,%%esp; popal"::"m"(savesp),"m"(g_inst),"m"(mats):CLOB); }
static void call_step(void){ __asm__ volatile("pushal; mov %%esp,%0; mov %1,%%esi; movaps %3,%%xmm0; pushl $0x3f800000; lea %2,%%eax; push %%eax; pushl $0x3c888889; mov $0x45b9b0,%%edx; call *%%edx; mov %0,%%esp; popal"::"m"(savesp),"m"(g_inst),"m"(mats),"m"(gdir):CLOB); }
static float fsin_(float x){float r;__asm__("fsin":"=t"(r):"0"(x));return r;}
static int rdf(const char*p,u8*dst,int max){int fd=sys3(5,(int)p,0,0),n=0,r;if(fd<0)return -1;while((r=sys3(3,fd,(int)dst+n,max-n))>0)n+=r;sys3(6,fd,0,0);return n;}

/* fake sink for the formatter: the game calls sink->vtbl[2](this, &args) with the key in edx */
static u32 g_sinkKey; static const u16 *g_sinkText;
static void __fastcall sink_fn(void *self, u32 key, void *argsp){ (void)self; g_sinkKey=key; { void **a=*(void***)argsp; (void)a; } }
static u32 sink_vt[4]; static u32 sink_obj[2];

__attribute__((force_align_arg_pointer,noinline)) void main_(void){
  u32 list, k, n=0;
  if(sys6(192,0x400000,0x1D7C000,7,0x32,-1,0)!=0x400000 || rdf(IMAGE,(u8*)0x400000,0x1D7C000)<=0){out("image fail\n");sys3(1,1,0,0);}
  { u32 *L=(u32*)0x10a95c4; out("page entry before: "); outx("",L[2]); }
#ifdef HAIR_ON
  /* like in game: the Hair Style feature was installed first and replaced its callbacks */
  g_hairOrigText=(HairText_t)*(u32*)(0x10a2054+0x28); *(u32*)(0x10a2054+0x28)=(u32)&HairText;
  { u32 vt=*(u32*)(*(u32*)(0x10a2054+0x30)+4); g_hairOrigVis=(HairVis_t)*(u32*)(vt+4); *(u32*)(vt+4)=(u32)&HairVisible; }
  out("(Hair Style feature installed first)\n");
#endif
  out("1. install\n");
  install_shortlen();
  check(g_slReady && g_slMenu && g_slLoc, "installed: cloth part, menu row, text lookup");
  check(g_slSelCount==2 && sl_get(1612)==7 && sl_get(195)==10 && sl_get(411)==5, "saved choices read (1612=7, 195=12 -> 10, junk lines skipped, 5 not stored)");
  list = *(u32*)(0x10a95c4+8);
  check(list==(u32)g_slList, "Body and Head page now points at the copy");
  check(*(u32*)0x10a1d10==1 && *(u32*)(0x10a1d10+4)==0xa50a07fd, "the game's own list is unchanged");
  { static const u32 want[]={0xa50a07fd,0x20fe790d,SL_LABEL_KEY,0xad57f2f4,0x427160bd,0xe336db7e,0xd3f47d2e,0x10e92bdd,0x00091a56,0x9dc022ed,0x1c9e49e2,0xbe9ef588,0xabed212b,0xefb05432,0xf5eb0c11,0xe7e5a56e,0xc4877142,0x001f296d,0xdbfc813b,0xb789cf88};
    int ok=1; for(k=0;*(u32*)(list+0x4c*k);k++){ if(k>=sizeof want/4 || *(u32*)(list+0x4c*k+4)!=want[k]) ok=0; n++; }
    check(ok && n==20 && *(u32*)(list+0x4c*20)==0, "order: Height, Weight, SHORT LENGTH, Skin Tone ... Hair Style ... Facial Hair Color, end record"); }
  { u8 *r=(u8*)(list+0x4c*2); check(*(u32*)r==1 && *(u32*)(r+0x10)==*(u32*)(list+0x4c+0x10) && *(u32*)(r+0x28)==(u32)&SlText && *(u32*)(r+0x34)==(u32)g_slStepObj && *(u32*)(r+0x30)==(u32)g_slVisObj, "our record: type 1, Weight's camera view, our text / step / visibility");
    check(g_slStepObj[0]==0 && g_slStepObj[1]==(u32)&SlNext && g_slStepObj[2]==(u32)&SlPrev, "left/right object");
    { int i, have=0; for(i=0;g_slVisVt[i];i++) if(g_slVisVt[i]==(u32)&SlVisible) have++; check(have==1 && i==3 && g_slVisVt[0]==0x4fdc70 && g_slVisVt[2]==0x4fdbb0, "visibility list = Hair Style's with slot 1 ours"); } }
  check(*(u8*)0x65fec0==0xE9 && *(u8*)0x65fec5==0x90 && g_slLocResume==0x65fec6, "text lookup detour 65FEC0 -> ours, resume at 65FEC6");

  out("2. text lookup (game function)\n");
  { static u32 emptyTab[8]; emptyTab[4]=(u32)&emptyTab[4];        /* +10: an empty list (sentinel = itself) */
    u32 a=game_text((u32)emptyTab, SL_LABEL_KEY), b=game_text((u32)emptyTab, SL_HELP_KEY & ~1u), c=game_text((u32)emptyTab, 0xa50a07fd);
    check(a==(u32)g_slLabelW, "label key -> \"Short Length\"");
    check(b==(u32)g_slHelpW, "help key (low bit clear, as the game passes it) -> help text");
    check(c==0, "any other key runs the original lookup (empty table here -> 0, no crash)"); }

  out("3. visibility (game function 4F8E90)\n");
  { static u8 rec[0x298]; u32 r;
    *(u32*)g_slEditRec=(u32)rec; *(short*)(rec+0x3C)=1612; rec[0x9B]=0;
    /* the other two functions of the list read live game state (same as on the Hair Style row); here the
       game's 4F8E90 runs on our record with only BodyHook's function in the list */
    { u32 save[8]; int i; for(i=0;i<8;i++) save[i]=g_slVisVt[i]; g_slVisVt[0]=(u32)&SlVisible; g_slVisVt[1]=0;
    r=game_vis(list+0x4c*2); outd("    regular player CFID 1612 ->", r); check(r==0, "shown");
    rec[0x9B]=1; r=game_vis(list+0x4c*2); outd("    created player ->", r); check(r==3, "hidden for created players");
    rec[0x9B]=0; cfg_sl=0; r=game_vis(list+0x4c*2); check(r==3, "hidden with Enabled=0"); cfg_sl=1;
    for(i=0;i<8;i++) g_slVisVt[i]=save[i]; } }

  out("4. value text + left/right\n");
  { static u8 rec[0x298]; u32 hits;
    *(u32*)g_slEditRec=(u32)rec; *(short*)(rec+0x3C)=1612; rec[0x9B]=0;
    sink_vt[2]=(u32)&sink_fn; sink_obj[0]=(u32)sink_vt;
    g_sinkKey=0; SlText(rec,sink_obj,0);
    check((g_sinkKey|1)==(STRING_FMT_KEY|1), "value text goes through the game's formatter with {0:STRING}");
    check(SlNext(rec,0)==1 && sl_get(1612)==8 && g_lastKey[0]=='1' && g_lastVal[0]=='8', "right: 7 -> 8, saved 1612=8");
    SlNext(rec,0); SlNext(rec,0); check(SlNext(rec,0)==0 && sl_get(1612)==10, "stops at 10");
    for(k=0;k<12;k++) SlPrev(rec,0); check(sl_get(1612)==0 && g_lastVal[0]=='0', "stops at 0");
    for(k=0;k<5;k++) SlNext(rec,0); check(sl_get(1612)==5 && g_lastVal[0]=='-', "back to 5: the line is removed from the ini");
    hits=g_writes; rec[0x9B]=1; check(SlNext(rec,0)==0 && g_writes==hits, "created player: nothing changes"); }

  out("5. cloth (game solver)\n");
  { static u8 player[0x298]; static u32 outb[4]; int L, f, i;
    int nrow=rdf(ROWFILE,row,sizeof row); u32 np;
    if(nrow<=0){out("no row\n");sys3(1,1,0,0);}
    call_fix();
    np=*(u32*)(row+0xb8)&0xfff;
    *(short*)(player+0x3C)=CFID_T; player[0x9B]=0;
    sl_set(CFID_T, LEN_T);
    g_inst=call_create(); outb[0]=g_inst;
    check(g_inst==(u32)instbuf && *(u32*)(g_inst+0xA8)==(u32)row, "game created the instance (header copy, +A8 = ClothMesh)");
    sl_after_build(player, outb);
    check(LEN_T==5 ? *(u32*)(g_inst+0x78)==*(u32*)(row+0x78) : *(u32*)(g_inst+0x78)!=*(u32*)(row+0x78), "instance rest shape: own copy only when length != 5");
    { /* practice mode: the record now holds another player (CFID 1013, length 0), cloth kept */
      u32 r10=*(u32*)(g_inst+0x78);
      sl_set(1013, 0); *(short*)(player+0x3C)=1013; sl_watch_once();
      check(*(u32*)(g_inst+0x78)!=r10 && (LEN_T==0 || *(u32*)(g_inst+0x78)!=*(u32*)(row+0x78)), "record now holds CFID 1013: watcher gives the kept cloth his length 0");
      sl_set(1013, 5); sl_watch_once(); check(*(u32*)(g_inst+0x78)==*(u32*)(row+0x78), "CFID 1013 back to 5: the cloth file's own shape");
      *(short*)(player+0x3C)=CFID_T; sl_watch_once();
      check(LEN_T==5 ? *(u32*)(g_inst+0x78)==*(u32*)(row+0x78) : *(u32*)(g_inst+0x78)==r10, "back to the first player: his length again"); }
    { /* practice: the cloth built for this record is DRAWN with another player (render node +08 / +30) */
      static u8 other[0x298]; static u32 node[0x40]; u32 want;
      *(short*)(other+0x3C)=1013; other[0x9B]=0; sl_set(1013, 1);
      node[2]=(u32)other; node[12]=(u32)outb;                 /* +08 player, +30 shorts cloth object */
      sl_node((u32)node); want=*(u32*)(g_inst+0x78);
      check(want!=*(u32*)(row+0x78), "drawn with CFID 1013: his length 1 on the shared cloth");
      sl_watch_once(); check(*(u32*)(g_inst+0x78)==want, "the watcher keeps the drawn player's length (no flip back)");
      sl_node((u32)node); check(*(u32*)(g_inst+0x78)==want, "unchanged node: nothing done (fast path)"); }
    for(i=0;i<256;i++){float*m=mats+12*i; m[0]=1;m[5]=1;m[10]=1;}
    { int fo=sys3(5,(int)OUTFILE,0x241,0644); float *cur=*(float**)(g_inst+0x70);
      call_reset();
      for(f=1;f<=NFRAMES;f++){
        float t=f/60.0f, st=MOVE?0.8f*fsin_(9.4f*t):0.0f;
        /* legs swing (bones 1,6,120 left / 13,18,121 right) like the ClothGuard test */
        for(i=0;i<256;i++){ float*m=mats+12*i; int k2; for(k2=0;k2<12;k2++) m[k2]=0; m[0]=m[5]=m[10]=1; }
        { int L1[3]={1,6,120},R1[3]={13,18,121}; int j; float c=1,s=0;
          for(j=0;j<3;j++){ float a=st, cc, ss; float*m; cc=1-a*a/2+a*a*a*a/24; ss=a-a*a*a/6;
            m=mats+12*L1[j]; m[5]=cc;m[6]=-ss;m[9]=ss;m[10]=cc; m[7]=-6.1f-( cc*-6.1f-ss*0.3f); m[11]=0.3f-(ss*-6.1f+cc*0.3f);
            m=mats+12*R1[j]; m[5]=cc;m[6]=ss;m[9]=-ss;m[10]=cc; m[7]=-6.1f-( cc*-6.1f+ss*0.3f); m[11]=0.3f-(-ss*-6.1f+cc*0.3f); } (void)c;(void)s; }
        if(LIVE_AT && f==LIVE_AT){ EnterCriticalSection(&g_slLock); sl_set(CFID_T, LIVE_LEN); sl_live(CFID_T, LIVE_LEN); LeaveCriticalSection(&g_slLock); }
        call_step();
        if(f%2==0){ sys3(4,fo,(int)cur,np*16); }
      }
      sys3(6,fo,0,0);
      { int bad=0; for(i=0;i<(int)np*4;i++){ float v=cur[i]; if(!(v==v) || v>1e5f || v<-1e5f) bad++; } check(!bad, "no NaN / runaway particles after the run"); } } }
#ifdef ROWFILE2
  out("6. the shorts are loaded again (other cloth_shorts at the same addresses)\n");
  { static u8 player[0x298]; static u32 outb[4]; u32 restA, restB, fpA, fpB, instA;
    *(short*)(player+0x3C)=CFID_T; player[0x9B]=0; sl_set(CFID_T, 4);
    rdf(ROWFILE,row,sizeof row); call_fix(); g_inst=call_create(); outb[0]=g_inst; sl_after_build(player, outb);
    restA=*(u32*)(g_inst+0x78); fpA=sl_fp((u32)row); instA=g_inst;
    check(restA!=*(u32*)(row+0x78), "first shorts: length 4 copy");
    rdf(ROWFILE2,row,sizeof row); call_fix(); fpB=sl_fp((u32)row);
    outx("    rest array first / second load: ", *(u32*)(row+0x78));
    check(fpA!=fpB, "the fingerprint sees the other shorts");
    g_tick += 1000;
    g_inst=call_create(); outb[0]=g_inst; sl_after_build(player, outb); restB=*(u32*)(g_inst+0x78);
    check(restB!=restA && restB!=*(u32*)(row+0x78), "second shorts: a NEW length 4 copy (not the first shorts' one)");
    { u32 np=*(u32*)(row+0xb8)&0xfff, i, same=1; float *A=(float*)*(u32*)(row+0x78), *B=(float*)restB;
      for(i=0;i<np*4;i++) if(i%4!=1 && A[i]!=B[i]) same=0;
      check(same, "the new copy is made from the second shorts (x / z of every particle = theirs)"); }
    /* an instance still on the first copy (built before the reload) is moved by the watcher */
    *(u32*)(g_inst+0x78)=restA; sl_watch_once();
    check(*(u32*)(g_inst+0x78)==restB, "an instance left on the old copy is moved to the new one");
    { int i, bad=0; float *cur=*(float**)(g_inst+0x70); u32 np=*(u32*)(row+0xb8)&0xfff, f;
      call_reset(); for(f=0;f<120;f++) call_step();
      for(i=0;i<(int)np*4;i++){ float v=cur[i]; if(!(v==v) || v>1e5f || v<-1e5f) bad++; }
      check(!bad, "second shorts run 120 frames: no NaN / runaway particles"); } }
#endif
  outd("FAILURES: ", fails);
  sys3(1,fails?1:0,0,0);
}
__asm__(".globl _start\n_start:\n and $-16,%esp\n call main_\n");
