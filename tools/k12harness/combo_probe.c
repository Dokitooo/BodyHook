#define __fastcall __attribute__((fastcall))
#include "rt.h"
#include "../../src/k12builds.h"
#include "../../src/k12combo.h"
#ifndef K12B
#define K12B 0
#endif
static const char *kImg[3] = { "img12.bin", "img12_B.bin", "img12_C.bin" };
static u8 buf[K12_PREC], pl[K12_PREC], cp[K12_PREC];
void _start(void){
  int i,j,d,n, hidden=0, shown=0, pairs=0;
  g_k12 = &g_k12Builds[K12B];
  if(!load_img(kImg[K12B])) sys3(1,1,0,0);
  for(i=0;i<K12_ACC_NVT;i++) k12_visOrig[i]=*(u32*)(K12_ACC_VT0+i*0x10+8);
  n=k12_probe_all(buf);
  out("probed "); dec(n); out("  region"); for(d=0;d<5;d++){out(" ");hex(k12_region[d],8);} out("\n");
  for(i=0;i<K12_ACC_N;i++){ out(i%8?" ":"\n"); hex(i,2); out(":"); hex(k12_patWord[i]*4+0xA0,2); out("/"); hex(k12_own[i][k12_patWord[i]],8);} out("\n");
  for(i=0;i<K12_ACC_N;i++) for(j=0;j<K12_ACC_N;j++){
    const u32*ri=(const u32*)(K12_ACC_TABLE+i*K12_ACC_STRIDE);
    if(i==j||k12_same_own(i,j)) continue;
    { int ov=0; for(d=0;d<5;d++) if(k12_own[i][d]&k12_own[j][d]) ov=1; if(ov) continue; }
    memset(pl,0,K12_PREC);
    ((u32*)(pl+0xA0))[k12_patWord[i]] |= k12_pat[i];
    ((u32*)(pl+0xA0))[k12_patWord[j]] |= k12_pat[j];
    pairs++;
    if(k12_vis_of(ri)((void*)ri,pl)) continue;
    hidden++;
    if(k12_combo_decide(i,pl,cp)) shown++;
    else { out("still hidden "); hex(i,2); out(" with "); hex(j,2); out("\n"); }
    if (hidden<40) { out("  stock hides "); hex(i,2); out(" when "); hex(j,2); out(" equipped\n"); }
  }
  out("pairs "); dec(pairs); out(" stock-hidden "); dec(hidden); out(" shown by combo "); dec(shown); out("\n");
  sys3(1,0,0,0);
}
