/* 1.11.30-ML: Freestyle practice rebounders / passers (as in Free Throw practice). Normal builds only (the research
 * build has the first version with hotkeys and logging in research.inc). Works in every mode that has practice
 * (Quick Game, Association, MyCareer ...): practice type [0x1E85CD4] == 4, Freestyle = sub [0x1E85CD8] == 0.
 * - two teammates of the user (off the floor: starters list 1, then bench list 3) are put on the floor, one each
 *   side of the lane at the user's basket, with the game's FT passer behaviour (0x71AA20); they rebound, pick the
 *   ball up and hold it (no dribble, no shot)
 * - Pass while one of them holds the ball: he passes to the user (no control swap); the user can't pass to them
 * - after a pause / sub (the game resets the practice players) a missing passer comes back 1.5 s later
 * [Practice] Passer=1 (0 = off), Passers=2 (1 = one under the basket). */
static int cfg_psHideIcons = 1;
static int cfg_psPasser = 1, cfg_psCount = 2, cfg_psOopA = 1, cfg_psOopB = 0;
typedef u8 *(__thiscall *PsNext_t)(u8 *);
typedef void (__thiscall *PsPl_t)(u8 *);
typedef void (__fastcall *PsVt_t)(u8 *, u32);
typedef void (__fastcall *PsTp_t)(u8 *, u32, float, float);
typedef void (__fastcall *PsPass_t)(u8 *, u8 *, u32, u32, u32, u32);
typedef u8 *(__thiscall *PsGet_t)(u8 *);
typedef int (__thiscall *PsHas_t)(u8 *);
typedef u8 *(__cdecl *PsState_t)(void);

static u8 *g_psA = 0, *g_psB = 0;                 /* the two passers (the asm thunks compare with both) */
static u8 **const g_psSlot[2] = { &g_psA, &g_psB };
static volatile int g_psFtOn = 1, g_psDropping = 0, g_psInPred = 0;
static int g_psMarked[2], g_psLogN = 0, g_psSide = 1, g_psSideSrc = 0, g_psShotLog = 0;
static u32 g_psFakeState[0x40 / 4];
static DWORD g_psBadSince[2], g_psSpawnAt = 0, g_psPassTick = 0;

#define PS_X    360.0f      /* each side, outside the lane lines (lane half width 244) */
#define PS_Z   1492.0f      /* just outside the baseline (1432.56) */

static int ps_fs(void) { return *(int*)0x1E85A90 && *(int*)0x1E85CD4 == 4 && *(int*)0x1E85CD8 == 0; }
static int ps_is(u8 *p) { return p && (p == g_psA || p == g_psB); }
static int ps_any(void) { return g_psA || g_psB; }
static int ps_same(const u8 *a, const u8 *b, int n) { while (n--) if (*a++ != *b++) return 0; return 1; }
static u8 *ps_list_first(int i) { u32 f = 0; read32(0x1D95200 + 8 * i, &f); return (f && f != 0x1D951D0u + 8 * i) ? (u8*)f : 0; }
static u8 *ps_list_next(u8 *o) { if (!readable(o, 4) || !readable((const void*)*(u32*)o, 0x10)) return 0;
    return ((PsNext_t)(*(u32*)(*(u32*)o + 0xC)))(o); }
static float ps_z(u8 *p) { u32 a = p ? *(u32*)(p + 0x20) : 0; return a && readable((const void*)(a + 0x30), 12) ? *(float*)(a + 0x38) : 0.0f; }
static u32 ps_top(u8 *p) { u8 *ai = *(u8**)(p + 0x3C); int n; if (!ai || !readable(ai + 0x1680, 4)) return 0;
    n = *(int*)(ai + 0x1680); return n > 0 ? *(u32*)(ai + 0x180 * (n - 1)) : 0; }

/* 0x6B44F0 (take off the floor) - the same code, but the passers stay (Freestyle keeps only controlled players) */
static void __fastcall PsOffFloor(u8 *o, u32 unused)
{
    u32 *vt; u8 *h;
    (void)unused;
    if (ps_is(o) && !g_psDropping) return;
    if (*(u32*)(o + 0x60) & 1) return;
    { u8 *n24 = *(u8**)(o + 0x24), *n28 = *(u8**)(o + 0x28); *(u8**)(n24 + 0x28) = n28; *(u8**)(n28 + 0x24) = n24; }
    h = *(u8**)0x1D95444;
    *(u8**)(o + 0x24) = h; *(u8**)(o + 0x28) = (u8*)(0x1D95444 - 0x24);
    *(u8**)(h + 0x28) = o; *(u8**)(*(u8**)(o + 0x28) + 0x24) = o;
    vt = *(u32**)o;
    ((PsVt_t)vt[0x90 / 4])(o, 0);
    ((PsVt_t)vt[0x8C / 4])(o, 1);
    *(u32*)(o + 0x60) |= 1;
    ((PsVt_t)vt[0x94 / 4])(o, 0);
}

/* the FT passer behaviour ends unless the top game state is 0xE (Free Throw): its own checks see state 0xE */
static u8 *__cdecl PsPasserState(void)
{
    if (ps_any() && ps_fs()) return (u8*)g_psFakeState;
    return ((PsState_t)0x004120B0u)();
}
static const u32 g_psStateSites[] = { 0x71A973, 0x783F4D, 0x7847CD, 0x7848FD, 0x784A43, 0x7A94D1 };
/* the Freestyle frame loop gives every player the Freestyle default behaviour - not the passers */
static void __fastcall PsFsDefault(u8 *o, u32 unused)
{
    (void)unused;
    if (ps_is(o)) return;
    ((PsVt_t)0x006E94E0u)(o, 0);
}

static u8 *ps_ball_holder(void)
{
    u8 *b = *(u8**)0x1CE716C, *h;
    if (!b || !readable(b, 4)) return 0;
    h = *(u8**)b;
    if (!h || !readable(h, 0x78) || *(u32*)(h + 0x74) != 1) return 0;
    return ((PsGet_t)(*(u32*)(*(u32*)h + 0x20)))(h);
}
/* while the user holds the ball the passers are no pass / switch targets (+60 bit 1) */
static void ps_ball_guard(void)
{
    u8 *h = ps_ball_holder(), *u = *(u8**)0x1DEE8D0; int k, want = cfg_psHideIcons || (h && h == u);   /* +60 bit 1: no pass target, no pass icon / name */
    for (k = 0; k < 2; k++) { u8 *p = *g_psSlot[k];
        if (!p) { g_psMarked[k] = 0; continue; }
        if (want && !g_psMarked[k]) { *(u32*)(p + 0x60) |= 2; g_psMarked[k] = 1; }
        else if (!want && g_psMarked[k]) { *(u32*)(p + 0x60) &= ~2u; g_psMarked[k] = 0; } }
}

static void ps_pass_to_user(void);
/* passer behaviour (as 0x71AA20 does it: spot = (0, 0, 1432.56 * side of [0x1DEE620])), then his own spot */
static float g_psLx[2], g_psLz[2]; static DWORD g_psLt[2]; static u8 *g_psLp[2];   /* ps_speed_cap */
static volatile DWORD g_psNoCap;   /* no speed cap until then (Freestyle start, placement) */
static void ps_place(u8 *cand, u8 *user, int side, float x, float z)
{
    g_psLp[0] = g_psLp[1] = 0;   /* placing him is not running: the speed cap starts again from here */
    g_psNoCap = GetTickCount() + 3000;
    u32 t = 0, sp = 0; int old = 0;
    u8 *ai;
    *(u32*)0x1DEE620 = *(u32*)(user + 0x4C); *(u32*)0x1DEE624 = *(u32*)(*(u32*)(user + 0x4C) + 0x34);
    *(u32*)0x1DEE8D0 = (u32)user; *(u32*)0x1DEE8D4 = 1; *(u32*)0x1DEE8D8 = 2; *(u32*)0x1DEE8DC = 2; *(u32*)0x1DEE8E0 = 0;
    if (read32(0x1DEE620, &t) && t && read32(t + 0x30, &sp) && sp && readable((const void*)sp, 4)) { old = *(int*)sp; *(int*)sp = side; }
    ((PsPl_t)0x006E26D0u)(cand);
    ((PsPl_t)0x0071AA20u)(cand);
    if (sp) *(int*)sp = old;
    /* his spot in the behaviour entry (+10 = x, y, z): the one he goes back to after a rebound / pass */
    ai = *(u8**)(cand + 0x3C);
    if (ai && ps_top(cand) == *(u32*)0x1198F38) {
        u8 *e = ai + 0x180 * (*(int*)(ai + 0x1680) - 1);
        *(float*)(e + 0x10) = x; *(float*)(e + 0x18) = z;
    } else { x = 0.0f; z = 1432.56f * side; }
    ((PsTp_t)0x00872BD0u)(cand, 1, x, z);
}


/* ---- 3 balls: the user's (idx 4), one each for the passers (idx 3, 2); idx 1 / 0 (rack) are kept out of sight.
 * Balls: list [0x1CB6AEC], next +11C, idx +10, holder (player) +0, position +70 / +80, state +1BC. Give the ball:
 * 0x7A1F70(ecx ball, edx player). The FT passer behaviour entry: +10 spot, +30 his ball. */
static u8 *g_psBall[5];
static u8 *g_psCut[2], *g_psCutHead;
static u8 *g_psSink[2];                          /* off-court players holding the 2 extra balls (out of sight) */
static u8 *g_psGive[2];                         /* ball handed to passer k at spawn */
static u8 *g_psPassed; static DWORD g_psPassedT;   /* a ball just passed to the user: nobody chases it */
static void ps_find_balls(void)
{
    u32 b = 0; int n = 0;
    g_psBall[0] = g_psBall[1] = g_psBall[2] = g_psBall[3] = g_psBall[4] = 0;
    read32(0x1CB6AEC, &b);
    for (; b && n < 8 && readable((void*)b, 0x1C0); b = *(u32*)(b + 0x11C), n++) {
        int i = *(int*)(b + 0x10);
        if (i >= 0 && i < 5) g_psBall[i] = (u8*)b;
    }
    /* a new Freestyle (other arena / reload) builds a new ball list: forget the old cut balls, cut the new ones */
    if ((g_psCut[0] || g_psCut[1]) && (u32)g_psCutHead != (read32(0x1CB6AEC, &b) ? b : 0)) { g_psCut[0] = g_psCut[1] = 0; g_psCutHead = 0; }
    if (g_psBall[0]) g_psCut[0] = 0;   /* idx 0 / 1 back in the list (the game rebuilt it): cut again */
    if (g_psBall[1]) g_psCut[1] = 0;
    if (g_psCut[0]) g_psBall[0] = g_psCut[0];
    if (g_psCut[1]) g_psBall[1] = g_psCut[1];
}
/* the ball list [0x1CB6AEC] (next +11C) is what the game draws, simulates and searches. The 2 extra balls are
 * unlinked while Freestyle runs and put back at its end (only if it is still the same list). */
static int g_psCutLog = 0;
static void ps_cut(u8 *b, int k)
{
    u32 o = 0, prev = 0; int n = 0;
    read32(0x1CB6AEC, &o);
    for (; o && n < 8; prev = o, o = *(u32*)(o + 0x11C), n++)
        if (o == (u32)b) {
            if (prev) *(u32*)(prev + 0x11C) = *(u32*)(b + 0x11C); else *(u32*)0x1CB6AEC = *(u32*)(b + 0x11C);
            *(u32*)(b + 0x11C) = 0; g_psCut[k] = b; read32(0x1CB6AEC, (u32*)&g_psCutHead);
            g_psNoCap = GetTickCount() + 3000;
            if (g_psCutLog < 6) { g_psCutLog++; blog("PRACTICE extra ball %d (%08X) taken out of the ball list", k, (u32)b); }
            return;
        }
}
static u8 *g_psCamBall; static float g_psCamSave[6];   /* the camera ball's own position, put back at the end */
static void ps_uncut(void)
{
    u32 o = 0, tail = 0; int n = 0, k;
    if (g_psCamBall && readable(g_psCamBall, 0x90)) { int j; for (j = 0; j < 3; j++) { ((float*)(g_psCamBall + 0x70))[j] = g_psCamSave[j]; ((float*)(g_psCamBall + 0x80))[j] = g_psCamSave[3 + j]; } }
    g_psCamBall = 0;
    if (!g_psCut[0] && !g_psCut[1]) return;
    read32(0x1CB6AEC, &o);
    if (o && (u8*)o == g_psCutHead && readable((void*)o, 0x120)) {
        for (; o && n < 8; tail = o, o = *(u32*)(o + 0x11C), n++) {}
        for (k = 1; k >= 0; k--) if (g_psCut[k] && tail) { *(u32*)(tail + 0x11C) = (u32)g_psCut[k]; *(u32*)((u32)g_psCut[k] + 0x11C) = 0; tail = (u32)g_psCut[k]; }
        blog("PRACTICE extra balls put back in the ball list");
    }
    g_psCut[0] = g_psCut[1] = 0; g_psCutHead = 0;
}
static u8 *ps_holder(u8 *b) { return b && readable(b, 4) ? *(u8**)b : 0; }
static u8 *ps_entry(u8 *p)                      /* his FT passer behaviour entry (any depth) */
{
    u8 *ai = p ? *(u8**)(p + 0x3C) : 0; int n;
    if (!ai || !readable(ai + 0x1680, 4)) return 0;
    n = *(int*)(ai + 0x1680);
    if (n <= 0 || n > 16 || !readable(ai, 0x180 * n)) return 0;   /* crash review: never trust the count */
    for (; n > 0; n--) { u8 *e = ai + 0x180 * (n - 1); if (*(u32*)e == *(u32*)0x1198F38) return e; }
    return 0;
}
static float ps_d2(u8 *p, u8 *b)                /* squared floor distance player - ball (+70) */
{
    u32 a = *(u32*)(p + 0x20); float dx, dz;
    if (!a || !readable((void*)(a + 0x30), 12)) return 1e12f;
    dx = *(float*)(a + 0x30) - *(float*)(b + 0x70); dz = *(float*)(a + 0x38) - *(float*)(b + 0x78);
    return dx * dx + dz * dz;
}
static u8 *ps_ball_of(u8 *p) { int i; if (!p) return 0; for (i = 2; i <= 4; i++) if (g_psBall[i] && ps_holder(g_psBall[i]) == p) return g_psBall[i]; return 0; }
/* 0x7A1F70 gives the ball only to a player whose ball mask (+64) has the ball's bit (1 << idx) */
static int g_psGiveLog = 0;
static void ps_give(u8 *b, u8 *p)
{
    int i = *(int*)(b + 0x10);
    if (i < 0 || i > 31) return;
    *(u32*)(p + 0x64) |= 1u << i;
    ((PsVt_t)0x007A1F70u)(b, (u32)p);
    if (g_psGiveLog < 12) { g_psGiveLog++; blog("PRACTICE ball %d -> player %08X (list %d): holder now %08X", i, (u32)p, *(int*)(p + 0x70), (u32)ps_holder(b)); }
}
/* each frame: rack balls out of sight; each passer's behaviour ball = his own, or the nearest loose one (not the
 * other passer's target) so the one without a ball fetches it */
static u8 *g_psCurWant;   /* the current ball the last full tick chose (0 = leave it to the game) */
static void ps_balls_tick(void)
{
    int k;
    u8 *tgt[2] = { 0, 0 };
    ps_find_balls();
    if (!g_psBall[4]) return;
    for (k = 0; k < 2; k++) {   /* the 2 extra balls (rack idx 1 / 0): taken out of the ball list -> not drawn, not used */
        u8 *b = g_psBall[k];
        if (b && !g_psCut[k] && !ps_holder(b)) ps_cut(b, k);
    }
    for (k = 0; k < 2; k++) { u8 *p = *g_psSlot[k], *e = ps_entry(p), *own;
        if (!e) continue;
        own = ps_ball_of(p);
        if (own) { u32 st = 0;
            tgt[k] = own; *(u8**)(e + 0x30) = own;
            /* given at spawn he dribbles: FT hold state instead */
            if (*(u32*)(p + 0x18) && read32(*(u32*)(p + 0x18) + 4, &st) && st == 0xF2E160)
                ((void (__fastcall *)(u8 *, u32, u32))0x00404DB0u)(p, 0xF30C70, 0); } }
    /* user without a ball: the current ball is a loose one (his shot / a pass to him), else the nearer passer's ball -
     * the game's alley-oop request (pass modifier + shoot) goes to the holder of the current ball */
    g_psCurWant = 0;
    {   u8 *u = *(u8**)0x1DEE8D0, *cur = 0, *ub = u && !ps_is(u) ? ps_ball_of(u) : 0; int i; float bd = 1e30f;
        int onUser = 0;
        static u8 *s_shot;   /* 1.11.31-ML: the ball the user last had (his shot) - the camera follows it */
        if (ub) s_shot = ub;
        if (u && !ps_is(u) && !ub) {
            /* the camera follows the current ball: it stays on the user's shot until he asks for a pass (the pass on
             * its way) or picks a ball up - a loose ball right at him (2 m) is current so he can pick it up */
            if (g_psPassed && GetTickCount() - g_psPassedT < 2500 && (!ps_holder(g_psPassed) || ps_is(ps_holder(g_psPassed)))) cur = g_psPassed;
            if (!cur) for (i = 2; i <= 4; i++) { u8 *b = g_psBall[i]; float d;
                if (!b || ps_holder(b) || b == s_shot) continue;
                d = ps_d2(u, b); if (d < 200.0f * 200.0f && d < bd) { bd = d; cur = b; } }
            bd = 1e30f;
            /* his shot picked up by a passer (rebound): the camera stays on the user (camera ball over him), it does
             * not follow the rebounder or the other loose balls */
            if (!cur && s_shot) for (i = 2; i <= 4; i++) if (g_psBall[i] == s_shot) { if (!ps_holder(s_shot)) cur = s_shot; else onUser = 1; break; }
            /* a loose ball is current (the game's pick-up / catch / rebound follow the current ball - otherwise
             * passers pushed loose balls away and the user could not pick one up): the pass on its way to him, else
             * the loose ball nearest to the user or to a passer without a ball. Only with no loose ball the passer's
             * ball (the alley-oop request is redirected at 0x78B510 anyway). */
            if (g_psPassed && GetTickCount() - g_psPassedT < 2500 && (!ps_holder(g_psPassed) || ps_is(ps_holder(g_psPassed)))) cur = g_psPassed;
            if (!cur && !onUser) for (i = 2; i <= 4; i++) { u8 *b = g_psBall[i]; float d; int q;
                if (!b || ps_holder(b)) continue;
                d = ps_d2(u, b);
                for (q = 0; q < 2; q++) { u8 *pp = *g_psSlot[q]; float d2; if (!pp || ps_ball_of(pp)) continue; d2 = ps_d2(pp, b); if (d2 < d) d = d2; }
                if (d < bd) { bd = d; cur = b; } }
            bd = 1e30f;
            /* 1.11.31-ML: no loose ball - the camera follows the current ball, so a passer's ball as current panned
             * the camera to him. A hidden extra ball (out of the ball list: not drawn, not simulated, no holder) is
             * kept above the user instead, so the camera stays on him. Only without a hidden ball: the passer's. */
            if (!cur) { u8 *px = g_psCut[0] ? g_psCut[0] : g_psCut[1]; u32 a = *(u32*)(u + 0x20);
                if (px && readable(px, 0x90) && a && readable((const void*)(a + 0x30), 12)) {
                    static int lg; float x = *(float*)(a + 0x30), z = *(float*)(a + 0x38);
                    if (g_psCamBall != px) { int j; g_psCamBall = px; for (j = 0; j < 3; j++) { g_psCamSave[j] = ((float*)(px + 0x70))[j]; g_psCamSave[3 + j] = ((float*)(px + 0x80))[j]; } }
                    *(u32*)px = 0;
                    *(float*)(px + 0x70) = x; *(float*)(px + 0x74) = 350.0f; *(float*)(px + 0x78) = z;
                    *(float*)(px + 0x80) = x; *(float*)(px + 0x84) = 350.0f; *(float*)(px + 0x88) = z;
                    cur = px;
                    if (lg < 3) { lg++; blog("PRACTICE camera: camera ball %08X kept over the user (his shot is with a passer / no loose ball)", (u32)px); }
                } }
            if (!cur) for (k = 0; k < 2; k++) { u8 *q = *g_psSlot[k], *qb = ps_ball_of(q); float d;
                if (!q || !qb) continue;
                d = ps_d2(u, qb); if (d < bd) { bd = d; cur = qb; } }
            if (cur) *(u8**)0x1CE716C = cur;
            g_psCurWant = cur;
        }
    }
    for (k = 0; k < 2; k++) { u8 *p = *g_psSlot[k], *e = ps_entry(p), *best = 0; float bd = 1e30f; int i;
        if (!e) continue;
        if (tgt[k]) continue;
        for (i = 2; i <= 4; i++) { u8 *b = g_psBall[i]; float d;
            if (!b || ps_holder(b) || b == tgt[k ^ 1] || (b == g_psPassed && GetTickCount() - g_psPassedT < 2500)) continue;
            d = ps_d2(p, b); if (d < bd) { bd = d; best = b; } }
        if (!best) continue;
        /* 1.11.31: loose balls are fetched the game's own way again at any distance (the "run to the spot" detour of
         * earlier today made them lazy). The far-ball "flash" is handled by ps_speed_cap (sprint speed at most). */
        tgt[k] = best; *(u8**)(e + 0x30) = best; }
}
/* the current ball [0x1CE716C] (Freestyle 0x894B56: the last ball with a holder) is never a passer's ball: the
 * game's one-ball code (user shot / pass / rebound) keeps following the user's ball */
static volatile int g_psCurSkip;
static void __cdecl ps_cur_check(u8 *h) { g_psCurSkip = ps_is(h) || (h && (h == g_psSink[0] || h == g_psSink[1])); }
__declspec(naked) static void PsCurBallThunk(void)
{
    __asm {
        pushad
        push eax
        call ps_cur_check
        add esp, 4
        popad
        cmp g_psCurSkip, 0
        jne skip
        mov dword ptr ds:[0x01CE716C], esi
    skip:
        push 0x00894B5C
        ret
    }
}
/* FT passer behaviour: no automatic pass to the user (0x784A87: shooter ready -> pass) - only on Pass */
__declspec(naked) static void PsAutoPassThunk(void)
{
    __asm {
        cmp edi, g_psA
        je no
        cmp edi, g_psB
        je no
        mov ecx, ebp
        mov eax, 0x0071A970
        call eax
        push 0x00784A8E
        ret
    no:
        push 0x00784C34
        ret
    }
}
/* FT passer behaviour, ball not his: "go for the ball" (0x784C25 -> 0x71B350) with HIS ball (ebp), not the current one */
__declspec(naked) static void PsGoBallThunk(void)
{
    __asm {
        mov edx, dword ptr ds:[0x01CE716C]
        cmp edi, g_psA
        je his
        cmp edi, g_psB
        jne go
    his:
        mov edx, ebp
    go:
        push 0x00784C2B
        ret
    }
}
/* no shots for the passers: 0x769D30 (push the shoot behaviour [0x1199540], ecx player) answers 0 for them */
/* 0x769D30 pushes the ball-action behaviour [0x1199540] (shot, or the lob when the user asks for an alley-oop).
 * For a passer it is allowed only right after the user's alley-oop request (command 0x78, pass modifier + shoot
 * without the ball), and then only with his own ball as the current one. Plain Shoot: blocked (no shots). */
static DWORD g_psOopT; static int g_psOopLog = 0;
static u32 ps_user_cmd(void)
{
    u8 *u = *(u8**)0x1DEE8D0, *c;
    if (!u || !readable(u, 0x20)) return 0;
    c = *(u8**)(u + 0x14);
    return c && readable(c + 0x20, 4) ? *(u32*)(c + 0x20) : 0;
}
static int __cdecl ps_action_ok(u8 *p, u32 flags)
{
    DWORD t = GetTickCount(); u8 *own;
    if (!ps_is(p)) return 1;
    if (ps_user_cmd() == 0x78) g_psOopT = t;
    own = ps_ball_of(p);
    if (!own || !g_psOopT || t - g_psOopT > 700) return 0;
    *(u8**)0x1CE716C = own;
    g_psPassed = own; g_psPassedT = t;
    if (g_psOopLog < 20) { g_psOopLog++; blog("PRACTICE alley-oop: passer %08X lobs his ball %08X (action flags %08X)", (u32)p, (u32)own, flags); }
    return 1;
}
__declspec(naked) static void PsShootThunk(void)
{
    __asm {
        push ecx
        push edx
        push edx
        push ecx
        call ps_action_ok
        add esp, 8
        pop edx
        pop ecx
        test eax, eax
        jz no
        push ebp
        mov ebp, esp
        and esp, 0xFFFFFFF0
        push 0x00769D36
        ret
    no:
        xor eax, eax
        ret 4
    }
}
/* the user's no-ball commands (0x78 alley-oop, 0x73 / 0x74) are handled by 0x78B510(user): they go to the holder of
 * the current ball. During that call the current ball is the nearer passer's ball. */
__declspec(naked) static int __stdcall PsCmdOrig(u8 *user)
{
    __asm {
        mov eax, dword ptr ds:[0x01CE716C]
        push 0x0078B515
        ret
    }
}
static int __stdcall PsCmdHook(u8 *user)
{
    u8 *ball = 0, *c; u32 save = *(u32*)0x1CE716C, cmd = 0; int k, r; float bd = 1e30f;
    c = user && readable(user, 0x20) ? *(u8**)(user + 0x14) : 0;
    if (c && readable(c + 0x20, 4)) cmd = *(u32*)(c + 0x20);
    if ((cmd == 0x78 || cmd == 0x73 || cmd == 0x74) && ps_any() && !ps_is(user) && !ps_ball_of(user))
        for (k = 0; k < 2; k++) { u8 *q = *g_psSlot[k], *qb = ps_ball_of(q); float d;
            if (!q || !qb) continue;
            d = ps_d2(user, qb); if (d < bd) { bd = d; ball = qb; } }
    if (!ball) return PsCmdOrig(user);
    *(u8**)0x1CE716C = ball; g_psOopT = GetTickCount();
    r = PsCmdOrig(user);
    if (g_psPassed != ball || GetTickCount() - g_psPassedT > 100) *(u32*)0x1CE716C = save;   /* no lob started: back */
    return r;
}
/* hold state F30C70 update (def +24 = 0x8631D0, ecx player): the hold animation follows the CURRENT ball's holder,
 * so while the user has the current ball a passer stood with his ball at the hip. During a passer's own update the
 * current ball is his ball. */
static void __fastcall PsHoldUpdate(u8 *p)
{
    u8 *own = ps_is(p) ? ps_ball_of(p) : 0;
    if (own) {
        u32 save = *(u32*)0x1CE716C;
        *(u8**)0x1CE716C = own;
        ((void (__fastcall *)(u8 *))0x008631D0u)(p);
        if (*(u8**)0x1CE716C == own) *(u32*)0x1CE716C = save;
    } else ((void (__fastcall *)(u8 *))0x008631D0u)(p);
}
/* TEST (alley-oop): the user's controller command ([user+14]+20, read by 0x863030) and the input words around it,
 * logged when they change - to find pass modifier + shoot without the ball */
static u32 g_psCmdLast[8];
static void ps_cmd_probe(void)
{
    u8 *u = *(u8**)0x1DEE8D0, *c; u32 v[8]; int i, ch = 0;
    if (!u || !readable(u, 0x20)) return;
    c = *(u8**)(u + 0x14);
    if (!c || !readable(c, 0x40)) return;
    for (i = 0; i < 8; i++) { v[i] = *(u32*)(c + 0x20 + 4 * i); if (i < 2 && v[i] != g_psCmdLast[i]) ch = 1; }
    if (!ch) return;
    /* pass modifier + shoot without the ball = command 0x78 (log f9ed55b6): the nearer passer with a ball throws the
     * user a lob - 0x6E2A90 with a = 1 as the game's own alley-oop pass (0x79AFAF, after 0x89B3C0 found the cutter) */
    /* Pass without the ball: 0x88 while a teammate holds the current ball, 0x72 while it is loose */
    if ((v[0] == 0x88 || v[0] == 0x72) && g_psCmdLast[0] != v[0]) ps_pass_to_user();
    if (v[0] == 0x78 && g_psCmdLast[0] != 0x78 && !ps_ball_of(u)) {
        u8 *p = 0, *ball = 0; int k; float bd = 1e30f;
        for (k = 0; k < 2; k++) { u8 *q = *g_psSlot[k], *qb = ps_ball_of(q); float d;
            if (!q || !qb) continue;
            d = ps_d2(u, qb); if (d < bd) { bd = d; p = q; ball = qb; } }
        if (p && GetTickCount() - g_psOopT > 1000) {
            /* the game hands an alley-oop request to the current ball's holder: 0x6E2CE0(holder, 7, user slot +B0C)
             * (0x78B6B9). The current ball is often loose now, so the request is made here for the nearer passer; his
             * lob (0x769D30) is let through for 0.7 s and his ball stays current meanwhile */
            DWORD t = GetTickCount();
            g_psOopT = t; g_psPassed = ball; g_psPassedT = t;
            *(u8**)0x1CE716C = ball;
            ((int (__fastcall *)(u8 *, u32, u32))0x006E2CE0u)(p, 7, *(u32*)(u + 0xB0C));
            if (g_psOopLog < 20) { g_psOopLog++; blog("PRACTICE alley-oop requested: passer %08X, ball %08X", (u32)p, (u32)ball); }
        }
    }
    for (i = 0; i < 8; i++) g_psCmdLast[i] = v[i];
}
/* back at his spot the FT passer behaviour turns to its facing vector (entry +20 x, +28 z: 0x784BB5 -> 0x6CBE50 angle
 * -> 0x8457D0 turn if more than 45 degrees off). It was fixed at the push (towards mid court); now it points at the
 * user, so after a rebound he turns to the user with the game's own turn (no forced heading). */
static void ps_face_user(void)
{
    u8 *u = *(u8**)0x1DEE8D0; int k; u32 ua;
    if (!u || !readable(u, 0x40) || !(ua = *(u32*)(u + 0x20)) || !readable((void*)(ua + 0x30), 12)) return;
    for (k = 0; k < 2; k++) {
        u8 *p = *g_psSlot[k], *e = ps_entry(p); float dx, dz, l;
        if (!e) continue;
        dx = *(float*)(ua + 0x30) - *(float*)(e + 0x10); dz = *(float*)(ua + 0x38) - *(float*)(e + 0x18);   /* spot -> user */
        l = dx * dx + dz * dz;
        if (l < 100.0f) continue;
        *(float*)(e + 0x20) = dx; *(float*)(e + 0x24) = 0.0f; *(float*)(e + 0x28) = dz;   /* direction only (atan2) */
    }
}
static u8 *ps_user(void)
{
    u8 *o; u32 ctl;
    for (o = ps_list_first(0); o; o = ps_list_next(o)) { ctl = 0xFFFFFFFF; if (*(u32*)(o + 0x14)) read32(*(u32*)(o + 0x14), &ctl);
        if (ctl != 0xFFFFFFFF) return o; }
    return 0;
}
/* a teammate who is not a passer yet: on the floor (not controlled), then starters off court, then the bench */
static u8 *ps_pick(u8 *user)
{
    u8 *o; u32 ctl, team = *(u32*)(user + 0x4C); int l;
    for (o = ps_list_first(0); o; o = ps_list_next(o)) { ctl = 0xFFFFFFFF; if (*(u32*)(o + 0x14)) read32(*(u32*)(o + 0x14), &ctl);
        if (o != user && ctl == 0xFFFFFFFF && *(u32*)(o + 0x4C) == team && !ps_is(o)) return o; }
    for (l = 1; l <= 3; l += 2)
        for (o = ps_list_first(l); o; o = ps_list_next(o)) if (*(u32*)(o + 0x4C) == team && !ps_is(o)) return o;
    return 0;
}
static void ps_make_passer(int k, u8 *user)
{
    u8 *cand = ps_pick(user);
    float x;
    if (!cand) return;
    if (*(int*)(cand + 0x70) != 0) {
        /* as the game's own bring-on (0x8948B1); one fewer player wanted off the floor or 0x6B4BE0 takes him off */
        u32 cnt = *(u32*)(user + 0x4C) == 0x1D955D0 ? 0x1E85D00 : 0x1E85D04;
        ((PsPl_t)0x006B4570u)(cand);
        ((PsVt_t)0x00405110u)(cand, 0);
        if (*(u32*)(cand + 0x3C)) ((PsVt_t)0x006BB5D0u)(*(u8**)(cand + 0x3C), 1);
        if (*(int*)cnt > 0) (*(int*)cnt)--;
    }
    if (!ps_any() && !g_psFsOwn) own_retire_all();   /* own cloth already runs from the start of Freestyle */
    g_clothForceOwn = 1;
    *g_psSlot[k] = cand; g_psBadSince[k] = 0; g_psMarked[k] = 0;
    x = cfg_psCount < 2 ? 0.0f : (k ? PS_X : -PS_X);
    ps_place(cand, user, g_psSide, x, PS_Z * g_psSide);
    ps_find_balls();
    {   u8 *b = g_psBall[k ? 2 : 3];   /* his own ball, if it is free */
        if (b && !ps_holder(b)) { ps_give(b, cand); g_psGive[k] = b; } }
    if (g_psLogN < 20) { g_psLogN++; blog("PRACTICE Freestyle passer %d: player %08X at x %d z %d (team side %d, user z %d)",
        k + 1, (u32)cand, (int)x, (int)(PS_Z * g_psSide), g_psSideSrc, (int)ps_z(user)); }
}

/* Pass while a passer holds the ball = swap 0x7965A0 with him: blocked, he passes to the user instead */
/* the nearer passer with a ball passes it to the user (Pass without the ball) */
static void ps_pass_to_user(void)
{
    u8 *u = *(u8**)0x1DEE8D0, *p = 0, *ball = 0; DWORD t = GetTickCount(); int k; float bd = 1e30f;
    if (!u || ps_is(u) || ps_ball_of(u) || t - g_psPassTick <= 600) return;
    for (k = 0; k < 2; k++) { u8 *q = *g_psSlot[k], *qb = ps_ball_of(q); float d;
        if (!q || !qb) continue;
        d = ps_d2(u, qb); if (d < bd) { bd = d; p = q; ball = qb; } }
    if (p) { g_psPassTick = t; g_psPassed = ball; g_psPassedT = t; *(u8**)0x1CE716C = ball; ((PsPass_t)0x006E2A90u)(p, u, (u32)ball, 0, 0, 0); }
}
static int __cdecl ps_on_swap(u8 *a, u8 *b)
{
    if (!ps_is(a) && !ps_is(b)) return 0;
    ps_pass_to_user();
    return 1;
}
__declspec(naked) static void PsSwapThunk(void)
{
    __asm {
        push ecx
        push edx
        push edx
        push ecx
        call ps_on_swap
        add esp, 8
        pop edx
        pop ecx
        test eax, eax
        jz go
        ret
    go:
        sub esp, 0xB8
        push 0x007965A6
        ret
    }
}
/* a passer holding the ball takes no action from the hold state (0x863030 -> 0x404DB0): that was the user's Shoot
 * making him shoot. Only his own pass behaviour ([0x1198F78] on top) goes through. */
static int __cdecl ps_block_action(u8 *p, u32 st)
{
    if (!ps_is(p) || ps_top(p) == *(u32*)0x1198F78) return 0;
    if (g_psShotLog < 10) { g_psShotLog++; blog("PRACTICE passer %08X: action state %08X blocked (he only passes)", (u32)p, st); }
    return 1;
}
__declspec(naked) static void PsActionThunk(void)
{
    __asm {
        push edx
        push ecx
        push edx
        push esi
        call ps_block_action
        add esp, 8
        pop ecx
        pop edx
        test eax, eax
        jnz blk
        push 0
        mov ecx, esi
        mov eax, 0x00404DB0
        call eax
        push 0x0086311D
        ret
    blk:
        push 0x00863128
        ret
    }
}
/* rebounds: the game lets only the nearest rebounder chase ([0x1198F68] on top, 0x7A9110), but only for the current
 * ball [0x1CE716C]; Freestyle has 5. For the passers the ball being chased (ebx) is used: the nearest free one goes. */
__declspec(naked) static void PsReboundBallThunk(void)
{
    __asm {
        cmp edi, g_psA
        je mine
        cmp edi, g_psB
        je mine
        cmp ebx, dword ptr ds:[0x01CE716C]
        jne skip
    mine:
        push 0x007A91E9
        ret
    skip:
        push 0x007A9408
        ret
    }
}
__declspec(naked) static void PsPredictThunk(void)
{
    __asm {
        movss xmm0, dword ptr [esp + 4]
        push edx
        push ecx
        mov edx, ecx
        mov ecx, dword ptr ds:[0x01CE716C]
        cmp edi, g_psA
        je ball
        cmp edi, g_psB
        jne go
    ball:
        mov ecx, ebx
    go:
        movss dword ptr [esp], xmm0
        mov eax, 0x008C4260
        call eax
        ret 4
    }
}
/* the name label under a player (PlayerIconComponent, 0xC726B0 ecx player: "draw it?") only skips +60 bit 0 - the
 * passers answer 0 (the icon itself already skips +60 bit 1) */
__declspec(naked) static void PsNameThunk(void)
{
    __asm {
        test ecx, ecx
        jz go
        cmp ecx, g_psA
        je no
        cmp ecx, g_psB
        je no
    go:
        push esi
        push edi
        mov edi, ecx
        mov eax, 0x004120B0
        call eax
        push 0x00C726B9
        ret
    no:
        xor eax, eax
        ret
    }
}
/* rebound pick-up 0x7A9434: the passers get FT practice's skip of "my team is on offense -> don't chase" */
__declspec(naked) static void PsReboundThunk(void)
{
    __asm {
        test edi, edi
        jz normal
        cmp edi, g_psA
        je yes
        cmp edi, g_psB
        jne normal
    yes:
        push 0x007A94AC
        ret
    normal:
        cmp dword ptr ds:[0x01E85CD8], 3
        jne other
        cmp dword ptr ds:[0x01E85CD4], 4
        jne other
        push 0x007A94AC
        ret
    other:
        push 0x007A9446
        ret
    }
}
/* FT ball handling for the passers: isFT (0x6B1FB0), state 0xE checks and "dribble allowed" (0x7B65A0) */
__declspec(naked) static void PsIsFtEsi(void)
{
    __asm {
        cmp g_psFtOn, 0
        je orig
        test esi, esi
        jz orig
        cmp esi, g_psA
        je yes
        cmp esi, g_psB
        jne orig
    yes:
        mov eax, 1
        ret
    orig:
        mov eax, 0x006B1FB0
        jmp eax
    }
}
__declspec(naked) static void PsIsFtEdi(void)
{
    __asm {
        cmp g_psFtOn, 0
        je orig
        test edi, edi
        jz orig
        cmp edi, g_psA
        je yes
        cmp edi, g_psB
        jne orig
    yes:
        mov eax, 1
        ret
    orig:
        mov eax, 0x006B1FB0
        jmp eax
    }
}
__declspec(naked) static void PsStEsi(void)
{
    __asm {
        cmp g_psFtOn, 0
        je orig
        test esi, esi
        jz orig
        cmp esi, g_psA
        je yes
        cmp esi, g_psB
        jne orig
    yes:
        lea eax, g_psFakeState
        ret
    orig:
        mov eax, 0x004120B0
        jmp eax
    }
}
__declspec(naked) static void PsStEbx(void)
{
    __asm {
        cmp g_psFtOn, 0
        je orig
        test ebx, ebx
        jz orig
        cmp ebx, g_psA
        je yes
        cmp ebx, g_psB
        jne orig
    yes:
        lea eax, g_psFakeState
        ret
    orig:
        mov eax, 0x004120B0
        jmp eax
    }
}
__declspec(naked) static void PsDribbleEsi(void)
{
    __asm {
        cmp g_psFtOn, 0
        je orig
        test esi, esi
        jz orig
        cmp esi, g_psA
        je yes
        cmp esi, g_psB
        jne orig
    yes:
        xor eax, eax
        ret
    orig:
        mov eax, 0x007B65A0
        jmp eax
    }
}

/* passer lost (pause / sub / drill change: the game reset the players) -> clean up; outside Freestyle forget them;
 * in Freestyle (new) passers 1.5 s after entering or after losing one */
static void ps_forget(int k)
{
    u8 *p = *g_psSlot[k];
    if (g_psMarked[k] && p && readable(p, 0x80)) *(u32*)(p + 0x60) &= ~2u;
    g_psMarked[k] = 0; g_psBadSince[k] = 0; *g_psSlot[k] = 0;
    if (!ps_any()) g_clothForceOwn = 0;
    own_retire_all();
}

/* 1.11.31-ML: a passer never moves faster than a sprint. Fetching a loose ball with the free-throw rebounder behaviour
 * made them "flash" across the floor (much faster than a run / a jump in place). Each frame his x / z may change by at
 * most 9.5 m/s x the time since the last frame (+5 cm), else he is put back to that distance on the same line. */
static void ps_speed_cap(void)
{
    int k; DWORD now = GetTickCount();
    for (k = 0; k < 2; k++) { u8 *p = *g_psSlot[k]; u32 a; float *pos, dx, dz, d, lim, dt;
        if (!p || !readable(p, 0x24) || !(a = *(u32*)(p + 0x20)) || !readable((const void*)(a + 0x30), 12)) { g_psLp[k] = 0; continue; }
        pos = (float*)(a + 0x30);
        /* 1.11.31: only while he chases a LOOSE ball (the catch "flash"), never right after a (re)start / placement:
         * it held passers where the game parked them (at the side, screenshot 9 Nov 23:46) */
        {   u8 *e = ps_entry(p), *tb = e && readable(e + 0x30, 4) ? *(u8**)(e + 0x30) : 0;
            if ((int)(now - g_psNoCap) < 0 || ps_ball_of(p) || !tb || ps_holder(tb)) { g_psLp[k] = 0; continue; } }
        if (g_psLp[k] != p || now - g_psLt[k] > 250) { g_psLp[k] = p; g_psLx[k] = pos[0]; g_psLz[k] = pos[2]; g_psLt[k] = now; continue; }
        dt = (float)(now - g_psLt[k]) * 0.001f; if (dt < 0.004f) dt = 0.004f;
        dx = pos[0] - g_psLx[k]; dz = pos[2] - g_psLz[k]; d = dx * dx + dz * dz;
        lim = 950.0f * dt + 5.0f;
        if (d > lim * lim) {
            static int lg; float r = ws_sqrt(d), f = lim / r;
            if (lg < 6) { lg++; blog("PRACTICE passer %08X moved %d cm in %d ms (faster than a sprint) - held to %d cm", (u32)p, (int)r, (int)(dt * 1000.0f), (int)lim); }
            pos[0] = g_psLx[k] + dx * f; pos[2] = g_psLz[k] + dz * f;
        }
        g_psLx[k] = pos[0]; g_psLz[k] = pos[2]; g_psLt[k] = now;
    }
}

static void ps_tick(void)
{
    DWORD t = GetTickCount(); int k, want = cfg_psCount < 2 ? 1 : 2, missing = 0;
    if (!ps_fs()) { for (k = 0; k < 2; k++) if (*g_psSlot[k]) ps_forget(k); g_psSpawnAt = 0; g_psSink[0] = g_psSink[1] = 0; ps_uncut(); return; }
    for (k = 0; k < want; k++) {
        u8 *p = *g_psSlot[k];
        if (!p) { missing = 1; continue; }
        if (readable(p, 0x80) && *(int*)(p + 0x70) == 0 && *(u32*)(p + 0x3C) && ((PsHas_t)0x006E2320u)(p)) { g_psBadSince[k] = 0; continue; }
        if (!g_psBadSince[k]) { g_psBadSince[k] = t; continue; }
        if (t - g_psBadSince[k] < 400) continue;
        ps_forget(k); missing = 1;
        if (!g_psSpawnAt || (int)(g_psSpawnAt - t) < 1500) g_psSpawnAt = t + 1500;
    }
    /* 1.11.31-ML: the 2 extra balls are taken out on the very first Freestyle frame (before the passers come), so
     * they are never drawn */
    if (!g_psCut[0] || !g_psCut[1]) { ps_find_balls(); for (k = 0; k < 2; k++) { u8 *b = g_psBall[k]; if (b && !g_psCut[k] && !ps_holder(b)) ps_cut(b, k); } }
    if (ps_any()) { ps_ball_guard(); ps_balls_tick(); ps_cmd_probe(); ps_face_user(); ps_speed_cap(); }
    if (!missing) return;
    if (!g_psSpawnAt) g_psSpawnAt = t;   /* 1.11.30-ML: right away (was 300 ms) */
    if ((int)(t - g_psSpawnAt) >= 0) {
        /* the user's basket, as the game's FT setup (0x71AA20 / 0x880380): z = 1432.56 * side, side = *[team + 0x30] */
        u8 *u = ps_user(); u32 sp = 0; int side = 0; float z;
        if (!u) return;
        ps_find_balls();
        if (!g_psBall[2] || !g_psBall[3]) return;   /* ball list not built yet: next tick (they start holding one) */
        if (!ps_any()) {   /* the basket is chosen once, with the first passer */
            if (*(u32*)(u + 0x4C) && read32(*(u32*)(u + 0x4C) + 0x30, &sp) && sp) read32(sp, (u32*)&side);
            z = ps_z(u);
            if (side == 1 || side == -1) g_psSide = side;
            else { if (z > -400.0f && z < 400.0f) return; g_psSide = z < 0 ? -1 : 1; }   /* no side set: his half */
            g_psSideSrc = side;
        }
        g_psSpawnAt = t + 100;    /* no teammate yet: try again soon (was 3 s) */
        for (k = 0; k < want; k++) if (!*g_psSlot[k]) ps_make_passer(k, u);
    }
}
/* practice predicate 0x6B1F50 (asked on the game thread every frame in practice): runs the passer logic */
static int __cdecl PsIsPractice(void)
{
    int sub = *(int*)0x1E85CD8;
    /* PERF 1.11.30-ML: the predicate is asked from 55 places, many times a frame - the passer logic runs at most
     * every 4 ms (each frame at up to 250 fps); in between only the chosen current ball is kept */
    if (!g_psInPred) {
        static u32 step, last; long long f, now;
        if (!step) { QueryPerformanceFrequency((LARGE_INTEGER*)&f); step = (u32)f / 250u; if (!step) step = 1; }
        QueryPerformanceCounter((LARGE_INTEGER*)&now);
        if (!last || (u32)now - last >= step || !ps_any()) {
            last = (u32)now; g_psInPred = 1; ps_tick(); g_psInPred = 0;
        } else if (g_psCurWant && *(u8**)0x1CE716C != g_psCurWant) {
            u8 *h = ps_holder(g_psCurWant);
            if (!h || ps_is(h)) *(u8**)0x1CE716C = g_psCurWant;
        }
    }
    return *(int*)0x1E85A90 && *(int*)0x1E85CD4 == 4 && sub != 1 && sub != 2 && sub != 4;
}


/* 1.11.31-ML: a pass from a passer caught in the backcourt was a backcourt violation (0x79DFB0, called only from
 * 0x8A191C) - the play stopped and control went back to the first player. Freestyle is practice: no violation. */
static int __cdecl ps_skip_violation(void)
{
    static int n;
    if (!ps_fs()) return 0;
    if (n < 5) { n++; blog("PRACTICE Freestyle: backcourt violation skipped"); }
    return 1;
}
__declspec(naked) static void PsViolation(void)
{
    __asm {
        pushad
        call ps_skip_violation
        test eax, eax
        popad
        jz orig
        xor eax, eax
        ret
    orig:
        push 0x0079DFB0
        ret
    }
}

/* 1.11.31-ML: every game stoppage routine that sets the "violation / dead ball" play (situation 0x12: travel, backcourt
 * on a dribble or a pass, 3 / 5 / 8 / 24 seconds, out of bounds ...) returns at once in Freestyle - none of them happens
 * in plain Freestyle, they only came with the passers (a team on the floor). Other modes: untouched. A small gate in
 * front of each routine: skip (ret / ret N) in Freestyle, else the routine's own first bytes and on. */
static int __cdecl ps_stop_gate(u32 fn)
{
    static int n;
    if (!ps_fs()) return 0;
    if (n < 8) { n++; blog("PRACTICE Freestyle: game stoppage %08X skipped (violation / dead ball)", fn); }
    return 1;
}
static int ps_install_stops(void)
{
    static const struct { u32 fn; u16 ret; u8 len; u8 pro[10]; } T[] = {
        {0x79D660,0,6,{0x55,0x8B,0xEC,0x83,0xE4,0xF0}}, {0x79D9B0,0,6,{0x55,0x8B,0xEC,0x83,0xE4,0xF0}}, {0x79DB60,0,6,{0x55,0x8B,0xEC,0x83,0xE4,0xF0}},
        {0x79DD50,0,6,{0x55,0x8B,0xEC,0x83,0xE4,0xF0}}, {0x79DFB0,0,6,{0x55,0x8B,0xEC,0x83,0xE4,0xF0}}, {0x79E2B0,0,6,{0x55,0x8B,0xEC,0x83,0xE4,0xF0}},
        {0x79E520,0,6,{0x55,0x8B,0xEC,0x83,0xE4,0xF0}}, {0x79E8E0,0,6,{0x55,0x8B,0xEC,0x83,0xE4,0xF0}}, {0x79ED70,0,6,{0x55,0x8B,0xEC,0x83,0xE4,0xF0}},
        {0x79F310,0,6,{0x55,0x8B,0xEC,0x83,0xE4,0xF0}}, {0x79F5B0,0,6,{0x55,0x8B,0xEC,0x83,0xE4,0xF0}}, {0x79F8A0,4,6,{0x55,0x8B,0xEC,0x83,0xE4,0xF0}},
        {0x79FB90,0,6,{0x55,0x8B,0xEC,0x83,0xE4,0xF0}}, {0x79FEB0,0,6,{0x55,0x8B,0xEC,0x83,0xE4,0xF0}}, {0x7A02A0,4,6,{0x55,0x8B,0xEC,0x83,0xE4,0xF0}},
        {0x7A0910,0,6,{0x55,0x8B,0xEC,0x83,0xE4,0xF0}}, {0x7A0B80,0,6,{0x55,0x8B,0xEC,0x83,0xE4,0xF0}}, {0x7A12E0,0,6,{0x55,0x8B,0xEC,0x83,0xE4,0xF0}},
        {0x7A1A30,0,6,{0x55,0x8B,0xEC,0x83,0xE4,0xF0}}, {0x7A58E0,4,6,{0x55,0x8B,0xEC,0x83,0xE4,0xF0}},
        {0x79CAA0,0,10,{0x83,0xEC,0x54,0x83,0x3D,0xE8,0x2B,0xB2,0x01,0x00}}, {0x7A05F0,8,5,{0x83,0xEC,0x28,0x53,0x55}},
    };
    u8 *mem = (u8 *)VirtualAlloc(0, 64 * (sizeof T / sizeof T[0]), MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    int i, n = 0;
    if (!mem) return 0;
    for (i = 0; i < (int)(sizeof T / sizeof T[0]); i++) {
        u8 *c = mem + 64 * i; int k = 0, j;
        if (!ps_same((const u8 *)T[i].fn, T[i].pro, T[i].len)) continue;
        c[k++] = 0x60;                                            /* pushad */
        c[k++] = 0x68; *(u32 *)(c + k) = T[i].fn; k += 4;         /* push fn */
        c[k++] = 0xB8; *(u32 *)(c + k) = (u32)&ps_stop_gate; k += 4; /* mov eax, gate */
        c[k++] = 0xFF; c[k++] = 0xD0;                             /* call eax */
        c[k++] = 0x83; c[k++] = 0xC4; c[k++] = 0x04;              /* add esp, 4 */
        c[k++] = 0x85; c[k++] = 0xC0;                             /* test eax, eax */
        c[k++] = 0x61;                                            /* popad (flags kept) */
        c[k++] = 0x74; c[k++] = T[i].ret ? 3 : 1;                 /* jz cont */
        if (T[i].ret) { c[k++] = 0xC2; *(u16 *)(c + k) = T[i].ret; k += 2; } else c[k++] = 0xC3;
        for (j = 0; j < T[i].len; j++) c[k++] = T[i].pro[j];      /* cont: the routine's own first bytes */
        c[k++] = 0xE9; *(u32 *)(c + k) = T[i].fn + T[i].len - ((u32)c + k + 4); k += 4;
        FlushInstructionCache(GetCurrentProcess(), c, 64);
        if (patch_jmp(T[i].fn, (u32)c)) n++;
    }
    blog("PRACTICE Freestyle: %d/%d game stoppage routines skipped in Freestyle (violations / dead balls)", n, (int)(sizeof T / sizeof T[0]));
    return n;
}

static void install_passer(void)
{
    static const u8 hp[] = {0x83,0x3D,0x90,0x5A,0xE8,0x01,0x00}, ho[] = {0x56,0x8B,0xF1,0x8B,0x46,0x60,0xF7,0xD0},
                    hs[] = {0x81,0xEC,0xB8,0x00,0x00,0x00,0x56}, hr[] = {0x83,0x3D,0xD8,0x5C,0xE8,0x01,0x03,0x75,0x09},
                    ha[] = {0x6A,0x00,0x8B,0xCE,0xE8}, hb[] = {0x3B,0x1D,0x6C,0x71,0xCE,0x01,0x0F,0x85}, hst[] = {0x53,0x56,0x57,0x8B,0xF9},
                    hc[] = {0x89,0x35,0x6C,0x71,0xCE,0x01}, hap[] = {0x8B,0xCD,0xE8}, hg[] = {0x8B,0x15,0x6C,0x71,0xCE,0x01},
                    hsh[] = {0x55,0x8B,0xEC,0x83,0xE4,0xF0}, hpl[] = {0x56,0x8B,0xF1,0x8B,0x4E,0x3C}, hnm[] = {0x56,0x57,0x8B,0xF9,0xE8}, hcm[] = {0xA1,0x6C,0x71,0xCE,0x01,0x83,0xEC,0x08};
    int i, n = 0, ok;
    cfg_psPasser = ini_int2("Practice", "Passer", 1);
    cfg_psCount  = ini_int2("Practice", "Passers", 2);
    cfg_psHideIcons = ini_int2("Practice", "HidePasserIcons", 1);
    cfg_psOopA   = ini_int2("Practice", "AlleyOopA", 1);
    cfg_psOopB   = ini_int2("Practice", "AlleyOopB", 0);
    if (!cfg_psPasser || g_is2k12) { blog("PRACTICE Freestyle passers off"); return; }
    /* all sites checked first: nothing is patched unless every one is the expected 2K14 code */
    ok = readable((const void*)0x6B1F50, 8) && ps_same((const u8*)0x6B1F50, hp, 7) && ps_same((const u8*)0x6B44F0, ho, 8) &&
         ps_same((const u8*)0x7965A0, hs, 7) && ps_same((const u8*)0x7A9434, hr, 9) &&
         ps_same((const u8*)0x863114, ha, 5) && ps_same((const u8*)0x7A91DD, hb, 8) &&
         ps_same((const u8*)0x404DB0, hst, 5) && ps_same((const u8*)0x894B56, hc, 6) && ps_same((const u8*)0x784A87, hap, 3) &&
         0x784A89 + 5 + *(int*)0x784A8A == 0x71A970 && ps_same((const u8*)0x784C25, hg, 6) && ps_same((const u8*)0x769D30, hsh, 6) && ps_same((const u8*)0x6E2A90, hpl, 6) && ps_same((const u8*)0xC726B0, hnm, 5) && ps_same((const u8*)0x78B510, hcm, 8) && 0xC726B4 + 5 + *(int*)0xC726B5 == 0x4120B0 &&
         *(u8*)0x7A91FD == 0xE8 && 0x7A91FD + 5 + *(int*)0x7A91FE == 0x8C4590 && 0x863118 + 5 + *(int*)0x863119 == 0x404DB0 &&
         *(u8*)0x7AB7F8 == 0xE8 && 0x7AB7F8 + 5 + *(int*)0x7AB7F9 == 0x6E94E0 &&
         *(u8*)0x7ED97C == 0xE8 && *(u8*)0x830000 == 0xE8;
    for (i = 0; ok && i < (int)(sizeof g_psStateSites / 4); i++) { u32 a = g_psStateSites[i];
        ok = *(u8*)a == 0xE8 && a + 5 + *(int*)(a + 1) == 0x4120B0; }
    ok = ok && *(u8*)0x7ED8D7 == 0xE8 && 0x7ED8D7 + 5 + *(int*)0x7ED8D8 == 0x4120B0 &&
               *(u8*)0x7ED946 == 0xE8 && 0x7ED946 + 5 + *(int*)0x7ED947 == 0x4120B0 &&
               *(u8*)0x82FAD9 == 0xE8 && 0x82FAD9 + 5 + *(int*)0x82FADA == 0x4120B0 &&
               *(u8*)0x7E5A9B == 0xE8 && 0x7E5A9B + 5 + *(int*)0x7E5A9C == 0x7B65A0 &&
               *(u8*)0x8631F0 == 0xE8 && 0x8631F0 + 5 + *(int*)0x8631F1 == 0x7B65A0;
    if (!ok) { blog("PRACTICE Freestyle passers NOT installed (unexpected game code)"); return; }
    g_psFakeState[0x2C / 4] = 0; g_psFakeState[0x34 / 4] = 1; g_psFakeState[0x14 / 4] = 0xE;
    for (i = 0; i < (int)(sizeof g_psStateSites / 4); i++) n += patch_call(g_psStateSites[i], (u32)&PsPasserState) ? 1 : 0;
    n += patch_call(0x7AB7F8, (u32)&PsFsDefault) ? 1 : 0;
    n += patch_call(0x7ED97C, (u32)&PsIsFtEsi) ? 1 : 0;
    n += patch_call(0x830000, (u32)&PsIsFtEdi) ? 1 : 0;
    n += patch_call(0x7ED8D7, (u32)&PsStEsi) ? 1 : 0;
    n += patch_call(0x7ED946, (u32)&PsStEsi) ? 1 : 0;
    n += patch_call(0x82FAD9, (u32)&PsStEbx) ? 1 : 0;
    n += patch_call(0x7E5A9B, (u32)&PsDribbleEsi) ? 1 : 0;
    n += patch_call(0x8631F0, (u32)&PsDribbleEsi) ? 1 : 0;
    n += patch_jmp(0x6B44F0, (u32)&PsOffFloor) ? 1 : 0;
    n += patch_jmp(0x7965A0, (u32)&PsSwapThunk) ? 1 : 0;
    n += patch_jmp(0x7A9434, (u32)&PsReboundThunk) ? 1 : 0;
    n += patch_jmp(0x863114, (u32)&PsActionThunk) ? 1 : 0;
    n += patch_call(0x7A91FD, (u32)&PsPredictThunk) ? 1 : 0;
    n += patch_jmp(0x7A91DD, (u32)&PsReboundBallThunk) ? 1 : 0;
    n += patch_jmp(0x894B56, (u32)&PsCurBallThunk) ? 1 : 0;
    n += patch_jmp(0x784A87, (u32)&PsAutoPassThunk) ? 1 : 0;
    n += patch_jmp(0x784C25, (u32)&PsGoBallThunk) ? 1 : 0;
    n += patch_jmp(0x769D30, (u32)&PsShootThunk) ? 1 : 0;
    if (cfg_psHideIcons) n += patch_jmp(0xC726B0, (u32)&PsNameThunk) ? 1 : 0;
    n += patch_jmp(0x78B510, (u32)&PsCmdHook) ? 1 : 0;
    { u32 f = (u32)&PsHoldUpdate; n += *(u32*)0xF30C94 == 0x8631D0 && patch_bytes(0xF30C94, &f, 4, 0) ? 1 : 0; }
    ps_install_stops();
    if (*(u8*)0x8A191C == 0xE8 && 0x8A191C + 5 + *(int*)0x8A191D == 0x79DFB0) n += patch_call(0x8A191C, (u32)&PsViolation) ? 1 : 0;
    else blog("PRACTICE WARNING: violation call not as expected - Freestyle can still call backcourt violations");
    n += patch_jmp(0x6B1F50, (u32)&PsIsPractice) ? 1 : 0;   /* last: starts the passer logic */
    g_psFsOwn = 1;
    blog("PRACTICE Freestyle passers installed (%d/29 sites, 3 balls, %d passer(s))", n, cfg_psCount < 2 ? 1 : 2);
}
