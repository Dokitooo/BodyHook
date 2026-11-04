/* ------------------------------------------------------------------------------------------ */
/* CLOTH POOL GUARD (1.11.4 / 1.17.1)                                                          */
/* The game keeps moving-cloth objects for the 10 players on the floor in a pool                */
/* (2K14 [0x01CB6AA8] x 0x18: shorts obj, shorts mesh, jersey obj, jersey mesh, player record;  */
/*  2K12 [0x01A22060] x 0x1C: ... jersey mesh +0C, extra obj +10, player record +14).            */
/* The update (2K14 0x006B3520, 2K12 0x006FCE80) rebuilds a slot's objects only when the       */
/* PLAYER RECORD pointer changes. Practice mode (and some menus) reuse one record for another   */
/* player: the old player's moving cloth stays on the new player. When the new player draws a  */
/* static (non-moving, png) jersey/shorts - or another body size - the old simulation is        */
/* applied to a different mesh -> huge stretched triangles across the court.                  */
/* Fix: the record compare also checks who is in the record (png CFID, body word, uniform).    */
/* When that changed, the slot's mesh fields are set to a value no mesh has, so the game frees */
/* the old objects and builds new ones (through the cloth bridge -> static for png uniforms).  */
/* ------------------------------------------------------------------------------------------ */
#define POOL_SLOTS 64
static u32 g_poolSig[POOL_SLOTS];
static u32 g_poolSame = 0, g_poolDiff = 0, g_poolBasePtr = 0, g_poolStride = 0;
static volatile u32 st_poolRebuilds = 0;

static u32 pool_sig(u32 player)
{
    u32 bw = 0, uni = 0;
    read32(player + P_offBodyWord, &bw);
    read32(player + P_offUniform, &uni);
    return (player_cfid((u8*)player) * 2654435761u) ^ bw ^ (uni << 7) ^ (uni >> 25) ^ 0x9E3779B9u;
}

/* 1 = same record and same person: keep the slot. 0 = let the game (re)build it. */
static int pool_same(u32 *entry, u32 player, u32 recOff, u32 meshA, u32 meshB)
{
    u32 base = 0, idx, sig;
    if (*(u32*)((u8*)entry + recOff) != player) return 0;
    if (!cfg_enabled || !cfg_cloth || !read32(g_poolBasePtr, &base) || !base || (u32)entry < base) return 1;
    idx = ((u32)entry - base) / g_poolStride;
    if (idx >= POOL_SLOTS) return 1;
    sig = pool_sig(player);
    if (g_poolSig[idx] == sig) return 1;
    if (!g_poolSig[idx]) { g_poolSig[idx] = sig; return 1; }          /* first look at this slot */
    g_poolSig[idx] = sig;
    *(u32*)((u8*)entry + meshA) = 1;                                  /* no mesh is at 1: rebuild */
    *(u32*)((u8*)entry + meshB) = 1;
    st_poolRebuilds++;
    if (cfg_log) {
        EnterCriticalSection(&g_logLock);
        blog("CLOTHPOOL slot %u: record %08X now holds CFID %s - its cloth is rebuilt", idx, player, cfid_str(player_cfid((u8*)player)));
        LeaveCriticalSection(&g_logLock);
    }
    return 0;
}
static void pool_diag(u32 *entry, u32 player, u32 recOff)
{
    static u32 last[POOL_SLOTS][3];
    u32 base = 0, idx, cf;
    if (!cfg_diagnostics || !read32(g_poolBasePtr, &base) || !base || (u32)entry < base) return;
    idx = ((u32)entry - base) / g_poolStride;
    if (idx >= POOL_SLOTS) return;
    cf = player_cfid((u8*)player);
    if (last[idx][0] == player && last[idx][1] == cf && last[idx][2] == *(u32*)((u8*)entry + recOff)) return;
    last[idx][0] = player; last[idx][1] = cf; last[idx][2] = *(u32*)((u8*)entry + recOff);
    EnterCriticalSection(&g_logLock);
    blog("POOLDIAG slot %u: player %08X CFID %s (slot had record %08X) | shorts obj %08X mesh %08X | jersey obj %08X mesh %08X",
         idx, player, cfid_str(cf), *(u32*)((u8*)entry + recOff), entry[0], entry[1], entry[2], entry[3]);
    LeaveCriticalSection(&g_logLock);
}
int __cdecl PoolSame14(u32 *entry, u32 player)
{
    int r;
    pool_diag(entry, player, 0x10);
    r = pool_same(entry, player, 0x10, 0x04, 0x0C);
    if (!r && entry[4] != player) {                                    /* new record: remember who */
        u32 base = 0, idx;
        if (read32(g_poolBasePtr, &base) && base && (u32)entry >= base && (idx = ((u32)entry - base) / g_poolStride) < POOL_SLOTS)
            g_poolSig[idx] = pool_sig(player);
    }
    return r;
}

/* 2K14: replaces "cmp [esi+10h],edi / je same" at 0x006B3558 (esi = slot, edi = player) */
__declspec(naked) void PoolCheck14(void)
{
    __asm {
        pushad
        push edi
        push esi
        call PoolSame14
        add  esp, 8
        test eax, eax
        popad
        jz   diff14
        jmp  dword ptr [g_poolSame]
    diff14:
        jmp  dword ptr [g_poolDiff]
    }
}

static void install_poolguard14(void)
{
    static const u8 k[] = { 0x8B,0x7D,0x00, 0x0B,0xC1, 0x39,0x7E,0x10, 0x74,0x66, 0x53 };
    const u32 a = 0x006B3553u;
    if (!cfg_enabled || !cfg_cloth) return;
    if (!readable((void*)a, sizeof(k)) || !match((const u8*)a, k, sizeof(k)) ||
        *(u32*)0x006B352Fu != 0x01CB6AA8u) {
        blog("cloth pool guard: pool update not found (not the RLD build) - not installed");
        return;
    }
    g_poolBasePtr = 0x01CB6AA8u; g_poolStride = 0x18;
    g_poolSame = 0x006B35C3u; g_poolDiff = 0x006B355Du;
    if (patch_jmp(a + 5, (u32)&PoolCheck14))
        blog("CLOTH POOL GUARD INSTALLED: 006B3558 - a reused player record (practice mode) gets its own cloth");
}

/* ---- NBA 2K12 ---- */
int __cdecl PoolSame12(u32 *entry, u32 player)
{
    int r;
    pool_diag(entry, player, 0x14);
    r = pool_same(entry, player, 0x14, 0x04, 0x0C);
    if (!r && entry[5] != player) {
        u32 base = 0, idx;
        if (read32(g_poolBasePtr, &base) && base && (u32)entry >= base && (idx = ((u32)entry - base) / g_poolStride) < POOL_SLOTS)
            g_poolSig[idx] = pool_sig(player);
    }
    return r;
}
/* 2K12: replaces "mov ecx,[esi+14h] / cmp ecx,[edi]" at 0x006FCEB6; same -> je target 0x006FCF3B,
 * otherwise 0x006FCEBD (test ecx,ecx ...) with ecx = [esi+14h] */
__declspec(naked) void PoolCheck12(void)
{
    __asm {
        pushad
        push dword ptr [edi]
        push esi
        call PoolSame12
        add  esp, 8
        test eax, eax
        popad
        mov  ecx, dword ptr [esi + 0x14]
        jz   diff12
        jmp  dword ptr [g_poolSame]
    diff12:
        jmp  dword ptr [g_poolDiff]
    }
}
static void install_poolguard12(void)
{
    /* 1.11.21-ML: the site is found in the running exe (any 2K12 build): "mov ecx,[esi+14h] / cmp ecx,[edi] /
     * je +7E / test ecx,ecx"; its function loads the pool base ("mov reg,[base]") 0x27 bytes before. */
    static const u8 k[] = { 0x8B,0x4E,0x14, 0x3B,0x0F, 0x74,0x7E, 0x85,0xC9 };
    u8 *p, *q;
    u32 a, base;
    if (!cfg_enabled || !cfg_cloth) return;
    p = find_text(k, sizeof(k), 0);
    q = p ? find_text(k, sizeof(k), p + 1) : 0;
    if (!p || q) { blog("cloth pool guard: 2K12 pool update %s - not installed", p ? "found twice" : "not found"); return; }
    a = (u32)p; base = *(u32*)(a - 0x27);
    if (base < g_imageBase || base >= g_imageBase + g_exeImageSize || in_text(base)) {
        blog("cloth pool guard: pool base at %08X not as expected (%08X) - not installed", a - 0x27, base); return;
    }
    g_poolBasePtr = base; g_poolStride = 0x1C;
    g_poolSame = a + 7 + 0x7E; g_poolDiff = a + 7;
    if (patch_jmp(a, (u32)&PoolCheck12))
        blog("CLOTH POOL GUARD INSTALLED: %08X (pool %08X) - a reused player record (practice mode) gets its own cloth", a, base);
}
