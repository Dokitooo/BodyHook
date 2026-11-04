import json, sys
R=json.load(open('resolved.json'))
skip=lambda k: k.startswith('NP') or k.startswith('POOL') or k in ('FETCH_HELPER','SOCK_LIN')
keys=[k for k in R['A'] if not skip(k)]
TS={'A':0x4F219A21,'B':0x4E63057D,'C':0x4E84D35F}
NAME={'A':'NBA 2K12 1.0x patch (RLD)','B':'NBA 2K12 1.0 original (e.g. "NBA 2K12/2K25 REMASTERED.exe", nba2k13.exe)','C':'NBA 2K12 1.0 SecuROM retail (4E84D35F)'}
o=['/* k12builds.h - generated (tools/k12map): every NBA 2K12 address BodyHook uses, per exe build.',
   ' * Build A (RLD 1.0x patch) is the one the port was made on; B and C were mapped from it by masked code',
   ' * signatures, code references and table contents, and every value is checked again before patching. */',
   'typedef struct {','    u32 ts; const char *name;']
o+=['    u32 %s;'%k.lower() for k in keys]
o+=['} K12Build;','','static const K12Build g_k12Builds[3] = {']
for b in 'ABC':
    o.append('  { 0x%08Xu, "%s",'%(TS[b],NAME[b].replace('"','\\"')))
    vals=['0x%08Xu'%R[b][k] for k in keys]
    for i in range(0,len(vals),6): o.append('    '+', '.join(vals[i:i+6])+',')
    o.append('  },')
o+=['};','static const K12Build *g_k12 = &g_k12Builds[0];']
open(sys.argv[1],'w').write('\n'.join(o)+'\n')
print(keys)
