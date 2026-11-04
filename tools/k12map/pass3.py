import sys, struct, json, collections; sys.path.insert(0,'.')
from sig import *; from syms import *
A=Img('../img/A.bin',0x401000,0xD4161B)
T={'B':Img('../img/B.bin',0x401000,0xD23000),'C':Img('../img/C.bin',0x401000,0xD22000)}
P1=json.load(open('pass1.json'))
def allpos(I,v,lo,hi,align=4):
    p=struct.pack('<I',v); r=[]; k=I.d.find(p,lo-BASE,hi-BASE)
    while k>=0:
        if k%align==0: r.append(k+BASE)
        k=I.d.find(p,k+1,hi-BASE)
    return r
A_hi=0x401000+len(A.d)
print('A helper vt', ['%08X'%(x-0x24) for x in allpos(A,CODE['HELPER'],0xD42000,0x1D88000)])
print('A accdtor', ['%08X'%x for x in allpos(A,CODE['ACC_DTOR'],0xD42000,0x1D88000)][:20])
for nm,I in T.items():
    h=P1[nm]['HELPER']; d=P1[nm]['ACC_DTOR']
    print(nm,'helper vt',['%08X'%(x-0x24) for x in allpos(I,h,0xD23000,0x1D11000)])
    ad=allpos(I,d,0xD23000,0x1D11000); print(nm,'accdtor',['%08X'%x for x in ad][:20], len(ad))
    # acc table: records with +4 = 0..96, stride 0x1C, vt = ad[0] + k*0x10
    vts=set(ad)
    for a in range(0xD23000,0x1D11000-0x1C*97,4):
        o=a-BASE
        if struct.unpack_from('<I',I.d,o+4)[0]!=0 or struct.unpack_from('<I',I.d,o)[0] not in vts: continue
        if all(struct.unpack_from('<I',I.d,o+0x1C*i+4)[0]==i and struct.unpack_from('<I',I.d,o+0x1C*i)[0] in vts for i in range(97)):
            print(nm,'ACC_TABLE %08X'%a)
    # sock vt: acc vt whose slot1 == SOCK_PICK
    sp=P1[nm]['SOCK_PICK']; print(nm,'sockvt',['%08X'%(x-4) for x in allpos(I,sp,0xD23000,0x1D11000)])
# A reference
for a in range(0xD42000,0x1D88000-0x1C*97,4):
    o=a-BASE
    if struct.unpack_from('<I',A.d,o+4)[0]==0 and struct.unpack_from('<I',A.d,o)[0]==0xE5FC24 and all(struct.unpack_from('<I',A.d,o+0x1C*i+4)[0]==i for i in range(97)): print('A ACC_TABLE %08X'%a)
