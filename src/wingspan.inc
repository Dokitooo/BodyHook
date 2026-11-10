/* ------------------------------------------------------------------------------------------ */
/* WINGSPAN (1.11.25-ML, NBA 2K14)                                                             */
/*                                                                                             */
/* Edit Player > Body and Head gets a row under Short Length: "Wingspan" 0..10, 5 = the        */
/* player's own arms, 10 = the longest (upper arm + forearm x1.15, [Wingspan] MaxPercent=15),  */
/* 0 = the shortest (x0.85). Kept per player (CFID / roster ID) in rosters\<roster>.ini.     */
/*                                                                                             */
/* HOW (found with the WINGTEST builds, v19 confirmed in game)                                 */
/*   Arms: the player's pose job (0x00BF1750 "push 0x00C05790" -> 0x00BF2D10, 2nd job argument */
/*   = the posed bone array, 64 bytes per bone, translation +30). Right after it, the bones    */
/*   of both arms from the shoulder on are moved: upper arm / forearm bones away from the      */
/*   shoulder by k-1, wrist + hand + fingers by the wrist's move (the hand keeps its shape).   */
/*   Everything that reads the bones after posing (skin, sleeves, hands) sees the new arm.     */
/*   Which player a bone array belongs to: the skin call 0x00BF1ECB (render desc in ebx, +08 = */
/*   player record) - it also gives the skeleton, whose bind table tells the bone order.        */
/*   A bone array the pose job did not change (first frame, other paths) is changed there.     */
/*   Ball: 0x009C5275 draws each held object with its matrix at [model+64] (rewritten by the   */
/*   game every frame): an object within 35 cm of a changed hand moves with that hand.        */
/*   Never twice: every change remembers the result (wrists / ball position) and is skipped     */
/*   while the game has not rewritten the bones / matrix.                                      */
/* ------------------------------------------------------------------------------------------ */
#define WS_LABEL_KEY 0x9E50F30Du   /* crc32("BODYHOOK_WINGSPAN") | 1 */
#define WS_HELP_KEY  0xB16A1C6Fu   /* crc32("BODYHOOK_WINGSPAN_HELP") */
#define WS_W        64             /* bone arrays tracked (players drawn at once: ~25) */
#define WS_SK       32

static int   cfg_ws = 1;
static float cfg_wsStep = 0.03f;   /* per level: 10 = 1 + 5 * step */
#define g_wsIni g_rosterIni        /* 1.11.26-ML: rosters\<roster>.ini [Wingspan] */
static CRITICAL_SECTION g_wsLock;
static int   g_wsReady = 0, g_wsHooks = 0;
static u8    g_wsVal[65536];       /* cfid -> 0..10 (5 = own arms) */
static u8    g_wsGen[65536];       /* 1.11.26-ML: generic / created faces, by roster ID */
static volatile int g_wsAny = 0;   /* players with a value other than 5: no work at all while 0 */
static volatile u32 st_wsJob = 0, st_wsSkin = 0, st_wsBall = 0;

static int ws_get(u32 cfid)
{
    if (cfid & GEN_BIT) return g_wsGen[cfid & 0xFFFF];
    return (cfid && cfid <= 0xFFFF) ? g_wsVal[cfid] : 5;
}
static void ws_set(u32 cfid, int v)
{
    int was;
    u8 *slot;
    if (!cfid || (!(cfid & GEN_BIT) && cfid > 0xFFFF)) return;
    if (v < 0) v = 0;
    if (v > 10) v = 10;
    slot = (cfid & GEN_BIT) ? &g_wsGen[cfid & 0xFFFF] : &g_wsVal[cfid];
    was = *slot;
    *slot = (u8)v;
    if (was == 5 && v != 5) g_wsAny++;
    if (was != 5 && v == 5 && g_wsAny > 0) g_wsAny--;
}
static float ws_scale(u32 cfid) { return 1.0f + (float)(ws_get(cfid) - 5) * cfg_wsStep; }

/* ---- skeleton: where are the arms? ----------------------------------------------------------- */
/* hi_body bind positions (cm), left arm; the right arm is the same with x mirrored */
static const float kWsShoulderL[3] = { 24.177f, 44.876f, 0.168f };   /* hi_body bone 78 */
static const float kWsElbowL[3]    = { 47.172f, 32.811f, 4.008f };   /* bone 82 */
static struct { u32 skel, n, bind; int base; } g_wsSk[WS_SK];

static int ws_near(const float *p, const float *v, float tol)
{
    int k;
    for (k = 0; k < 3; k++) {
        float d;
        if (!(p[k] == p[k]) || p[k] > 1.0e5f || p[k] < -1.0e5f) return 0;   /* NaN / garbage */
        d = p[k] - v[k];
        if (!(d >= -tol && d <= tol)) return 0;
    }
    return 1;
}
/* right-side bone = left-side bone seen in a mirror (|x|, y, |z| within 5 cm) */
static float ws_abs(float v) { return v < 0.0f ? -v : v; }
static int ws_mirror(const float *r, const float *l)
{
    float m[3], a[3];
    m[0] = ws_abs(r[0]); m[1] = r[1]; m[2] = ws_abs(r[2]);
    a[0] = ws_abs(l[0]); a[1] = l[1]; a[2] = ws_abs(l[2]);
    return r[0] < -5.0f && ws_near(m, a, 5.0f);
}
/* both arms where an arm should be: left shoulder -> elbow -> wrist going out (+x), right side mirrored */
static int ws_arms_ok(const u8 *bind, int base)
{
    const float *ls = (const float*)(bind + (base + 78) * 48), *le = (const float*)(bind + (base + 82) * 48),
                *lw = (const float*)(bind + (base + 87) * 48), *rs = (const float*)(bind + (base + 50) * 48),
                *re = (const float*)(bind + (base + 54) * 48), *rw = (const float*)(bind + (base + 59) * 48);
    if (!(ls[0] > 5.0f && le[0] > ls[0] + 5.0f && lw[0] > le[0] + 5.0f && lw[0] < 150.0f)) return 0;
    return ws_mirror(rs, ls) && ws_mirror(re, le) && ws_mirror(rw, lw);
}
/* bone index shift of this skeleton (hi_body order + base), -1000 = not a body we know */
static int ws_skel_base(u32 skel, u32 *pn)
{
    u32 n = 0, bind = 0, i, h = (skel >> 4) % WS_SK;
    int base = -1000;
    if (!read32(skel + 0x60, &n) || !read32(skel + 0x64, &bind)) return -1000;
    if (g_wsSk[h].skel == skel && g_wsSk[h].n == n && g_wsSk[h].bind == bind) { *pn = n; return g_wsSk[h].base; }
    if (n >= 103 && n <= 256 && readable((void*)bind, n * 48)) {
        for (i = 0; i < n; i++)                                       /* stock body: the elbow bind */
            if (ws_near((const float*)(bind + i * 48), kWsElbowL, 0.5f)) break;
        if (i < n && (int)i - 82 >= -50 && (int)i - 82 + 103 <= (int)n && ws_arms_ok((const u8*)bind, (int)i - 82)) base = (int)i - 82;
        /* custom body (other proportions) with the stock bone order: 122 bones, base 1 */
        if (base <= -1000 && n == 122 && ws_arms_ok((const u8*)bind, 1)) base = 1;
    }
    g_wsSk[h].skel = skel; g_wsSk[h].n = n; g_wsSk[h].bind = bind; g_wsSk[h].base = base;
    {
        static int logged = 0;
        if (cfg_diagnostics || logged < 6) {
            logged++;
            blog("WINGSPAN skeleton %08X: %u bones, arms %s (bone shift %d)", skel, n, base > -1000 ? "found" : "NOT found - this body keeps its arms", base);
        }
    }
    *pn = n;
    return base;
}

/* ---- the arm ---------------------------------------------------------------------------------- */
static void ws_arm(u8 *world, int S, int W, int last, float k)
{
    float s[3], d[3];
    int b, j;
    for (j = 0; j < 3; j++) s[j] = *(float*)(world + S * 64 + 0x30 + j * 4);
    for (j = 0; j < 3; j++) d[j] = (k - 1.0f) * (*(float*)(world + W * 64 + 0x30 + j * 4) - s[j]);
    for (b = S + 1; b <= last; b++) {
        float *t = (float*)(world + b * 64 + 0x30);
        if (b < W) for (j = 0; j < 3; j++) t[j] += (k - 1.0f) * (t[j] - s[j]);
        else       for (j = 0; j < 3; j++) t[j] += d[j];
    }
}

static float ws_sqrt(float x)
{
    union { float f; u32 u; } v;
    int it;
    if (!(x > 0.0f) || x > 1.0e30f) return 0.0f;
    v.f = x; v.u = (v.u >> 1) + 0x1FBD1DF5u;                   /* first guess within a few % */
    for (it = 0; it < 4; it++) v.f = 0.5f * (v.f + x / v.f);
    return v.f;
}
/* 1.11.25-ML b3: move one arm's wrist + hand by c, the arm bones by their share of the way to the wrist */
static void ws_shift(u8 *world, int S, int W, int last, const float *c)
{
    const float *s = (const float*)(world + S * 64 + 0x30), *w = (const float*)(world + W * 64 + 0x30);
    float sw = ws_sqrt((w[0] - s[0]) * (w[0] - s[0]) + (w[1] - s[1]) * (w[1] - s[1]) + (w[2] - s[2]) * (w[2] - s[2]));
    int b, j;
    if (sw < 1.0f) return;
    for (b = S + 1; b <= last; b++) {
        float *t = (float*)(world + b * 64 + 0x30), f = 1.0f;
        if (b < W) {
            f = ws_sqrt((t[0] - s[0]) * (t[0] - s[0]) + (t[1] - s[1]) * (t[1] - s[1]) + (t[2] - s[2]) * (t[2] - s[2])) / sw;
            if (f > 1.0f) f = 1.0f;
        }
        for (j = 0; j < 3; j++) t[j] += f * c[j];
    }
}

/* one posed bone array */
typedef struct {
    u32 world, cfid, n, seen, job, hd;   /* seen = last skin, job = last pose job change, hd = hand data time */
    int base, wr;                        /* wr: bone memory writable (0 = not checked yet, 1 yes, 2 no) */
    float w[2][3];                       /* wrists after our change (= "already done") */
    float o[2][3], d[2][3];              /* wrists before, and how far they moved (for the ball) */
} WsW;
static WsW g_wsW[WS_W];

static WsW *ws_find(u32 world, int create, u32 now)
{
    int i, old = 0;
    for (i = 0; i < WS_W; i++) if (g_wsW[i].world == world) return &g_wsW[i];
    if (!create) return 0;
    for (i = 0; i < WS_W && g_wsW[i].world; i++) ;
    if (i < WS_W) old = i;                                     /* a free slot, else the one seen longest ago */
    else for (i = 1; i < WS_W; i++) if (now - g_wsW[i].seen > now - g_wsW[old].seen) old = i;
    memset(&g_wsW[old], 0, sizeof(WsW));
    g_wsW[old].world = world; g_wsW[old].base = -1000; g_wsW[old].seen = now;
    return &g_wsW[old];
}
static float *ws_wrist(u32 world, int base, int side) { return (float*)(world + (base + (side ? 59 : 87)) * 64 + 0x30); }
static int ws_done(WsW *e)
{
    float *l = ws_wrist(e->world, e->base, 0), *r = ws_wrist(e->world, e->base, 1);
    return l[0] == e->w[0][0] && l[1] == e->w[0][1] && l[2] == e->w[0][2] &&
           r[0] == e->w[1][0] && r[1] == e->w[1][1] && r[2] == e->w[1][2];
}
static int ws_pose_ok(u32 world, int base);
/* lock held; world verified readable for e->n bones */
static int ws_apply(WsW *e, float k, u32 now)
{
    float *l, *r;
    int j;
    if (ws_done(e)) { e->hd = now; return 0; }          /* not re-posed since our change */
    if (!ws_pose_ok(e->world, e->base)) return 0;          /* not a posed body (yet): leave it */
    l = ws_wrist(e->world, e->base, 0); r = ws_wrist(e->world, e->base, 1);
    for (j = 0; j < 3; j++) { e->o[0][j] = l[j]; e->o[1][j] = r[j]; }
    ws_arm((u8*)e->world, e->base + 78, e->base + 87, e->base + 102, k);
    ws_arm((u8*)e->world, e->base + 50, e->base + 59, e->base + 74, k);
    {   /* b3: hands together (two-hand hold / ball between the hands): both hands move the same way, so they
         * stay on the ball - each arm alone would carry its hand outward along itself and the hands would
         * spread apart around the ball. Full when the wrists were up to 35 cm apart, none from 50 cm. */
        float x = e->o[0][0] - e->o[1][0], y = e->o[0][1] - e->o[1][1], z = e->o[0][2] - e->o[1][2];
        float wd = ws_sqrt(x * x + y * y + z * z), bl = wd <= 35.0f ? 1.0f : (50.0f - wd) / 15.0f;
        if (bl > 0.0f) {
            float cl[3], cr[3];
            for (j = 0; j < 3; j++) {
                float a = 0.5f * ((l[j] - e->o[0][j]) + (r[j] - e->o[1][j]));
                cl[j] = bl * (a - (l[j] - e->o[0][j]));
                cr[j] = bl * (a - (r[j] - e->o[1][j]));
            }
            ws_shift((u8*)e->world, e->base + 78, e->base + 87, e->base + 102, cl);
            ws_shift((u8*)e->world, e->base + 50, e->base + 59, e->base + 74, cr);
        }
    }
    for (j = 0; j < 3; j++) {
        e->w[0][j] = l[j]; e->w[1][j] = r[j];
        e->d[0][j] = l[j] - e->o[0][j]; e->d[1][j] = r[j] - e->o[1][j];
    }
    e->hd = now;
    return 1;
}
/* the bones look like a posed body: upper arm and forearm 10..60 cm on both sides, both arms alike */
static float ws_d2(u32 world, int a, int b)
{
    const float *p = (const float*)(world + a * 64 + 0x30), *q = (const float*)(world + b * 64 + 0x30);
    float x = p[0] - q[0], y = p[1] - q[1], z = p[2] - q[2], r = x * x + y * y + z * z;
    return (r == r) ? r : -1.0f;
}
static int ws_pose_ok(u32 world, int base)
{
    float uL = ws_d2(world, base + 78, base + 82), fL = ws_d2(world, base + 82, base + 87),
          uR = ws_d2(world, base + 50, base + 54), fR = ws_d2(world, base + 54, base + 59);
    if (!(uL >= 100.0f && uL <= 3600.0f && fL >= 100.0f && fL <= 3600.0f &&
          uR >= 100.0f && uR <= 3600.0f && fR >= 100.0f && fR <= 3600.0f)) return 0;
    return uL < uR * 1.6f && uR < uL * 1.6f && fL < fR * 1.6f && fR < fL * 1.6f;
}
static int ws_world_ok(WsW *e)
{
    if (e->base <= -1000 || e->base + 50 < 0 || (u32)(e->base + 103) > e->n || e->n > 256) return 0;
    if (!e->wr) {                                              /* once per bone array: committed + writable */
        MEMORY_BASIC_INFORMATION mbi;
        u32 a = e->world, end = e->world + e->n * 64;
        e->wr = 1;
        while (a < end) {
            if (!VirtualQuery((const void*)a, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
                !(mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE))) { e->wr = 2; break; }
            a = (u32)mbi.BaseAddress + (u32)mbi.RegionSize;
        }
    }
    return e->wr == 1 && readable((void*)e->world, e->n * 64);
}

/* ---- pose job: right after the game posed the player ------------------------------------------ */
void __fastcall WsPost(u32 model, u32 world)
{
    WsW *e;
    u32 now;
    (void)model;
    if (!g_wsAny || !world) return;
    now = GetTickCount();
    EnterCriticalSection(&g_wsLock);
    e = ws_find(world, 0, now);
    if (e && e->cfid && now - e->seen < 500 && ws_world_ok(e)) {   /* drawn lately as this player */
        float k = ws_scale(e->cfid);
        if (k != 1.0f) {
            if (ws_apply(e, k, now) && !st_wsJob++) blog("WINGSPAN first arm change (pose job): CFID %u, x%d.%02d", e->cfid, (int)k, (int)(k * 100.0f + 0.5f) % 100);
            e->job = now;
        }
        else e->hd = 0;
    }
    LeaveCriticalSection(&g_wsLock);
}
u32 g_wsJobOrig = 0x00C05790u;
__declspec(naked) void WsJobStub(void)
{
    __asm {
        push ecx
        push dword ptr [esp + 0x14]
        push dword ptr [esp + 0x14]
        push dword ptr [esp + 0x14]
        push dword ptr [esp + 0x14]
        mov  eax, g_wsJobOrig
        call eax                       /* 0x00C05790 (ret 10h) -> 0x00BF2D10(arg2, arg3, arg4) */
        pop  ecx
        push eax
        mov  edx, [esp + 0Ch]          /* arg2 = the posed bones */
        call WsPost
        pop  eax
        ret  10h
    }
}

/* ---- skin: which player owns this bone array (and change it if the pose job did not) ---------- */
typedef u32 (__fastcall *WsSkinFn_t)(u32 out, u32 skel, u32 world);
u32 __fastcall WsSkin(u32 out, u32 skel, u32 world, u32 desc)
{
    if (g_wsAny && world && skel && desc) {
        u32 n = 0, player = 0, cfid, now = GetTickCount();
        if (read32(desc + 0x08, &player) && player) {
            int base;
            cfid = player_key((u8*)player);
            EnterCriticalSection(&g_wsLock);
            base = ws_skel_base(skel, &n);
            if (base > -1000) {
                WsW *e = ws_find(world, cfid && ws_get(cfid) != 5, now);   /* only players who need it get a slot */
                if (e) {
                    if (e->n != n) e->wr = 0;
                    e->cfid = cfid; e->base = base; e->n = n; e->seen = now;
                    if (ws_world_ok(e)) {
                        float k = ws_scale(cfid);
                        if (k == 1.0f) e->hd = 0;
                        else if (now - e->job > 250) {                       /* no pose job for it */
                            if (ws_apply(e, k, now) && !st_wsSkin++) blog("WINGSPAN first arm change (at draw): CFID %u, x%d.%02d", cfid, (int)k, (int)(k * 100.0f + 0.5f) % 100);
                        }
                        else if (ws_done(e)) e->hd = now;
                    }
                }
            }
            LeaveCriticalSection(&g_wsLock);
        }
    }
    return ((WsSkinFn_t)0x00DB8570u)(out, skel, world);
}
/* replaces "call 0x00DB8570" at 0x00BF1ECB: ecx, edx, [esp+4] = out, skeleton, bones; ebx = render desc */
__declspec(naked) void WsSkinStub(void)
{
    __asm {
        push ebx
        push dword ptr [esp + 8]
        call WsSkin
        ret 4
    }
}

/* ---- the ball (and anything else held) -------------------------------------------------------- */
typedef void (__fastcall *WsDrawObj_t)(u32 model);
static struct { u32 m; float t[3]; } g_wsBall[16];
/* 1.11.25-ML b2: per 30 s, what the ball hooks saw (main pass 009C5275 / other passes 009C52A6) */
static volatile u32 g_wsBd[2] = { 0, 0 }, g_wsBm = 0, g_wsBlog = 0;
static volatile float g_wsBnear = 1e9f;
static void ws_ball_report(u32 now)
{
    if (now - g_wsBlog < 30000) return;
    if (g_wsBlog && (g_wsBd[0] || g_wsBd[1]))
        blog("WINGSPAN ball (last 30 s): drawn %u in games / %u in other screens, moved %u, nearest changed hand %d cm",
             g_wsBd[0], g_wsBd[1], g_wsBm, g_wsBnear < 1e8f ? (int)g_wsBnear : -1);
    g_wsBlog = now; g_wsBd[0] = g_wsBd[1] = 0; g_wsBm = 0; g_wsBnear = 1e9f;
}
static void ws_ball(u32 model, int site)
{
    u32 m = 0, now;
    float p[3], best = 1e18f, bd[3] = { 0, 0, 0 }, sw = 0.0f, fall = 0.0f;
    int i, h, j, hit = 0;
    if (!g_wsAny || !read32(model + 0x64, &m) || !m || !readable((void*)m, 0x40)) return;
    now = GetTickCount();
    g_wsBd[site]++;
    memcpy(p, (void*)(m + 0x30), 12);
    if (p[0] == p[0] && p[1] == p[1] && p[2] == p[2]) {
        float *t = (float*)(m + 0x30);
        EnterCriticalSection(&g_wsLock);
        for (i = 0; i < 16; i++)                               /* moved already, not rewritten since? */
            if (g_wsBall[i].m == m && t[0] == g_wsBall[i].t[0] && t[1] == g_wsBall[i].t[1] && t[2] == g_wsBall[i].t[2]) break;
        if (i == 16) {
            for (i = 0; i < WS_W; i++) {
                WsW *e = &g_wsW[i];
                if (!e->world || !e->hd || now - e->hd > 200) continue;
                for (h = 0; h < 2; h++) {
                    /* b3: every changed hand within 35 cm counts (two hands on the ball: their average), nearer = more */
                    float dx = p[0] - e->o[h][0], dy = p[1] - e->o[h][1], dz = p[2] - e->o[h][2], d2 = dx * dx + dy * dy + dz * dz, r, f, wi;
                    if (d2 < best) best = d2;
                    /* 1.11.31-ML: a held ball sits in the fingers, 25-35 cm from the wrist (two-hand holds, free
                     * throw stance, passes, layups...): full up to 38 cm, fading out to 55 cm (was 20 / 35: the ball
                     * moved only partly or not at all in those holds) */
                    if (d2 >= 55.0f * 55.0f) continue;
                    r = ws_sqrt(d2);
                    f = r <= 38.0f ? 1.0f : (55.0f - r) / 17.0f;
                    wi = f / (r + 5.0f);
                    for (j = 0; j < 3; j++) bd[j] += wi * e->d[h][j];
                    sw += wi;
                    if (f > fall) fall = f;
                    hit = 1;
                }
            }
            if (best < 1e17f) { float r = ws_sqrt(best); if (r < g_wsBnear) g_wsBnear = r; }
            /* 1.11.31-ML: menus (Edit Player preview): the bone array is shifted against the ball (wrists come out
             * 130-170 cm in front of the body's first bone, the ball 30-60 cm from it - log afb27748), so no hand is
             * "near" the ball. There a held object within 150 cm of such a player's first bone moves the way both his
             * hands moved (the shift is the same for all his bones, so the hands' move is still right). */
            if (!hit) {
                WsW *pick = 0; float pb = 150.0f * 150.0f;
                for (i = 0; i < WS_W; i++) { WsW *e = &g_wsW[i]; const float *b0; float wa, wb2, d2, x, y, z;
                    if (!e->world || !e->hd || now - e->hd > 200 || e->base <= -1000) continue;
                    b0 = (const float*)(e->world + 0x30);
                    x = e->o[0][0] - b0[0]; y = e->o[0][1] - b0[1]; z = e->o[0][2] - b0[2]; wa = x * x + y * y + z * z;
                    x = e->o[1][0] - b0[0]; y = e->o[1][1] - b0[1]; z = e->o[1][2] - b0[2]; wb2 = x * x + y * y + z * z;
                    if (wa < 110.0f * 110.0f || wb2 < 110.0f * 110.0f) continue;      /* hands where they belong: not this case */
                    x = p[0] - b0[0]; y = p[1] - b0[1]; z = p[2] - b0[2]; d2 = x * x + y * y + z * z;
                    if (d2 < pb) { pb = d2; pick = e; }
                }
                if (pick) {
                    for (j = 0; j < 3; j++) bd[j] = 0.5f * (pick->d[0][j] + pick->d[1][j]);
                    sw = 1.0f; fall = 1.0f; hit = 1;
                    { static int lg; if (lg < 3) { lg++; blog("WINGSPAN menu ball: held object moved with both hands (%d %d %d cm)", (int)bd[0], (int)bd[1], (int)bd[2]); } }
                }
            }
            if (hit && sw > 0.0f) {
                float w = fall;
                for (j = 0; j < 3; j++) t[j] += fall * bd[j] / sw;
                h = -1;
                for (i = 0; i < 16; i++) if (g_wsBall[i].m == m) { h = i; break; }
                if (h < 0) { static u32 nx = 0; h = (int)(nx++ & 15); }
                g_wsBall[h].m = m; g_wsBall[h].t[0] = t[0]; g_wsBall[h].t[1] = t[1]; g_wsBall[h].t[2] = t[2];
                g_wsBm++;
                if (!st_wsBall++) blog("WINGSPAN first held object moved with a hand (weight %d%%, %s)", (int)(w * 100.0f), site ? "other screen" : "game");
            }
        }
        LeaveCriticalSection(&g_wsLock);
    }
    ws_ball_report(now);
}
/* 009C5275: pass 0 (games); 009C52A6: the other passes (draw loop 0x009C5200 with ecx = 1: menus, frontend...) */
void __fastcall WsBallDraw(u32 model)  { ws_ball(model, 0); ((WsDrawObj_t)0x00958E70u)(model); }
void __fastcall WsBallDraw1(u32 model) { ws_ball(model, 1); ((WsDrawObj_t)0x00958E70u)(model); }

/* ---- saved values ----------------------------------------------------------------------------- */
static void ws_load(void)
{
    static char buf[32768];
    char *p;
    DWORD n = GetPrivateProfileSectionA("Wingspan", buf, sizeof(buf), g_wsIni);
    memset(g_wsVal, 5, sizeof(g_wsVal)); memset(g_wsGen, 5, sizeof(g_wsGen)); g_wsAny = 0;   /* 1.11.26-ML: per roster */
    for (p = buf; n && *p; p += s_len(p) + 1) {
        u32 cfid = 0; int i = 0, v = 0;
        char k[16];
        while (p[i] && p[i] != '=' && i < 15) { k[i] = p[i]; i++; }
        k[i] = 0;
        if (p[i] != '=' || !(cfid = key_parse(k))) continue;
        { char vv[64]; s_cpy(vv, p + i + 1, 64); val_split(vv, cfid); }
        i++;
        while (p[i] == ' ') i++;
        if (p[i] < '0' || p[i] > '9') continue;
        for (; p[i] >= '0' && p[i] <= '9' && v < 100; i++) v = v * 10 + (p[i] - '0');
        ws_set(cfid, v);
        { char ks[12]; key_str(cfid, ks); blog("   wingspan: %s -> %d (saved choice)", ks, ws_get(cfid)); }
    }
}

/* ---- the menu row (added to the list by install_shortlen) -------------------------------------- */
u16 g_wsLabelW[] = { 'W','i','n','g','s','p','a','n',0 };
u16 g_wsHelpW[]  = { 'S','e','l','e','c','t',' ','W','i','n','g','s','p','a','n','.',' ','5',' ','=',' ','o','r','i','g','i','n','a','l',',',' ',
                     '1','0',' ','=',' ','l','o','n','g','e','s','t',' ','a','r','m','s',0 };

static void ws_save(u32 cfid, int v, u8 *rec)
{
    char key[12], val[4], vn[64];
    key_str(cfid, key);
    u32_to_dec((u32)v, val);
    val_named(vn, sizeof(vn), val, cfid, rec);
    pend_write("Wingspan", key, v == 5 ? NULL : vn);   /* 2.1: written when the game saves */
}

static void install_wingspan(void)
{
    int i;
    memset(g_wsVal, 5, sizeof(g_wsVal)); memset(g_wsGen, 5, sizeof(g_wsGen));
    cfg_ws = ini_int2("Wingspan", "Enabled", 1);
    i = ini_int2("Wingspan", "MaxPercent", 15);
    if (i < 5) i = 5;
    if (i > 25) i = 25;
    cfg_wsStep = (float)i / 500.0f;                            /* 10 = +MaxPercent, 0 = -MaxPercent */
    if (!cfg_ws) { blog("wingspan: off ([Wingspan] Enabled=0)"); return; }
    if (g_is2k12) { cfg_ws = 0; blog("wingspan: NBA 2K14 only"); return; }
    InitializeCriticalSection(&g_wsLock);
    ws_load();

    /* all three game sites must be exactly as known, or nothing is changed */
    if (!readable((void*)0x00BF1750u, 5) || *(u8*)0x00BF1750u != 0x68 || *(u32*)0x00BF1751u != 0x00C05790u ||
        !hair_bytes(0x00BF1EC8u, "8B 49 0C E8") || call_target_of(0x00BF1ECBu) != 0x00DB8570u ||
        !readable((void*)0x009C5275u, 5) || *(u8*)0x009C5275u != 0xE8 || call_target_of(0x009C5275u) != 0x00958E70u ||
        !hair_bytes(0x00C05790u, "8B 44 24 10 50 8B 44 24 10 50 8B 44 24 10 50 E8")) {
        cfg_ws = 0;
        blog("WARNING wingspan: game code not as expected (other exe version or another mod) - no Wingspan");
        return;
    }
    if (!patch_call(0x00BF1ECBu, (u32)&WsSkinStub)) { cfg_ws = 0; blog("WARNING wingspan: skin call could not be changed - no Wingspan"); return; }
    {
        u32 v = (u32)&WsJobStub;
        if (!patch_bytes(0x00BF1751u, &v, 4, 1)) {
            patch_call(0x00BF1ECBu, 0x00DB8570u);              /* put the skin call back */
            cfg_ws = 0; blog("WARNING wingspan: pose job could not be changed - no Wingspan"); return;
        }
    }
    if (!patch_call(0x009C5275u, (u32)&WsBallDraw)) blog("WARNING wingspan: ball draw could not be changed - the ball will not follow longer arms");
    /* the same loop's other pass: model [entry+4], same matrix (009C5296 mov [edx+64],esi) */
    if (!hair_bytes(0x009C5293u, "51 8B CE 89 72 64") || !hair_bytes(0x009C52A3u, "8B 4F 04 E8") || call_target_of(0x009C52A6u) != 0x00958E70u ||
        !patch_call(0x009C52A6u, (u32)&WsBallDraw1))
        blog("WARNING wingspan: other-screen ball draw 009C52A6 not as expected - there the ball will not follow longer arms");
    g_wsHooks = 1;
    g_wsReady = 1;
    blog("WINGSPAN INSTALLED: pose job 00BF1750, skin 00BF1ECB, ball 009C5275 + 009C52A6. 0..10 = x%d.%02d..x%d.%02d, %d saved value(s) (rosters\\<roster>.ini)",
         (int)(1.0f - 5 * cfg_wsStep), (int)((1.0f - 5 * cfg_wsStep) * 100.0f + 0.5f) % 100,
         (int)(1.0f + 5 * cfg_wsStep), (int)((1.0f + 5 * cfg_wsStep) * 100.0f + 0.5f) % 100, g_wsAny);
}
