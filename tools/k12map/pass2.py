import sys, struct, json, collections; sys.path.insert(0,'.')
from sig import *; from syms import *
A=Img('../img/A.bin',0x401000,0xD4161B)
T={'B':Img('../img/B.bin',0x401000,0xD23000),'C':Img('../img/C.bin',0x401000,0xD22000)}
def refs(I,v,lo,hi,maxn=40):
    p=struct.pack('<I',v); r=[]; k=I.d.find(p,lo-BASE,hi-BASE)
    while k>=0 and len(r)<maxn: r.append(k+BASE); k=I.d.find(p,k+1,hi-BASE)
    return r
res={k:{} for k in T}
for name,D in DATA.items():
    rs=refs(A,D,0x401000,0xD4161B)
    for nm,I in T.items():
        votes=collections.Counter()
        for r in rs[:25]:
            for back in (8,16,24):
                m=resolve_at(A,I,r,back)
                if m: votes[I.u32(m)]+=1; break
        res[nm][name]=votes.most_common(3)
    print('%-10s A %08X refs %2d  B %s  C %s'%(name,D,len(rs),' '.join('%08X:%d'%v for v in res['B'][name]),' '.join('%08X:%d'%v for v in res['C'][name])))
json.dump({n:{k:(v[0][0] if v else None) for k,v in res[n].items()} for n in res},open('pass2.json','w'))
