/* netcloth.inc - 1.11.30-ML: jerseys / shorts keep the stock cloth size whatever the exe says (NBA 2K14).
 *
 * The old "net size" hex edit (file offset 00AC85D0 = 00EC99D0: stock 3B706E81 3B708081 x3, edited to 80/90) changes the one
 * constant of the game's cloth constraint solver (00 4559D0 / 00455F80, subps at 455D12 455D41 4562B7 456312).
 * Nets and the players' moving jerseys / shorts share that solver, so a longer net also made moving shorts
 * shorter / stretched. Here the solver calls (45B95F 45B97A 45BEFB 45BF23 -> 4559D0, ecx = cloth; 45B96F 45BF0F
 * -> 455F80, eax = cloth) go through a small switch: a player garment (a cloth instance the outfit draw - owner
 * guard - saw in the last 5 s) runs a copy of the solver that reads the STOCK constant; everything else (the
 * nets) runs the game's own, with the exe's value. Both solvers are leaf functions (no calls out), so the
 * copies are the same bytes with only the 4 constant addresses changed.
 * ini [Uniforms] StockClothSize=1 (0 = off: everything uses the exe's value).
 */
/* cfg_stockCloth: bodyhook.c */
static u32 g_ncCopyA, g_ncCopyB;
/* the stock exe's value: 4 x 3B808081 (bytes 81 80 80 3B; logs 3d15fdc6 / fc458583: the default exe has 80, the
 * long-net exe 81 6E 70 3B, 81 80 70 3B x3) */
static __declspec(align(16)) u32 g_ncNetVal[4];   /* the exe's (net) value, used by the net copy */
static const u32 g_ncStock[4] = { 0x3B808081u, 0x3B808081u, 0x3B808081u, 0x3B808081u };
#define NC_N 512
static volatile u32 g_ncInst[NC_N], g_ncTick[NC_N];
static volatile LONG g_ncGar, g_ncNet;

static u32 nc_slot(u32 p) { return ((p >> 4) * 2654435761u) % NC_N; }
static volatile u32 g_ncMesh[NC_N];
static void nc_note(u32 inst)   /* a player's jersey / shorts cloth instance (from owner_guard) */
{
    u32 i, k, t = GetTickCount() | 1, m = 0;
    if (!inst || !g_ncCopyA) return;
    read32(inst + 0xA8, &m);
    for (k = 0, i = nc_slot(inst); k < 16; k++, i = (i + 1) % NC_N) {
        if (g_ncInst[i] == inst) { g_ncTick[i] = t; g_ncMesh[i] = m; return; }
        if (!g_ncInst[i] || t - g_ncTick[i] > 60000) { g_ncMesh[i] = m; g_ncTick[i] = t; g_ncInst[i] = inst; return; }
    }
}
/* 1.11.30-ML b: once a garment, always a garment while it still has the ClothMesh it had when the outfit draw saw
 * it (no 5 s expiry: a garment solved with the exe's value in between was the shaking) */
/* 1.11.31-ML: a cloth is a garment when its ClothMesh is one of the game's uniform ClothMeshes (jersey array x36,
 * shorts array x12) or one Short Length knows (copies / built shorts) - in practice the outfit draw does not always
 * see the user's cloth, so it was solved as a net (exe value) and Short Length looked wrong there */
static int nc_mesh_scan(u32 m);
static int nc_mesh_garment(u32 m)   /* cached per ClothMesh for 1 s (the solver asks many times a frame) */
{
    static u32 cm[16], cr[16], ct; static int cn; DWORD t = GetTickCount(); int i;
    if (!m) return 0;
    if (t - ct > 1000) { ct = t; cn = 0; }
    for (i = 0; i < cn; i++) if (cm[i] == m) return (int)cr[i];
    i = nc_mesh_scan(m);
    if (cn < 16) { cm[cn] = m; cr[cn] = (u32)i; cn++; }
    return i;
}
static int nc_mesh_scan(u32 m)
{
    u32 i, v, aj = g_simArr[0] ? g_simArr[0] : 0x020217BCu, as = g_simArr[1] ? g_simArr[1] : 0x0202199Cu;
    if (!m) return 0;
    for (i = 0; i < 36; i++) if (read32(aj + i * 4, &v) && v == m) return 1;
    for (i = 0; i < 12; i++) if (read32(as + i * 4, &v) && v == m) return 1;
    for (i = 0; i < (u32)g_slVarCount && i < SL_MAX_VAR; i++) if (g_slVar[i].mesh == m) return 1;
    for (i = 0; i < SL_MAX_INST; i++) if (g_slInst[i].mesh == m) return 1;
    return 0;
}
static int nc_garment(u32 inst)
{
    u32 i, k, m = 0;
    read32(inst + 0xA8, &m);
    for (k = 0, i = nc_slot(inst); k < 16; k++, i = (i + 1) % NC_N)
        if (g_ncInst[i] == inst) { if (m == g_ncMesh[i] || GetTickCount() - g_ncTick[i] < 5000) return 1; break; }
    return nc_mesh_garment(m);
}
static volatile u32 g_ncLastA[64], g_ncLastK[64];   /* flip check: instance -> 1 garment / 2 other */
static void nc_flip(u32 inst, u32 kind)
{
    u32 i = (inst >> 4) & 63;
    if (g_ncLastA[i] == inst && g_ncLastK[i] != kind) {
        static volatile LONG n; if (++n <= 20) blog("stock cloth size: cloth %08X switched to %s", inst, kind == 1 ? "player garment" : "other (exe value)");
    }
    g_ncLastA[i] = inst; g_ncLastK[i] = kind;
}
static u32 __stdcall nc_pickA(u32 inst)
{
    if (nc_garment(inst)) { nc_flip(inst, 1); if (++g_ncGar == 1) blog("stock cloth size: first player jersey/shorts cloth %08X solved with the stock size", inst); return 0x004559D0u; }
    nc_flip(inst, 2);
    if (++g_ncNet == 1) blog("stock cloth size: first other cloth (net) %08X solved with the exe's net size", inst);
    return g_ncCopyA;
}
static u32 __stdcall nc_pickB(u32 inst)
{
    if (nc_garment(inst)) { nc_flip(inst, 1); if (++g_ncGar == 1) blog("stock cloth size: first player jersey/shorts cloth %08X solved with the stock size", inst); return 0x00455F80u; }
    nc_flip(inst, 2);
    if (++g_ncNet == 1) blog("stock cloth size: first other cloth (net) %08X solved with the exe's net size", inst);
    return g_ncCopyB;
}
__declspec(naked) static void NcSolveA(void)   /* ecx = cloth */
{
    __asm {
        push 0
        pushad
        push dword ptr [esp + 24]
        call nc_pickA
        mov [esp + 32], eax
        popad
        ret
    }
}
__declspec(naked) static void NcSolveB(void)   /* eax = cloth */
{
    __asm {
        push 0
        pushad
        push dword ptr [esp + 28]
        call nc_pickB
        mov [esp + 32], eax
        popad
        ret
    }
}

static void install_netcloth(void)
{
    static const u32 sitesA[4] = { 0x0045B95F, 0x0045B97A, 0x0045BEFB, 0x0045BF23 }, sitesB[2] = { 0x0045B96F, 0x0045BF0F };
    static const u32 offA[2] = { 0x455D12 - 0x4559D0 + 3, 0x455D41 - 0x4559D0 + 3 }, offB[2] = { 0x4562B7 - 0x455F80 + 3, 0x456312 - 0x455F80 + 3 };
    const u32 lenA = 0x455F80 - 0x4559D0, lenB = 0x456550 - 0x455F80;
    u8 *mem; int i, n = 0, ok = 1; u32 exeVal[4];
    if (g_is2k12 || !cfg_enabled || !cfg_cloth || !cfg_stockCloth) { blog("stock cloth size: off"); return; }
    if (g_exeTimestamp != 0x52202C7Fu) { blog("stock cloth size: not the known NBA 2K14 exe - off"); return; }
    for (i = 0; i < 2; i++) {
        if (*(u32 *)(0x4559D0 + offA[i]) != 0x00EC99D0u || *(u32 *)(0x455F80 + offB[i]) != 0x00EC99D0u) ok = 0;
    }
    for (i = 0; i < 4 && ok; i++) if (*(u8 *)sitesA[i] != 0xE8 || call_target_of(sitesA[i]) != 0x004559D0u) ok = 0;
    for (i = 0; i < 2 && ok; i++) if (*(u8 *)sitesB[i] != 0xE8 || call_target_of(sitesB[i]) != 0x00455F80u) ok = 0;
    if (!ok) { blog("WARNING stock cloth size: cloth solver not as expected - off"); return; }
    memcpy(exeVal, (void *)0x00EC99D0, 16);
    if (exeVal[0] == g_ncStock[0] && exeVal[1] == g_ncStock[1] && exeVal[2] == g_ncStock[2] && exeVal[3] == g_ncStock[3]) { blog("stock cloth size: the exe has the stock cloth size (80) - nothing to change"); return; }
    mem = (u8 *)VirtualAlloc(0, lenA + lenB + 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!mem) { blog("WARNING stock cloth size: no memory - off"); return; }
    /* 1.11.30-ML d: the NETS get the copy (with the exe's edited value); the game's own solver gets the stock
     * value back, so jerseys / shorts run the untouched game code exactly like the default exe */
    memcpy(g_ncNetVal, exeVal, 16);
    memcpy(mem, (void *)0x4559D0, lenA);
    memcpy(mem + lenA + 16, (void *)0x455F80, lenB);
    for (i = 0; i < 2; i++) { *(u32 *)(mem + offA[i]) = (u32)g_ncNetVal; *(u32 *)(mem + lenA + 16 + offB[i]) = (u32)g_ncNetVal; }
    FlushInstructionCache(GetCurrentProcess(), mem, lenA + lenB + 32);
    g_ncCopyA = (u32)mem; g_ncCopyB = (u32)(mem + lenA + 16);
    {   DWORD op; if (!VirtualProtect((void *)0x00EC99D0, 16, PAGE_READWRITE, &op)) { blog("WARNING stock cloth size: constant not writable - off"); return; }
        memcpy((void *)0x00EC99D0, g_ncStock, 16); VirtualProtect((void *)0x00EC99D0, 16, op, &op); }
    for (i = 0; i < 4; i++) n += patch_call(sitesA[i], (u32)&NcSolveA) ? 1 : 0;
    for (i = 0; i < 2; i++) n += patch_call(sitesB[i], (u32)&NcSolveB) ? 1 : 0;
    blog("STOCK CLOTH SIZE INSTALLED: %d/6 solver calls - jerseys / shorts run the game's own solver with the stock 3B808081 x4, nets a copy with the exe's %08X %08X %08X %08X",
         n, exeVal[0], exeVal[1], exeVal[2], exeVal[3]);
}
