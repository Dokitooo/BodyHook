/* ------------------------------------------------------------------------------------------ */
/* HAIR STYLES - LIVE REFRESH IN EDIT PLAYER  (NBA 2K14, merged from the HairRefresh 1.1.0 tool) */
/*                                                                                             */
/* Picking another Hair Style (left/right) used to apply only the next time the player model   */
/* was built. With [HairStyles] LiveRefresh=1, BodyHook asks the Edit Player preview to rebuild */
/* the player right away, so the new png<CFID>_<tag>.iff shows immediately:                    */
/*   1. hair_step() (our left/right handler) queues the request for the edited record.          */
/*   2. a small worker (8 ms) waits until the preview queue is idle for that record and sets    */
/*      the preview's "rebuild" flag (preview +138h = 1). BodyHook's CreateFileA swap then      */
/*      loads the chosen file.                                                                  */
/*   3. while the head is being rebuilt the head-camera framing has no head bone and falls back */
/*      to a wrong framing; a guard at 4FB2DD skips those camera frames (only while armed, only */
/*      for the edited player), and when the rebuilt head is ready the Hair Style framing cache */
/*      (head focus +70h) is reset so the camera re-frames the new hair.                        */
/* Everything is checked against the RLD nba2k14.exe layout first; otherwise it stays off.      */
/* ------------------------------------------------------------------------------------------ */
#define HL_EDITREC     0x015DD518u   /* global: record being edited (same as the Hair Style item uses) */
#define HL_PREVIEW     0x01F8CB70u   /* global: Edit Player preview player / build queue                */
#define HL_FOCUS_A     0x0109DE50u   /* head-focus camera block                                          */
#define HL_FOCUS       0x0109DE90u   /* head-focus framing state (+70h = framing cache)                  */
#define HL_MGR_PTR     0x02021A30u   /* edit-player manager (+1E4h = record)                             */
#define HL_MGR_ON      0x02021A34u
#define HL_CAM_SITE    0x004FB2DDu   /* test eax,eax / je 4FB2EB / xor ebx,ebx  (head bone lookup result) */

static volatile u32 g_hlPendRec = 0, g_hlPendCfid = 0, g_hlPendUntil = 0;
static volatile u32 g_hlBusy = 0, g_hlBusyRec = 0, g_hlBusyUntil = 0, g_hlSettle = 0, g_hlReset = 0;
static volatile u32 g_hlRequests = 0, g_hlRebuilds = 0;
static int g_hlInstalled = 0;
/* used by the camera guard (naked code below) */
volatile u32 g_hlArmed = 0, g_hlArmRec = 0, g_hlArmPrv = 0, g_hlSkipped = 0;
u32 g_hlRetSkip = 0x004FB5A2u;   /* function epilogue: skip this camera frame        */
u32 g_hlRetFall = 0x004FB2EBu;   /* original "no head bone" path                     */
u32 g_hlRetOk   = 0x004FB2E3u;   /* original "head bone found" path (after xor ebx)  */

static const u16 kHlQa[10] = { 24, 28, 32, 36, 40, 44, 48, 52, 304, 308 };
static const u16 kHlQb[10] = { 316, 320, 324, 328, 332, 336, 340, 344, 596, 600 };

static u32 hl_u(u32 a) { u32 v = 0; read32(a, &v); return v; }

/* the preview queue holds this record and nothing is pending (both state copies equal) */
static int hl_queue_idle(u32 prv, u32 rec)
{
    int i;
    if (!prv || rec < 0x10000 || !readable((void*)prv, 0x570)) return 0;
    if (hl_u(prv) != 1 || hl_u(prv + 8) != 0 || hl_u(prv + 0x38) != rec || hl_u(prv + 0x138) != 0 || hl_u(prv + 0x268) != 1) return 0;
    for (i = 0; i < 10; i++) if (hl_u(prv + kHlQa[i]) != hl_u(prv + kHlQb[i])) return 0;
    for (i = 0; i < 31; i++) {
        if (hl_u(prv + 0x38 + 4 * i) != hl_u(prv + 0x15C + 4 * i)) return 0;
        if (hl_u(prv + 0xB4 + 4 * i) != hl_u(prv + 0x1D8 + 4 * i)) return 0;
    }
    return 1;
}
/* camera is in the Hair Style head framing */
static int hl_head_focus(void)
{
    return hl_u(HL_FOCUS_A) == 0x010A96C0u && hl_u(HL_FOCUS_A + 0x14) == 2 &&
           hl_u(HL_FOCUS) == 4 && hl_u(HL_FOCUS + 4) == 0 && hl_u(HL_FOCUS + 8) == 0x0109EED0u &&
           hl_u(HL_FOCUS + 0x70) <= 1;
}
/* framing cached from the fallback (no head yet): tall bounds */
static int hl_mixed_bounds(void)
{
    u32 a = hl_u(HL_FOCUS + 0x84), b = hl_u(HL_FOCUS + 0x94);
    float fa, fb;
    if (hl_u(HL_FOCUS + 0x70) != 1) return 0;
    if ((a & 0x7F800000u) == 0x7F800000u || (b & 0x7F800000u) == 0x7F800000u) return 0;   /* NaN / inf */
    memcpy(&fa, &a, 4); memcpy(&fb, &b, 4);
    return fb - fa > 25.0f;
}
static int hl_mgr_has(u32 rec)
{
    u32 m;
    if (hl_u(HL_MGR_ON) != 1) return 0;
    m = hl_u(HL_MGR_PTR);
    return m && hl_u(m + 0x1E4) == rec;
}

/* called from hair_step (Edit Player left/right on Hair Style) */
static void hair_live_queue(u32 cfid)
{
    u32 rec = 0;
    if (!g_hlInstalled) return;
    if (!read32(HL_EDITREC, &rec) || rec < 0x10000) return;
    if (!hl_head_focus()) { blog("HAIR STYLE   live refresh skipped (camera is not on the Hair Style head view)"); return; }
    g_hlPendCfid = cfid;
    g_hlPendUntil = GetTickCount() + 3000;
    g_hlPendRec = rec;
    g_hlRequests++;
}

static DWORD WINAPI hair_live_thread(LPVOID unused)
{
    u32 lastLogBusy = 0;
    (void)unused;
    for (;;) {
        u32 now = GetTickCount(), rec = hl_u(HL_EDITREC), prv = hl_u(HL_PREVIEW);
        if (g_hlPendRec) {
            if (rec != g_hlPendRec || (int)(now - g_hlPendUntil) > 0) g_hlPendRec = 0;
            else if (!g_hlBusy && prv && hl_queue_idle(prv, rec) && hl_mgr_has(rec)) {
                g_hlArmRec = rec; g_hlArmPrv = prv; g_hlArmed = 1;
                *(volatile u32*)(prv + 0x138) = 1;               /* rebuild the preview player */
                g_hlBusy = prv; g_hlBusyRec = rec; g_hlBusyUntil = now + 3000;
                g_hlSettle = 0xFFFFFFFFu; g_hlReset = 0;
                g_hlPendRec = 0; g_hlRebuilds++;
                blog("HAIR STYLE   live refresh: rebuilding the Edit Player preview (CFID %u)", g_hlPendCfid);
            }
        }
        if (g_hlBusy) {
            if (hl_u(HL_PREVIEW) != g_hlBusy || hl_u(HL_EDITREC) != g_hlBusyRec || (int)(now - g_hlBusyUntil) > 0) {
                if (lastLogBusy != g_hlRebuilds && !g_hlReset) blog("HAIR STYLE   live refresh: preview left before the rebuild finished");
                lastLogBusy = g_hlRebuilds;
                g_hlBusy = 0;
            } else if (hl_queue_idle(g_hlBusy, g_hlBusyRec) && hl_u(g_hlBusy + 0x56C) == 1 &&
                       hl_mgr_has(g_hlBusyRec) && hl_head_focus()) {
                if (!g_hlReset || hl_mixed_bounds()) {
                    *(volatile u32*)(HL_FOCUS + 0x70) = 0;       /* re-frame the camera on the new head */
                    if (!g_hlReset) g_hlSettle = now + 80;
                    g_hlReset = 1;
                }
                if (g_hlSettle != 0xFFFFFFFFu && (int)(now - g_hlSettle) >= 0) {
                    g_hlBusy = 0;
                    blog("HAIR STYLE   live refresh: new hair shown (camera frames held so far: %u)", g_hlSkipped);
                }
            }
        }
        if (!g_hlBusy) g_hlArmed = 0;
        Sleep((g_hlPendRec || g_hlBusy) ? 8 : 40);       /* PERF: 8 ms only while a hair change is being shown */
    }
}

/* camera guard: replaces  test eax,eax / je 4FB2EB / xor ebx,ebx  at 4FB2DD */
__declspec(naked) void HairCamGuard(void)
{
    __asm {
        test eax, eax
        jnz  found
        pushfd
        pushad
        cmp  g_hlArmed, 1
        jne  pass
        cmp  esi, 109DE90h
        jne  pass
        mov  ecx, 109DE50h
        cmp  dword ptr [ecx], 10A96C0h
        jne  pass
        cmp  dword ptr [ecx + 14h], 2
        jne  pass
        cmp  dword ptr [ecx + 40h], 4
        jne  pass
        cmp  dword ptr [ecx + 44h], 0
        jne  pass
        cmp  dword ptr [ecx + 48h], 109EED0h
        jne  pass
        mov  ecx, 15DD518h
        mov  eax, [ecx]
        cmp  eax, g_hlArmRec
        jne  pass
        mov  ecx, 1F8CB70h
        mov  edx, [ecx]
        cmp  edx, g_hlArmPrv
        jne  pass
        test edx, edx
        jz   pass
        cmp  dword ptr [edx], 1
        jne  pass
        cmp  [edx + 38h], eax
        jne  pass
        lock inc g_hlSkipped
        popad
        popfd
        jmp  dword ptr [g_hlRetSkip]
    pass:
        popad
        popfd
        jmp  dword ptr [g_hlRetFall]
    found:
        xor  ebx, ebx
        jmp  dword ptr [g_hlRetOk]
    }
}

static int hl_text_refs(u32 value)
{
    u8 pat[4];
    memcpy(pat, &value, 4);
    return find_text(pat, 4, 0) != 0;
}

static void install_hair_live(void)
{
    static const u8 site[] = { 0x8B,0x4E,0x04, 0x8D,0x44,0x24,0x20, 0x50, 0xE8,0x53,0xA1,0x5C,0x00,
                               0x85,0xC0, 0x74,0x0A, 0x33,0xDB, 0x39,0x5F,0x44, 0x0F,0x94,0xC3 };
    u8 b[6];
    int rel;
    if (!cfg_hairLive) { blog("hair styles: live refresh off (LiveRefresh=0)"); return; }
    if (g_hairEditRec != HL_EDITREC || !readable((void*)(HL_CAM_SITE - 13), sizeof(site)) ||
        !match((const u8*)(HL_CAM_SITE - 13), site, sizeof(site)) ||
        !hl_text_refs(HL_PREVIEW) || !hl_text_refs(HL_FOCUS) || !hl_text_refs(HL_MGR_PTR) ||
        !readable((void*)HL_FOCUS, 0xB0) || !readable((void*)HL_FOCUS_A, 0x20) || !readable((void*)HL_PREVIEW, 4)) {
        blog("hair styles: live refresh not available for this exe (needs the RLD NBA 2K14 build) - picks apply on the next model load");
        return;
    }
    /* the guard is only reached in the Edit Player head framing (installed at start-up, before anyone is there) */
    b[0] = 0xE9; rel = (int)((u32)&HairCamGuard - (HL_CAM_SITE + 5)); memcpy(b + 1, &rel, 4); b[5] = 0x90;
    if (!patch_bytes(HL_CAM_SITE, b, 6, 1)) { blog("WARNING hair styles: live refresh camera guard could not be written - off"); return; }
    g_hlInstalled = 1;
    CreateThread(NULL, 0, hair_live_thread, NULL, 0, NULL);
    blog("HAIR STYLES LIVE REFRESH: on (preview %08X, camera guard %08X -> %08X)", HL_PREVIEW, HL_CAM_SITE, (u32)&HairCamGuard);
}
