import struct,re,sys
H=0xABED212B
def hb(d,a,pat):
    b=d[a-0x400000:a-0x400000+64]
    for i,t in enumerate(pat.split()):
        if t!='??' and b[i]!=int(t,16): return False
    return True
for nm,p,thi in (('A','../img/A.bin',0xD42000),('B','../img/B.bin',0xD23000),('C','../img/C.bin',0xD23000)):
    d=open(p,'rb').read(); u=lambda a: struct.unpack_from('<I',d,a-0x400000)[0]
    k=d.find(struct.pack('<I',H),thi-0x400000); item=None
    while k>=0:
        a=k+0x400000
        if u(a-4)==1 and u(a+12)==4 and 0x401000<=u(a+36)<thi and u(a+44) and u(a+48): item=a-4; break
        k=d.find(struct.pack('<I',H),k+1)
    textFn=u(item+0x28); visObj=u(item+0x30); stepObj=u(item+0x34); visVt=u(visObj+4)
    vis0=u(visVt); inc=u(stepObj+4); dec=u(stepObj+8)
    VP="8B 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 85 C0 75 ?? A1"
    fmt=textFn+0x33; ft=(fmt+5+struct.unpack_from('<i',d,fmt+1-0x400000)[0])&0xffffffff
    print(nm,'item %08X text %08X vis0 %s txt %s fmt %s %s inc %s dec %s key %s'%(item,textFn,hb(d,vis0,VP),hb(d,textFn,"83 EC 14 8B C2 89 0C 24"),
      d[fmt-0x400000]==0xE8, hb(d,ft,"8B 44 24 04 89 44 24 04 85 C0"), hb(d,inc,"53 56 57 8B F9 0F B7 B7 9E 00 00 00"), hb(d,dec,"53 56 57 8B F1 0F B7 BE 9E 00 00 00"), d[textFn+0x14-0x400000]==0xBA))
