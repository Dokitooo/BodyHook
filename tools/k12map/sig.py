import struct, re
BASE=0x400000
class Img:
    def __init__(s,path,tlo,thi):
        s.d=open(path,'rb').read(); s.tlo=tlo; s.thi=thi
    def u8(s,a): return s.d[a-BASE]
    def u32(s,a): return struct.unpack_from('<I',s.d,a-BASE)[0]
    def i32(s,a): return struct.unpack_from('<i',s.d,a-BASE)[0]
    def b(s,a,n): return s.d[a-BASE:a-BASE+n]
def mask_of(I,start,n):
    b=I.b(start,n); m=[True]*n
    i=0
    while i<n:
        if i+4<=n:
            v=struct.unpack_from('<I',b,i)[0]
            if 0x400000<=v<0x2C00000: m[i:i+4]=[False]*4; i+=4; continue
        # rel32 after E8/E9 or 0F 8x
        if b[i] in (0xE8,0xE9) and i+5<=n:
            t=start+i+5+struct.unpack_from('<i',b,i+1)[0]
            if I.tlo<=t<I.thi: m[i+1:i+5]=[False]*4; i+=5; continue
        if b[i]==0x0F and i+6<=n and 0x80<=b[i+1]<=0x8F:
            m[i+2:i+6]=[False]*4; i+=6; continue
        i+=1
    return b,m
def to_re(b,m):
    return re.compile(b''.join(re.escape(bytes([x])) if k else b'.' for x,k in zip(b,m)),re.DOTALL)
def find(T,b,m,lo=None,hi=None,maxhits=3):
    r=to_re(b,m); lo=lo or T.tlo; hi=hi or T.thi
    out=[]
    for mt in r.finditer(T.d,lo-BASE,hi-BASE):
        out.append(mt.start()+BASE)
        if len(out)>=maxhits: break
    return out
def resolve_at(A,T,x,back=0,lens=(16,24,32,48,64,96,128,192),lo=None,hi=None):
    """unique match of A's bytes [x-back, x-back+n) in T; returns mapped x"""
    for n in lens:
        b,m=mask_of(A,x-back,n)
        if sum(m)<8: continue
        h=find(T,b,m,lo,hi)
        if len(h)==1: return h[0]+back
        if not h: return None
    return None
