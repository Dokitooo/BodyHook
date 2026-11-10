/* ------------------------------------------------------------------------------------------ */
/* SHORT LENGTH (1.19.4 / 1.11.15-ML, NBA 2K14)                                                */
/*                                                                                             */
/* Edit Player > Body and Head gets a new row right under Weight: "Short Length" 0..10,        */
/* 5 = the shorts exactly as the cloth file has them. The value is kept per player (CFID) in   */
/* BodyHook_Shorts.ini and works in every mode (menus, games, replays).                        */
/*                                                                                             */
/* THE ROW                                                                                     */
/*   The Body and Head page is one entry of the Edit Player page table (0x1C bytes per page:   */
/*   name hash C412B93B, +8 = its item list). Items are 0x4C-byte records that end with a      */
/*   type-0 record (the page code 0x004FB640 counts them that way). BodyHook builds a copy of  */
/*   that list with one more record after "Weight" and points the page at the copy - the      */
/*   game's own list is never written. The new record is a copy of the "Hair Style" record     */
/*   (same kind of row: a value with left/right) with BodyHook's own callbacks:                */
/*     +04 label  +08 help text   (hashes 250C7803 / 6BF51A23 - answered by a 6-byte detour    */
/*                                 at the start of the game's text lookup 0x0065FEC0)          */
/*     +10 camera view (Weight's)  +28 value text                                              */
/*     +30 visibility: {mode, list of functions}; the game calls each and keeps the highest     */
/*         (0 shown, 3 hidden) - BodyHook's list = the Hair Style list with slot 1 replaced     */
/*     +34 left/right: {0, next, previous, 0}                                                   */
/*                                                                                             */
/* THE SHORTS                                                                                  */
/*   Moving cloth_shorts: every player has his own cloth instance - a 0xD0-byte copy of the    */
/*   ClothMesh header (0x021651B0) whose +78 (rest shape), +8C (stretch) and +90 (bend)        */
/*   pointers lead to the SHARED ClothMesh data. For a player whose Short Length is not 5,     */
/*   BodyHook points his instance at a copy of those three arrays in which the leg tubes are   */
/*   longer or shorter: every drawn particle below the lowest skinned (waistband) particle is  */
/*   moved along the leg (y) and every stretch / bend rest length is scaled to the new shape   */
/*   (the same rule as the Jersey Cloth Editor). One copy per ClothMesh and length, shared by   */
/*   every player with that length; the shared ClothMesh is never modified.                    */
/*   Applied right after the shorts cloth builder returns (before the first cloth reset), and  */
/*   in Edit Player at once for the instances of the edited player (the cloth settles into the */
/*   new length within a few frames).                                                          */
/*   Static (NONMOVING) shorts: not changed yet - their geometry lives in one GPU vertex buffer */
/*   shared by every player, so a per-player length needs a per-player copy of that buffer.     */
/* ------------------------------------------------------------------------------------------ */
#define SL_PAGE_HASH    0xC412B93Bu   /* "Body and Head" */
#define SL_PAGE_HASH12  0xED18E476u   /* NBA 2K12: its "Body" page (Height, Weight, ...; Hair Style is on another page) */
#define SL_WEIGHT_HASH  0x20FE790Du   /* "Weight" */
#define SL_LABEL_KEY    0x250C7803u   /* crc32("BODYHOOK_SHORT_LENGTH") */
#define SL_HELP_KEY     0x6BF51A23u   /* crc32("BODYHOOK_SHORT_LENGTH_HELP") */
#define SL_MAX_SEL      2048
#define SL_MAX_ITEMS    64
#define SL_MAX_VAR      256
#define SL_MAX_INST     96
#define CLOTH_MAGIC     (g_is2k12 ? 0x019957A3u : 0xF7CB9859u)   /* ClothMesh +A4 (1.11.21-ML: 2K12 has its own) */

static int   cfg_sl = 1, cfg_slShowCreated = 0;
static float cfg_slStep = 0.04f;              /* hem moves this share of the shorts' height per level */
#define g_slIni g_rosterIni        /* 1.11.26-ML: rosters\<roster>.ini [ShortLength] */
static CRITICAL_SECTION g_slLock;
static int   g_slReady = 0, g_slMenu = 0, g_slLoc = 0;
static volatile u32 st_slBuilds = 0, st_slApplied = 0, st_slLive = 0, st_slVariants = 0;

/* saved choices: cfid -> 0..10 (5 is never stored) */
static struct { u32 cfid; int len; } g_slSel[SL_MAX_SEL];
static int g_slSelCount = 0;

static int sl_get(u32 cfid)
{
    int i;
    for (i = 0; i < g_slSelCount; i++) if (g_slSel[i].cfid == cfid) return g_slSel[i].len;
    return 5;
}
static volatile u32 g_slGen = 1;      /* 1.11.24-ML: +1 on every change, so the draw-time cache (sl_node) re-applies */
static void sl_set(u32 cfid, int len)
{
    int i;
    g_slGen++;
    for (i = 0; i < g_slSelCount; i++) if (g_slSel[i].cfid == cfid) break;
    if (len == 5) {                                  /* default: drop the entry */
        if (i < g_slSelCount) g_slSel[i] = g_slSel[--g_slSelCount];
        return;
    }
    if (i == g_slSelCount) {
        if (g_slSelCount >= SL_MAX_SEL) return;
        g_slSelCount++;
        g_slSel[i].cfid = cfid;
    }
    g_slSel[i].len = len;
}

/* ---- length copies of a ClothMesh ---------------------------------------------------------- */
typedef struct {
    u32 mesh, rest0, len;            /* key: ClothMesh, its own rest array (guards against a reload), length */
    float *rest; u8 *dist, *bend;    /* the copies */
    u32 fp, dead;                    /* 1.19.9: content fingerprint of the ClothMesh the copy was made from;
                                        dead = made from a ClothMesh that has since been replaced */
} SlVar;
static SlVar g_slVar[SL_MAX_VAR];
static int g_slVarCount = 0;

/* one cloth instance we have seen being built */
static struct { u32 inst, mesh, cfid, player; } g_slInst[SL_MAX_INST];
static int g_slInstNext = 0;

static float sl_sqrt(float x)
{
    float r;
    if (x <= 0.0f) return 0.0f;
#if defined(_MSC_VER)
    __asm { fld x
            fsqrt
            fstp r }
#else
    __asm__("fsqrt" : "=t"(r) : "0"(x));
#endif
    return r;
}

/* scale every stretch / bend rest length by new / old particle distance (Jersey Cloth Editor rule) */
static void sl_scale_cons(u8 *blk, u32 n, const float *R0, const float *R1, u32 np)
{
    u32 k;
    for (k = 0; k < n; k++) {
        u32 *w = (u32*)(blk + 48 * (k >> 3) + 4 * (k & 7));
        u32 raw = *w & 0x3FF, a = (*w >> 10) & 0xFFF, b = a + ((*w >> 22) & 0x3FF);
        float dx, dy, dz, od, nd;
        int nr;
        if (a >= np || b >= np) continue;
        dx = R0[4*a] - R0[4*b]; dy = R0[4*a+1] - R0[4*b+1]; dz = R0[4*a+2] - R0[4*b+2];
        od = sl_sqrt(dx*dx + dy*dy + dz*dz);
        dx = R1[4*a] - R1[4*b]; dy = R1[4*a+1] - R1[4*b+1]; dz = R1[4*a+2] - R1[4*b+2];
        nd = sl_sqrt(dx*dx + dy*dy + dz*dz);
        if (od < 1e-4f) continue;
        if (raw == 32 && (raw / 32.0f - od > 0.25f * od || od - raw / 32.0f > 0.25f * od)) continue;   /* block padding */
        nr = (int)((float)raw * nd / od + 0.5f);
        if (nr < 1) nr = 1;
        if (nr > 1023) nr = 1023;
        *w = (*w & ~0x3FFu) | (u32)nr;
    }
}

/* the drawn particles (render skin +7C: 4-wide SoA, w0 bits 0-11 = particle) */
static void sl_drawn(const u8 *mesh, u8 *mark, u32 np)
{
    u32 nr = (*(const u32*)(mesh + 0xB8) >> 12) & 0x3FFF, v;
    const u8 *skin = *(u8* const*)(mesh + 0x7C);
    if (!readable(skin, ((nr + 3) / 4) * 64)) return;
    for (v = 0; v < nr; v++) {
        u32 p = *(const u32*)(skin + 64 * (v >> 2) + 4 * (v & 3)) & 0xFFF;
        if (p < np) mark[p] = 1;
    }
}

/* 1.19.9 / 1.11.20-ML: a fingerprint of a ClothMesh's CONTENT (counts, data pointers and a sample of the rest
 * shape, stretch and bend data). A cloth file / png that is loaded again (LivePngRefresh, a mode change) can get
 * its new ClothMesh at the SAME addresses as the old one - the copies made from the old shorts were then taken
 * for the new ones: changing Short Length showed the shorts that had been replaced (another cloth_shorts), and a
 * copy whose particles belong to another mesh is what tore the shorts into grey triangles in practice. */
static u32 sl_fp(u32 mesh)
{
    const u8 *M = (const u8*)mesh;
    u32 c = 0x811C9DC5u, np, i;
    const u32 *R;
#define SLMIX(v) (c = (c ^ (u32)(v)) * 0x01000193u)
    if (!mesh || !readable(M, 0xD0)) return 0;
    np = *(const u32*)(M + 0xB8) & 0xFFF;
    SLMIX(*(const u32*)(M + 0xB8)); SLMIX(*(const u32*)(M + 0xBC)); SLMIX(*(const u32*)(M + 0xC0));
    SLMIX(*(const u32*)(M + 0x78)); SLMIX(*(const u32*)(M + 0x8C)); SLMIX(*(const u32*)(M + 0x90));   /* 1.11.24-ML: no +7C */
    R = *(const u32* const*)(M + 0x78);
    if (np && np <= 4095 && readable(R, np * 16))
        for (i = 0; i < 48; i++) { u32 k = (i * (np * 4 - 1)) / 47; SLMIX(R[k]); }
    /* 1.11.24-ML: the constraint CONTENTS are not hashed any more - they changed while the shorts stayed the
     * same (log: one ClothMesh "loaded again" 137 times with the identical shape, a new copy every 1.5 s) */
#undef SLMIX
    return c ? c : 1;
}

/* the same, remembered for half a second per ClothMesh (the bench / practice checks ask many times a frame) */
static struct { u32 mesh, fp, tick; } g_slFpc[16];
static u32 sl_fp_cached(u32 mesh)
{
    u32 h = (mesh >> 5) & 15, now = GetTickCount();
    if (g_slFpc[h].mesh == mesh && g_slFpc[h].fp && now - g_slFpc[h].tick < 500) return g_slFpc[h].fp;
    g_slFpc[h].fp = 0; g_slFpc[h].mesh = mesh;
    g_slFpc[h].fp = sl_fp(mesh); g_slFpc[h].tick = now;
    return g_slFpc[h].fp;
}

static volatile int g_clothForceOwn;   /* bench.inc */
static volatile int g_psFsOwn = 0;      /* passer.inc: Freestyle passers installed */
/* 1.11.30-ML: own cloth in Freestyle from its first frame (was only once the passers were made, ~2 s in: meanwhile the
 * shared replay cloth - handed round between records, with another player's short-length copy - left the user's
 * shorts invisible, log f39c78fe) */
#define CLOTH_PRACTICE_OWN() (*(int*)0x1E85CD4 == 4 && (g_clothForceOwn || (g_psFsOwn && *(int*)0x1E85A90 && *(int*)0x1E85CD8 == 0)))
static int g_slKeepLog = 0;
static SlVar *sl_variant(u32 mesh, int len)
{
    const u8 *M = (const u8*)mesh;
    u32 np, nk, ncap, nd, nb, i, rest0, szd, szb;
    const float *R0;
    float *R1, ytop = -1e30f, yhem = 1e30f, ykin = 1e30f, H, yc, s, hem2;
    SlVar *v;
    u8 *mark;
    int k;
    u32 fp = sl_fp(mesh);
    rest0 = *(const u32*)(M + 0x78);
    for (k = 0; k < g_slVarCount; k++)
        if (g_slVar[k].mesh == mesh && g_slVar[k].rest0 == rest0 && g_slVar[k].len == (u32)len && !g_slVar[k].dead) {
            if (g_slVar[k].fp == fp) return &g_slVar[k];
            /* 1.11.30-ML: practice with the Freestyle passers - a png's moving/static shorts make the shared ClothMesh
             * look "loaded again" over and over (log 761dae9b), and the copy was then made from the png's shorts: the
             * passer drawn with the global shorts got torn cloth. There the first copy is kept. */
            if (CLOTH_PRACTICE_OWN()) {
                if (g_slKeepLog < 10) { g_slKeepLog++; blog("SHORT LENGTH  ClothMesh %08X changed during practice - length %d copy kept", mesh, len); }
                return &g_slVar[k];
            }
            /* same addresses, other shorts: never use it again (instances still on it are moved by sl_apply) */
            g_slVar[k].dead = 1;
            blog("SHORT LENGTH  ClothMesh %08X was loaded again with other shorts - its length %d copy is made again", mesh, len);
        }
    np = *(const u32*)(M + 0xB8) & 0xFFF; ncap = (*(const u32*)(M + 0xB8) >> 26) & 0x3F;
    nk = *(const u32*)(M + 0xC0) & 0xFFF;
    nd = *(const u32*)(M + 0xBC) & 0xFFFF; nb = *(const u32*)(M + 0xBC) >> 16;
    szd = ((nd + 7) / 8) * 48; szb = ((nb + 7) / 8) * 48;
    R0 = (const float*)rest0;
    if (!np || np > 4095 || !readable(R0, np * 16) || !readable(*(void* const*)(M + 0x8C), szd) || !readable(*(void* const*)(M + 0x90), szb))
        return 0;
    mark = (u8*)VirtualAlloc(NULL, np, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!mark) return 0;
    sl_drawn(M, mark, np);
    for (i = 2 * ncap; i < np; i++) if (mark[i]) {
        float y = R0[4*i+1];
        if (y > ytop) ytop = y;
        if (y < yhem) yhem = y;
        if (i < nk && y < ykin) ykin = y;
    }
    H = ytop - yhem;
    if (!(H > 5.0f) || H > 1000.0f) { VirtualFree(mark, 0, MEM_RELEASE); return 0; }
    /* the leg tubes start under the lowest skinned (waistband) particle; never higher than 30% down */
    yc = ykin;
    if (!(yc < ytop - 0.3f * H)) yc = ytop - 0.3f * H;
    if (!(yc > yhem + 0.15f * H)) yc = yhem + 0.45f * H;
    hem2 = yhem - (float)(len - 5) * cfg_slStep * H;      /* new hem height */
    s = (yc - hem2) / (yc - yhem);
    if (s < 0.25f) s = 0.25f;
    if (s > 2.5f) s = 2.5f;
    if (g_slVarCount >= SL_MAX_VAR) { VirtualFree(mark, 0, MEM_RELEASE); return 0; }
    R1 = (float*)VirtualAlloc(NULL, np * 16 + szd + szb + 64, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!R1) { VirtualFree(mark, 0, MEM_RELEASE); return 0; }
    memcpy(R1, R0, np * 16);
    for (i = 2 * ncap; i < np; i++)                        /* capsule end points (the legs) stay */
        if (mark[i] && R0[4*i+1] < yc) R1[4*i+1] = yc + (R0[4*i+1] - yc) * s;
    v = &g_slVar[g_slVarCount];
    v->mesh = mesh; v->rest0 = rest0; v->len = (u32)len; v->rest = R1; v->fp = fp; v->dead = 0;
    v->dist = (u8*)R1 + np * 16; v->bend = v->dist + szd;
    memcpy(v->dist, *(void* const*)(M + 0x8C), szd);
    memcpy(v->bend, *(void* const*)(M + 0x90), szb);
    sl_scale_cons(v->dist, nd, R0, R1, np);
    sl_scale_cons(v->bend, nb, R0, R1, np);
    VirtualFree(mark, 0, MEM_RELEASE);
    g_slVarCount++;                                        /* published last: readers only look below the count */
    st_slVariants++;
    blog("SHORT LENGTH  ClothMesh %08X length %d: hem %d -> %d cm (leg part x%d%%, %u particles, %u+%u constraints)",
         mesh, len, (int)yhem, (int)hem2, (int)(s * 100.0f + 0.5f), np, nd, nb);
    return v;
}

/* is this (still) a cloth instance of that mesh, pointing at its own data or one of our copies? */
static int sl_inst_ok(u32 inst, u32 mesh)
{
    u32 r, k;
    if (!inst || !mesh || !readable((void*)inst, 0xD0) || !readable((void*)mesh, 0xD0)) return 0;
    if (*(u32*)(inst + 0xA4) != CLOTH_MAGIC || *(u32*)(inst + 0xA8) != mesh) return 0;
    if (*(u32*)(inst + 0xB8) != *(u32*)(mesh + 0xB8) || *(u32*)(inst + 0xBC) != *(u32*)(mesh + 0xBC)) return 0;
    r = *(u32*)(inst + 0x78);
    if (r == *(u32*)(mesh + 0x78)) return 1;
    for (k = 0; k < (u32)g_slVarCount; k++) if (g_slVar[k].mesh == mesh && (u32)g_slVar[k].rest == r) return 1;
    return 0;
}

/* point one instance at the copy for this length (5 = the ClothMesh's own data) */
static int sl_apply(u32 inst, u32 mesh, int len)
{
    u32 rest, dist, bend;
    if (!sl_inst_ok(inst, mesh)) return 0;
    if (len == 5) {
        rest = *(u32*)(mesh + 0x78); dist = *(u32*)(mesh + 0x8C); bend = *(u32*)(mesh + 0x90);
    } else {
        SlVar *v = sl_variant(mesh, len);
        if (!v) return 0;
        rest = (u32)v->rest; dist = (u32)v->dist; bend = (u32)v->bend;
    }
    if (*(u32*)(inst + 0x78) == rest && *(u32*)(inst + 0x8C) == dist && *(u32*)(inst + 0x90) == bend) return 1;
    /* the solver reads these once per frame step; each write is a single aligned 4-byte store */
    *(volatile u32*)(inst + 0x8C) = dist;
    *(volatile u32*)(inst + 0x90) = bend;
    *(volatile u32*)(inst + 0x78) = rest;
    return 2;
}

/* called right after the shorts cloth builder (0x00BDFA80 via ClothSim1): out+0 = the instance */
static void sl_after_build(void *player, void *out)
{
    u32 inst = 0, mesh, cfid;
    int len, k, slot = -1;
    if (!g_slReady || !out || !player) return;
    if (!read32((u32)out, &inst) || !inst || !read32(inst + 0xA8, &mesh) || !mesh) return;   /* static shorts: no instance */
    cfid = player_key((u8*)player);
    EnterCriticalSection(&g_slLock);
    st_slBuilds++;
    for (k = 0; k < SL_MAX_INST; k++) if (g_slInst[k].inst == inst) { slot = k; break; }
    if (slot < 0) slot = g_slInstNext++ % SL_MAX_INST;
    g_slInst[slot].inst = inst; g_slInst[slot].mesh = mesh; g_slInst[slot].cfid = cfid; g_slInst[slot].player = (u32)player;
    len = cfid ? sl_get(cfid) : 5;
    if (sl_apply(inst, mesh, len) == 2 && len != 5) {
        st_slApplied++;
        if (cfg_diagnostics) { char ks[12]; key_str(cfid, ks); blog("SHORT LENGTH  %s %s: length %d on cloth instance %08X", key_what(cfid), ks, len, inst); }
    }
    LeaveCriticalSection(&g_slLock);
}

static int bh_own_cloth(u32 inst);   /* bench.inc: inst lies in BodyHook's own cloth object memory */
/* Edit Player: change the instances of that player right away */
static void sl_live(u32 cfid, int len)
{
    int k, n = 0;
    for (k = 0; k < SL_MAX_INST; k++)
        if (g_slInst[k].cfid == cfid && g_slInst[k].inst && bh_own_cloth(g_slInst[k].inst) &&   /* 1.11.24-ML: game ones at draw */
            sl_apply(g_slInst[k].inst, g_slInst[k].mesh, len)) n++;
    st_slLive += (u32)n;
    { char ks[12]; key_str(cfid, ks); blog("SHORT LENGTH  %s %s -> %d%s (%d moving cloth instance(s) changed now)", key_what(cfid), ks, len, len == 5 ? " (original)" : "", n); }
}

/* 1.19.6 / 1.11.17-ML: practice mode (and some menus) keep a player's cloth when another player takes
 * over his record (the record is filled with the new player after the cloth was built), so the cloth
 * kept the old player's length. Every 200 ms the record behind each known cloth instance is checked:
 * when it now holds another player, his length is applied (5 = the cloth file's own shape). */
static volatile u32 st_slWatch = 0;
static void sl_watch_once(void)
{
    int k;
    EnterCriticalSection(&g_slLock);
    for (k = 0; k < SL_MAX_INST; k++) {
        u32 inst = g_slInst[k].inst, cfid, p = g_slInst[k].player;
        int len, r;
        if (!inst) continue;
        if (!sl_inst_ok(inst, g_slInst[k].mesh)) { g_slInst[k].inst = 0; continue; }   /* freed / reused */
        cfid = (p && readable((void*)p, 0x158)) ? player_key((u8*)p) : g_slInst[k].cfid;
        len = cfid ? sl_get(cfid) : 5;
        /* 1.11.24-ML: only BodyHook's own cloth objects are changed from this thread. A game cloth instance
         * can be freed at any moment (its memory still looks like an instance) - writing into it damaged the
         * game's heap (crash later in the game's allocator, nba2k14+99D642). Game instances get their length
         * when they are built and every time they are drawn (sl_node), which is always on live memory. */
        r = bh_own_cloth(inst) ? sl_apply(inst, g_slInst[k].mesh, len) : 0;
        if (cfid != g_slInst[k].cfid || r == 2) {
            if (r == 2) st_slWatch++;
            { char ks[12]; key_str(cfid, ks); blog("SHORT LENGTH  cloth %08X: record %08X now holds %s %s -> length %d", inst, p, key_what(cfid), ks, len); }
            g_slInst[k].cfid = cfid;
        }
    }
    LeaveCriticalSection(&g_slLock);
}
static DWORD WINAPI sl_watch_thread(LPVOID unused)
{
    (void)unused;
    for (;;) { Sleep(200); sl_watch_once(); }
}

/* 1.19.7 / 1.11.18-ML: the length follows the player who is DRAWN with the cloth. The player model's
 * functions (FLOOR vtable, wrapped in bench.inc floor_call) get a render node: +08 = player record,
 * +30 = shorts cloth object (+0 = cloth instance). Practice mode draws the player with a cloth set that
 * was built for someone else (one shared set; seen in BENCHDIAG logs), so the builder never saw him -
 * here every node gets its player's length right before the game uses it. Fast path: a small cache
 * (instance -> player, CFID, rest pointer) so an unchanged node costs a few reads. */
static struct { u32 inst, player, cfid, rest, gen; } g_slNode[64];
static volatile u32 st_slNode = 0;
static void sl_node(u32 d)
{
    u32 obj = 0, inst = 0, player = 0, cfid, mesh = 0, h, rest;
    int len, r;
    if (!g_slReady || !d) return;
    if (!read32(d + 0x30, &obj) || !obj || !read32(obj, &inst) || !inst || !read32(d + 0x08, &player) || !player) return;
    if (!read32(inst + 0x78, &rest)) return;
    h = (inst >> 4) & 63;
    cfid = player_key((u8*)player);
    if (g_slNode[h].inst == inst && g_slNode[h].player == player && g_slNode[h].cfid == cfid && g_slNode[h].rest == rest &&
        g_slNode[h].gen == g_slGen) return;
    if (!read32(inst + 0xA8, &mesh) || !mesh) return;
    EnterCriticalSection(&g_slLock);
    len = cfid ? sl_get(cfid) : 5;
    r = sl_apply(inst, mesh, len);
    if (r) {
        int k;
        for (k = 0; k < SL_MAX_INST; k++)                      /* the watcher must agree with the drawn player */
            if (g_slInst[k].inst == inst) { g_slInst[k].player = player; g_slInst[k].cfid = cfid; }
        g_slNode[h].inst = inst; g_slNode[h].player = player; g_slNode[h].cfid = cfid; g_slNode[h].rest = *(u32*)(inst + 0x78); g_slNode[h].gen = g_slGen;
        if (r == 2) {
            st_slNode++;
            { char ks[12]; key_str(cfid, ks); blog("SHORT LENGTH  drawn: %s %s on cloth %08X (record %08X) -> length %d", key_what(cfid), ks, inst, player, len); }
        }
    }
    LeaveCriticalSection(&g_slLock);
}

static void sl_load(void)
{
    static char buf[32768];
    char *p;
    DWORD n = GetPrivateProfileSectionA("ShortLength", buf, sizeof(buf), g_slIni);
    g_slSelCount = 0; g_slGen++;
    for (p = buf; n && *p; p += s_len(p) + 1) {
        u32 cfid = 0; int i = 0, len = 0;
        char k[16];
        while (p[i] && p[i] != '=' && i < 15) { k[i] = p[i]; i++; }
        k[i] = 0;
        if (p[i] != '=' || !(cfid = key_parse(k))) continue;
        { char v[64]; s_cpy(v, p + i + 1, 64); val_split(v, cfid); }
        i++;
        while (p[i] == ' ') i++;
        if (p[i] < '0' || p[i] > '9') continue;
        for (; p[i] >= '0' && p[i] <= '9' && len < 100; i++) len = len * 10 + (p[i] - '0');
        if (len > 10) len = 10;
        sl_set(cfid, len);
    }
}

/* ---- the menu row ---------------------------------------------------------------------------- */
u16 g_slLabelW[] = { 'S','h','o','r','t',' ','L','e','n','g','t','h',0 };
u16 g_slHelpW[]  = { 'S','e','l','e','c','t',' ','S','h','o','r','t',' ','L','e','n','g','t','h','.',' ',
                            '5',' ','=',' ','o','r','i','g','i','n','a','l',0 };
u32 g_slLocResume = 0;

/* detour at the start of the text lookup 0x0065FEC0 (fastcall: ecx = text table, edx = key; returns
 * the UTF-16 text or 0). Our two keys answer with our text, everything else runs the original. */
__declspec(naked) void SlTextLookup(void)
{
    __asm {
        mov  eax, edx
        or   eax, 1
        cmp  eax, 250C7803h
        je   label_
        cmp  eax, 6BF51A23h
        je   help_
        cmp  eax, 9E50F30Dh
        je   wlabel_
        cmp  eax, 0B16A1C6Fh
        je   whelp_
        sub  esp, 0Ch
        mov  eax, [ecx + 10h]
        jmp  dword ptr [g_slLocResume]
    label_:
        lea  eax, g_slLabelW
        ret
    help_:
        lea  eax, g_slHelpW
        ret
    wlabel_:
        lea  eax, g_wsLabelW
        ret
    whelp_:
        lea  eax, g_wsHelpW
        ret
    }
}

/* 1.11.30-ML: the same answers from INSIDE the lookup (2K14, 0065FED8: cmp ecx,eax / je 0065FF56 / lea esp,[esp]).
 * Another mod (SigExt 1.10 / GotoShotExt) owning the first bytes made the start pattern fail -> no Short Length /
 * Wingspan rows anywhere (log f39c78fe). Here edx = key|1 and ebx ebp esi edi + 0C bytes of locals are on the stack. */
u32 g_slMidFound = 0, g_slMidLoop = 0;
__declspec(naked) void SlTextLookupMid(void)
{
    __asm {
        cmp  edx, 250C7803h
        je   label_
        cmp  edx, 6BF51A23h
        je   help_
        cmp  edx, 9E50F30Dh
        je   wlabel_
        cmp  edx, 0B16A1C6Fh
        je   whelp_
        cmp  ecx, eax
        je   none_
        jmp  dword ptr [g_slMidLoop]
    none_:
        jmp  dword ptr [g_slMidFound]
    label_:
        lea  eax, g_slLabelW
        jmp  out_
    help_:
        lea  eax, g_slHelpW
        jmp  out_
    wlabel_:
        lea  eax, g_wsLabelW
        jmp  out_
    whelp_:
        lea  eax, g_wsHelpW
    out_:
        pop  edi
        pop  esi
        pop  ebp
        pop  ebx
        add  esp, 0Ch
        ret
    }
}

static HairVis_t g_slVisCreated = 0;   /* the stock Hair Style visibility: 0 = created player */
static Fmt_t     g_slFmt = 0;
static u32       g_slStrHandler = 0, g_slEditRec = 0, g_slFmtKey = STRING_FMT_KEY;   /* 2K12: the Hair Style row's own key */
static u8        g_slRec[0x4C];
static u32       g_slVisObj[9], g_slVisVt[8], g_slStepObj[4];
static u8       *g_slList = 0;

static u8 *sl_rec(u8 *fromEcx)              /* the record Edit Player is editing */
{
    u32 p = 0;
    u8 *rec = fromEcx;
    if (g_slEditRec && read32(g_slEditRec, &p) && p && readable((void*)p, 0xA0)) rec = (u8*)p;
    if (!rec || !readable(rec, 0xA0)) return 0;
    return rec;
}
static u32 sl_rec_cfid(u8 *fromEcx) { return player_key(sl_rec(fromEcx)); }        /* value shown */
static u32 sl_rec_raw(u8 *fromEcx)  { return player_key_raw(sl_rec(fromEcx)); }    /* row shown / changed */
static void ws_set(u32 cfid, int v);
static void ws_save(u32 cfid, int v, u8 *rec);
/* a change for a generic player: if his roster ID held someone else before, that player's old values go */
static void gen_claim(u32 key, u8 *rec)
{
    char nm[40]; const char *saved;
    if (!(key & GEN_BIT) || !rec) return;
    player_name(rec, nm, 40);
    if (!nm[0]) return;
    saved = gn_get(key & 0xFFFF);
    if (saved && saved[0] && !name_eq(saved, nm)) {
        char ks[12];
        key_str(key, ks);
        blog("roster ID %s now holds %s (was %s): his old Short Length / Wingspan dropped", ks + 1, nm, saved);
        es_remember(ES_SL, key); es_remember(ES_WS, key);          /* 2.0 fix: tentative while Edit Player edits a copy */
        EnterCriticalSection(&g_slLock); sl_set(key, 5); LeaveCriticalSection(&g_slLock);
        edit_write("ShortLength", ks, NULL);
        ws_set(key, 5); edit_write("Wingspan", ks, NULL);
    }
    gn_set(key & 0xFFFF, nm);
}

/* one of the row's visibility functions (the game asks each and keeps the highest: 0 = shown, 3 = hidden) */
int __cdecl SlVisible(void)
{
    if (!cfg_sl) return 3;
    return sl_rec_raw(0) ? 0 : 3;              /* 1.11.26-ML: generic / created faces too (by roster ID) */
}

void __fastcall SlText(u8 *rec, void *sink, u32 unused)
{
    static u16 wbuf[16];
    /* 1.11.24-ML: the game's own string formatter (2K12, type 959F7918) reads the value object at +24h as
     * a pointer to {text, 0}: it read stack garbage there (crash in wcslen, nba2k12 C6D8F0). +24h = itself. */
    struct { const u16 *w; const char *a; u32 pad[7]; void *self; u32 pad2[6]; } sv;
    void *vals[1];
    struct { u32 zero, count; void **values; u32 handler; } args;
    int len;
    (void)unused;
    EnterCriticalSection(&g_slLock);
    len = sl_get(sl_rec_cfid(rec));
    LeaveCriticalSection(&g_slLock);
    if (len >= 10) { wbuf[0] = '1'; wbuf[1] = '0'; wbuf[2] = 0; }
    else { wbuf[0] = (u16)('0' + len); wbuf[1] = 0; }
    memset(&sv, 0, sizeof(sv));
    sv.w = wbuf; sv.a = 0; sv.self = &sv;
    vals[0] = &sv;
    args.zero = 0; args.count = 1; args.values = vals; args.handler = g_slStrHandler;
    g_slFmt(sink, g_slFmtKey, &args);
}

static int sl_step(u8 *rec, int dir)
{
    u32 cfid = sl_rec_raw(rec);
    int len, now;
    char key[12], val[4], v[64];
    if (!cfid) return 0;
    gen_claim(cfid, sl_rec(rec));
    es_remember(ES_SL, cfid);                  /* 2.0 fix: undone if Edit Player is left without accepting */
    EnterCriticalSection(&g_slLock);
    len = sl_get(cfid);
    now = len + dir;
    if (now < 0) now = 0;
    if (now > 10) now = 10;
    if (now == len) { LeaveCriticalSection(&g_slLock); return 0; }
    sl_set(cfid, now);
    sl_live(cfid, now);
    LeaveCriticalSection(&g_slLock);
    key_str(cfid, key);
    u32_to_dec((u32)now, val);
    val_named(v, sizeof(v), val, cfid, sl_rec(rec));
    edit_write("ShortLength", key, now == 5 ? NULL : v);   /* 2.1: written when the game saves (2.0 fix: once accepted) */
    return 1;
}
int __fastcall SlNext(u8 *rec, u32 unused) { (void)unused; return sl_step(rec, +1); }
int __fastcall SlPrev(u8 *rec, u32 unused) { (void)unused; return sl_step(rec, -1); }

/* 1.11.25-ML: the Wingspan row (wingspan.inc) - same kind of row, its own value */
static u32 g_wsVisObj[9], g_wsVisVt[8], g_wsStepObj[4];
static u8  g_wsRec[0x4C];
int __cdecl WsVisible(void)
{
    if (!cfg_ws) return 3;
    return sl_rec_raw(0) ? 0 : 3;
}
static void sl_text_num(void *sink, int v)
{
    static u16 wbuf[16];
    struct { const u16 *w; const char *a; u32 pad[7]; void *self; u32 pad2[6]; } sv;   /* see SlText */
    void *vals[1];
    struct { u32 zero, count; void **values; u32 handler; } args;
    if (v >= 10) { wbuf[0] = '1'; wbuf[1] = '0'; wbuf[2] = 0; }
    else { wbuf[0] = (u16)('0' + (v < 0 ? 0 : v)); wbuf[1] = 0; }
    memset(&sv, 0, sizeof(sv));
    sv.w = wbuf; sv.a = 0; sv.self = &sv;
    vals[0] = &sv;
    args.zero = 0; args.count = 1; args.values = vals; args.handler = g_slStrHandler;
    g_slFmt(sink, g_slFmtKey, &args);
}
void __fastcall WsText(u8 *rec, void *sink, u32 unused)
{
    (void)unused;
    sl_text_num(sink, ws_get(sl_rec_cfid(rec)));
}
static int ws_step(u8 *rec, int dir)
{
    u32 cfid = sl_rec_raw(rec);
    int v, now;
    if (!cfid || !cfg_ws) return 0;
    gen_claim(cfid, sl_rec(rec));
    v = ws_get(cfid);
    now = v + dir;
    if (now < 0) now = 0;
    if (now > 10) now = 10;
    if (now == v) return 0;
    es_remember(ES_WS, cfid);                  /* 2.0 fix: undone if Edit Player is left without accepting */
    ws_set(cfid, now);
    ws_save(cfid, now, sl_rec(rec));
    { char ks[12]; key_str(cfid, ks); blog("WINGSPAN  %s %s -> %d%s", key_what(cfid), ks, now, now == 5 ? " (original)" : ""); }
    return 1;
}
int __fastcall WsNext(u8 *rec, u32 unused) { (void)unused; return ws_step(rec, +1); }
int __fastcall WsPrev(u8 *rec, u32 unused) { (void)unused; return ws_step(rec, -1); }

/* find the Edit Player page table entry "Body and Head" whose item list holds the Hair Style record */
typedef struct { u32 item, page; } SlFind;
static int sl_page_cb(u32 lo_, u32 hi_, void *ctx)
{
    SlFind *f = (SlFind*)ctx;
    u32 a;
    for (a = (lo_ + 3) & ~3u; a + 0x1C <= hi_; a += 4) {
        const u32 *q = (const u32*)a;
        u32 list = q[2], it;
        int n;
        if (q[0] != (g_is2k12 ? SL_PAGE_HASH12 : SL_PAGE_HASH) || q[1] != 0 || list < g_imageBase || list >= g_imageBase + g_exeImageSize) continue;
        if (!readable((void*)list, 0x4C)) continue;
        if (g_is2k12) {   /* 1.11.21-ML: the 2K12 Body page (Hair Style is not on it): the one with Weight */
            for (it = list, n = 0; n < SL_MAX_ITEMS && readable((void*)it, 0x4C) && *(u32*)it; it += 0x4C, n++)
                if (*(u32*)(it + 4) == SL_WEIGHT_HASH) { f->page = a; return 1; }
            continue;
        }
        for (it = list, n = 0; n < SL_MAX_ITEMS && readable((void*)it, 0x4C) && *(u32*)it; it += 0x4C, n++)
            if (it == f->item) { f->page = a; return 1; }
    }
    return 0;
}

/* the Hair Style record. hair_item_cb() wants its text callback (+28) inside the exe, but once the Hair Style
 * feature is installed that callback is BodyHook's HairText - accept both (1.19.4 first build missed this). */
static int sl_hair_cb(u32 lo_, u32 hi_, void *ctx)
{
    HairFind *f = (HairFind*)ctx;
    u32 a;
    for (a = ((lo_ + 3) & ~3u) + 4; a + 0x4C <= hi_; a += 4) {
        const u32 *q = (const u32*)a;
        if (q[0] != HAIR_ITEM_HASH) continue;
        if (q[-1] != 1 || q[3] != 4) continue;
        if (!(in_text(q[9]) || q[9] == (u32)&HairText) || !q[11] || !q[12]) continue;
        f->item = a - 4;
        return 1;
    }
    return 0;
}

static void install_shortlen(void)
{
    HairFind hf;
    SlFind sf;
    u8 *loc;
    u32 list, it, n = 0, weightAt = 0, textFn, visObj, visVt, visFn, stepObj, i;
    static const u8 kLoc[] = { 0x83,0xEC,0x0C, 0x8B,0x41,0x10, 0x53, 0x83,0xC1,0x10, 0x55, 0x56, 0x83,0xCA,0x01, 0x57 };
    InitializeCriticalSection(&g_slLock);
    cfg_sl = ini_int2("ShortLength", "Enabled", 1);
    {
        int st = ini_int2("ShortLength", "StepPercent", 4);
        if (st < 1) st = 1;
        if (st > 8) st = 8;
        cfg_slStep = (float)st / 100.0f;
    }
    if (!cfg_sl) blog("short length: off ([ShortLength] Enabled=0)");
    else {
        sl_load();
        for (i = 0; i < (u32)g_slSelCount; i++) { char ks[12]; key_str(g_slSel[i].cfid, ks); blog("   short length: %s -> %d (saved choice)", ks, g_slSel[i].len); }
        g_slReady = 1;                              /* the cloth part works even if the menu row can't be added */
        CreateThread(NULL, 0, sl_watch_thread, NULL, 0, NULL);
    }
    if (!cfg_sl && !cfg_ws) return;             /* 1.11.25-ML: the rows are shared by Short Length and Wingspan */

    /* 1. text lookup detour (label + help of the new row) */
    loc = find_text(kLoc, sizeof(kLoc), 0);
    if (loc && !find_text(kLoc, sizeof(kLoc), loc + 1)) {
        u8 b[6];
        int rel = (int)((u32)&SlTextLookup - ((u32)loc + 5));
        g_slLocResume = (u32)loc + 6;
        b[0] = 0xE9; memcpy(b + 1, &rel, 4); b[5] = 0x90;
        g_slLoc = patch_bytes((u32)loc, b, 6, 1);
    }
    if (!g_slLoc && !g_is2k12) {   /* 1.11.30-ML: the start is another mod's: hook inside it */
        static const u8 kMid[] = { 0x89,0x4C,0x24,0x18, 0x89,0x44,0x24,0x10, 0x3B,0xC1, 0x74,0x7A, 0x8D,0x64,0x24,0x00,
                                   0x8B,0x40,0x08, 0x89,0x44,0x24,0x14, 0x8B,0xE8 };
        static const u8 kMid2[] = { 0x3B,0xC1, 0x74,0x7A, 0x8D,0x64,0x24,0x00, 0x8B,0x40,0x08, 0x89,0x44,0x24,0x14, 0x8B,0xE8,
                                    0x8D,0xA4,0x24,0x00,0x00,0x00,0x00, 0x0F,0xB7,0x45,0x02 };
        u8 *m = find_text(kMid, sizeof(kMid), 0), *m2;
        m2 = m ? m + 8 : find_text(kMid2, sizeof(kMid2), 0);   /* 0065FED0 may be GotoShotExt's jump */
        if (m2 && !(m && find_text(kMid, sizeof(kMid), m + 1)) && !(!m && find_text(kMid2, sizeof(kMid2), m2 + 1))) {
            u8 b[8];
            int rel = (int)((u32)&SlTextLookupMid - ((u32)m2 + 5));
            g_slMidLoop = (u32)m2 + 8; g_slMidFound = (u32)m2 + 4 + 0x7A;
            b[0] = 0xE9; memcpy(b + 1, &rel, 4); b[5] = b[6] = b[7] = 0x90;
            g_slLoc = patch_bytes((u32)m2, b, 8, 1);
            if (g_slLoc) blog("SHORT LENGTH  text lookup start is changed by another mod - hooked inside it at %08X", (u32)m2);
        }
    }
    if (!g_slLoc) { blog("WARNING short length: text lookup not found - no Edit Player row (saved lengths still apply)"); return; }

    /* 2. the Hair Style record (template) and the page that lists it */
    hf.item = 0;
    for_data_pages(sl_hair_cb, &hf);
    if (!hf.item) { blog("WARNING short length: Edit Player 'Hair Style' record not found - no row"); return; }
    textFn  = g_hairOrigText ? (u32)g_hairOrigText : *(u32*)(hf.item + 0x28);
    visObj  = *(u32*)(hf.item + 0x30);
    stepObj = *(u32*)(hf.item + 0x34);
    visVt   = readable((void*)visObj, 0x24) ? *(u32*)(visObj + 4) : 0;
    visFn   = g_hairOrigVis ? (u32)g_hairOrigVis : (visVt && readable((void*)visVt, 12) ? *(u32*)(visVt + 4) : 0);
    if (!in_text(textFn) || !hair_bytes(textFn, "83 EC 14 8B C2 89 0C 24") || *(u8*)(textFn + 0x33) != 0xE8 ||
        !hair_bytes(call_target_of(textFn + 0x33), "8B 44 24 04 89 44 24 04 85 C0") ||
        !in_text(visFn) || !hair_bytes(visFn, "8B 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 85 C0 75 ?? A1") ||
        !readable((void*)stepObj, 16) || !readable((void*)visVt, 12)) {
        blog("WARNING short length: Hair Style record not as expected - no row"); return;
    }
    g_slFmt = (Fmt_t)call_target_of(textFn + 0x33);
    g_slEditRec = *(u32*)(visFn + 2);
    g_slVisCreated = (HairVis_t)visFn;
#ifdef BODYHOOK_HAS_2K12
    if (g_is2k12) {   /* 1.11.21-ML: 2K12 has no {0:STRING} handler: the Hair Style row's own key with HairStr12 */
        if (*(u8*)(textFn + 0x14) != 0xBA) { blog("WARNING short length: 2K12 format key not found - no row"); return; }
        g_slFmtKey = *(u32*)(textFn + 0x15);
        g_slStrHandler = (u32)&HairStr12;
    } else
#endif
    g_slStrHandler = g_hairStrHandler ? g_hairStrHandler : find_str_handler();
    if (!g_slStrHandler) { blog("WARNING short length: string argument handler not found - no row"); return; }
    sf.item = hf.item; sf.page = 0;
    for_data_pages(sl_page_cb, &sf);
    if (!sf.page) { blog("WARNING short length: %s page not found - no row", g_is2k12 ? "Body" : "Body and Head"); return; }
    list = *(u32*)(sf.page + 8);
    for (it = list; n < SL_MAX_ITEMS && *(u32*)it; it += 0x4C, n++)
        if (*(u32*)(it + 4) == SL_WEIGHT_HASH) weightAt = n;
    if (!weightAt && *(u32*)(list + 4) != SL_WEIGHT_HASH) { blog("WARNING short length: 'Weight' row not found - no row"); return; }

    /* 3. our record: the Hair Style record with our texts and callbacks */
    memcpy(g_slRec, (void*)hf.item, 0x4C);
    memcpy(g_slVisObj, (void*)visObj, sizeof(g_slVisObj));
    {   /* 1.11.28-ML: Weight's list of visibility functions + ours. It was the Hair Style list, whose first function
           (004FDC70) hides a row in MyCareer's Edit Player (there only Height and Weight are shown) - so the Short
           Length / Wingspan rows were never in MyCareer. Weight's list (004FDCC0) keeps them wherever Weight is. */
        /* 1.11.29-ML: Weight's 004FDCC0 answers 2 (greyed, not editable) in MyCareer: our rows use only SlVisible on
           2K14 (shown + editable wherever the player has a key); 2K12 keeps the Hair Style rules. */
        int k = 0;
        u32 wVt = g_is2k12 ? 0 : 1;
        if (!k && wVt) { g_slVisVt[0] = (u32)&SlVisible; g_slVisVt[1] = 0; k = 1; }
        else {   /* not as expected: the old way (Hair Style list, its created-players-only function replaced) */
            int put = 0;
            for (; k < 6 && *(u32*)(visVt + 4 * k); k++) {
                u32 f = *(u32*)(visVt + 4 * k);
                if (f == visFn || f == (u32)&HairVisible) { f = (u32)&SlVisible; put = 1; }
                g_slVisVt[k] = f;
            }
            if (!put) g_slVisVt[k++] = (u32)&SlVisible;
            g_slVisVt[k] = 0;
        }
        blog("SHORT LENGTH / WINGSPAN rows: %s", wVt ? "shown and editable in every mode (MyCareer too)" : "Hair Style rules");
    }
    memcpy(g_slStepObj, (void*)stepObj, 16);
    g_slVisObj[1] = (u32)g_slVisVt;
    g_slStepObj[1] = (u32)&SlNext; g_slStepObj[2] = (u32)&SlPrev;
    *(u32*)(g_slRec + 0x04) = SL_LABEL_KEY;
    *(u32*)(g_slRec + 0x08) = SL_HELP_KEY;
    *(u32*)(g_slRec + 0x0C) = 0;
    *(u32*)(g_slRec + 0x10) = *(u32*)(list + weightAt * 0x4C + 0x10);   /* camera view: the one Weight uses (not the head) */
    *(u32*)(g_slRec + 0x28) = (u32)&SlText;
    *(u32*)(g_slRec + 0x30) = (u32)g_slVisObj;
    *(u32*)(g_slRec + 0x34) = (u32)g_slStepObj;
    /* 1.11.25-ML: the Wingspan record, built the same way */
    memcpy(g_wsRec, g_slRec, 0x4C);
    memcpy(g_wsVisObj, g_slVisObj, sizeof(g_wsVisObj));
    memcpy(g_wsVisVt, g_slVisVt, sizeof(g_wsVisVt));
    for (i = 0; i < 8; i++) if (g_wsVisVt[i] == (u32)&SlVisible) g_wsVisVt[i] = (u32)&WsVisible;
    memcpy(g_wsStepObj, g_slStepObj, sizeof(g_wsStepObj));
    g_wsVisObj[1] = (u32)g_wsVisVt;
    g_wsStepObj[1] = (u32)&WsNext; g_wsStepObj[2] = (u32)&WsPrev;
    *(u32*)(g_wsRec + 0x04) = WS_LABEL_KEY;
    *(u32*)(g_wsRec + 0x08) = WS_HELP_KEY;
    *(u32*)(g_wsRec + 0x28) = (u32)&WsText;
    *(u32*)(g_wsRec + 0x30) = (u32)g_wsVisObj;
    *(u32*)(g_wsRec + 0x34) = (u32)g_wsStepObj;

    /* 4. the new list: ... Weight, Short Length, ... , end record. The game's list stays as it is. */
    {
        u32 add = (cfg_sl ? 1 : 0) + (cfg_ws ? 1 : 0), at = weightAt + 1;
        g_slList = (u8*)VirtualAlloc(NULL, (n + 1 + add) * 0x4C, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!g_slList) return;
        memcpy(g_slList, (void*)list, (weightAt + 1) * 0x4C);
        if (cfg_sl) { memcpy(g_slList + at * 0x4C, g_slRec, 0x4C); at++; }
        if (cfg_ws) { memcpy(g_slList + at * 0x4C, g_wsRec, 0x4C); at++; }
        memcpy(g_slList + at * 0x4C, (void*)(list + (weightAt + 1) * 0x4C), (n - weightAt - 1 + 1) * 0x4C);  /* rest + end record */
    }
    {
        u32 nl = (u32)g_slList;
        if (!patch_bytes(sf.page + 8, &nl, 4, 0)) { blog("WARNING short length: page could not be changed - no row"); return; }
    }
    g_slMenu = 1;
    blog("SHORT LENGTH INSTALLED: Edit Player > %s row %u (after Weight), page %08X list %08X -> %08X, "
         "text lookup %08X, %d saved length(s) in BodyHook_Shorts.ini, step %d%% per level%s%s",
         g_is2k12 ? "Body" : "Body and Head", weightAt + 2, sf.page, list, (u32)g_slList, (u32)loc, g_slSelCount, (int)(cfg_slStep * 100.0f + 0.5f),
         cfg_sl ? "" : " (Short Length row off)", cfg_ws ? ", + Wingspan row" : "");
}

/* the Hair Style code patches its record's text callback (+28) in the game's list; our copy of the
 * list must carry that too. Called after install_hair(). */
static void sl_sync_hair(void)
{
    u32 k, orig = 0;
    HairFind hf;
    if (!g_slMenu) return;
    hf.item = 0;
    for_data_pages(sl_hair_cb, &hf);
    if (!hf.item) return;
    orig = *(u32*)(hf.item + 0x28);
    for (k = 0; k < SL_MAX_ITEMS + 1 && *(u32*)(g_slList + 0x4C * k); k++)
        if (*(u32*)(g_slList + 0x4C * k + 4) == HAIR_ITEM_HASH && *(u32*)(g_slList + 0x4C * k + 0x28) != orig)
            *(u32*)(g_slList + 0x4C * k + 0x28) = orig;
}
