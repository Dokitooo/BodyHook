#define __fastcall __attribute__((fastcall))
#include "rt.h"
#include "../../src/k12builds.h"
#include "../../src/k12combo.h"
#ifndef K12B
#define K12B 0
#endif
static const char *kImg[3] = { "img12.bin", "img12_B.bin", "img12_C.bin" };
static u8 buf[K12_PREC], pl[K12_PREC], cp[K12_PREC];
static int shown(int i){ const u32*r=(const u32*)(K12_ACC_TABLE+i*K12_ACC_STRIDE); if(k12_vis_of(r)((void*)r,pl)) return 1; return k12_combo_decide(i,pl,cp); }
static int stock(int i){ const u32*r=(const u32*)(K12_ACC_TABLE+i*K12_ACC_STRIDE); return k12_vis_of(r)((void*)r,pl); }
void _start(void){
  int i,j,k,c,bad=0,tests=0,gain=0;
  g_k12 = &g_k12Builds[K12B];
  if(!load_img(kImg[K12B])) sys3(1,1,0,0);
  for(i=0;i<K12_ACC_NVT;i++) k12_visOrig[i]=*(u32*)(K12_ACC_VT0+i*0x10+8);
  k12_probe_all(buf);
  for(i=0;i<K12_ACC_N;i++) for(j=i+1;j<K12_ACC_N;j++){
    int sc[17]={0}, cc[17]={0};
    memset(pl,0,K12_PREC);
    ((u32*)(pl+0xA0))[k12_patWord[i]] |= k12_pat[i];
    ((u32*)(pl+0xA0))[k12_patWord[j]] |= k12_pat[j];
    tests++;
    for(k=0;k<K12_ACC_N;k++){ c=(*(u32*)(K12_ACC_TABLE+k*K12_ACC_STRIDE)-K12_ACC_VT0)/16; sc[c]+=stock(k); cc[c]+=shown(k); }
    for(c=0;c<17;c++){ if(cc[c]>sc[c]) gain++; if(cc[c]>sc[c] && sc[c]>0){ bad++; if(c!=0 && bad<400){out("more than stock: pats "); hex(i,2); out("+"); hex(j,2); out(" class "); dec(c); out(" stock "); dec(sc[c]); out(" combo "); dec(cc[c]); out("\n");} } }
  }
  out("tests "); dec(tests); out(" gains "); dec(gain); out(" classes with more pieces than stock (shirt sleeves next to arm sleeves, 2nd sock next to a leg sleeve) "); dec(bad); out("\n");
  sys3(1,0,0,0);
}
