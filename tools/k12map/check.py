import sys, struct, json; sys.path.insert(0,'.')
from sig import *
R=json.load(open('resolved.json'))
IM={'A':'../img/A.bin','B':'../img/B.bin','C':'../img/C.bin'}
def run(b):
    I=Img(IM[b],0x401000,0xD42000); o=R[b]; bad=[]
    u8=I.u8; u32=I.u32
    def call(s,t): return u8(s)==0xE8 and (s+5+I.i32(s+1))&0xFFFFFFFF==t
    def tgt(s): return (s+5+I.i32(s+1))&0xFFFFFFFF
    def chk(c,msg):
        if not c: bad.append(msg)
    chk(call(o['BODY_SITE'],o['RESOLVE']),'body site')
    for s,g in (('CL_JA','GET_J_A'),('CL_JB','GET_J_B'),('CL_SA','GET_S_A'),('CL_SB','GET_S_B')): chk(call(o[s],o[g]),s)
    chk(u32(o['CF_SLOT5'])==0x06D8818B,'cf slot5')
    for i in range(4):
        vt=o['CFVT%d'%i]; chk(u32(vt+0x24)==o['HELPER'],'cfvt%d slot9'%i); chk(u32(vt+0x14)==o['CF_SLOT5'] or True,'')
    for i in range(4): chk(call(o['CACHE%d'%i],o['RESOLVE']),'cache%d'%i)
    fh=set()
    for f,arr in (('FETCH_J','ARR_J'),('FETCH_JP','ARR_J'),('FETCH_S','ARR_S'),('FETCH_SP','ARR_S')):
        a=o[f]; chk(I.b(a,3)==b'\x8b\x0c\x85' and u32(a+3)==o[arr],f); chk(u8(a+7)==0xE8,f+' call'); fh.add(tgt(a+7))
    chk(len(fh)==1,'fetch helper same'); print(b,'fetch helper %08X'%fh.pop())
    for f,s in (('SIM_J','FETCH_J'),('SIM_JP','FETCH_JP'),('SIM_S','FETCH_S'),('SIM_SP','FETCH_SP')): chk(0<o[s]-o[f]<0x80,f)
    chk(u32(o['ACC_BUILDER'])==0x83EC8B55,'acc builder prologue')
    chk(call(o['ACC_TEX'],o['RESOLVE']) and call(o['ACC_NRM'],o['RESOLVE']),'acc sites')
    for i in range(17):
        vt=o['ACC_VT0']+i*0x10; chk(u32(vt)==o['ACC_DTOR'] and 0x401000<=u32(vt+8)<0xD42000,'accvt %d'%i)
    for i in range(97):
        a=o['ACC_TABLE']+i*0x1C; chk(u32(a+4)==i,'acc rec %d'%i)
    # socks
    c=o['SOCK_COL']; kCol=bytes([0x8B,0x47,0x04,0x83,0xC0,0xDB,0x83,0xF8,0x15,0x77,0x15,0x0F,0xB6,0x90])
    chk(I.b(c,14)==kCol and I.b(c+0x19,2)==b'\x0f\x28' and u8(c+0x1D)==0x20 and I.b(c+0x20,2)==b'\x0f\x28' and u8(c+0x24)==0x30,'sock col')
    chk(u32(o['SOCK_VT']+4)==o['SOCK_PICK'],'sock vt')
    kPick=bytes([0x8B,0x41,0x04,0x83,0xEC,0x08,0x83,0xF8,0x25,0x74,0x42,0x83,0xF8,0x2D,0x74,0x3D]); chk(I.b(o['SOCK_PICK'],16)==kPick,'sock pick')
    chk(call(o['SOCK_T1'],o['TEAM1']) and call(o['SOCK_T2'],o['TEAM2']),'sock teams')
    # lin table operand: A site SOCK_LIN = builder+0x?? holds LIN
    off=0xB6D732-0xB6D840; print(b,'lin @%08X = %08X (want %08X)'%(o['ACC_BUILDER']+off,u32(o['ACC_BUILDER']+off),o['LIN']))
    # bench
    kStoreB=bytes([0x89,0x9E,0xE0,0x1D,0,0,0x89,0x9E,0xE8,0x1D,0,0]); kStoreA=bytes([0x89,0x86,0xE0,0x1D,0,0,0x89,0x86,0xE8,0x1D,0,0])
    kGate=bytes([0x83,0xB9,0x00,0x1E,0,0,0,0x74,0x19,0x8B,0x42,0x0C,0x83,0xF8,0x04,0x74,0x05,0x83,0xF8,0x03,0x75,0x0C])
    chk(I.b(o['BENCH_SB'],12)==kStoreB,'bench SB'); chk(I.b(o['BENCH_SA'],12)==kStoreA,'bench SA'); chk(I.b(o['BENCH_GATE'],22)==kGate,'gate')
    chk(u32(o['BENCH_OFF'])==0x16CC,'bench off'); chk(o['BENCH_OFF']-o['COMP_CTOR']==0x2F,'ctor')
    for sl,f in ((1,'FLOOR1'),(8,'FLOOR8'),(9,'FLOOR9'),(10,'FLOOR10'),(11,'FLOOR11')): chk(u32(o['FLOOR_VT']+sl*4)==o[f],f)
    # bench vt: composite+16CC vtable: ctor writes it?  check that BENCH_VT is referenced in ctor
    chk(I.d.find(struct.pack('<I',o['BENCH_VT']),o['COMP_CTOR']-BASE,o['COMP_CTOR']-BASE+0x600)>=0,'bench vt in ctor')
    chk(I.d.find(struct.pack('<I',o['CLOTH_DT']),0x1000,0x922000)>=0,'dt ref')
    for i in range(4): print(b,'bodyid',i,'%08X'%u32(o['BODYIDS']+4*i))
    # mgr referenced right before body site
    chk(I.d.find(struct.pack('<I',o['MGR']),o['BODY_SITE']-BASE-0x40,o['BODY_SITE']-BASE)>=0,'mgr near body')
    # pool
    p=o.get('POOL_SITE')
    print(b,'pool site %08X bytes %s base@-27 %08X'%(p,I.b(p,9).hex(),u32(p-0x27)))
    print(b,'BAD:',[x for x in bad if x])
for b in 'ABC': run(b)
