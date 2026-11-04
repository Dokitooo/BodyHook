/* ------------------------------------------------------------------------------------------ */
/* WINGSPAN TEST (research build only, /DBODYHOOK_WINGTEST)                                     */
/*                                                                                             */
/* Question: where does the game take the arm length from, and does the ball follow?           */
/*   F7  = the exe's 21-joint animation skeleton (0x00F21BD0, 16 bytes per joint): upper arm +  */
/*         forearm offsets of both arms x Scale (toggle).                                      */
/*   F8  = the body model's own skeleton (hi_body bones): every table in the loaded body files  */
/*         that holds the left elbow bind position is listed in the log; each press scales the  */
/*         NEXT table found (arm bones moved away from the shoulder by Scale), the one before   */
/*         goes back. After the last table, everything is back to normal.                      */
/*   F6  = everything back to normal.                                                          */
/* Affects ALL players (test only). Ini: [WingspanTest] Scale=1.15                             */
/* ------------------------------------------------------------------------------------------ */
#ifdef BODYHOOK_WINGTEST
#ifdef BODYHOOK_MINIMAL_WINAPI
__declspec(dllimport) short WINAPI GetAsyncKeyState(int);
#endif
#define VK_F6 0x75
#define VK_F7 0x76
#define VK_F8 0x77
#define VK_F9 0x78
static float g_wtScale = 1.15f;

/* bind positions (hi_body, from the model): left arm 76..102, right arm 48..74 */
static const float kWtElbowL[3] = { 47.172f, 32.811f, 4.008f };    /* bone 82 */
static const float kWtWristL[3] = { 67.654f, 20.761f, 16.824f };   /* bone 87 */
static const float kWtRootL[3]  = { 24.177f, 44.876f, 0.168f };    /* bone 78: upper arm starts here */

/* ---- F7: exe animation skeleton ---- */
/* v15: every copy of the 21-joint arm offsets in the exe (upper arm 27.156 / forearm 20.482, both sides):
 * the animation table plus the constants that 11 gameplay functions add inline (addps xmm, [const]) */
static const u32 kWtExe[12] = { 0x00F21CA0u, 0x00F21CB0u, 0x00F21D00u, 0x00F21D10u,
                                0x00FBEA90u, 0x00FC9B00u, 0x00FBFC40u, 0x00F69D90u,
                                0x00FBE3C0u, 0x00FBE380u, 0x00FBE370u, 0x00FBE360u };
static float g_wtExeOrig[12][3];
static int g_wtExeOn = 0, g_wtExeSaved = 0;
static void wt_write(u32 a, const void *src, u32 n)
{
    DWORD old;
    if (!VirtualProtect((LPVOID)a, n, PAGE_READWRITE, &old)) { blog("WINGTEST cannot write %08X", a); return; }
    memcpy((void*)a, src, n);
    VirtualProtect((LPVOID)a, n, old, &old);
}
static void wt_exe(int on)
{
    int i, k;
    if (!g_wtExeSaved) {
        for (i = 0; i < 12; i++) memcpy(g_wtExeOrig[i], (void*)kWtExe[i], 12);
        g_wtExeSaved = 1;
    }
    for (i = 0; i < 12; i++) {
        float v[3];
        for (k = 0; k < 3; k++) v[k] = g_wtExeOrig[i][k] * (on ? g_wtScale : 1.0f);
        wt_write(kWtExe[i], v, 12);
    }
    g_wtExeOn = on;
    blog("WINGTEST F7 exe animation skeleton arms %s (upper arm %d.%d -> %d.%d cm)", on ? "LONGER" : "normal",
         (int)g_wtExeOrig[0][0], ((int)(g_wtExeOrig[0][0] * 10.0f)) % 10,
         (int)(g_wtExeOrig[0][0] * (on ? g_wtScale : 1.0f)), ((int)(g_wtExeOrig[0][0] * (on ? g_wtScale : 1.0f) * 10.0f)) % 10);
}

/* ---- F8: body model skeleton tables ---- */
/* left arm bind positions 76..87 (hi_body); right arm = bones 48..59, x mirrored */
static const float kWtL[12][3] = {
    {20.2617f, 52.0897f, 0.1813f}, {20.0157f, 47.0587f, -0.5267f}, {24.1767f, 44.8756f, 0.1681f},
    {30.9657f, 41.3137f, 1.3017f}, {37.7547f, 37.7519f, 2.4353f}, {43.0107f, 34.9943f, 3.3130f},
    {47.1717f, 32.8112f, 4.0078f}, {49.9316f, 31.1874f, 5.7348f}, {54.9673f, 28.2248f, 8.8859f},
    {60.0030f, 25.2622f, 12.0370f}, {63.7112f, 23.0806f, 14.3574f}, {67.6538f, 20.7614f, 16.8244f} };
#define WT_B(b) kWtL[(b) - 76]
#define WT_MAX 64
#define WT_BODIES 32
/* form: 0 = model-space positions, 1 = offsets from the parent bone (bone b - bone b-1) */
typedef struct { u32 elbow, stride, form; float sgn, unit; u32 at[48]; float orig[48][3]; int n; } WtTable;
static WtTable g_wt[WT_MAX];
static int g_wtN = 0;
static struct { u32 world, desc, tick; int base; } g_wtWorlds[32]; static volatile u32 g_wtWorldsNext = 0;
static u32 g_wtWLo = 0, g_wtWHi = 0; static volatile int g_wtBallMode = 0;
static volatile u32 g_wtLastWorld = 0, g_wtLastDesc = 0; static volatile int g_wtLastBase = 0;
static volatile u32 g_wtBody[WT_BODIES];
static volatile int g_wtBodyN = 0;
static void wt_note(u32 body)
{
    int i;
    if (!body) return;
    for (i = 0; i < g_wtBodyN; i++) if (g_wtBody[i] == body) return;
    if (g_wtBodyN < WT_BODIES) g_wtBody[g_wtBodyN++] = body;
}
static void wt_ref(int b, int form, float sgn, float unit, float *o)
{
    int k;
    for (k = 0; k < 3; k++) o[k] = (form ? WT_B(b)[k] - WT_B(b - 1)[k] : WT_B(b)[k]) * sgn * unit;
}
static int wt_near(const float *p, const float *v, float tol)
{
    int k;
    for (k = 0; k < 3; k++) {
        float d;
        if (!(p[k] == p[k]) || p[k] > 1.0e6f || p[k] < -1.0e6f) return 0;      /* NaN / garbage never matches */
        d = p[k] - v[k];
        if (!(d >= -tol && d <= tol)) return 0;
    }
    return 1;
}
static void wt_scan_region(u32 lo, u32 hi, u32 body)
{
    static const float units[2] = { 1.0f, 0.01f };
    u32 a;
    int form, si, ui;
    for (a = lo; a + 16 <= hi && g_wtN < WT_MAX; a += 4) {
        const float *p = (const float*)a;
        for (form = 0; form < 2; form++) for (si = 0; si < 2; si++) for (ui = 0; ui < 2; ui++) {
            float e[3], w[3], sgn = si ? -1.0f : 1.0f, unit = units[ui], tol = 0.02f * unit;
            u32 st, i;
            wt_ref(82, form, sgn, unit, e);
            if (!wt_near(p, e, tol)) continue;
            for (i = 0; i < (u32)g_wtN; i++) if (g_wt[i].elbow == a) break;
            if (i < (u32)g_wtN) continue;
            wt_ref(87, form, sgn, unit, w);
            for (st = 12; st <= 256; st += 4) {
                if (a + 5 * st + 12 > hi) { st = 999; break; }
                if (readable((void*)(a + 5 * st), 12) && wt_near((const float*)(a + 5 * st), w, tol)) break;
            }
            if (st > 256) {
                continue;
            }
            {
                WtTable *t = &g_wt[g_wtN++];
                int b, r;
                t->elbow = a; t->stride = st; t->form = (u32)form; t->sgn = sgn; t->unit = unit; t->n = 0;
                for (r = 0; r < 2; r++)              /* v7: left arm 79..102 incl. fingers, right arm 51..74 (28 bones before) */
                    for (b = 79; b <= 102; b++) {
                        u32 at = a + (u32)((b - 82) * (int)st) - (r ? 28u * st : 0u);
                        if (!readable((void*)at, 12)) continue;
                        t->at[t->n] = at; memcpy(t->orig[t->n], (void*)at, 12); t->n++;
                    }
                blog("WINGTEST   table %d: body %08X, elbow at %08X, %u bytes per bone, %s, sign %d, %s, right arm %s",
                     g_wtN - 1, body, a, st, form ? "OFFSETS from parent" : "POSITIONS", (int)sgn, ui ? "metres" : "cm",
                     (t->n == 48) ? "found" : "not readable");
            }
        }
    }
}
static void wt_find(void)
{
    int i;
    for (i = 0; i < g_wtBodyN; i++) {
        u32 body = g_wtBody[i], lo, hi, rlo, rhi;
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery((void*)body, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) continue;
        rlo = (u32)mbi.BaseAddress; rhi = rlo + (u32)mbi.RegionSize;
        lo = body > 0x100000u ? body - 0x100000u : 0; hi = body + 0x800000u;
        if (lo < rlo) lo = rlo;
        if (hi > rhi) hi = rhi;
        blog("WINGTEST   scanning body model %08X (%08X..%08X)", body, lo, hi);
        wt_scan_region(lo, hi, body);
    }
    blog("WINGTEST F8: %d skeleton table(s) found in %d body model(s)", g_wtN, g_wtBodyN);
}
static void wt_table(int i, int on)
{
    WtTable *t = &g_wt[i];
    int j, k;
    for (j = 0; j < t->n; j++) {
        int right = j >= 24, b = 79 + (j % 24);
        float v[3], r[3];
        for (k = 0; k < 3; k++) {
            if (t->form) v[k] = on ? t->orig[j][k] * g_wtScale : t->orig[j][k];      /* longer bone offsets */
            else {
                r[k] = WT_B(78)[k] * t->sgn * t->unit * ((right && k == 0) ? -1.0f : 1.0f);   /* about the upper arm root */
                v[k] = on ? r[k] + (t->orig[j][k] - r[k]) * g_wtScale : t->orig[j][k];
            }
        }
        (void)b;
        if (readable((void*)t->at[j], 12)) memcpy((void*)t->at[j], v, 12);
    }
}
static int g_wtOn[2] = { 0, 0 };          /* [form]: 0 = positions (F8), 1 = offsets (F9) */
static void wt_group(int form, int on)
{
    int i, n = 0;
    if (!g_wtN) wt_find();
    for (i = 0; i < g_wtN; i++) if ((int)g_wt[i].form == form) { wt_table(i, on); n++; }
    g_wtOn[form] = on;
    blog("WINGTEST %s: %d %s table(s) %s", form ? "F9" : "F8", n, form ? "OFFSET" : "POSITION", on ? "LONGER" : "normal");
}

/* ---- F10 (v8): who writes the skin bind table every frame? -----------------------------------
 * The POSITION tables (48 bytes per bone) are rewritten by the game each frame. F10 puts a guard on
 * their memory pages for 3 seconds; every access to a table entry records the game code address
 * (EIP) and its callers. The result is logged after the 3 s. Nothing is changed in the game. */
#ifdef BODYHOOK_MINIMAL_WINAPI
__declspec(dllimport) void * WINAPI AddVectoredExceptionHandler(u32, void *);
__declspec(dllimport) u32 WINAPI RemoveVectoredExceptionHandler(void *);
#endif
#define WT_PAGES 32
#define WT_HITS  96
static volatile u32 g_wtTrace = 0;
static u32 g_wtPage[WT_PAGES]; static int g_wtPageN = 0;
static struct { u32 eip, write, addr, n, call[4]; } g_wtHit[WT_HITS];
static volatile LONG g_wtHitN = 0;
static volatile u32 g_wtHitsTotal = 0;
static void wt_guard_all(void)
{
    int i; DWORD old;
    for (i = 0; i < g_wtPageN; i++) VirtualProtect((LPVOID)g_wtPage[i], 4096, PAGE_READWRITE | 0x100 /* PAGE_GUARD */, &old);
}
static int wt_in_table(u32 a)
{
    int i;
    for (i = 0; i < g_wtN; i++) {
        WtTable *t = &g_wt[i];
        u32 lo, hi;
        if (t->form) continue;
        lo = t->elbow - 30u * t->stride; hi = t->elbow + 21u * t->stride;   /* both arms 51..102 */
        if (a >= lo && a < hi) return 1;
    }
    return 0;
}
static LONG __stdcall wt_veh(void *ep)
{
    u32 *rec = *(u32**)ep, *ctx = *((u32**)ep + 1);
    u8 *c = (u8*)ctx;
    u32 code = rec[0];
    if (code == 0x80000001u) {                         /* STATUS_GUARD_PAGE_VIOLATION */
        u32 a = rec[6], w = rec[5], eip = *(u32*)(c + 0xB8), esp = *(u32*)(c + 0xC4);
        int i, known = 0;
        if (!g_wtTrace) return 0;                      /* EXCEPTION_CONTINUE_SEARCH */
        if (g_wtTrace == 2) return -1;                 /* disarming: the guard is already gone, just go on */
        if (g_wtBallMode ? ((a >= 0x01E6C340u && a < 0x01E6C380u) || (a >= 0x01040C90u && a < 0x01040CB0u) || (a >= 0x01E6CB40u && a < 0x01E6CB60u))
                         : g_wtWHi ? (a >= g_wtWLo && a < g_wtWHi) : wt_in_table(a)) {
            g_wtHitsTotal++;
            {   /* v14: a hit inside the shared matrix helpers counts for the function that called them */
                u32 o, v;
                if (eip >= 0x00D94000u && eip < 0x00D97000u)
                    for (o = 0; o < 0x80; o += 4) if (read32(esp + o, &v) && v >= 0x00401000u && v < 0x00EC2000u && *(u8*)(v - 5) == 0xE8 &&
                                                      !(v >= 0x00D94000u && v < 0x00D97000u)) { eip = v; break; }
                if (eip >= 0x00BF2D10u && eip < 0x00C05790u) eip = 0x00BF2D10u;   /* the pose builder itself = one entry */
            }
            for (i = 0; i < g_wtHitN; i++) if (g_wtHit[i].eip == eip && g_wtHit[i].write == w) { g_wtHit[i].n++; known = 1; break; }
            if (!known && g_wtHitN < WT_HITS) {
                LONG k = __sync_add_and_fetch(&g_wtHitN, 1) - 1;
                if (k < WT_HITS) {
                    int nc = 0; u32 o;
                    g_wtHit[k].eip = eip; g_wtHit[k].write = w; g_wtHit[k].addr = a; g_wtHit[k].n = 1;
                    for (o = 0; o < 0x400 && nc < 4; o += 4) {
                        u32 v;
                        if (!read32(esp + o, &v)) break;
                        if (v >= 0x00401000u && v < 0x00EC2000u && *(u8*)(v - 5) == 0xE8) g_wtHit[k].call[nc++] = v;
                    }
                }
            }
        }
        *(u32*)(c + 0xC0) |= 0x100;                   /* trap flag: re-arm the guard after this instruction */
        return -1;                                     /* EXCEPTION_CONTINUE_EXECUTION */
    }
    if (code == 0x80000004u && g_wtTrace) {            /* STATUS_SINGLE_STEP */
        if (g_wtTrace == 1) wt_guard_all();
        return -1;
    }
    return 0;
}
static void wt_trace(void)
{
    if (1) {   /* v17: the ball positions found by F5 (exe data, fixed addresses) - who writes / reads them */
        static const u32 kBall[3] = { 0x01E6C370u, 0x01040CA0u, 0x01E6CB50u };
        int i, j; void *h; DWORD old;
        g_wtPageN = 0;
        for (i = 0; i < 3; i++) {
            u32 p = kBall[i] & ~0xFFFu;
            for (j = 0; j < g_wtPageN; j++) if (g_wtPage[j] == p) break;
            if (j == g_wtPageN) g_wtPage[g_wtPageN++] = p;
        }
        g_wtBallMode = 1; g_wtHitN = 0; g_wtHitsTotal = 0;
        h = AddVectoredExceptionHandler(1, (void*)wt_veh);
        blog("WINGTEST F10: watching the ball positions 01E6C370 / 01040CA0 / 01E6CB50 for 2 s ...");
        g_wtTrace = 1; wt_guard_all(); Sleep(2000);
        for (i = 0; i < g_wtPageN; i++) VirtualProtect((LPVOID)g_wtPage[i], 4096, PAGE_READWRITE, &old);
        g_wtTrace = 2; Sleep(200);
        for (i = 0; i < g_wtPageN; i++) VirtualProtect((LPVOID)g_wtPage[i], 4096, PAGE_READWRITE, &old);
        Sleep(200); g_wtTrace = 0; g_wtBallMode = 0; (void)h;
        blog("WINGTEST F10: %u access(es) to the ball positions, %d code place(s):", g_wtHitsTotal, (int)(g_wtHitN < WT_HITS ? g_wtHitN : WT_HITS));
        for (i = 0; i < g_wtHitN && i < WT_HITS; i++)
            blog("WINGTEST   %s at %08X  x%u  (address %08X)  callers %08X %08X %08X %08X", g_wtHit[i].write ? "WRITE" : "read ",
                 g_wtHit[i].eip, g_wtHit[i].n, g_wtHit[i].addr, g_wtHit[i].call[0], g_wtHit[i].call[1], g_wtHit[i].call[2], g_wtHit[i].call[3]);
        return;
    }
    if (g_wtLastWorld) {   /* v11: the posed bones of one player - who reads the hands (the ball?) */
        u32 w = g_wtLastWorld; int b = g_wtLastBase;
        u32 lo = w + (u32)(b + 59) * 64, hi = w + (u32)(b + 103) * 64, p, desc = g_wtLastDesc, pl = 0;   /* v14: right wrist..left fingers */
        int i; void *h; DWORD old;
        read32(desc + 8, &pl);
        g_wtWLo = lo; g_wtWHi = hi; g_wtPageN = 0;
        for (p = lo & ~0xFFFu; p < hi && g_wtPageN < WT_PAGES; p += 4096) g_wtPage[g_wtPageN++] = p;
        g_wtHitN = 0; g_wtHitsTotal = 0;
        h = AddVectoredExceptionHandler(1, (void*)wt_veh);
        blog("WINGTEST F10: watching the arm bones of one player (bones %08X, player %08X CFID %u) for 2 s ...", w, pl, pl ? player_cfid((u8*)pl) : 0);
        g_wtTrace = 1; wt_guard_all(); Sleep(2000);
        for (i = 0; i < g_wtPageN; i++) VirtualProtect((LPVOID)g_wtPage[i], 4096, PAGE_READWRITE, &old);
        g_wtTrace = 2; Sleep(200);
        for (i = 0; i < g_wtPageN; i++) VirtualProtect((LPVOID)g_wtPage[i], 4096, PAGE_READWRITE, &old);
        Sleep(200); g_wtTrace = 0; (void)h;
        blog("WINGTEST F10: %u access(es) to his arm bones, %d code place(s):", g_wtHitsTotal, (int)(g_wtHitN < WT_HITS ? g_wtHitN : WT_HITS));
        for (i = 0; i < g_wtHitN && i < WT_HITS; i++)
            blog("WINGTEST   %s at %08X  x%u  (bone %d)  callers %08X %08X %08X %08X", g_wtHit[i].write ? "WRITE" : "read ",
                 g_wtHit[i].eip, g_wtHit[i].n, (int)((g_wtHit[i].addr - w) / 64) - b, g_wtHit[i].call[0], g_wtHit[i].call[1], g_wtHit[i].call[2], g_wtHit[i].call[3]);
        g_wtWLo = g_wtWHi = 0;
        return;
    }

    int i, j; void *h; DWORD old;
    if (!g_wtN) wt_find();
    g_wtPageN = 0;
    for (i = 0; i < g_wtN; i++) {
        u32 p0, p1, p;
        if (g_wt[i].form) continue;
        p0 = (g_wt[i].elbow - 30u * g_wt[i].stride) & ~0xFFFu; p1 = (g_wt[i].elbow + 21u * g_wt[i].stride) & ~0xFFFu;
        for (p = p0; p <= p1 && g_wtPageN < WT_PAGES; p += 4096) {
            for (j = 0; j < g_wtPageN; j++) if (g_wtPage[j] == p) break;
            if (j == g_wtPageN) g_wtPage[g_wtPageN++] = p;
        }
    }
    if (!g_wtPageN) { blog("WINGTEST F10: no POSITION tables to watch (get on court first)"); return; }
    g_wtHitN = 0; g_wtHitsTotal = 0;
    h = AddVectoredExceptionHandler(1, (void*)wt_veh);
    if (!h) { blog("WINGTEST F10: exception handler could not be added"); return; }
    blog("WINGTEST F10: watching %d page(s) for 3 s ...", g_wtPageN);
    g_wtTrace = 1;
    wt_guard_all();
    Sleep(3000);
    for (i = 0; i < g_wtPageN; i++) VirtualProtect((LPVOID)g_wtPage[i], 4096, PAGE_READWRITE, &old);
    g_wtTrace = 2;                                     /* handler: swallow late guard / single-step, no re-arm */
    Sleep(200);
    for (i = 0; i < g_wtPageN; i++) VirtualProtect((LPVOID)g_wtPage[i], 4096, PAGE_READWRITE, &old);
    Sleep(200);
    g_wtTrace = 0;
    /* the handler stays installed (harmless when idle): a late guard hit on another thread is still handled */
    (void)h;
    blog("WINGTEST F10: %u access(es) to the tables, %d different code place(s):", g_wtHitsTotal, (int)(g_wtHitN < WT_HITS ? g_wtHitN : WT_HITS));
    for (i = 0; i < g_wtHitN && i < WT_HITS; i++)
        blog("WINGTEST   %s at %08X  x%u  (entry %08X)  callers %08X %08X %08X %08X", g_wtHit[i].write ? "WRITE" : "read ",
             g_wtHit[i].eip, g_wtHit[i].n, g_wtHit[i].addr, g_wtHit[i].call[0], g_wtHit[i].call[1], g_wtHit[i].call[2], g_wtHit[i].call[3]);
}


/* ---- F11 / F12 (v9): longer arms in the player's posed bones --------------------------------
 * 0x00BF1ECB (player model update, ebx = render desc, +08 = player record) calls 0x00DB8570
 * (ecx = skin palette out, edx = skeleton: +60 bone count, +64 bind positions 48 B/bone; arg = posed
 * world matrices, 64 B/bone, translation at +30). The palette is world * translate(-bind).
 * Longer arm = elbow, forearm, wrist and fingers moved out along the arm, bone shapes unchanged:
 *   upper arm root S (78 / 50), elbow E (82 / 54), wrist W (87 / 59)
 *   twist + forearm bones 79..86 / 51..58 : + (k-1) * (P - S)
 *   wrist + fingers 87..102 / 59..74      : + (k-1) * (W - S)
 * F11 = only the drawn skin (copy of the bones), F12 = the game's own bone array (anything that
 * reads it afterwards, maybe the ball, follows). All players, test only. */
typedef u32 (__fastcall *SkinFn_t)(u32 out, u32 skel, u32 world);
static u32 g_wtSkinFn = 0;
static volatile int g_wtSkinMode = 0;        /* 0 off, 1 copy, 2 in place */
static volatile u32 g_wtSkinCalls = 0, g_wtSkinDone = 0, g_wtSkinBad = 0;
static __declspec(align(16)) u8 g_wtSkinBuf[4][256 * 64];
static volatile LONG g_wtSkinBufN = 0;
static struct { u32 world; float w[2][3]; } g_wtInPlace[64];
static void wt_arm(u8 *world, int S, int first, int W, int last, float k)
{
    float s[3], w[3], d[3];
    int b, j;
    for (j = 0; j < 3; j++) { s[j] = *(float*)(world + S * 64 + 0x30 + j * 4); w[j] = *(float*)(world + W * 64 + 0x30 + j * 4); }
    for (j = 0; j < 3; j++) d[j] = (k - 1.0f) * (w[j] - s[j]);
    for (b = first; b <= last; b++) {
        float *t = (float*)(world + b * 64 + 0x30);
        if (b < W) for (j = 0; j < 3; j++) t[j] += (k - 1.0f) * (t[j] - s[j]);
        else       for (j = 0; j < 3; j++) t[j] += d[j];
    }
}
static struct { u32 skel, n, base; } g_wtSk[32];
static volatile u32 g_wtSkLog = 0;
/* where is the left elbow in this skeleton's bind table? -> bone index shift (0 = hi_body order) */
static int wt_skel_base(u32 skel, u32 *pn)
{
    u32 n = 0, bind = 0, i, h = (skel >> 4) & 31;
    if (g_wtSk[h].skel == skel) { *pn = g_wtSk[h].n; return (int)g_wtSk[h].base; }
    if (!read32(skel + 0x60, &n) || !read32(skel + 0x64, &bind) || n < 40 || n > 400 || !readable((void*)bind, n * 48)) return -1000;
    for (i = 0; i < n; i++)
        if (wt_near((const float*)(bind + i * 48), WT_B(82), 0.3f)) break;
    g_wtSk[h].skel = skel; g_wtSk[h].n = n; g_wtSk[h].base = (i < n) ? i - 82 : (u32)-1000;
    if (g_wtSkLog < 12) {
        g_wtSkLog++;
        blog("WINGTEST   skeleton %08X: %u bones, left elbow bind at bone %d%s", skel, n, i < n ? (int)i : -1,
             i < n ? "" : " (not found - not a body skeleton)");
    }
    *pn = n;
    return (int)g_wtSk[h].base;
}
static void wt_arm2(u8 *world, int base, float k)
{
    wt_arm(world, base + 78, base + 79, base + 87, base + 102, k);
    wt_arm(world, base + 50, base + 51, base + 59, base + 74, k);
}
u32 __fastcall WingSkin(u32 out, u32 skel, u32 world, u32 desc)
{
    u32 n = 0, mode = (u32)g_wtSkinMode;
    int base;
    (void)desc;
    g_wtSkinCalls++;
    base = wt_skel_base(skel, &n);
    if (base > -1000 && base + 103 <= (int)n) {
        int k, f = -1;
        g_wtLastWorld = world; g_wtLastBase = base; g_wtLastDesc = desc;
        for (k = 0; k < 32; k++) if (g_wtWorlds[k].world == world) { f = k; break; }
        if (f < 0) { f = (int)(g_wtWorldsNext++ & 31); g_wtWorlds[f].world = world; g_wtWorlds[f].base = base; g_wtWorlds[f].desc = desc; }
        g_wtWorlds[f].tick = GetTickCount();
    }
    if (mode != 1) return ((SkinFn_t)g_wtSkinFn)(out, skel, world);   /* v12: F12 works in the pose job */
    if (base <= -1000 || base + 50 < 0 || (u32)(base + 103) > n || n > 256 || !readable((void*)world, n * 64)) {
        g_wtSkinBad++; return ((SkinFn_t)g_wtSkinFn)(out, skel, world);
    }
    if (mode == 1) {
        u8 *buf = g_wtSkinBuf[(__sync_add_and_fetch(&g_wtSkinBufN, 1)) & 3];
        memcpy(buf, (void*)world, n * 64);
        wt_arm2(buf, base, g_wtScale);
        g_wtSkinDone++;
        return ((SkinFn_t)g_wtSkinFn)(out, skel, (u32)buf);
    } else {
        int h = (int)((world >> 6) & 63), j;
        float *wl = (float*)(world + (base + 87) * 64 + 0x30), *wr = (float*)(world + (base + 59) * 64 + 0x30);
        if (g_wtInPlace[h].world == world) {
            int same = 1;
            for (j = 0; j < 3; j++) if (wl[j] != g_wtInPlace[h].w[0][j] || wr[j] != g_wtInPlace[h].w[1][j]) same = 0;
            if (same) return ((SkinFn_t)g_wtSkinFn)(out, skel, world);
        }
        wt_arm2((u8*)world, base, g_wtScale);
        g_wtInPlace[h].world = world;
        for (j = 0; j < 3; j++) { g_wtInPlace[h].w[0][j] = wl[j]; g_wtInPlace[h].w[1][j] = wr[j]; }
        g_wtSkinDone++;
        return ((SkinFn_t)g_wtSkinFn)(out, skel, world);
    }
}
/* replaces "call 0x00DB8570" at 0x00BF1ECB: ecx, edx, [esp+4] = bones; ebx = render desc */
__declspec(naked) void WingSkinStub(void)
{
    __asm {
        push ebx
        push dword ptr [esp + 8]
        call WingSkin
        ret 4
    }
}

/* ---- v12: F12 = longer arms right after the player's pose job --------------------------------
 * 0x00BF1750 "push 0x00C05790" registers the model's pose job (thunk -> 0x00BF2D10, ecx = model,
 * edx passed through). After it, the model's posed bones are: pose = [model+130], p = [pose+40]
 * (only when [pose+3C] > 0), bones = [p+64], skeleton = [p+58]. Everything that reads the hands
 * after the pose job (the ball?) then sees the longer arm. */
static volatile u32 g_wtJobCalls = 0, g_wtJobDone = 0;
static struct { u32 world, tick; float o[2][3], d[2][3]; } g_wtHd[32];
void __fastcall WingPost(u32 model, u32 world)
{
    u32 n = 122;
    int base = g_wtLastBase, h, j;
    float *wl, *wr;
    (void)model;
    g_wtJobCalls++;
    if (g_wtSkinMode != 2 || !world || !g_wtLastWorld) return;
    /* 0x00BF2D10 arg 1 (ebx) = the bones it writes; bone layout = the one the draw hook saw (122, +1) */
    if (base <= -1000 || base + 50 < 0 || (u32)(base + 103) > n || !readable((void*)world, n * 64)) return;
    h = (int)((world >> 6) & 63);
    wl = (float*)(world + (base + 87) * 64 + 0x30); wr = (float*)(world + (base + 59) * 64 + 0x30);
    if (g_wtInPlace[h].world == world) {
        int same = 1;
        for (j = 0; j < 3; j++) if (wl[j] != g_wtInPlace[h].w[0][j] || wr[j] != g_wtInPlace[h].w[1][j]) same = 0;
        if (same) return;
    }
    {   /* v18: remember where the hands were and how far they moved (for the ball) */
        float ol[3], orr[3];
        int hs = (int)((world >> 6) & 31);
        for (j = 0; j < 3; j++) { ol[j] = wl[j]; orr[j] = wr[j]; }
        wt_arm2((u8*)world, base, g_wtScale);
        for (j = 0; j < 3; j++) {
            g_wtHd[hs].o[0][j] = ol[j];  g_wtHd[hs].d[0][j] = wl[j] - ol[j];
            g_wtHd[hs].o[1][j] = orr[j]; g_wtHd[hs].d[1][j] = wr[j] - orr[j];
        }
        g_wtHd[hs].world = world; g_wtHd[hs].tick = GetTickCount();
    }
    g_wtInPlace[h].world = world;
    for (j = 0; j < 3; j++) { g_wtInPlace[h].w[0][j] = wl[j]; g_wtInPlace[h].w[1][j] = wr[j]; }
    g_wtJobDone++;
}
u32 g_wtJobOrig = 0x00C05790u;
__declspec(naked) void WingJobStub(void)
{
    __asm {
        push ecx
        push dword ptr [esp + 0x14]
        push dword ptr [esp + 0x14]
        push dword ptr [esp + 0x14]
        push dword ptr [esp + 0x14]
        mov  eax, g_wtJobOrig
        call eax
        pop  ecx
        push eax
        mov  edx, [esp + 0Ch]
        call WingPost
        pop  eax
        ret  10h
    }
}
static void wt_job_install(void)
{
    u32 site = 0x00BF1750u, v = (u32)&WingJobStub;
    if (!readable((void*)site, 5) || *(u8*)site != 0x68 || *(u32*)(site + 1) != 0x00C05790u) {
        blog("WINGTEST pose job hook: 00BF1750 is not 'push 00C05790' - F12 stays at draw time");
        return;
    }
    if (patch_bytes(site + 1, &v, 4, 1)) blog("WINGTEST pose job hook installed at 00BF1750 (F12 = longer arms right after posing)");
}


/* ---- v18: the ball follows the longer hand ----------------------------------------------------
 * 0x009C5275 "call 0x00958E70" draws one attached object (the ball) with ecx = its model, whose +64
 * points at its matrix (array at 0x01E6C340, 0x50 bytes each, translation +30). While F12 is on, a
 * ball within 35 cm of a lengthened hand (where the game put the hand) is drawn moved by that hand's
 * change (full up to 20 cm, fading out to 35 cm). Drawing only: the game's ball itself is untouched. */
typedef void (__fastcall *DrawObj_t)(u32 model);
static volatile u32 g_wtBallDraws = 0, g_wtBallMoved = 0;
static __declspec(align(16)) float g_wtBallM[16];
void __fastcall WingBallDraw(u32 model)
{
    u32 m = 0, now = GetTickCount();
    float p[3], best = 1e9f, bd[3] = { 0, 0, 0 };
    int k, hnd, j;
    g_wtBallDraws++;
    if (g_wtSkinMode != 2 || !read32(model + 0x64, &m) || !readable((void*)m, 64)) { ((DrawObj_t)0x00958E70u)(model); return; }
    memcpy(p, (void*)(m + 0x30), 12);
    for (k = 0; k < 32; k++) {
        if (!g_wtHd[k].world || now - g_wtHd[k].tick > 200) continue;
        for (hnd = 0; hnd < 2; hnd++) {
            float dx = p[0] - g_wtHd[k].o[hnd][0], dy = p[1] - g_wtHd[k].o[hnd][1], dz = p[2] - g_wtHd[k].o[hnd][2];
            float d2 = dx * dx + dy * dy + dz * dz;
            if (d2 < best) { best = d2; for (j = 0; j < 3; j++) bd[j] = g_wtHd[k].d[hnd][j]; }
        }
    }
    if (best < 35.0f * 35.0f) {
        float w = 1.0f;
        if (best > 20.0f * 20.0f) {   /* fade 20..35 cm (linear in distance, sqrt by Newton) */
            float d = best, r = d * 0.5f; int it;
            for (it = 0; it < 6; it++) r = 0.5f * (r + d / r);
            w = (35.0f - r) / 15.0f;
        }
        /* v19: move the game's own draw matrix (rewritten from the ball every frame by 0x009C567F); never
         * twice on the same unchanged matrix */
        {
            float *t = (float*)(m + 0x30);
            int same = (t[0] == g_wtBallM[12] && t[1] == g_wtBallM[13] && t[2] == g_wtBallM[14]);
            if (!same) {
                for (j = 0; j < 3; j++) t[j] += w * bd[j];
                g_wtBallM[12] = t[0]; g_wtBallM[13] = t[1]; g_wtBallM[14] = t[2];
                g_wtBallMoved++;
            }
        }
    }
    ((DrawObj_t)0x00958E70u)(model);
}
static void wt_ball_install(void)
{
    u32 site = 0x009C5275u;
    if (!readable((void*)site, 5) || *(u8*)site != 0xE8 || call_target_of(site) != 0x00958E70u) {
        blog("WINGTEST ball hook: 009C5275 is not 'call 00958E70' - ball stays"); return;
    }
    if (patch_call(site, (u32)&WingBallDraw)) blog("WINGTEST ball hook installed at 009C5275 (F12: the ball follows the longer hand)");
}

static void wt_skin_install(void)
{
    static const u8 site_ctx[] = { 0x8B, 0x49, 0x0C, 0xE8 };     /* mov ecx,[ecx+0C] / call */
    u32 site = 0x00BF1ECBu;
    if (!readable((void*)(site - 3), 8) || !bytes_eq((const u8*)(site - 3), site_ctx, 4) || call_target_of(site) != 0x00DB8570u) {
        blog("WINGTEST skin hook: 00BF1ECB is not 'call 00DB8570' - F11/F12 off");
        return;
    }
    g_wtSkinFn = 0x00DB8570u;
    if (!patch_call(site, (u32)&WingSkinStub)) { blog("WINGTEST skin hook: could not patch 00BF1ECB"); g_wtSkinFn = 0; return; }
    blog("WINGTEST skin hook installed at 00BF1ECB (F11 = drawn skin only, F12 = the game's bone array)");
}
static void wt_skin_toggle(int mode)
{
    if (!g_wtSkinFn) { blog("WINGTEST skin hook not installed"); return; }
    g_wtSkinMode = (g_wtSkinMode == mode) ? 0 : mode;
    blog("WINGTEST %s: arms %s (%s). draw calls %u, changed %u, skipped %u | pose jobs %u, changed %u | ball draws %u, moved %u",
         mode == 1 ? "F11" : "F12", g_wtSkinMode ? "LONGER" : "normal", mode == 1 ? "drawn skin only" : "right after posing",
         g_wtSkinCalls, g_wtSkinDone, g_wtSkinBad, g_wtJobCalls, g_wtJobDone, g_wtBallDraws, g_wtBallMoved);
}


/* ---- F5 (v16): where is the ball? -----------------------------------------------------------
 * 1. hands of every player drawn in the last second (wrist bones, world space, translation +30)
 * 2. one pass over the game's writable memory: every float triple within 60 cm (horizontal) of a
 *    hand and below hand height + 40 cm = candidate (skipping the bone arrays themselves)
 * 3. 40 samples, 50 ms apart: the ball is what goes down to the floor (y < 25) and up near the hand
 *    (y > 55) while you dribble. Logged: address, low / high, last value, the 32 bytes before it. */
#define WT_CAND 262144
static u32 g_wtCand[WT_CAND]; static float g_wtCMin[WT_CAND], g_wtCMax[WT_CAND]; static u32 g_wtCMoves[WT_CAND];
static int g_wtCandN = 0;
static float g_wtHand[64][3]; static int g_wtHandN = 0;
static int wt_in_worlds(u32 a)
{
    int k;
    for (k = 0; k < 32; k++) if (g_wtWorlds[k].world && a >= g_wtWorlds[k].world && a < g_wtWorlds[k].world + 130 * 64) return 1;
    return 0;
}
static void wt_ball(void)
{
    u32 now = GetTickCount(), a, lo, hi;
    int k, i, t, found = 0;
    MEMORY_BASIC_INFORMATION mbi;
    g_wtHandN = 0;
    for (k = 0; k < 32; k++) {
        u32 w = g_wtWorlds[k].world; int b = g_wtWorlds[k].base, hnd;
        if (!w || now - g_wtWorlds[k].tick > 1000) continue;
        for (hnd = 0; hnd < 2 && g_wtHandN < 64; hnd++) {
            u32 at = w + (u32)(b + (hnd ? 87 : 59)) * 64 + 0x30;
            if (!readable((void*)at, 12)) continue;
            memcpy(g_wtHand[g_wtHandN], (void*)at, 12);
            g_wtHandN++;
        }
    }
    if (!g_wtHandN) { blog("WINGTEST F5: no players drawn right now - get on court"); return; }
    blog("WINGTEST F5: %d hands, first at (%d, %d, %d) cm. Scanning memory ...", g_wtHandN,
         (int)g_wtHand[0][0], (int)g_wtHand[0][1], (int)g_wtHand[0][2]);
    g_wtCandN = 0;
    for (a = 0x00010000u; a < 0x7FFE0000u && g_wtCandN < WT_CAND; a = (u32)mbi.BaseAddress + (u32)mbi.RegionSize) {
        if (!VirtualQuery((void*)a, &mbi, sizeof(mbi))) break;
        if (mbi.State != MEM_COMMIT || (mbi.Protect & 0xFF) != PAGE_READWRITE || (mbi.Protect & 0x100)) continue;
        lo = (u32)mbi.BaseAddress; hi = lo + (u32)mbi.RegionSize;
        if (lo >= (u32)g_wtCand && lo < (u32)g_wtCand + sizeof(g_wtCand)) continue;
        for (i = 0; i + 12 <= (int)(hi - lo) && g_wtCandN < WT_CAND; i += 4) {
            const float *p = (const float*)(lo + (u32)i);
            if (!(i & 0xFFF) && !readable((void*)(lo + (u32)i), 4096)) { i += 4092; continue; }   /* page gone meanwhile */
            float y = p[1];
            if (!(y > 5.0f && y < 400.0f)) continue;
            for (k = 0; k < g_wtHandN; k++) {
                float dx = p[0] - g_wtHand[k][0], dz = p[2] - g_wtHand[k][2];
                if (dx > -60.0f && dx < 60.0f && dz > -60.0f && dz < 60.0f && y < g_wtHand[k][1] + 40.0f) break;
            }
            if (k == g_wtHandN || wt_in_worlds(lo + (u32)i)) continue;
            g_wtCand[g_wtCandN] = lo + (u32)i; g_wtCMin[g_wtCandN] = y; g_wtCMax[g_wtCandN] = y; g_wtCMoves[g_wtCandN] = 0;
            g_wtCandN++;
        }
    }
    blog("WINGTEST F5: %d candidate(s)%s. Watching them for 2 s (keep dribbling) ...", g_wtCandN, g_wtCandN >= WT_CAND ? " (list full)" : "");
    for (t = 0; t < 40; t++) {
        Sleep(50);
        for (i = 0; i < g_wtCandN; i++) {
            u32 v;
            float y;
            if (!read32(g_wtCand[i] + 4, &v)) { g_wtCand[i] = 0; continue; }
            memcpy(&y, &v, 4);
            if (!(y > -50.0f && y < 1000.0f)) continue;
            if (y < g_wtCMin[i]) { g_wtCMin[i] = y; g_wtCMoves[i]++; }
            if (y > g_wtCMax[i]) { g_wtCMax[i] = y; g_wtCMoves[i]++; }
        }
    }
    for (i = 0; i < g_wtCandN && found < 40; i++) {
        float v[3];
        u32 pre[8]; int j;
        if (!g_wtCand[i] || !(g_wtCMin[i] < 25.0f && g_wtCMax[i] > 55.0f && g_wtCMoves[i] >= 4)) continue;
        if (!readable((void*)(g_wtCand[i] - 32), 44)) continue;
        memcpy(v, (void*)g_wtCand[i], 12);
        for (j = 0; j < 8; j++) pre[j] = *(u32*)(g_wtCand[i] - 32 + j * 4);
        blog("WINGTEST   BALL? %08X  y %d..%d (%u moves)  now (%d, %d, %d)  before: %08X %08X %08X %08X %08X %08X %08X %08X",
             g_wtCand[i], (int)g_wtCMin[i], (int)g_wtCMax[i], g_wtCMoves[i], (int)v[0], (int)v[1], (int)v[2],
             pre[0], pre[1], pre[2], pre[3], pre[4], pre[5], pre[6], pre[7]);
        found++;
    }
    blog("WINGTEST F5: %d bouncing position(s) found", found);
}

static DWORD WINAPI wt_thread(LPVOID unused)
{
    int p5 = 0, p6 = 0, p7 = 0, p8 = 0, p9 = 0, p10 = 0, p11 = 0, p12 = 0;
    (void)unused;
    for (;;) {
        int k5 = (GetAsyncKeyState(0x74) & 0x8000) != 0, k6 = (GetAsyncKeyState(VK_F6) & 0x8000) != 0, k7 = (GetAsyncKeyState(VK_F7) & 0x8000) != 0,
            k8 = (GetAsyncKeyState(VK_F8) & 0x8000) != 0, k9 = (GetAsyncKeyState(VK_F9) & 0x8000) != 0, k10 = (GetAsyncKeyState(0x79) & 0x8000) != 0, k11 = (GetAsyncKeyState(0x7A) & 0x8000) != 0, k12 = (GetAsyncKeyState(0x7B) & 0x8000) != 0;
        if (k7 && !p7) wt_exe(!g_wtExeOn);
        if (k8 && !p8) wt_group(0, !g_wtOn[0]);
        if (k9 && !p9) wt_group(1, !g_wtOn[1]);
        if (k10 && !p10) wt_trace();
        if (k5 && !p5) wt_ball();
        if (k11 && !p11) wt_skin_toggle(1);
        if (k12 && !p12) wt_skin_toggle(2);
        if (k6 && !p6) {
            if (g_wtExeOn) wt_exe(0);
            if (g_wtOn[0]) wt_group(0, 0);
            if (g_wtOn[1]) wt_group(1, 0);
            g_wtSkinMode = 0;
            blog("WINGTEST F6: all back to normal");
        }
        p5 = k5; p6 = k6; p7 = k7; p8 = k8; p9 = k9; p10 = k10; p11 = k11; p12 = k12;
        /* v6: the game puts these values back right away -> keep writing ours while a group is on */
        if (g_wtOn[0] || g_wtOn[1]) {
            int i;
            for (i = 0; i < g_wtN; i++) if (g_wtOn[g_wt[i].form]) wt_table(i, 1);
            Sleep(2);
        } else Sleep(50);
    }
}
static void install_wingtest(void)
{
    char b[32];
    GetPrivateProfileStringA("WingspanTest", "Scale", "1.15", b, sizeof(b), g_ini);
    g_wtScale = ec_atof(b);
    if (g_wtScale < 0.5f || g_wtScale > 2.0f) g_wtScale = 1.15f;
    wt_skin_install();
    wt_job_install();
    wt_ball_install();
    CreateThread(NULL, 0, wt_thread, NULL, 0, NULL);
    blog("WINGTEST ready: F7 = exe animation skeleton arms, F8 = body POSITION tables, F9 = body OFFSET tables, F10 = trace, F11 = longer arms (drawn skin), F12 = longer arms (game bones), F6 = all normal. Scale x%d.%02d",
         (int)g_wtScale, ((int)(g_wtScale * 100.0f + 0.5f)) % 100);
}
#endif
