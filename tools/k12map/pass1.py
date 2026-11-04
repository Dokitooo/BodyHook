import sys; sys.path.insert(0,'.')
from sig import *; from syms import *
A=Img('../img/A.bin',0x401000,0xD4161B)
T={'B':Img('../img/B.bin',0x401000,0xD23000),'C':Img('../img/C.bin',0x401000,0xD22000)}
out={k:{} for k in T}
for nm,I in T.items():
    for name,x in list(CODE.items())+list(SITES.items()):
        r=None
        for back in (0,8,16,32,0x40):
            r=resolve_at(A,I,x,back)
            if r: break
        out[nm][name]=r
for name in list(CODE)+list(SITES):
    print('%-12s A %08X  B %s  C %s'%(name,(CODE.get(name) or SITES.get(name)),*(('%08X'%out[n][name]) if out[n][name] else '--------' for n in 'BC')))
import json; json.dump(out,open('pass1.json','w'))
