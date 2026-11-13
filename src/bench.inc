/* ------------------------------------------------------------------------------------------ */
/* HIGH-POLY BENCH (1.10.0)                                                                    */
/*                                                                                             */
/* Every player has a composite model (ctor 0x00C69C90): PLAYERMODEL_FLOOR at +0 (hi_body,     */
/* hihead, jersey/shorts models), PLAYERMODEL_BENCH at +C30 (one shared low-poly body SCNE      */
/* 80B3D4D1 with a baked static uniform, plus lohead) and PLAYERMODEL_REFLECTION at +E58.       */
/* All three are requested and built for every player. The model drawn for render state s is  */
/* [comp+F74+(2*s+1)*4]; states: 1 = bench in a replay, 2 = bench live, 3 = court in a replay, */
/* 4 = court live. Stock: 1,2 -> BENCH, 3,4 -> FLOOR.                                          */
/*                                                                                             */
/* Live bench (state 2) -> FLOOR. Confirmed in game since 1.8.0.                               */
/*                                                                                             */
/* Replays (state 1): a FLOOR jersey/shorts made from a MOVING cloth file is not ordinary      */
/* geometry. Its materials (545AD06C/853AD748 jersey, BD5D7BC8/6C3D7CEC shorts) are drawn only  */
/* by the cloth system (0x00BF1100 -> 0x004B9900, needs this frame's cloth results at           */
/* FLOOR+55C/+560/+564), and replays only carry cloth for the players who were on the court -> */
/* bare torso (the 1.8.x result). NONMOVING files use the "nocloth" materials                   */
/* (109120CB/E61A45F0, 03F81AF3/F9968B6F), which are ordinary skinned geometry and need no     */
/* cloth data. So ReplayBench=auto uses FLOOR in replays when the loaded cloth set is           */
/* nonmoving and keeps the stock BENCH when it is moving (jerseys never disappear).            */
/* ------------------------------------------------------------------------------------------ */
static u32 g_benchStoreSite = 0, g_benchGateSite = 0;
static volatile int g_clothMoving = -1;      /* -1 unknown, 0 nonmoving set, 1 moving set */
static volatile u32 st_benchComposites = 0, st_benchReplayFloor = 0;
static u32 P_clothCountJ = 36, P_clothCountS = 12;   /* ClothMesh array sizes (2K12: 28 / 12) */

static int bytes_eq(const u8 *a, const u8 *b, u32 n) { while (n--) if (*a++ != *b++) return 0; return 1; }

/* MOVING cloth set? 1.10.0 asked the asset resolver for ClothMesh rows; that lookup does not find them
 * (it said NONMOVING with the stock moving files -> bare torsos in replays). 1.10.1 reads what the game
 * itself uses: the shared ClothMesh arrays (jersey 0x020217BC x36, shorts 0x0202199C x12), filled when the
 * uniform cache is built. Any entry set = moving. Cache not built yet = unknown = treated as moving. */
static int cloth_set_moving(void)
{
    u32 ready = 0, v, i, arrJ, arrS, flag = 0;
    if (g_clothMoving >= 0) return g_clothMoving;
    arrJ = g_simArr[0] ? g_simArr[0] : 0x020217BCu;
    arrS = g_simArr[1] ? g_simArr[1] : 0x0202199Cu;
    if (g_clothGetter[0] && readable((void*)g_clothGetter[0], 8) && ((u8*)g_clothGetter[0])[0] == 0x83)
        flag = *(u32*)((u8*)g_clothGetter[0] + 2);              /* cmp dword [flag],1 */
    if (!flag) flag = 0x02021728u;
    if (!read32(flag, &ready) || (int)ready < 1) return 1;     /* not built yet: play safe */
    g_clothMoving = 0;
    for (i = 0; i < P_clothCountJ && !g_clothMoving; i++) if (read32(arrJ + i * 4, &v) && v) g_clothMoving = 1;
    for (i = 0; i < P_clothCountS && !g_clothMoving; i++) if (read32(arrS + i * 4, &v) && v) g_clothMoving = 1;
    blog("BENCH    cloth set is %s (ClothMesh arrays %08X/%08X) -> replays use %s for bench players",
         g_clothMoving ? "MOVING" : "NONMOVING", arrJ, arrS,
         cfg_benchReplay == 1 ? "the high-poly model (forced in ini)" : cfg_benchReplay == 0 ? "the stock bench model (ini)" :
         g_clothMoving ? "the stock bench model (moving jerseys need court cloth data)" : "the high-poly model");
    return g_clothMoving;
}

/* called from the composite constructor instead of "mov [esi+F80],eax / mov [esi+F88],eax" */
void __cdecl BenchDecide(u32 bench, u32 comp)
{
    u32 s1 = bench, s2 = bench;
    if (cfg_enabled) {
        if (cfg_benchLive) s2 = comp;
        if (cfg_benchReplay == 1 || (cfg_benchReplay == 2 && cfg_benchLive && (cfg_benchUniforms || !cloth_set_moving()))) s1 = comp;
    }
    *(u32*)(comp + 0xF80) = s1;
    *(u32*)(comp + 0xF88) = s2;
    st_benchComposites++;
    if (s1 == comp) st_benchReplayFloor++;
}

#if defined(_MSC_VER)
__declspec(naked) void BenchStore(void)
{
    __asm {
        pushad
        push esi            /* composite (= its FLOOR model) */
        push eax            /* its BENCH model               */
        call BenchDecide
        add  esp, 8
        popad
        ret
    }
}
#endif

/* ---- bench uniforms (1.10.2, from the replay memory dump) ---------------------------------------
 * Render descriptor (the node in front of each composite, comp-0xF0): +08 player record (stride 0x298),
 * +0C render state, +2C jersey cloth object, +30 shorts cloth object, +94 (1 = no game uniform).
 * The cloth objects come from a pool of only 10 (pointer 0x01CB6AA8, count 0x01CB6AAC, 0x18 each:
 * {shorts obj, shorts ClothMesh, jersey obj, jersey ClothMesh, player}) = the 10 players on the court.
 * Everybody else has +2C = +30 = 0, and PLAYERMODEL_FLOOR draws a jersey/shorts ONLY when its cloth
 * object is set (0x00BF0E70: slot 1 needs desc+2C, slot 0 needs desc+30; the cloth update 0x00BF1E70
 * likewise). That is why every bench player drawn with the court model had neither, live or replay.
 * Fix: for a bench state (1/2) with no cloth objects, BodyHook builds this player his OWN static cloth
 * objects with the game's builders (0x00BDFC70 jersey / 0x00BDFA80 shorts) while the ClothMesh slot
 * is hidden for that thread - exactly what a NONMOVING cloth file produces (object with no simulation
 * instance) - and puts them in the descriptor only for the duration of the model's own calls.
 *   BenchUniforms=1 (1.10.3): REAL moving cloth - the builders run normally (png bridge included)
 *                    into BodyHook's own memory (size from the builders' size functions), and the
 *                    first frame sets desc+38 so the cloth is placed on the body (0x00BE0870).
 *   BenchUniforms=2: cloth path with no simulation (what nonmoving files do for every player)
 *   BenchUniforms=3: also turns the model's cloth flag (FLOOR+100) off for those calls, so the
 *                    garments go through the plain static draw 0x00BF00A0 instead           */

#define BH_STYLE_MASK (g_is2k12 ? 7u : 0xFu)

/* ---- cloth object lifetime (1.11.23-ML / 1.19.10) ------------------------------------------
 * The game simulates cloth on its cloth worker thread, AFTER the model function that handed it the
 * object has returned. BodyHook's own cloth objects (bench, own cloth, static guard) were rebuilt IN
 * PLACE (memset + build) when a record got another player / uniform - while the worker could still be
 * simulating the old contents: random crash in the game's cloth code (2K12 dump: thread 0xC8AE40,
 * nba2k12+49C38, object = g_benchObj[2].objS). Now a rebuilt object goes to ANOTHER slot; the old slot
 * is retired and only reused after CLOTH_GRACE_MS without use. One record is rebuilt at most once per
 * CLOTH_REBUILD_MS (a record whose signature keeps changing no longer rebuilds every frame - the
 * repeating OWNCLOTH lines). The signature now uses the right record offsets on NBA 2K12 as well
 * (2K12 used 2K14's +98 / +208, read the wrong fields and could change at any time).          */
#define CLOTH_GRACE_MS   3000
#define CLOTH_REBUILD_MS 1500
typedef u32 (__fastcall *ModelFn_t)(void *model, void *desc);
typedef u32 (__cdecl *SizeFn_t)(void);
#define BENCH_OBJ 0x8000                /* object + its own simulation memory */
#define BENCH_PLAYERS 40
/* objects must be 16-byte aligned (0x00BE0580 uses movaps on obj+10); 128 for the sim memory after it */
__declspec(align(128)) static struct { u8 objJ[BENCH_OBJ], objS[BENCH_OBJ]; u32 player, styleJ, styleS, ok, fresh, mode, sig, lastTick, lastDesc, placeUntil, lastSimQ, used, retired, retireTick, builtTick; u8 pad[0x44]; } g_benchObj[BENCH_PLAYERS];
static int g_benchObjN = 0;
static u32 g_benchNeedJ = 0, g_benchNeedS = 0, g_benchNeedTick = 0;
/* PERF 1.11.14 / 1.19.3: the cloth update (0x00BF1E70 -> simulation 0x00BE0580) runs from TWO model
 * functions, FLOOR slot 0 (0x00BF20B0) and slot 17 (0x00BF2BD0). BodyHook gave a bench player the cloth
 * time step in every call, so his jersey and shorts were simulated twice (or more: replays run more
 * update passes) per frame - double the cloth work for up to 30 bench players, and cloth moving at
 * double speed. Now one simulation per object per frame: a second call less than 4 ms after the last
 * simulated one (same frame) gets no time step; the cloth keeps this frame's result. */
static int bench_sim_due(u32 objJ)
{
    u32 i = (objJ - (u32)&g_benchObj[0]) / (u32)sizeof(g_benchObj[0]), q = t_now(), per;
    if (i >= BENCH_PLAYERS) return 1;
    if (!g_qpcPerUs) {
        LARGE_INTEGER f; f.LowPart = 0; f.HighPart = 0;
        QueryPerformanceFrequency(&f);
        g_qpcPerUs = f.LowPart >= 1000000u ? f.LowPart / 1000000u : 1;
    }
    per = g_qpcPerUs * 4000u;                                   /* 4 ms */
    if (g_benchObj[i].lastSimQ && q - g_benchObj[i].lastSimQ < per) return 0;
    g_benchObj[i].lastSimQ = q ? q : 1;
    return 1;
}     /* bytes the builders need after the object (moving cloth) */
static u32 g_floorOrig[18];             /* original FLOOR vtable entries we wrap (index = slot) */
static volatile u32 st_benchUniforms = 0;
static struct { u32 comp, state, key; } g_benchSeen[128];
static int g_benchSeenN = 0;

static void bench_diag(u32 m, u32 d, u32 state, const char *what)
{
    u32 j = 0, s = 0, j0 = 0, s0 = 0, pl = 0, key, i, seen = 0;
    u8 *f = (u8*)m;
    if (!cfg_benchDiag) return;
    read32(d + 0x2C, &j); read32(d + 0x30, &s); read32(d + 0x08, &pl);
    if (j) read32(j, &j0);
    if (s) read32(s, &s0);
    nc_note(j0); nc_note(s0);   /* 1.11.30-ML: player garment cloth -> stock cloth size (netcloth.inc) */
    key = (j ? 1 : 0) | (s ? 2 : 0) | (*(u32*)(f + 0x560) ? 4 : 0) | (*(u32*)(f + 0x55C) ? 8 : 0) | (*(u32*)(f + 0x100) ? 16 : 0) |
          (j0 ? 32 : 0) | (s0 ? 64 : 0) | ((pl ? player_cfid((u8*)pl) : 0) << 8);
    EnterCriticalSection(&g_logLock);
    for (i = 0; i < (u32)g_benchSeenN; i++)
        if (g_benchSeen[i].comp == m && g_benchSeen[i].state == state && g_benchSeen[i].key == key) seen = 1;
    if (!seen) {
        i = g_benchSeenN < 128 ? (u32)g_benchSeenN++ : (GetTickCount() & 127);
        g_benchSeen[i].comp = m; g_benchSeen[i].state = state; g_benchSeen[i].key = key;
        blog("BENCHDIAG model %08X state %u (%s) %s: player %08X CFID %s | desc jersey %08X (sim %08X) shorts %08X (sim %08X) +94=%u "
             "| FLOOR cloth=%u outfit=%u | cloth results 55C=%08X 560=%08X",
             m, state, state == 1 ? "bench, replay" : state == 2 ? "bench, live" : state == 3 ? "court, replay" : state == 4 ? "court, live" : "other",
             what, pl, cfid_str(pl ? player_cfid((u8*)pl) : 0), j, j0, s, s0, *(u32*)(d + 0x94), *(u32*)(f + 0x100), *(u32*)(f + 0x538),
             *(u32*)(f + 0x55C), *(u32*)(f + 0x560));
    }
    LeaveCriticalSection(&g_logLock);
}

/* memory a builder needs for one object + simulation instance: its size function sits 0x60 before it
 * (push ebx/ebp/esi/edi, call <align fn>), the align function is called at builder+0x3F           */
static u32 bench_need(u32 builder)
{
    const u8 *f = (const u8*)(builder - 0x60);
    u32 al, sz, alignFn;
    if (!builder || !readable(f, 8) || !(f[0] == 0x53 && f[1] == 0x55 && f[2] == 0x56 && f[3] == 0x57 && f[4] == 0xE8)) return 0;
    alignFn = call_target_of(builder + (g_is2k12 ? 8 : 0x3F));         /* 2K12: called right after the prologue */
    if (call_target_of((u32)f + 4) != alignFn) return 0;
    sz = ((SizeFn_t)(u32)f)();
    al = ((SizeFn_t)alignFn)();
    if (!sz || sz > 0x100000 || al > 0x1000) return 0;
    return sz + al + 0x40;
}

/* this player's own cloth objects (built once per player + style + mode, rebuilt when the record
 * holds someone else). 1.11.4: the cloth is placed on the body again (desc build flag, for every
 * call of that frame) whenever the object was not drawn for a while or moves to another render
 * node. Before, a player who had been on the court (or teleported in practice / after a quarter
 * break) came back to his bench object with its particles still where he had left the bench ->
 * huge stretched jersey/shorts triangles across the court. */
/* 1.19.9 / 1.11.20-ML: which ClothMeshes (address + content, shortlen.inc sl_fp) this player's moving cloth is
 * built from right now. Kept cloth objects (bench, own cloth in practice) are built again when it changes: a
 * cloth file / png loaded again left them on the old shorts' data - in practice that was a player drawn with
 * shorts torn into a big grey triangle across the floor. */
static volatile int g_clothForceOwn;
static u32 cloth_src_key(u32 player, u32 sj, u32 ss)
{
    int nofp = CLOTH_PRACTICE_OWN();   /* passer: mesh fp flips each draw -> rebuild thrash */
    u32 bodyw = 0, k = 0x9E3779B9u, mesh, st;
    int bt, kind;
    if (!read32(player + P_offBodyWord, &bodyw)) return 0;
    bt = ((int)(bodyw << P_btShl)) >> 29;
    if (bt < 0 || bt > 3) return 0;
    for (kind = 0; kind < 2; kind++) {
        st = kind ? ss : sj;
        mesh = 0;
        if (g_simArr[kind] && st < 16 && read32(g_simArr[kind] + (u32)(bt + (int)st * 4) * 4, &mesh) && mesh)
            k = (k ^ mesh ^ (nofp ? 0 : sl_fp_cached(mesh))) * 0x01000193u;
        else k = (k ^ (u32)(kind + 1)) * 0x01000193u;
    }
    return k;
}

/* a slot for a new / rebuilt cloth object: never used, or retired / not drawn for CLOTH_GRACE_MS.
 * -1 = none free now (the caller keeps what it has). Generic over the three pools. */
#define CLOTH_PICK(arr, n, cap, now, skip, out) do {                                                  \
    int k_; (out) = -1;                                                                              \
    if ((n) < (cap)) { (out) = (n)++; break; }                                                       \
    for (k_ = 0; k_ < (cap); k_++) {                                                                 \
        if (k_ == (skip)) continue;                                                                  \
        if ((arr)[k_].retired ? (now) - (arr)[k_].retireTick > CLOTH_GRACE_MS                         \
                              : (now) - (arr)[k_].lastTick > CLOTH_GRACE_MS) {                        \
            if ((out) < 0 || ((arr)[k_].retired && !(arr)[(out)].retired) ||                          \
                ((arr)[k_].retired == (arr)[(out)].retired && (arr)[k_].lastTick < (arr)[(out)].lastTick)) \
                (out) = k_;                                     /* retired slots first, then oldest */ \
        }                                                                                            \
    }                                                                                                \
} while (0)
static volatile u32 st_clothDeferred = 0;

#define BENCH_REPLACE_GAP_MS 250
#define BENCH_PLACE_MS        60
/* 2.2 (stutter on slow PCs): the draw call never waits for the cloth lock. It is held while a cloth object is built
 * (10-25 ms, also on the game's loading thread). Busy -> the objects this player got last time (kept at least the
 * 3 s grace before any slot is reused), or - never seen - the game's own cloth for this one frame. */
typedef struct { volatile u32 player, j, s, tick; } ClothLast;
static ClothLast g_lastBench[64], g_lastStat[64], g_lastOwn[64];
static volatile u32 st_drawNoWait = 0;
static void cl_put(ClothLast *t, u32 player, u32 j, u32 s)
{
    ClothLast *e = &t[(player >> 4) & 63];
    e->player = 0; e->j = j; e->s = s; e->tick = GetTickCount(); e->player = player;   /* player last: all or nothing */
}
/* only an answer from the last second: a slot is reused only after CLOTH_GRACE_MS (3 s) unused or retired, so these
 * objects can't be in the middle of a rebuild for someone else */
static int cl_get(ClothLast *t, u32 player, u32 *outJ, u32 *outS)
{
    ClothLast *e = &t[(player >> 4) & 63]; u32 j, s, tk;
    st_drawNoWait++;
    if (e->player != player) return 0;
    j = e->j; s = e->s; tk = e->tick;
    if (e->player != player || !j || !s || GetTickCount() - tk > 1000) return 0;
    *outJ = j; *outS = s;
    return 1;
}
static int bench_objects(u32 player, u32 desc, u32 *outJ, u32 *outS, int *fresh)
{
    u32 uni = 0, sj = 1, ss = 0, v, mode = (u32)cfg_benchUniforms, sig, now = GetTickCount(), bodyw = 0, tb;
    int i, n, rebuild = 0, revived = 0;
    if (!g_simFn[0] || !g_simFn[1] || !g_simFetch[0] || !g_simFetch[1]) return 0;   /* need the fetch stubs */
    if (!readable((void*)player, P_offUniform + 8) || !read32(player + P_offUniform, &uni)) return 0;
    if (uni && !readable((void*)uni, 0x38)) return 0;                                /* not a usable record */
    if (uni && read32(uni + 4, &v)) sj = (v >> 9) & BH_STYLE_MASK;
    if (uni && read32(uni + 0x34, &v)) ss = v >> 30;
    if (mode == 1 && (!g_benchNeedJ || !g_benchNeedS || now - g_benchNeedTick > 2000)) {                                        /* sizes need the built uniform cache */
        /* PERF 1.11.14 / 1.19.3: was asked on every bench model call (hundreds per frame) */
        g_benchNeedJ = bench_need(g_simFn[0]);
        g_benchNeedS = bench_need(g_simFn[1]);
        g_benchNeedTick = now;
    }
    if (mode == 1 && (!g_benchNeedJ || !g_benchNeedS || g_benchNeedJ > BENCH_OBJ || g_benchNeedS > BENCH_OBJ)) mode = 2;
    /* who is in this record: png CFID + body word (bodytype) - records are reused for other people */
    read32(player + P_offBodyWord, &bodyw);
    sig = player_cfid((u8*)player) ^ (bodyw << 16) ^ (bodyw >> 16) ^ (mode == 1 ? cloth_src_key(player, sj, ss) : 0);
    if (!TryEnterCriticalSection(&g_clothLock)) { *fresh = 0; return cl_get(g_lastBench, player, outJ, outS); }   /* 2.2 */
    for (i = 0; i < g_benchObjN; i++)
        if (!g_benchObj[i].retired && g_benchObj[i].used && g_benchObj[i].player == player && g_benchObj[i].mode == mode) break;
    if (i < g_benchObjN && (g_benchObj[i].styleJ != sj || g_benchObj[i].styleS != ss || g_benchObj[i].sig != sig))
        rebuild = 1;                                       /* same record, different player / uniform */
    if (rebuild && now - g_benchObj[i].builtTick < CLOTH_REBUILD_MS) rebuild = 0;   /* not again so soon */
    if (i == g_benchObjN || rebuild) {   /* 2.1: built before for this player + uniform and only retired: use it again (no build) */
        int k;
        for (k = 0; k < g_benchObjN; k++)
            if (g_benchObj[k].retired && g_benchObj[k].used && g_benchObj[k].ok && g_benchObj[k].player == player && g_benchObj[k].sig == sig &&
                g_benchObj[k].styleJ == sj && g_benchObj[k].styleS == ss && g_benchObj[k].mode == mode) break;
        if (k < g_benchObjN) {
            if (rebuild) { g_benchObj[i].retired = 1; g_benchObj[i].retireTick = now; }
            i = k; g_benchObj[i].retired = 0; g_benchObj[i].lastTick = 0; revived = 1; rebuild = 0; st_clothRevived++;
        } else if (rebuild && !cloth_budget_ok()) rebuild = 0;          /* 2.1: rebuilt a frame later, the old one meanwhile */
    }
    if (!revived && (i == g_benchObjN || rebuild)) {
        CLOTH_PICK(g_benchObj, g_benchObjN, BENCH_PLAYERS, now, rebuild ? i : -1, n);
        if (n < 0) {                                       /* nothing free: keep the old one / stock bench */
            st_clothDeferred++;
            if (!rebuild) { LeaveCriticalSection(&g_clothLock); return 0; }
        } else {
            if (rebuild) { g_benchObj[i].retired = 1; g_benchObj[i].retireTick = now; }   /* the worker may still use it */
            i = n;
            memset(&g_benchObj[i], 0, sizeof(g_benchObj[i]));
            g_benchObj[i].sig = sig; g_benchObj[i].used = 1; g_benchObj[i].builtTick = now;
            g_benchObj[i].player = player; g_benchObj[i].styleJ = sj; g_benchObj[i].styleS = ss; g_benchObj[i].mode = mode;
            tb = t_now();
            if (mode == 1) {
                /* real moving cloth, through the png bridge (a png's static uniform still gets none) */
                sim_common(0, (void*)player, sj, g_benchObj[i].objJ);
                sim_common(1, (void*)player, ss, g_benchObj[i].objS);
                g_benchObj[i].ok = 1;
            } else {
                g_suppressTid = GetCurrentThreadId();           /* ClothMesh slot reads as empty */
                ((ClothSim_t)g_simFn[0])((void*)player, sj, g_benchObj[i].objJ);
                ((ClothSim_t)g_simFn[1])((void*)player, ss, g_benchObj[i].objS);
                g_suppressTid = 0;
                g_benchObj[i].ok = *(u32*)g_benchObj[i].objJ == 0 && *(u32*)g_benchObj[i].objS == 0;
            }
            t_done(T_BUILD, tb); cloth_budget_used();
            g_benchObj[i].fresh = 1;
            blog("BENCH    %s uniform for player %08X%s (memory %X/%X) (jersey style %u%s, shorts style %u%s)%s",
                 mode == 1 ? "moving-cloth" : "static", player, rebuild ? " (rebuilt in a new slot)" : "", g_benchNeedJ, g_benchNeedS,
                 sj, *(u32*)g_benchObj[i].objJ ? " simulated" : "", ss, *(u32*)g_benchObj[i].objS ? " simulated" : "",
                 g_benchObj[i].ok ? "" : " - FAILED, not used");
        }
    }
    /* (re)place the cloth on the body: new object, not drawn for a while, or another render node */
    if (revived) g_benchObj[i].fresh = 1;
    if (g_benchObj[i].fresh || now - g_benchObj[i].lastTick > BENCH_REPLACE_GAP_MS || g_benchObj[i].lastDesc != desc)
        g_benchObj[i].placeUntil = now + BENCH_PLACE_MS;
    g_benchObj[i].lastTick = now; g_benchObj[i].lastDesc = desc;
    *outJ = (u32)g_benchObj[i].objJ; *outS = (u32)g_benchObj[i].objS;
    *fresh = (int)(now - g_benchObj[i].placeUntil) > 0 ? 0 : 1;   /* every call until placeUntil */
    g_benchObj[i].fresh = 0;
    v = g_benchObj[i].ok;
    if (v) cl_put(g_lastBench, player, *outJ, *outS); else g_lastBench[(player >> 4) & 63].player = 0;
    LeaveCriticalSection(&g_clothLock);
    return (int)v;
}

/* 1.11.24-ML: the Edit Player preview. The record's body size / uniform changes the moment the user steps
 * Weight / Height, the preview model a moment later - the owner / static guards then put cloth made for the
 * NEW size on the OLD model (torn fat shorts) and rebuilt it every 1.5 s. The preview keeps the game's cloth. */
static int edit_preview(u32 player)
{
    u32 on = 0, mgr = 0, rec = 0;
    if (g_is2k12 || !player) return 0;
    if (!read32(0x02021A34u, &on) || on != 1 || !read32(0x02021A30u, &mgr) || !mgr || !read32(mgr + 0x1E4, &rec)) return 0;
    return rec == player;
}
/* a ClothMesh BodyHook may build from: readable, the cloth magic, and it fits our object memory */
static int mesh_usable(u32 mesh)
{
    if (!mesh) return 1;                                  /* no ClothMesh = static, always fine */
    return readable((void*)mesh, 0xD0) && *(u32*)(mesh + 0xA4) == CLOTH_MAGIC;
}
static int own_fits(void)
{
    u32 now = GetTickCount();
    if (!g_benchNeedJ || !g_benchNeedS || now - g_benchNeedTick > 2000) {
        g_benchNeedJ = bench_need(g_simFn[0]); g_benchNeedS = bench_need(g_simFn[1]); g_benchNeedTick = now;
    }
    return g_benchNeedJ && g_benchNeedS && g_benchNeedJ <= BENCH_OBJ && g_benchNeedS <= BENCH_OBJ;
}

/* ---- static-cloth guard (1.11.6) ------------------------------------------------------------
 * A render node can carry SIMULATED cloth objects that were not built for this player: in practice
 * mode the "court, replay" state (3) draws the roster player with one shared replay cloth set
 * (seen in a cloth-diagnostics log: CFID 195, 358 and 1018 all drew with objects 1B7B4980/1B7B9C00,
 * running simulation). A player whose jersey/shorts model comes from his own png is STATIC (the cloth
 * bridge switches his simulation off), and moving cloth on a static mesh = huge stretched triangles.
 * For such a player the node's simulated object is swapped, for the model's own calls only, for a
 * static object built for him (ClothMesh hidden while building, exactly what the bridge builds). */
#define STAT_PLAYERS 24
__declspec(align(128)) static struct { u8 objJ[BENCH_OBJ], objS[BENCH_OBJ]; u32 player, sig, styleJ, styleS, ok, lastTick, used, retired, retireTick, builtTick; u8 pad[0x58]; } g_statObj[STAT_PLAYERS];
static int g_statObjN = 0;
static struct { u32 player, tick; u8 st[2]; } g_garmCache[64];
static volatile u32 st_staticSwaps = 0;

/* 1 = this player's jersey (kind 0) / shorts (kind 1) model is his own png's static model */
static int garment_static(u32 player, int kind)
{
    u32 now = GetTickCount(), cf, i, slot = (player >> 4) & 63;
    int k, f;
    u8 st[2];
    if (g_garmCache[slot].player == player && now - g_garmCache[slot].tick < 1000) return g_garmCache[slot].st[kind];
    for (k = 0; k < 2; k++) {
        int site = k ? 2 : 0;
        f = 0;
        if (g_clothGetter[site] && png_ready((u8*)player, &cf)) {
            PngMasks pm;
            if (!cfg_fastPath || !g_scanDone || !player_info((u8*)player, &pm) || pm.clothMask)
                cloth_common_ex((u8*)player, site, &f, 2);   /* 2.2: 2 = from the draw call, never waits */
        }
        if (f < 0)   /* 2.2: the data lock was busy - answer from the last check (or "no") and ask again next frame */
            return g_garmCache[slot].player == player ? g_garmCache[slot].st[kind] : 0;
        st[k] = (u8)(f != 0);
    }
    (void)i;
    g_garmCache[slot].st[0] = st[0]; g_garmCache[slot].st[1] = st[1];
    g_garmCache[slot].player = player; g_garmCache[slot].tick = now;
    return g_garmCache[slot].st[kind];
}

static int static_objects(u32 player, u32 *outJ, u32 *outS)
{
    u32 uni = 0, sj = 1, ss = 0, v, bodyw = 0, sig, now = GetTickCount(), tb;
    int i, n, rebuild = 0, revived = 0;
    if (!g_simFn[0] || !g_simFn[1] || !g_simFetch[0] || !g_simFetch[1]) return 0;
    if (!readable((void*)player, P_offUniform + 8) || !read32(player + P_offUniform, &uni)) return 0;
    if (uni && !readable((void*)uni, 0x38)) return 0;
    if (uni && read32(uni + 4, &v)) sj = (v >> 9) & BH_STYLE_MASK;
    if (uni && read32(uni + 0x34, &v)) ss = v >> 30;
    read32(player + P_offBodyWord, &bodyw);
    sig = player_cfid((u8*)player) ^ (bodyw << 16) ^ (bodyw >> 16);
    if (!TryEnterCriticalSection(&g_clothLock)) return cl_get(g_lastStat, player, outJ, outS);   /* 2.2 */
    for (i = 0; i < g_statObjN; i++) if (!g_statObj[i].retired && g_statObj[i].used && g_statObj[i].player == player) break;
    if (i < g_statObjN && (g_statObj[i].sig != sig || g_statObj[i].styleJ != sj || g_statObj[i].styleS != ss)) rebuild = 1;
    if (rebuild && now - g_statObj[i].builtTick < CLOTH_REBUILD_MS) rebuild = 0;
    if (i == g_statObjN || rebuild) {   /* 2.1: built before for this player + uniform and only retired: use it again (no build) */
        int k;
        for (k = 0; k < g_statObjN; k++)
            if (g_statObj[k].retired && g_statObj[k].used && g_statObj[k].ok && g_statObj[k].player == player && g_statObj[k].sig == sig &&
                g_statObj[k].styleJ == sj && g_statObj[k].styleS == ss) break;
        if (k < g_statObjN) {
            if (rebuild) { g_statObj[i].retired = 1; g_statObj[i].retireTick = now; }
            i = k; g_statObj[i].retired = 0; g_statObj[i].lastTick = 0; revived = 1; rebuild = 0; st_clothRevived++;
        } else if (rebuild && !cloth_budget_ok()) rebuild = 0;          /* 2.1: rebuilt a frame later, the old one meanwhile */
    }
    if (!revived && (i == g_statObjN || rebuild)) {
        CLOTH_PICK(g_statObj, g_statObjN, STAT_PLAYERS, now, rebuild ? i : -1, n);
        if (n < 0) {
            st_clothDeferred++;
            if (!rebuild) { LeaveCriticalSection(&g_clothLock); return 0; }
        } else {
            if (rebuild) { g_statObj[i].retired = 1; g_statObj[i].retireTick = now; }
            i = n;
            memset(&g_statObj[i], 0, sizeof(g_statObj[i]));
            g_statObj[i].player = player; g_statObj[i].sig = sig; g_statObj[i].styleJ = sj; g_statObj[i].styleS = ss;
            g_statObj[i].used = 1; g_statObj[i].builtTick = now;
            tb = t_now();
            g_suppressTid = GetCurrentThreadId();               /* ClothMesh slot reads as empty: static object */
            ((ClothSim_t)g_simFn[0])((void*)player, sj, g_statObj[i].objJ);
            ((ClothSim_t)g_simFn[1])((void*)player, ss, g_statObj[i].objS);
            g_suppressTid = 0;
            t_done(T_BUILD, tb); cloth_budget_used();
            g_statObj[i].ok = *(u32*)g_statObj[i].objJ == 0 && *(u32*)g_statObj[i].objS == 0;
            blog("STATIC   own static cloth objects for player %08X CFID %s (png jersey/shorts drawn with someone else's moving cloth)%s",
                 player, cfid_str(player_cfid((u8*)player)), g_statObj[i].ok ? "" : " - FAILED, not used");
        }
    }
    g_statObj[i].lastTick = now;
    *outJ = (u32)g_statObj[i].objJ; *outS = (u32)g_statObj[i].objS;
    v = g_statObj[i].ok;
    if (v) cl_put(g_lastStat, player, *outJ, *outS); else g_lastStat[(player >> 4) & 63].player = 0;
    LeaveCriticalSection(&g_clothLock);
    return (int)v;
}

/* swap simulated node objects of a png-static garment; returns what to restore (bit 1 jersey, 2 shorts) */
static int static_guard(u32 d, u32 *saveJ, u32 *saveS)
{
    u32 j = 0, s = 0, j0 = 0, s0 = 0, player = 0, oJ, oS;
    int sj, ss, r = 0;
    if (!cfg_enabled || !cfg_staticGuard || !cfg_cloth) return 0;
    if (!read32(d + 0x2C, &j) || !read32(d + 0x30, &s) || !read32(d + 0x08, &player) || !player) return 0;
    if (j) read32(j, &j0);
    if (s) read32(s, &s0);
    if (!j0 && !s0) return 0;                                /* nothing simulated on this node */
    if (edit_preview(player)) return 0;                      /* 1.11.24-ML */
    sj = j0 && garment_static(player, 0);
    ss = s0 && garment_static(player, 1);
    if (!sj && !ss) return 0;
    if (!static_objects(player, &oJ, &oS)) return 0;
    if (sj) { *saveJ = j; *(u32*)(d + 0x2C) = oJ; r |= 1; }
    if (ss) { *saveS = s; *(u32*)(d + 0x30) = oS; r |= 2; }
    st_staticSwaps++;
    if (cfg_log) {
        EnterCriticalSection(&g_logLock);
        if (!seen_before(player ^ 0x08000000u, j, s, (u32)r))
            blog("STATIC   CFID %s: node %08X had moving cloth (%s%s) on his own static png model - static cloth used",
                 cfid_str(player_cfid((u8*)player)), d, sj ? "jersey " : "", ss ? "shorts" : "");
        LeaveCriticalSection(&g_logLock);
    }
    return r;
}
static void static_restore(u32 d, int r, u32 saveJ, u32 saveS)
{
    if (r & 1) *(u32*)(d + 0x2C) = saveJ;
    if (r & 2) *(u32*)(d + 0x30) = saveS;
}

/* ---- cloth owner guard (1.19.8 / 1.11.19-ML) ------------------------------------------------
 * Practice mode draws its player with a moving cloth set that the game built for SOMEONE ELSE (one
 * shared set). With a png-uniform player on the team (e.g. a thin player whose png has static shorts)
 * that set ends up built with his body size, so every other player was drawn in thin moving shorts,
 * at the cloth file's length. Replays and games build cloth per player, so they were right.
 * Now a render node whose jersey / shorts cloth was made from another ClothMesh than the one this
 * player's own body size + uniform style uses (the game's arrays, [size + style*4]) gets this
 * player's own cloth objects for the call - built like the game builds his (cloth bridge, so a png
 * static garment stays static), simulated with the node's time step. Short Length then applies.  */
#define OWN_PLAYERS 24
__declspec(align(128)) static struct { u8 objJ[BENCH_OBJ], objS[BENCH_OBJ]; u32 player, sig, styleJ, styleS, ok, lastTick, used, retired, retireTick, builtTick; u8 pad[0x58]; } g_ownObj[OWN_PLAYERS];
static int g_ownObjN = 0;
static volatile u32 st_ownSwaps = 0;
static int bh_own_cloth(u32 inst)
{
    u32 a = (u32)&g_benchObj[0], b = (u32)&g_benchObj[BENCH_PLAYERS];
    if (inst >= a && inst < b) return 1;
    a = (u32)&g_ownObj[0]; b = (u32)&g_ownObj[OWN_PLAYERS];
    return inst >= a && inst < b;
}

/* the ClothMesh the game itself would give this player for jersey (kind 0) / shorts (kind 1) */
static u32 own_expected_mesh(u32 player, int kind, u32 *style)
{
    u32 bodyw = 0, uni = 0, v = 0, st, mesh = 0;
    int bt;
    if (!g_simArr[kind] || !read32(player + P_offBodyWord, &bodyw) || !read32(player + P_offUniform, &uni)) return 0xFFFFFFFFu;
    if (uni && !readable((void*)uni, 0x38)) return 0xFFFFFFFFu;
    bt = ((int)(bodyw << P_btShl)) >> 29;
    if (bt < 0 || bt > 3) return 0xFFFFFFFFu;
    if (kind == 0) st = (uni && read32(uni + 4, &v)) ? (v >> 9) & BH_STYLE_MASK : 1;
    else           st = (uni && read32(uni + 0x34, &v)) ? v >> 30 : 0;
    *style = st;
    if (!read32(g_simArr[kind] + (u32)(bt + (int)st * 4) * 4, &mesh)) return 0xFFFFFFFFu;
    return mesh;
}

static int own_objects(u32 player, u32 sj, u32 ss, u32 *outJ, u32 *outS, int *fresh)
{
    u32 bodyw = 0, sig, now = GetTickCount(), v, tb;
    int i, n, rebuild = 0, revived = 0;
    read32(player + P_offBodyWord, &bodyw);
    sig = player_cfid((u8*)player) ^ (bodyw << 16) ^ (bodyw >> 16) ^ cloth_src_key(player, sj, ss);
    if (!TryEnterCriticalSection(&g_clothLock)) { *fresh = 0; return cl_get(g_lastOwn, player, outJ, outS); }   /* 2.2 */
    for (i = 0; i < g_ownObjN; i++) if (!g_ownObj[i].retired && g_ownObj[i].used && g_ownObj[i].player == player) break;
    *fresh = 0;
    if (i < g_ownObjN && (g_ownObj[i].sig != sig || g_ownObj[i].styleJ != sj || g_ownObj[i].styleS != ss)) rebuild = 1;
    if (rebuild && now - g_ownObj[i].builtTick < CLOTH_REBUILD_MS) rebuild = 0;   /* not again so soon */
    if (i == g_ownObjN || rebuild) {   /* 2.1: built before for this player + uniform and only retired: use it again (no build) */
        int k;
        for (k = 0; k < g_ownObjN; k++)
            if (g_ownObj[k].retired && g_ownObj[k].used && g_ownObj[k].ok && g_ownObj[k].player == player && g_ownObj[k].sig == sig &&
                g_ownObj[k].styleJ == sj && g_ownObj[k].styleS == ss) break;
        if (k < g_ownObjN) {
            if (rebuild) { g_ownObj[i].retired = 1; g_ownObj[i].retireTick = now; }
            i = k; g_ownObj[i].retired = 0; g_ownObj[i].lastTick = 0; revived = 1; rebuild = 0; st_clothRevived++; sl_after_build((void*)player, g_ownObj[i].objS);
        } else if (rebuild && !cloth_budget_ok()) rebuild = 0;          /* 2.1: rebuilt a frame later, the old one meanwhile */
        else if (!rebuild && !cloth_budget_ok()) { LeaveCriticalSection(&g_clothLock); return 0; }                 /* 2.1: built a frame later, the game's cloth meanwhile */
    }
    if (!revived && (i == g_ownObjN || rebuild)) {
        CLOTH_PICK(g_ownObj, g_ownObjN, OWN_PLAYERS, now, rebuild ? i : -1, n);
        if (n < 0) {
            st_clothDeferred++;
            if (!rebuild) { LeaveCriticalSection(&g_clothLock); return 0; }
        } else {
            if (rebuild) { g_ownObj[i].retired = 1; g_ownObj[i].retireTick = now; }   /* the worker may still use it */
            i = n;
            memset(&g_ownObj[i], 0, sizeof(g_ownObj[i]));
            g_ownObj[i].player = player; g_ownObj[i].sig = sig; g_ownObj[i].styleJ = sj; g_ownObj[i].styleS = ss;
            g_ownObj[i].used = 1; g_ownObj[i].builtTick = now;
            tb = t_now();
            sim_common(0, (void*)player, sj, g_ownObj[i].objJ);
            sim_common(1, (void*)player, ss, g_ownObj[i].objS);
            t_done(T_BUILD, tb); cloth_budget_used();
            sl_after_build((void*)player, g_ownObj[i].objS);   /* Short Length knows this cloth */
            g_ownObj[i].ok = 1;
            *fresh = 1;
            blog("OWNCLOTH own cloth objects for player %08X CFID %s%s (he was drawn with cloth made for another body size / uniform)",
                 player, cfid_str(player_cfid((u8*)player)), rebuild ? " (rebuilt in a new slot)" : "");
        }
    }
    if (now - g_ownObj[i].lastTick > BENCH_REPLACE_GAP_MS) *fresh = 1;   /* not drawn for a while: place it on him */
    g_ownObj[i].lastTick = now;
    *outJ = (u32)g_ownObj[i].objJ; *outS = (u32)g_ownObj[i].objS;
    v = g_ownObj[i].ok;
    if (v) cl_put(g_lastOwn, player, *outJ, *outS); else g_lastOwn[(player >> 4) & 63].player = 0;
    LeaveCriticalSection(&g_clothLock);
    return (int)v;
}


/* 1.11.29-ML practice passer: in practice the game draws every player with ONE moving cloth set. With a second
 * player on the floor (Freestyle passer, FT practice) both were drawn with the same simulated cloth object, so
 * one of them showed it far away / not at all. The first player seen with an object keeps it; anyone else drawn
 * with the same object within 300 ms gets his own cloth objects. */
static struct { u32 obj, player, tick; } g_clothShare[16];
static void own_retire_all(void)   /* 1.11.29-ML: the game rebuilt its players (pause / restart): drop our own cloth objects */
{ int i; u32 now = GetTickCount(); EnterCriticalSection(&g_clothLock); for (i = 0; i < g_ownObjN; i++) if (g_ownObj[i].used && !g_ownObj[i].retired) { g_ownObj[i].retired = 1; g_ownObj[i].retireTick = now; } for (i = 0; i < 64; i++) g_lastOwn[i].player = 0; LeaveCriticalSection(&g_clothLock); }   /* 1.11.29-ML: practice passer on the floor -> every player gets his own cloth */
static int cloth_shared(u32 obj, u32 player, u32 now)
{
    int i, free_ = -1;
    if (!obj || *(int*)0x1E85CD4 != 4) return 0;
    for (i = 0; i < 16; i++) {
        if (g_clothShare[i].obj == obj) {
            if (g_clothShare[i].player == player || now - g_clothShare[i].tick > 300) { g_clothShare[i].player = player; g_clothShare[i].tick = now; return 0; }
            return 1;
        }
        if (free_ < 0 && (!g_clothShare[i].obj || now - g_clothShare[i].tick > 2000)) free_ = i;
    }
    if (free_ >= 0) { g_clothShare[free_].obj = obj; g_clothShare[free_].player = player; g_clothShare[free_].tick = now; }
    return 0;
}
/* bit 1 jersey / 2 shorts swapped, bit 4: place flag (+38) set */
static int owner_guard(u32 d, u32 *saveJ, u32 *saveS, u32 *save38)
{
    u32 j = 0, s = 0, j0 = 0, s0 = 0, player = 0, mJ = 0, mS = 0, eJ, eS, sj = 1, ss = 0, oJ, oS;
    int bad = 0, fresh = 0, r = 0, noSim;
    if (!cfg_enabled || !cfg_cloth || !g_simFn[0] || !g_simFn[1] || !g_simFetch[0] || !g_simFetch[1]) return 0;
    if (!read32(d + 0x2C, &j) || !read32(d + 0x30, &s) || !read32(d + 0x08, &player) || !player) return 0;
    if (j) read32(j, &j0);
    if (s) read32(s, &s0);
    /* 1.11.30-ML: practice with the passers - the outfit nodes are handed round between players; a node built for a
     * png player with static shorts (no cloth instance) went to a passer whose shorts move: drawn stiff (log
     * 517b9fd1: node shorts 2B865380 sim 0 on CFID 2149, then on 19805). Such a player gets his own moving cloth. */
    noSim = s && !s0 && CLOTH_PRACTICE_OWN() && !garment_static(player, 1);
    if (!j0 && !s0 && !noSim) return 0;                      /* nothing simulated on this node */
    if (j0) read32(j0 + 0xA8, &mJ);
    if (s0) read32(s0 + 0xA8, &mS);
    if (edit_preview(player)) return 0;                      /* 1.11.24-ML: Edit Player keeps the game's cloth */
    eJ = own_expected_mesh(player, 0, &sj);
    eS = own_expected_mesh(player, 1, &ss);
    if (eJ == 0xFFFFFFFFu || eS == 0xFFFFFFFFu) return 0;
    if (!mesh_usable(eJ) || !mesh_usable(eS) || !own_fits()) return 0;   /* 1.11.24-ML: never build from a bad / too big mesh */
    if (j0 && mJ != eJ) bad |= 1;
    if (s0 && mS != eS) bad |= 2;
    if (noSim && eS) bad |= 2;                               /* his shorts move: own cloth */
    { u32 now = GetTickCount();
      if (CLOTH_PRACTICE_OWN()) { if (j0) bad |= 1; if (s0) bad |= 2; }
      if (j0 && !(bad & 1) && cloth_shared(j0, player, now)) bad |= 1;
      if (s0 && !(bad & 2) && cloth_shared(s0, player, now)) bad |= 2; }
    if (cloth_kept(j)) bad &= ~1;                              /* 1.11.31: kept on purpose (Edit Player body size step) */
    if (cloth_kept(s)) bad &= ~2;
    if (!bad) return 0;
    if (!own_objects(player, sj, ss, &oJ, &oS, &fresh)) return 0;
    { u32 x = 0; if (oJ && read32(oJ, &x)) nc_note(x); x = 0; if (oS && read32(oS, &x)) nc_note(x); }
    if (bad & 1) { *saveJ = j; *(u32*)(d + 0x2C) = oJ; r |= 1; }
    if (bad & 2) { *saveS = s; *(u32*)(d + 0x30) = oS; r |= 2; }
    if (fresh) { *save38 = *(u32*)(d + 0x38); *(u32*)(d + 0x38) = 1; r |= 4; }
    st_ownSwaps++;
    { static u32 lp, lJ, lS, lb;   /* PERF 1.11.30-ML: the same swap every frame takes no lock just to be skipped */
      if (cfg_log && (lp != player || lJ != mJ || lS != mS || lb != (u32)bad)) {
        lp = player; lJ = mJ; lS = mS; lb = (u32)bad;
        EnterCriticalSection(&g_logLock);
        if (!seen_before(player ^ 0x09000000u, mJ, mS, (u32)bad))
            blog("OWNCLOTH CFID %s: node %08X had %s%s%s cloth made for another body size / uniform (ClothMesh %08X/%08X, his %08X/%08X) - his own used",
                 cfid_str(player_cfid((u8*)player)), d, (bad & 1) ? "jersey" : "", (bad == 3) ? " + " : "", (bad & 2) ? "shorts" : "",
                 mJ, mS, eJ, eS);
        LeaveCriticalSection(&g_logLock);
    } }
    return r;
}

static u32 floor_call(int slot, void *model, void *desc)
{
    u32 m = (u32)model, d = (u32)desc, state = 0, j = 0, s = 0, no = 0, player = 0, oJ, oS, saveFlag = 0, save38 = 0, save34 = 0, r, t0 = 0;
    int patched = 0, fresh = 0, dup = 0;
    if (cfg_enabled && m && d && *(u32*)(m + 0xC30) == 0xFC028Cu && read32(d + 0x0C, &state)) {
        u32 coach = 0;
        /* coaches (desc+98 = 1, records 0x01F09A80+) keep the stock bench model: their court model on
         * the bench draws the polo outfit without arms (1.10.x bug). Fixed for every later frame. */
        if (!cfg_benchCoaches && read32(d + 0x98, &coach) && coach) {
            /* 1.11.8 / 1.17.5: only in a real game. MyCareer cutscenes build their people (records on the heap,
             * stride 0x4650) with desc+98 set too, but the shared low-poly bench assets are not loaded there:
             * switching such a model to its BENCH sub-model made the game draw a never-built model and crash
             * (nba2k14.exe+9B1DBE via BENCH draw 0x00BE8FA0, crash report of 13 Oct 2026). Game records
             * (players 0x01E86730+, coaches 0x01F09A80+) live inside the exe image; anything else keeps the
             * model the composite was built with. */
            u32 rec = 0;
            if (!read32(d + 0x08, &rec) || rec < g_imgLo || rec >= g_imgHi) {
                if (cfg_log) {
                    EnterCriticalSection(&g_logLock);
                    if (!seen_before(m ^ 0x05000000u, 0, 0, 0))
                        blog("BENCH    model %08X (record %08X, not a game roster record - cutscene) left as built", m, rec);
                    LeaveCriticalSection(&g_logLock);
                }
                return ((ModelFn_t)g_floorOrig[slot])(model, desc);
            }
            if (*(u32*)(m + 0xF80) == m || *(u32*)(m + 0xF88) == m) {
                *(u32*)(m + 0xF80) = m + 0xC30;
                *(u32*)(m + 0xF88) = m + 0xC30;
                if (cfg_log) {
                    EnterCriticalSection(&g_logLock);
                    if (!seen_before(m ^ 0x04000000u, 0, 0, 0)) blog("BENCH    coach model %08X keeps the stock bench model", m);
                    LeaveCriticalSection(&g_logLock);
                }
            }
            return ((ModelFn_t)g_floorOrig[slot])(model, desc);
        }
        if (state >= 1 && state <= 4) bench_diag(m, d, state, slot == 16 ? "draw" : slot == 15 ? "parts" : "update");
        if (cfg_benchUniforms && (state == 1 || state == 2) && read32(d + 0x2C, &j) && read32(d + 0x30, &s) &&
            !j && !s && read32(d + 0x94, &no) && !no && read32(d + 0x08, &player) && player &&
            bench_objects(player, d, &oJ, &oS, &fresh)) {
            *(u32*)(d + 0x2C) = oJ; *(u32*)(d + 0x30) = oS;
            if (fresh) { save38 = *(u32*)(d + 0x38); *(u32*)(d + 0x38) = 1; }   /* first frame: place the cloth */
            /* the cloth time step: desc+34, which the game fills (from 0x01CB6AB8 = clock now - clock before,
             * also in replays) only for the 10 players with pool cloth. Bench players have 0, and
             * 0x00BE0580 skips the simulation when it is 0 -> cloth frozen in its rest shape (1.10.3). */
            save34 = *(u32*)(d + 0x34);
            if (cfg_benchUniforms == 1 && !save34 && readable((void*)0x01CB6AB8, 4)) {
                u32 dt = *(u32*)0x01CB6AB8;
                if (dt && (slot == 0 || slot == 17) && !bench_sim_due(oJ)) { dt = 0; dup = 1; }   /* simulated already this frame */
                *(u32*)(d + 0x34) = dt;
            }
            /* no time step this frame (nothing simulates the cloth): keep it placed on the body
             * instead of leaving the particles behind where it was last simulated */
            if (cfg_benchUniforms == 1 && !*(u32*)(d + 0x34) && !fresh && !dup) { fresh = 1; save38 = *(u32*)(d + 0x38); *(u32*)(d + 0x38) = 1; }
            if (cfg_benchUniforms == 3) { saveFlag = *(u32*)(m + 0x100); *(u32*)(m + 0x100) = 0; }
            patched = 1;
            st_benchUniforms++;
        }
    }
    {
        u32 gJ = 0, gS = 0;
        u32 s38 = 0, tg = 0;
        int g = 0;
        if (m && d && state >= 1 && state <= 4) {             /* 2.1: timed ("draw guards" in the PERF line) */
            tg = t_now();
            if (!patched) g = static_guard(d, &gJ, &gS);
            if (!g && !patched) g = owner_guard(d, &gJ, &gS, &s38);
            sl_node(d);                                       /* Short Length: the drawn player's length */
            t_done(T_GUARD, tg);
        }
        if (patched) t0 = t_now();
        r = ((ModelFn_t)g_floorOrig[slot])(model, desc);
        if (patched) t_done(T_BENCH, t0);
        if (g) static_restore(d, g, gJ, gS);
        if (g & 4) *(u32*)(d + 0x38) = s38;
    }
    if (patched) {
        if (*(u32*)(d + 0x2C) == oJ) *(u32*)(d + 0x2C) = 0;
        if (*(u32*)(d + 0x30) == oS) *(u32*)(d + 0x30) = 0;
        if (fresh) *(u32*)(d + 0x38) = save38;
        *(u32*)(d + 0x34) = save34;
        if (cfg_benchUniforms == 3) *(u32*)(m + 0x100) = saveFlag;
    }
    return r;
}
u32 __fastcall FloorFn0(void *m, void *d)  { return floor_call(0, m, d); }
u32 __fastcall FloorFn14(void *m, void *d) { return floor_call(14, m, d); }
u32 __fastcall FloorFn15(void *m, void *d) { return floor_call(15, m, d); }
u32 __fastcall FloorFn16(void *m, void *d) { return floor_call(16, m, d); }
u32 __fastcall FloorFn17(void *m, void *d) { return floor_call(17, m, d); }

static void install_floor_wrappers(void)
{
    static const int slots[5] = { 0, 14, 15, 16, 17 };
    const u32 fns[5] = { (u32)&FloorFn0, (u32)&FloorFn14, (u32)&FloorFn15, (u32)&FloorFn16, (u32)&FloorFn17 };
    u32 vt = 0xFC0304u, i, n = 0;
    if (!readable((void*)vt, 0x48)) { blog("WARNING bench uniforms: FLOOR vtable not readable"); return; }
    for (i = 0; i < 5; i++) {
        u32 a = vt + slots[i] * 4, o = *(u32*)a;
        if (o < (u32)g_textBase || o >= (u32)g_textBase + g_textSize) continue;
        g_floorOrig[slots[i]] = o;
        if (patch_bytes(a, &fns[i], 4, 0)) n++;
    }
    blog("BENCH UNIFORMS: %u/5 model functions wrapped, mode %d (%s)%s", n, cfg_benchUniforms,
         cfg_benchUniforms == 1 ? "moving cloth for bench players" : cfg_benchUniforms == 2 ? "static, cloth path" :
         cfg_benchUniforms == 3 ? "static draw" : "off",
         cfg_benchDiag ? ", BENCHDIAG on" : "");
}

static void install_bench(void)
{
    /* mov [esi+F80],eax / mov [esi+F88],eax / mov [esi+F90],esi */
    static const u8 kStore[] = { 0x89,0x86,0x80,0x0F,0x00,0x00, 0x89,0x86,0x88,0x0F,0x00,0x00, 0x89,0xB6,0x90,0x0F,0x00,0x00 };
    /* cmp [ecx+F9C],0 / je / mov eax,[edx+0C] / cmp eax,4 / je / cmp eax,3 / jne */
    static const u8 kGate[] = { 0x83,0xB9,0x9C,0x0F,0x00,0x00,0x00,0x74,0x19,0x8B,0x42,0x0C,0x83,0xF8,0x04,0x74,0x05,0x83,0xF8,0x03,0x75,0x0C };
    u8 *p, *end = g_textBase + g_textSize - 32;
    if (!cfg_enabled || (!cfg_benchLive && cfg_benchReplay != 1)) { blog("high-poly bench: off in ini"); return; }
    for (p = g_textBase; p < end && !(g_benchStoreSite && g_benchGateSite); p++) {
        if (!g_benchStoreSite && p[0] == 0x89 && bytes_eq(p, kStore, sizeof(kStore))) g_benchStoreSite = (u32)p;
        if (!g_benchGateSite && p[0] == 0x83 && bytes_eq(p, kGate, sizeof(kGate))) g_benchGateSite = (u32)p;
    }
    if (!g_benchStoreSite || !g_benchGateSite) {
        blog("WARNING high-poly bench: composite code not found (store %08X, gate %08X) - bench stays stock",
             g_benchStoreSite, g_benchGateSite);
        return;
    }
    {
        /* 12 bytes -> call BenchStore + 7 nops */
        u8 b[12]; int i;
        b[0] = 0xE8; *(int*)(b + 1) = (int)((u32)&BenchStore - (g_benchStoreSite + 5));
        for (i = 5; i < 12; i++) b[i] = 0x90;
        patch_bytes(g_benchStoreSite, b, 12, 1);
    }
    {
        /* floor-only draw extras (+52C) for every non-zero state: "cmp eax,3 / jne" -> "test eax,eax / je" */
        static const u8 g[5] = { 0x85, 0xC0, 0x74, 0x0D, 0x90 };
        patch_bytes(g_benchGateSite + 17, g, 5, 1);
    }
    install_floor_wrappers();
    blog("HIGH-POLY BENCH INSTALLED: composite %08X, gate %08X. Live bench: %s. Replays: %s.%s",
         g_benchStoreSite, g_benchGateSite, cfg_benchLive ? "high-poly" : "stock",
         cfg_benchReplay == 1 ? "high-poly (forced)" : cfg_benchReplay == 0 ? "stock" : "auto",
         "");
}
