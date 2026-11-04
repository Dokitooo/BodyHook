import sys, struct, json; sys.path.insert(0,'.')
from sig import *; from syms import *
P1=json.load(open('pass1.json'))
R={'A':dict(CODE,**SITES,**DATA)}
for b in 'BC':
    r=dict(P1[b])
    r.update(MGR=0x01CA2590, ARR_J=0x01C866DC, ARR_S=0x01C8685C, CLOTH_DT=0x01984F10, POOL_BASE=0x01984F00,
             ACC_TABLE=0x011BFB8C, LIN=0x00E507E0 if b=='B' else 0x00E50820)
    R[b]=r
R['B'].update(CFVT0=0xDB5790,CFVT1=0xDB8C90,CFVT2=0xE3A308,CFVT3=0xE458B4,ACC_VT0=0xE3C554,SOCK_VT=0xE3C5D4,
              BENCH_VT=0xE3EEA4,FLOOR_VT=0xE3EF0C,DETAIL=0x013222E4,BODYIDS=0xE3CBD4)
R['C'].update(CFVT0=0xDB57E0,CFVT1=0xDB8CD0,CFVT2=0xE3A338,CFVT3=0xE45914,ACC_VT0=0xE3C594,SOCK_VT=0xE3C614,
              BENCH_VT=0xE3EF04,FLOOR_VT=0xE3EF6C,DETAIL=0x013222E4,BODYIDS=0xE3CC04)
json.dump(R,open('resolved.json','w'),indent=0)
