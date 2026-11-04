import struct
H={0x0DAE36BF:'np',0x853D76ED:'team',0xA2FC0BF9:'spec'}
def scan(path,lo,hi):
    d=open(path,'rb').read(); r=[]
    for h in H:
        p=struct.pack('<I',h); k=d.find(p,lo-0x400000,hi-0x400000)
        while k>=0:
            a=k+0x400000; pre=d[k-4:k]
            kind='?'
            if pre[-1] in (0xBF,0xBA,0xBE,0xB9,0xB8,0xBB,0x68): kind='mov r/push %02X'%pre[-1]
            if pre==bytes([0xC7,0x44,0x24,0x0C]): kind='C7 [esp+0C]'
            elif pre[1:]==bytes([0xC7,0x44,0x24]): kind='C7 [esp+%02X]'%pre[0] if False else 'C7 44 24 %02X?'%pre[3]
            hb=d.find(struct.pack('<I',0x46A48CA9),k,k+0x30)
            r.append((a,H[h],kind,pre.hex(),'HEADBAND+%X'%(hb-k) if hb>=0 else ''))
            k=d.find(p,k+1,hi-0x400000)
    for x in sorted(r): print('%08X %-5s %-14s %s %s'%x)
    print(len(r))
import sys
scan(*sys.argv[1:2], int(sys.argv[2],16), int(sys.argv[3],16))
