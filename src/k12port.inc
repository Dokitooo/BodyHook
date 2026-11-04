/* ------------------------------------------------------------------------------------------ */
/* NBA 2K12 PORT (nba2k12.exe, RLD build, TimeDateStamp 4F219A21)                             */
/*                                                                                            */
/* 2K12 runs the same asset engine as 2K14 (ResolveAsset 0x00C864E0 is byte-identical to      */
/* 2K14's 0x00D94400, same container list / FindInContainer layout), so BodyHook's lookup      */
/* logic is shared. What differs is where the game asks, and the player record layout:        */
/*   player record   +0x9C bits 9..11 bodytype (2K14 +0x98 bits 25..27)                       */
/*                   +0x1DC cyberface object   (2K14 +0x1F0)                                  */
/*                   +0x1F4 uniform descriptor (2K14 +0x208): +04 jersey style bits 9..11,     */
/*                          +34 shorts style bits 30..31                                      */
/*                   +0x3A CF_ID (u16), +0x9C bit 8 = generic/created face (no png)            */
/*   cyberface obj   +0x08 valid flag, +0x6D8 own png container hash (vtbl slot 5 0x00B6E050) */
/*                   asset helper = VIRTUAL, vtbl slot 9 (0x00B6E080 -> 0x00B6EC70), modes as */
/*                   2K14 (0 any, 1 GLOBAL, 3 own png)                                        */
/* Hooks (each verified before patching, any mismatch = that feature stays stock):            */
/*   body      0x00B7A1C8 call ResolveAsset (player at [esp+24h] of the caller)               */
/*   legs +    vtbl slot 9 of the 4 cyberface classes (0x00DD7210, 0x00DDBB90, 0x00E5D7E8,    */
/*   headband  0x00E69224): leg tattoo / accessory TXTR requests in mode 0                    */
/*   uniforms  getters jersey A 0x00B69110 / B 0x00B69210, shorts A 0x00B69150 / B 0x00B69250, */
/*             called at 0x00B7A537 / 0x00B7A55E / 0x00B7A524 / 0x00B7A54B                     */
/*   cache     ResolveAsset calls of the uniform cache builder 0x00B68D59 .. 0x00B69073       */
/*   cloth sim ClothMesh arrays jersey 0x01D2422C / shorts 0x01D243AC, fetches 0x00B694D2,     */
/*             0x00B69538 / 0x00B69362, 0x00B693C2; builders 0x00B694A0 / 0x00B69330          */
/*             (player, style, out) and 0x00B694F0 / 0x00B69380 (player, out)                  */
/*   accessory material builder 0x00B6D840 ([ebp+8] = player): texture 0x00B6D906, normal    */
/*             map 0x00B6D925                                                                 */
/* ------------------------------------------------------------------------------------------ */
#define K12_TIMESTAMP   0x4F219A21u
#include "k12builds.h"
#define K12_RESOLVE     g_k12->resolve
#define K12_MGR         g_k12->mgr
#define K12_BODY_SITE   g_k12->body_site
#define K12_HELPER      g_k12->helper     /* original vtbl slot 9 (asset helper wrapper) */

static int k12_is_call(u32 site, u32 target)
{
    return readable((void*)site, 5) && *(u8*)site == 0xE8 && call_target_of(site) == target;
}

/* body call site: ecx = manager, edx = section, the player was stored at [esp+0Ch] before the 6 pushes */
__declspec(naked) void BodyLookupThunk12(void)
{
    __asm {
        push dword ptr [esp + 28h]
        push edx
        push ecx
        call BodyLookup
        add  esp, 12
        ret  0x18
    }
}

/* the cyberface asset helper (virtual in 2K12): legs and accessory textures, mode 0 */
static int is_leg_id(u32 id)
{
    int i;
    for (i = 0; i < cfg_legCount; i++) if (cfg_legIds[i] == id) return 1;
    return 0;
}
void *__fastcall HelperHook12(void *cf, u32 mode, u32 type, u32 fileId)
{
    int kind;
    if (mode == 0 && type == TYPE_TXTR && cf) {
        if (cfg_legs && is_leg_id(fileId)) return LegLookup(cf, mode, type, fileId);
        if (cfg_acc && acc_index(fileId, &kind) >= 0) return AccHelperLookup(cf, mode, type, fileId);
    }
    return g_assetHelper(cf, mode, type, fileId);
}

/* 2K12 builders that read the style from the player: (ecx = player, edx = out), return out */
static u32 k12_style(void *player, int kind)
{
    u32 uni = 0, w = 0;
    if (!read32((u32)player + P_offUniform, &uni) || !uni) return kind ? 0 : 1;
    if (kind == 0) return read32(uni + 4, &w) ? (w >> 9) & 7 : 1;
    return read32(uni + 0x34, &w) ? w >> 30 : 0;
}

static void *simp_common(int kind, void *player, void *out)
{
    u32 t0 = t_now();
    void *r = sim_core(kind, player, k12_style(player, kind), out, 1);
    t_done(T_SIM, t0);
    return r;
}
void *__fastcall ClothSimP0(void *p, void *out) { return simp_common(0, p, out); }
void *__fastcall ClothSimP1(void *p, void *out) { void *r = simp_common(1, p, out); sl_after_build(p, r); return r; }   /* 1.11.21-ML: Short Length */
static int k12_code_ready_for(const K12Build *k)
{
    return k12_is_call(k->body_site, k->resolve) && k12_is_call(k->cl_ja, k->get_j_a) &&
           readable((void*)k->helper_in, 16) && readable((void*)k->cf_slot5, 4) && *(u32*)k->cf_slot5 == 0x06D8818Bu;
}
/* 1.11.21-ML: the build with this exe's TimeDateStamp first, then any build whose code matches */
static int k12_code_ready(void)
{
    int i;
    for (i = 0; i < 3; i++) if (g_k12Builds[i].ts == g_exeTimestamp && k12_code_ready_for(&g_k12Builds[i])) { g_k12 = &g_k12Builds[i]; return 1; }
    for (i = 0; i < 3; i++) if (k12_code_ready_for(&g_k12Builds[i])) { g_k12 = &g_k12Builds[i]; return 1; }
    return 0;
}

static void k12_patch_vslot(u32 slotAddr, u32 expect, u32 fn, const char *what)
{
    u32 v = 0;
    if (!read32(slotAddr, &v) || v != expect) { blog("   WARNING %s: vtable slot %08X = %08X, expected %08X - not hooked", what, slotAddr, v, expect); return; }
    if (patch_bytes(slotAddr, &fn, 4, 0)) g_legSites++;
}

#include "k12combo.h"

/* ------------------------------------------------------------------------------------------ */
/* 2K12: NBA PATCH ON HEADBANDS ONLY                                                           */
/* The patch decal (unif_nbapatch 0DAE36BF, team variant 853D76ED, special A2FC0BF9) is asked  */
/* for by name at the jersey/shorts material sites, two menu sites and in the 14 sock records. */
/* All of them get all_black (D43CE53B, the game's own "no logo"); the headband site            */
/* 0x00B70120 / 0x00B7013F (followed by the headband texture 46A48CA9) is left alone.         */
/* ------------------------------------------------------------------------------------------ */
static void install_nbapatch12(void)
{
    /* 1.11.21-ML: the sites are found in the running exe (any 2K12 build): "mov edi,imm" (BF), "mov edx,imm" (BA)
     * or "mov dword [esp+0Ch],imm" (C7 44 24 0C) with one of the three patch decals. A site followed within
     * 0xA0 bytes by the headband texture 46A48CA9 is the headband's own and is kept. Build A: 16 + 2 kept. */
    static const u32 kHash[3] = { TX_NBAPATCH, 0x853D76EDu, 0xA2FC0BF9u };
    const u8 *p = g_textBase + 4, *end = g_textBase + g_textSize - 0xA8;
    int i, code = 0, data = 0, kept = 0;
    u32 nv = TX_ALLBLACK;
    if (!cfg_enabled || !cfg_nbaPatchHeadbandOnly) { blog("NBA patch headband-only: off ([NBAPatch] HeadbandOnly=0)"); return; }
    for (; p < end; p++) {
        u32 v = *(const u32*)p;
        int k, op, hb = 0;
        if (v != kHash[0] && v != kHash[1] && v != kHash[2]) continue;
        op = (p[-1] == 0xBF || p[-1] == 0xBA) ? 1 : (p[-4] == 0xC7 && p[-3] == 0x44 && p[-2] == 0x24 && p[-1] == 0x0C) ? 2 : 0;
        if (!op) continue;
        for (k = 4; k < 0xA0; k++) if (*(const u32*)(p + k) == 0x46A48CA9u) { hb = 1; break; }
        if (hb) { kept++; continue; }
        if (patch_bytes((u32)p, &nv, 4, 1)) code++;
        p += 3;
    }
    if (readable((void*)K12_ACC_TABLE, K12_ACC_N * K12_ACC_STRIDE))
        for (i = 0; i < K12_ACC_N; i++) {
            u32 a = K12_ACC_TABLE + i * K12_ACC_STRIDE;
            if (*(u32*)(a + 4) == (u32)i && *(u32*)(a + 0x14) == TX_NBAPATCH && patch_bytes(a + 0x14, &nv, 4, 0)) data++;
        }
    blog("NBA PATCH HEADBAND-ONLY INSTALLED: %d code site(s) and %d accessory decal(s) now use all_black, %d headband site(s) kept",
         code, data, kept);
}

/* ------------------------------------------------------------------------------------------ */
/* 2K12: ACCESSORY COMBINATIONS (k12combo.h). vtable slot 2 (IsVisible) of the 17 accessory    */
/* classes -> AccVisible12. The original is asked first; only an accessory the game hid gets  */
/* the second question on a copy of the player with the other slots cleared.                  */
/* ------------------------------------------------------------------------------------------ */
static u8 k12_probeBuf[K12_PREC], k12_copyBuf[K12_PREC];
static int g_k12Combos = 0;

int __fastcall AccVisible12(void *record, void *player)
{
    const u32 *rec = (const u32*)record;
    K12Vis_t vis = k12_vis_of(rec);
    u32 idx, t0;
    int res;
    if (!vis) return 0;
    res = vis(record, player);
    if (res || !player || !cfg_enabled || !cfg_accCombos) return res;
    idx = rec[1];
    if (idx >= K12_ACC_N || (u32)rec != K12_ACC_TABLE + idx * K12_ACC_STRIDE || !k12_pat[idx]) return 0;
    t0 = t_now();
    EnterCriticalSection(&g_logLock);               /* one shared copy buffer */
    res = k12_combo_decide((int)idx, (const u8*)player, k12_copyBuf);
    LeaveCriticalSection(&g_logLock);
    if (res) st_accShown++;
    t_done(T_COMBO, t0);
    return res;
}

static void install_combos12(void)
{
    int i, n = 0, probed;
    if (!cfg_enabled || !cfg_acc || !cfg_accCombos) {
        blog("accessory combinations: off (%s)", cfg_acc ? "AllowCombinations=0" : "[Accessories] Enabled=0");
        return;
    }
    if (!readable((void*)K12_ACC_VT0, K12_ACC_NVT * 0x10) || !readable((void*)K12_ACC_TABLE, K12_ACC_N * K12_ACC_STRIDE)) {
        blog("WARNING accessory combinations: accessory tables not readable - stay stock"); return;
    }
    for (i = 0; i < K12_ACC_NVT; i++) {
        u32 vt = K12_ACC_VT0 + i * 0x10, v = *(u32*)(vt + 8);
        if (*(u32*)vt != g_k12->acc_dtor || !in_text(v)) {
            blog("WARNING accessory combinations: accessory class %08X not as expected - stay stock", vt); return;
        }
        k12_visOrig[i] = v;
    }
    probed = k12_probe_all(k12_probeBuf);
    for (i = 0; i < K12_ACC_NVT; i++) {
        u32 nv = (u32)&AccVisible12;
        if (patch_bytes(K12_ACC_VT0 + i * 0x10 + 8, &nv, 4, 0)) n++;
    }
    g_k12Combos = n;
    blog("ACCESSORY COMBINATIONS INSTALLED: %d accessory class(es), %d of %d accessories have a slot "
         "(slot bits +A0 %08X +A4 %08X +A8 %08X). Accessories in different slots no longer hide each other.",
         n, probed, K12_ACC_N, k12_region[0], k12_region[1], k12_region[2]);
}

/* ------------------------------------------------------------------------------------------ */
/* 2K12: TEAM COLOUR SOCKS. Same as 2K14: the sock class's colour picker (vtable slot 1 of     */
/* class 0x00E5FCA4, 0x00B6CB60) -> SockColors, and the third (blue) colour of the material   */
/* builder 0x00B6D840 (record in EDI here, ESI in 2K14) -> SockBlueA12/B12.                   */
/* ------------------------------------------------------------------------------------------ */
__declspec(naked) void SockBlueA12(void)
{
    __asm {
        pushad
        sub    esp, 0x80
        movups xmmword ptr [esp + 0x10], xmm1
        movups xmmword ptr [esp + 0x20], xmm2
        movups xmmword ptr [esp + 0x30], xmm3
        movups xmmword ptr [esp + 0x40], xmm4
        movups xmmword ptr [esp + 0x50], xmm5
        movups xmmword ptr [esp + 0x60], xmm6
        movups xmmword ptr [esp + 0x70], xmm7
        mov    eax, esp
        push   eax
        push   dword ptr [ebp + 8]
        push   edi
        call   SockBlue
        add    esp, 12
        movups xmm1, xmmword ptr [esp + 0x10]
        movups xmm2, xmmword ptr [esp + 0x20]
        movups xmm3, xmmword ptr [esp + 0x30]
        movups xmm4, xmmword ptr [esp + 0x40]
        movups xmm5, xmmword ptr [esp + 0x50]
        movups xmm6, xmmword ptr [esp + 0x60]
        movups xmm7, xmmword ptr [esp + 0x70]
        test   eax, eax
        jnz    useA
        movups xmm0, xmmword ptr [esp + 0xC0]
        jmp    outA
    useA:
        movups xmm0, xmmword ptr [esp]
    outA:
        add    esp, 0x80
        popad
        jmp    dword ptr [g_sockCont]
    }
}
__declspec(naked) void SockBlueB12(void)
{
    __asm {
        pushad
        sub    esp, 0x80
        movups xmmword ptr [esp + 0x10], xmm1
        movups xmmword ptr [esp + 0x20], xmm2
        movups xmmword ptr [esp + 0x30], xmm3
        movups xmmword ptr [esp + 0x40], xmm4
        movups xmmword ptr [esp + 0x50], xmm5
        movups xmmword ptr [esp + 0x60], xmm6
        movups xmmword ptr [esp + 0x70], xmm7
        mov    eax, esp
        push   eax
        push   dword ptr [ebp + 8]
        push   edi
        call   SockBlue
        add    esp, 12
        movups xmm1, xmmword ptr [esp + 0x10]
        movups xmm2, xmmword ptr [esp + 0x20]
        movups xmm3, xmmword ptr [esp + 0x30]
        movups xmm4, xmmword ptr [esp + 0x40]
        movups xmm5, xmmword ptr [esp + 0x50]
        movups xmm6, xmmword ptr [esp + 0x60]
        movups xmm7, xmmword ptr [esp + 0x70]
        test   eax, eax
        jnz    useB
        movups xmm0, xmmword ptr [esp + 0xD0]
        jmp    outB
    useB:
        movups xmm0, xmmword ptr [esp]
    outB:
        add    esp, 0x80
        popad
        jmp    dword ptr [g_sockCont]
    }
}

static void install_socks12(void)
{
    /* mov eax,[edi+4] / add eax,-25h / cmp eax,15h / ja / movzx edx,byte [eax+tbl] / jmp [edx*4+tbl] /
       movaps xmm0,[esp+20h] / jmp / movaps xmm0,[esp+30h] */
    static const u8 kCol[] = { 0x8B,0x47,0x04,0x83,0xC0,0xDB,0x83,0xF8,0x15,0x77,0x15,0x0F,0xB6,0x90 };
    static const u8 kPick[] = { 0x8B,0x41,0x04,0x83,0xEC,0x08,0x83,0xF8,0x25,0x74,0x42,0x83,0xF8,0x2D,0x74,0x3D };
    const u32 col = g_k12->sock_col, vt = g_k12->sock_vt, pick = g_k12->sock_pick;
    const u8 *c = (const u8*)col;
    int i;
    if (!cfg_enabled || !cfg_socks) { blog("team colour socks: off (TeamColorSocks Enabled=0)"); return; }
    if (!readable(c, 0x30) || !match(c, kCol, sizeof(kCol)) || c[0x19] != 0x0F || c[0x1A] != 0x28 || c[0x1D] != 0x20 ||
        c[0x20] != 0x0F || c[0x21] != 0x28 || c[0x24] != 0x30 || !readable((void*)vt, 8) || *(u32*)(vt + 4) != pick ||
        !match((const u8*)pick, kPick, sizeof(kPick)) || !k12_is_call(g_k12->sock_t1, g_k12->team1) || !k12_is_call(g_k12->sock_t2, g_k12->team2) ||
        *(u32*)0x00B6D732u != 0x00E74860u) {
        blog("WARNING team colour socks: 2K12 code not as expected - socks stay stock");
        return;
    }
    g_sockPicker = pick; g_sockVtable = vt; g_sockCont = col + 0x25;
    g_team1 = g_k12->team1; g_team2 = g_k12->team2; g_linTable = g_k12->lin;
    {
        u32 nv = (u32)&SockColors;
        if (!patch_bytes(vt + 4, &nv, 4, 0) || !patch_jmp(col + 0x19, (u32)&SockBlueA12) || !patch_jmp(col + 0x20, (u32)&SockBlueB12)) {
            blog("WARNING team colour socks: patch failed"); return;
        }
    }
    g_tcVt[0] = vt; g_tcPick[0] = pick; g_tcVtCount = 1;
    if (cfg_tcAccCount) {
        int done = 0;
        for (i = 0; i < K12_ACC_NVT && g_tcVtCount < 32; i++) {
            u32 v = K12_ACC_VT0 + i * 0x10, pk;
            if (v == vt || !readable((void*)(v + 4), 4)) continue;
            pk = *(u32*)(v + 4);
            if (!in_text(pk)) continue;
            g_tcVt[g_tcVtCount] = v; g_tcPick[g_tcVtCount] = pk;
            {
                u32 nv = (u32)&TeamColors;
                if (patch_bytes(v + 4, &nv, 4, 0)) { g_tcVtCount++; done++; }
            }
        }
        blog("TEAM COLOUR ACCESSORIES: %d texture(s) listed, %d accessory class(es) hooked", cfg_tcAccCount, done);
    }
    blog("TEAM COLOUR SOCKS INSTALLED: style %d (%s), %d sock texture(s), DetectStockMasks=%d, picker %08X, class %08X, "
         "colour site %08X, team1 %08X, team2 %08X",
         cfg_sockStyle, cfg_sockStyle == 1 ? "red=team 1, green=team 2, blue=sock colour" : "red=sock colour, green=team 1, blue=team 2",
         cfg_sockCount, cfg_tcDetect, g_sockPicker, g_sockVtable, col, g_team1, g_team2);
}

/* ------------------------------------------------------------------------------------------ */
/* 2K12: HIGH-POLY BENCH (bench.inc, same scheme as 2K14)                                      */
/* Composite per player (ctor 0x00BCC9C0): PLAYERMODEL_FLOOR at +0, PLAYERMODEL_BENCH at       */
/* +16CC, REFLECTION at +1A44. Model for render state s = [comp+1DD4+(2s+1)*4]: 1 bench in a  */
/* replay (+1DE0), 2 bench live (+1DE8), 3 court replay (+1DF0), 4 court live (+1DF8).          */
/* The ctor stores the BENCH model for states 1/2 at 0x00BCCD08 (ebx) / 0x00BCCC6B (eax, the  */
/* other detail path) -> BenchStore12B/A -> BenchDecide12. Floor-only extras flag FLOOR+374:  */
/* gate 0x00BCC750 like 2K14's. Render node (2K14 offsets in brackets): +08 player, +0C state, */
/* +2C/+30 jersey/shorts cloth objects, +38 cloth time step [+34], +3C build-from-object flag  */
/* [+38], +94/+98/+9C no-garment / coach flags. Time source 0x01A22070 [0x01CB6AB8].           */
/* FLOOR vtable 0x00E62674: slots 1, 8, 9, 10, 11 = 2K14's 0, 14, 15, 16, 17.                  */
/* ------------------------------------------------------------------------------------------ */
#define K12_COMP_BENCH   0x16CC
#define K12_BENCH_VT     g_k12->bench_vt
#define K12_COMP_S1      0x1DE0
#define K12_COMP_S2      0x1DE8
#define K12_CLOTH_DT     g_k12->cloth_dt

void __cdecl BenchDecide12(u32 bench, u32 comp)
{
    u32 s1 = bench, s2 = bench;
    if (cfg_enabled) {
        if (cfg_benchLive) s2 = comp;
        if (cfg_benchReplay == 1 || (cfg_benchReplay == 2 && cfg_benchLive && (cfg_benchUniforms || !cloth_set_moving()))) s1 = comp;
    }
    *(u32*)(comp + K12_COMP_S1) = s1;
    *(u32*)(comp + K12_COMP_S2) = s2;
    st_benchComposites++;
    if (s1 == comp) st_benchReplayFloor++;
}
__declspec(naked) void BenchStore12B(void)       /* mode-0 path: bench model in EBX, composite in ESI */
{
    __asm {
        pushad
        push esi
        push ebx
        call BenchDecide12
        add  esp, 8
        popad
        ret
    }
}
__declspec(naked) void BenchStore12A(void)       /* mode-1 path: bench model in EAX */
{
    __asm {
        pushad
        push esi
        push eax
        call BenchDecide12
        add  esp, 8
        popad
        ret
    }
}

static int k12_is_player(u32 player)
{
    u32 cf = 0, vt = 0;
    if (!readable((void*)player, K12_PREC) || !read32(player + 0x1DC, &cf) || !cf || !read32(cf, &vt)) return 0;
    return vt == g_k12->cfvt0 || vt == g_k12->cfvt1 || vt == g_k12->cfvt2 || vt == g_k12->cfvt3;
}

static void bench_diag12(u32 m, u32 d, u32 state, int slot)
{
    u32 key, i, seen = 0, v[8];
    static const u16 off[8] = { 0x2C, 0x30, 0x34, 0x38, 0x3C, 0x94, 0x98, 0x9C };
    if (!cfg_benchDiag) return;
    for (i = 0; i < 8; i++) { v[i] = 0; read32(d + off[i], &v[i]); }
    key = (v[0] ? 1 : 0) | (v[1] ? 2 : 0) | (v[2] ? 4 : 0) | (v[4] ? 8 : 0) | (*(u32*)(m + 0x380) ? 16 : 0) | (*(u32*)(m + 0x374) ? 32 : 0);
    EnterCriticalSection(&g_logLock);
    for (i = 0; i < (u32)g_benchSeenN; i++)
        if (g_benchSeen[i].comp == m && g_benchSeen[i].state == state && g_benchSeen[i].key == key) seen = 1;
    if (!seen) {
        i = g_benchSeenN < 128 ? (u32)g_benchSeenN++ : (GetTickCount() & 127);
        g_benchSeen[i].comp = m; g_benchSeen[i].state = state; g_benchSeen[i].key = key;
        blog("BENCHDIAG model %08X state %u (%s) slot %d: node jersey %08X shorts %08X +34 %08X dt %08X build %u | +94 %u +98 %u +9C %u "
             "| FLOOR cloth=%u extras=%u",
             m, state, state == 1 ? "bench, replay" : state == 2 ? "bench, live" : state == 3 ? "court, replay" : "court, live",
             slot, v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], *(u32*)(m + 0x380), *(u32*)(m + 0x374));
    }
    LeaveCriticalSection(&g_logLock);
}

static u32 g_floorOrig12[12];
static u32 floor_call12(int slot, void *model, void *desc)
{
    u32 m = (u32)model, d = (u32)desc, state = 0, j = 0, s = 0, no = 0, player = 0, oJ = 0, oS = 0, saveFlag = 0, save3C = 0, save38 = 0, r;
    int patched = 0, fresh = 0;
    if (cfg_enabled && m && d && *(u32*)(m + K12_COMP_BENCH) == K12_BENCH_VT && read32(d + 0x0C, &state)) {
        u32 c1 = 0, c2 = 0;
        if (!cfg_benchCoaches && ((read32(d + 0x98, &c1) && c1) || (read32(d + 0x9C, &c2) && c2))) {
            /* coaches / no-garment people keep the stock bench model */
            if (*(u32*)(m + K12_COMP_S1) == m || *(u32*)(m + K12_COMP_S2) == m) {
                *(u32*)(m + K12_COMP_S1) = m + K12_COMP_BENCH;
                *(u32*)(m + K12_COMP_S2) = m + K12_COMP_BENCH;
                if (cfg_log) {
                    EnterCriticalSection(&g_logLock);
                    if (!seen_before(m ^ 0x04000000u, 0, 0, 0)) blog("BENCH    model %08X (node +98 %u, +9C %u: coach) keeps the stock bench model", m, c1, c2);
                    LeaveCriticalSection(&g_logLock);
                }
            }
            return ((ModelFn_t)g_floorOrig12[slot])(model, desc);
        }
        if (state >= 1 && state <= 4) bench_diag12(m, d, state, slot);
        if (state >= 1 && state <= 4) sl_node(d);   /* 1.11.21-ML: Short Length follows the drawn player (node +08 / +30 as 2K14) */
        if (cfg_benchUniforms && (state == 1 || state == 2) && read32(d + 0x2C, &j) && read32(d + 0x30, &s) &&
            !j && !s && read32(d + 0x94, &no) && !no && read32(d + 0x08, &player) && player && k12_is_player(player) &&
            bench_objects(player, d, &oJ, &oS, &fresh)) {
            *(u32*)(d + 0x2C) = oJ; *(u32*)(d + 0x30) = oS;
            if (fresh) { save3C = *(u32*)(d + 0x3C); *(u32*)(d + 0x3C) = 1; }   /* first frame: place the cloth */
            save38 = *(u32*)(d + 0x38);
            if (cfg_benchUniforms == 1 && !save38 && readable((void*)K12_CLOTH_DT, 4)) *(u32*)(d + 0x38) = *(u32*)K12_CLOTH_DT;
            if (cfg_benchUniforms == 1 && !*(u32*)(d + 0x38) && !fresh) { fresh = 1; save3C = *(u32*)(d + 0x3C); *(u32*)(d + 0x3C) = 1; }
            if (cfg_benchUniforms == 3) { saveFlag = *(u32*)(m + 0x380); *(u32*)(m + 0x380) = 0; }
            patched = 1;
            st_benchUniforms++;
        }
    }
    {
        u32 gJ = 0, gS = 0;
        int g = (!patched && m && d && state >= 1 && state <= 4) ? static_guard(d, &gJ, &gS) : 0;
        r = ((ModelFn_t)g_floorOrig12[slot])(model, desc);
        if (g) static_restore(d, g, gJ, gS);
    }
    if (patched) {
        if (*(u32*)(d + 0x2C) == oJ) *(u32*)(d + 0x2C) = 0;
        if (*(u32*)(d + 0x30) == oS) *(u32*)(d + 0x30) = 0;
        if (fresh) *(u32*)(d + 0x3C) = save3C;
        *(u32*)(d + 0x38) = save38;
        if (cfg_benchUniforms == 3) *(u32*)(m + 0x380) = saveFlag;
    }
    return r;
}
u32 __fastcall FloorFn12_1(void *m, void *d)  { return floor_call12(1, m, d); }
u32 __fastcall FloorFn12_8(void *m, void *d)  { return floor_call12(8, m, d); }
u32 __fastcall FloorFn12_9(void *m, void *d)  { return floor_call12(9, m, d); }
u32 __fastcall FloorFn12_10(void *m, void *d) { return floor_call12(10, m, d); }
u32 __fastcall FloorFn12_11(void *m, void *d) { return floor_call12(11, m, d); }

static void install_bench12(void)
{
    static const u8 kStoreB[12] = { 0x89,0x9E,0xE0,0x1D,0x00,0x00, 0x89,0x9E,0xE8,0x1D,0x00,0x00 };
    static const u8 kStoreA[12] = { 0x89,0x86,0xE0,0x1D,0x00,0x00, 0x89,0x86,0xE8,0x1D,0x00,0x00 };
    static const u8 kGate[22] = { 0x83,0xB9,0x00,0x1E,0x00,0x00,0x00,0x74,0x19,0x8B,0x42,0x0C,0x83,0xF8,0x04,0x74,0x05,0x83,0xF8,0x03,0x75,0x0C };
    static const int slots[5] = { 1, 8, 9, 10, 11 };
    const u32 expect[5] = { g_k12->floor1, g_k12->floor8, g_k12->floor9, g_k12->floor10, g_k12->floor11 };
    const u32 siteB = g_k12->bench_sb, siteA = g_k12->bench_sa, gate = g_k12->bench_gate, fvt = g_k12->floor_vt;
    u32 fns[5], mode = 0;
    int i, stores = 0, wraps = 0;
    fns[0] = (u32)&FloorFn12_1; fns[1] = (u32)&FloorFn12_8; fns[2] = (u32)&FloorFn12_9; fns[3] = (u32)&FloorFn12_10; fns[4] = (u32)&FloorFn12_11;
    P_clothCountJ = 28; P_clothCountS = 12;               /* 2K12 ClothMesh arrays 0x01D2422C / 0x01D243AC */
    if (!cfg_enabled || (!cfg_benchLive && cfg_benchReplay != 1)) { blog("high-poly bench: off in ini"); return; }
    if (!readable((void*)siteB, 12) || !readable((void*)siteA, 12) || !readable((void*)gate, 24) ||
        !match((const u8*)gate, kGate, sizeof(kGate)) || *(u32*)g_k12->bench_off != K12_COMP_BENCH) {
        blog("WARNING high-poly bench: 2K12 composite code not as expected - bench stays stock"); return;
    }
    {
        u8 b[12]; int k;
        if (match((const u8*)siteB, kStoreB, 12)) {
            b[0] = 0xE8; *(int*)(b + 1) = (int)((u32)&BenchStore12B - (siteB + 5));
            for (k = 5; k < 12; k++) b[k] = 0x90;
            if (patch_bytes(siteB, b, 12, 1)) stores++;
        }
        if (match((const u8*)siteA, kStoreA, 12)) {
            b[0] = 0xE8; *(int*)(b + 1) = (int)((u32)&BenchStore12A - (siteA + 5));
            for (k = 5; k < 12; k++) b[k] = 0x90;
            if (patch_bytes(siteA, b, 12, 1)) stores++;
        }
    }
    if (!stores) { blog("WARNING high-poly bench: composite store not found - bench stays stock"); return; }
    {
        static const u8 g[5] = { 0x85, 0xC0, 0x74, 0x0D, 0x90 };   /* floor extras for every non-zero state */
        patch_bytes(gate + 17, g, 5, 1);
    }
    if (cfg_benchUniforms && readable((void*)fvt, 0x30)) {
        for (i = 0; i < 5; i++) {
            u32 a = fvt + slots[i] * 4;
            if (*(u32*)a != expect[i]) continue;
            g_floorOrig12[slots[i]] = expect[i];
            if (patch_bytes(a, &fns[i], 4, 0)) wraps++;
        }
        blog("BENCH UNIFORMS: %d/5 model functions wrapped, mode %d (%s)%s", wraps, cfg_benchUniforms,
             cfg_benchUniforms == 1 ? "moving cloth for bench players" : cfg_benchUniforms == 2 ? "static, cloth path" :
             cfg_benchUniforms == 3 ? "static draw" : "off", cfg_benchDiag ? ", BENCHDIAG on" : "");
    }
    read32(g_k12->detail, &mode);
    blog("HIGH-POLY BENCH INSTALLED: composite %08X (%d of 2 detail paths%s), gate %08X. Live bench: %s. Replays: %s.",
         g_k12->comp_ctor, stores, mode >= 2 ? "; this detail mode uses the low-poly path, which stays stock" : "", gate,
         cfg_benchLive ? "high-poly" : "stock", cfg_benchReplay == 1 ? "high-poly (forced)" : cfg_benchReplay == 0 ? "stock" : "auto");
}

/* ------------------------------------------------------------------------------------------ */
/* 2K12: ARM PARTS UNDER ELBOW SLEEVES. 0x00B6FE80 shows all 10 arm segments of the body       */
/* (0x00B6FC00: bit 1 = part 879B0E62 "0-9", 0x20 = part 413EC657 "0-10", ...) and then hides  */
/* the segments of every visible accessory listed in {model hash, bits} at 0x00E5FE98 (a zero */
/* bits field ends the list). Elbow sleeve L 392137B8 -> 0x20, R 0EAF7929 -> 0x01; elbow pad L */
/* 140E9220 -> 0x20, R BD1F151A -> 0x01. Their model hash is replaced by one no model has, so  */
/* the entry never matches (bits stay non-zero so the list does not end early).               */
/* ------------------------------------------------------------------------------------------ */
/* 1.11.21-ML: data tables are found by their contents, so every 2K12 build works */
static u32 k12_find_data(const u32 *seq, int n)
{
    u32 a = ((u32)g_textBase + g_textSize + 0xFFF) & ~0xFFFu, end = g_imageBase + g_exeImageSize;
    for (; a + 4 * n <= end; a += 4) {
        int i;
        if ((a & 0xFFF) == 0 && !readable((void*)a, 0x1000)) { a += 0xFFC; continue; }
        if (*(u32*)a != seq[0]) continue;
        if ((a & 0xFFF) + 4 * n > 0x1000 && !readable((void*)a, 4 * n)) continue;
        for (i = 1; i < n && ((u32*)a)[i] == seq[i]; i++) ;
        if (i == n) return a;
    }
    return 0;
}

static void install_keeparm12(void)
{
    static const u32 kSeq[8] = { 0x392137B8u, 0x20, 0x0EAF7929u, 0x01, 0x140E9220u, 0x20, 0xBD1F151Au, 0x01 };
    static const char *what[4] = { "elbow sleeve L", "elbow sleeve R", "elbow pad L", "elbow pad R" };
    const u32 none = 0x4B415231u;
    u32 tab;
    int i, n = 0, want;
    if (!cfg_enabled || !cfg_keepArmElbow) { blog("arm parts under elbow sleeves: stock (KeepArmUnderElbowSleeve=0)"); return; }
    want = cfg_keepArmElbow >= 2 ? 4 : 2;
    tab = k12_find_data(kSeq, 8);
    if (!tab) { blog("WARNING arm parts under elbow sleeves: accessory table not found in this exe - stock"); return; }
    for (i = 0; i < want; i++)
        if (patch_bytes(tab + 8 * i, &none, 4, 0)) n++; else blog("   WARNING %s entry at %08X not patched", what[i], tab + 8 * i);
    blog("ARM PARTS UNDER ELBOW SLEEVES: table %08X, %d accessory entr%s changed (%s) - body parts 0-9 / 0-10 stay visible",
         tab, n, n == 1 ? "y" : "ies", want == 4 ? "elbow sleeves + elbow pads" : "elbow sleeves");
}

static void install_2k12(void)
{
    const u32 vt[4] = { g_k12->cfvt0, g_k12->cfvt1, g_k12->cfvt2, g_k12->cfvt3 };
    const struct { u32 site, getter; int slot; } kCloth[4] = {
        { g_k12->cl_ja, g_k12->get_j_a, 0 }, { g_k12->cl_jb, g_k12->get_j_b, 1 },   /* jersey A (moving) / B (static_hi) */
        { g_k12->cl_sa, g_k12->get_s_a, 2 }, { g_k12->cl_sb, g_k12->get_s_b, 3 } }; /* shorts A / B */
    const u32 kCache[4] = { g_k12->cache0, g_k12->cache1, g_k12->cache2, g_k12->cache3 };
    int i, n;
    u32 t;

    g_mgr = K12_MGR;
    g_resolve = (ResolveAsset_t)K12_RESOLVE;
    g_assetHelper = (AssetHelper_t)K12_HELPER;

    /* 1. body */
    g_callSite = K12_BODY_SITE;
    if (patch_call(K12_BODY_SITE, (u32)&BodyLookupThunk12)) {
        g_hooked = 1;
        blog("HOOK INSTALLED: %08X now calls BodyLookup (%08X). Per-cyberface bodies are active.", K12_BODY_SITE, (u32)&BodyLookupThunk12);
    } else blog("ERROR body hook: VirtualProtect failed at %08X", K12_BODY_SITE);

    /* 2. legs + headband (asset helper vtable slot 9) */
    if (cfg_legs || cfg_acc) {
        for (i = 0; i < 4; i++) k12_patch_vslot(vt[i] + 0x24, K12_HELPER, (u32)&HelperHook12, "asset helper");
        if (g_legSites) blog("LEG + HEADBAND HOOKS INSTALLED: asset helper (vtable slot 9) of %d cyberface class(es). Per-cyberface leg tattoos are active.", g_legSites);
    }

    /* 3. uniforms */
    if (cfg_cloth) {
        for (i = 0; i < 4; i++) {
            u32 fn = i == 0 ? (u32)&ClothGet0 : i == 1 ? (u32)&ClothGet1 : i == 2 ? (u32)&ClothGet2 : (u32)&ClothGet3;
            if (!k12_is_call(kCloth[i].site, kCloth[i].getter)) { blog("   WARNING uniform call site %08X not as expected", kCloth[i].site); continue; }
            g_clothGetter[kCloth[i].slot] = kCloth[i].getter;
            if (patch_call(kCloth[i].site, fn)) {
                g_clothSites++;
                blog("uniform call site     : %08X -> %s model getter %08X", kCloth[i].site, kCloth[i].slot >= 2 ? "shorts" : "jersey", kCloth[i].getter);
            }
        }
        if (g_clothSites == 4) blog("UNIFORM HOOKS INSTALLED: jerseys 2, shorts 2. Per-cyberface jersey/shorts models are active.");
        else blog("WARNING uniform hooks: only %d of 4 call sites", g_clothSites);

        /* shared-cache guard */
        if (cfg_cacheNoPng) {
            for (i = n = 0; i < 4; i++)
                if (k12_is_call(kCache[i], K12_RESOLVE) && patch_call(kCache[i], (u32)&ResolveNoPng)) {
                    n++; blog("uniform cache lookup  : %08X -> skips png containers", kCache[i]);
                }
            g_noPngSites = n;
            blog("SHARED-CACHE GUARD INSTALLED: %d lookup(s).", n);
        }

        /* cloth-simulation bridge */
        {
            const struct { u32 fn, fetch1, fetch2, arr; int kind, pstyle; } kSim[4] = {
                { g_k12->sim_j, g_k12->fetch_j, 0, g_k12->arr_j, 0, 1 }, { g_k12->sim_jp, g_k12->fetch_jp, 0, g_k12->arr_j, 0, 0 },
                { g_k12->sim_s, g_k12->fetch_s, 0, g_k12->arr_s, 1, 1 }, { g_k12->sim_sp, g_k12->fetch_sp, 0, g_k12->arr_s, 1, 0 } };
            u8 *p, *end = g_textBase + g_textSize - 8;
            int ok = 1;
            for (i = 0; i < 4; i++) {
                const u8 *f = (const u8*)kSim[i].fetch1;
                if (!readable((void*)f, 12) || f[0] != 0x8B || f[1] != 0x0C || f[2] != 0x85 || *(u32*)(f + 3) != kSim[i].arr ||
                    f[7] != 0xE8 || !in_text(call_target_of(kSim[i].fetch1 + 7))) { ok = 0; blog("   WARNING cloth builder %08X not as expected", kSim[i].fn); }
            }
            if (ok && cfg_clothBridge) {
                g_simFn[0] = g_k12->sim_j; g_simArr[0] = g_k12->arr_j; g_simFetch[0] = g_k12->fetch_j;
                g_simFn[1] = g_k12->sim_s; g_simArr[1] = g_k12->arr_s; g_simFetch[1] = g_k12->fetch_s;
                g_simFnP[0] = g_k12->sim_jp; g_simFnP[1] = g_k12->sim_sp;
                for (i = 0; i < 4; i++) {
                    u8 *f = (u8*)kSim[i].fetch1, code[7];
                    u32 stub = kSim[i].kind ? (u32)&ClothFetch1 : (u32)&ClothFetch0;
                    code[0] = 0xE8; *(int*)(code + 1) = (int)(stub - ((u32)f + 5)); code[5] = 0x90; code[6] = 0x90;
                    patch_bytes((u32)f, code, 7, 1);
                }
                for (p = g_textBase; p < end; p++) {
                    if (p[0] != 0xE8) continue;
                    t = call_target_of((u32)p);
                    if (t == g_k12->sim_j) { if (patch_call((u32)p, (u32)&ClothSim0)) g_simSites++; }
                    else if (t == g_k12->sim_s) { if (patch_call((u32)p, (u32)&ClothSim1)) g_simSites++; }
                    else if (t == g_k12->sim_jp) { if (patch_call((u32)p, (u32)&ClothSimP0)) g_simSites++; }
                    else if (t == g_k12->sim_sp) { if (patch_call((u32)p, (u32)&ClothSimP1)) g_simSites++; }
                }
                blog("cloth builder         : %08X / %08X (jersey, ClothMesh array %08X)", g_k12->sim_j, g_k12->sim_jp, g_k12->arr_j);
                blog("cloth builder         : %08X / %08X (shorts, ClothMesh array %08X)", g_k12->sim_s, g_k12->sim_sp, g_k12->arr_s);
                blog("CLOTH-SIM BRIDGE INSTALLED: %d call site(s), per-thread (shared cloth data is never modified).", g_simSites);
                install_poolguard12();
            }
        }
    } else blog("uniform models: disabled in ini");

    /* 4. accessory textures + normal maps */
    if (cfg_acc) {
        const u32 kAcc[2] = { g_k12->acc_tex, g_k12->acc_nrm };
        if (readable((void*)g_k12->acc_builder, 8) && *(u32*)g_k12->acc_builder == 0x83EC8B55u) {
            g_accBuilder = g_k12->acc_builder;
            for (i = 0; i < 2; i++)
                if (k12_is_call(kAcc[i], K12_RESOLVE) && patch_call(kAcc[i], (u32)&AccTexThunk)) {
                    g_accSites++;
                    blog("accessory texture site: %08X -> AccTexLookup (material builder %08X)", kAcc[i], g_accBuilder);
                }
        }
        blog("ACCESSORY TEXTURE HOOKS INSTALLED: %d site(s) + headband via the asset helper, %d texture FileIDs, %d normal-map FileIDs%s.",
             g_accSites, cfg_accTexCount, cfg_accNrmCount, cfg_accNormals ? "" : " (NormalMaps=0)");
    } else blog("accessories: disabled in ini");

    /* 5. the rest of 1.11.3 (coach polo / coach arms are not part of the 2K12 port) */
    if (cfg_acc) install_combos12(); else blog("accessory combinations: off ([Accessories] Enabled=0)");
    install_socks12();
    install_nbapatch12();
    install_bench12();
    if (cfg_enabled && cfg_cloth && (cfg_staticGuard || ini_int2("ShortLength", "Enabled", 1)) && !g_floorOrig12[10]) {   /* static-cloth guard / Short Length need the wrappers */
        static const int sl[5] = { 1, 8, 9, 10, 11 };
        const u32 ex[5] = { g_k12->floor1, g_k12->floor8, g_k12->floor9, g_k12->floor10, g_k12->floor11 };
        u32 fn[5]; int k, w = 0;
        fn[0] = (u32)&FloorFn12_1; fn[1] = (u32)&FloorFn12_8; fn[2] = (u32)&FloorFn12_9; fn[3] = (u32)&FloorFn12_10; fn[4] = (u32)&FloorFn12_11;
        for (k = 0; k < 5; k++)
            if (*(u32*)(g_k12->floor_vt + sl[k] * 4) == ex[k]) { g_floorOrig12[sl[k]] = ex[k]; if (patch_bytes(g_k12->floor_vt + sl[k] * 4, &fn[k], 4, 0)) w++; }
        blog("STATIC CLOTH GUARD: %d/5 model functions wrapped", w);
    }
    install_keeparm12();
}

static DWORD WINAPI init_thread_2k12(LPVOID unused)
{
    DWORD t0 = GetTickCount();
    (void)unused;
    {
        int i; const char *nm = 0;
        for (i = 0; i < 3; i++) if (g_k12Builds[i].ts == g_exeTimestamp) nm = g_k12Builds[i].name;
        blog("NBA 2K12 detected (exe TimeDateStamp %08X: %s).", g_exeTimestamp, nm ? nm : "unknown build - trying every known build, each site is checked first");
    }
    for (;;) {
        if (k12_code_ready()) break;
        if ((int)(GetTickCount() - t0) > cfg_waitSeconds * 1000) {
            blog("ERROR the NBA 2K12 code was not found after %d s. This exe is none of the 3 supported NBA 2K12 builds (4F219A21, 4E63057D, 4E84D35F). Game runs unmodified.", cfg_waitSeconds);
            return 0;
        }
        Sleep(5);
    }
    Sleep(500);
    if (!k12_code_ready()) { blog("ERROR the game code changed while unpacking - not patching."); return 0; }
    {   /* 1.11.21-ML: SecuROM builds unpack the low part of the code later than the rest: wait for a known
         * pattern there (the cloth pool update) before patching, at most 30 s */
        static const u8 kLow[] = { 0x8B,0x4E,0x14, 0x3B,0x0F, 0x74,0x7E, 0x85,0xC9 };
        DWORD t1 = GetTickCount();
        while (!find_text(kLow, sizeof(kLow), 0) && (int)(GetTickCount() - t1) < 30000) Sleep(50);
        if (!find_text(kLow, sizeof(kLow), 0)) blog("WARNING low part of the game code not recognised after 30 s - patching anyway");
        else if (GetTickCount() - t1 > 100) blog("low part of the game code unpacked after another %u ms", GetTickCount() - t1);
    }
    blog("game code ready after %u ms - using the address table of %s (%08X)", GetTickCount() - t0, g_k12->name, g_k12->ts);
    g_imgFast = 1;
    blog("body call site      : %08X (NBA 2K12)", K12_BODY_SITE);
    blog("ResolveAsset        : %08X", K12_RESOLVE);
    blog("asset manager       : %08X", K12_MGR);
    {
        u32 v = 0; int i_k12;
        for (i_k12 = 0; i_k12 < 4; i_k12++) {
            read32(g_k12->bodyids + 4 * i_k12, &v);
            blog("   bodytype %d -> %08X %s", i_k12, v, v == kBodyIds[i_k12] ? kBodyNames[i_k12] : "(CHANGED from stock!)");
        }
    }
    blog("player->cyberface   : [player+%X], valid flag +%X, container hash +%X", cfg_offPlayerCF, cfg_offCFValid, cfg_offCFHash);
    blog("fallback container  : %s (%08X)", container_name(cfg_fallbackHash), cfg_fallbackHash);
    if (!cfg_enabled) { blog("Enabled=0 in BodyHook.ini - NOT patching (logging only)."); return 0; }
    install_2k12();
    scan_loose_pngs();
    install_hair();      /* after the scan: saved hair choices update the png row info */
    install_shortlen();  /* 1.11.21-ML: after install_hair (copies the Hair Style record's callbacks) */
    sl_sync_hair();
    if (cfg_log) CreateThread(NULL, 0, perf_thread, NULL, 0, NULL);
    {
        int k;
        for (k = 0; k < 60; k++) {
            Sleep(1000);
            if (g_hooked && call_target_of(K12_BODY_SITE) != (u32)&BodyLookupThunk12) {
                blog("WARNING hook at %08X was overwritten - re-installing", K12_BODY_SITE);
                if (!patch_call(K12_BODY_SITE, (u32)&BodyLookupThunk12)) break;
            }
        }
    }
    return 0;
}
