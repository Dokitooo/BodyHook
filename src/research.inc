/* ------------------------------------------------------------------------------------------ */
/* RESEARCH BUILD ONLY (build with /DBODYHOOK_RESEARCH): memory dumps and file logging used to */
/* reverse-engineer new features. Nothing here changes the game. ini section [Research].      */
/*   DumpImage=1  BodyHook_image.bin = decrypted exe image, a few seconds after start          */
/*   FileLog=1    log every .iff / .ros / .cmg the game opens (and FindFirstFile patterns)     */
/*   Hotkeys=1    F10 = log recent player records, F11 = BodyHook_memdump_N.bin (whole memory) */
/* ------------------------------------------------------------------------------------------ */
#define MEM_IMAGE_   0x1000000
static int cfg_rsDumpImage = 0, cfg_rsFileLog = 0, cfg_rsHotkeys = 0;
static struct { u32 player, cfid, tick, cf; } g_rsPlayers[16];
static volatile u32 g_rsPlayerNext = 0;

static void rs_note_player(u8 *player)
{
    u32 i, cf = 0;
    if (!cfg_rsHotkeys || !player) return;
    for (i = 0; i < 16; i++) if (g_rsPlayers[i].player == (u32)player) { g_rsPlayers[i].tick = GetTickCount(); return; }
    i = g_rsPlayerNext++ & 15;
    read32((u32)player + cfg_offPlayerCF, &cf);
    g_rsPlayers[i].player = (u32)player; g_rsPlayers[i].cfid = player_cfid(player);
    g_rsPlayers[i].tick = GetTickCount(); g_rsPlayers[i].cf = cf;
}

static HANDLE rs_create(const char *name)
{
    char p[MAX_PATH];
    s_cpy(p, g_dir, MAX_PATH); s_cat(p, name, MAX_PATH);
    return CreateFileA(p, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
}
static void rs_write(HANDLE h, const void *p, u32 n)
{
    DWORD w;
    static u8 zero[4096];
    while (n) {
        u32 k = n > 0x100000 ? 0x100000 : n;
        if (!WriteFile(h, p, k, &w, NULL) || w != k) {
            u32 z = k;
            while (z) { u32 q = z > 4096 ? 4096 : z; WriteFile(h, zero, q, &w, NULL); z -= q; }
        }
        p = (const u8*)p + k; n -= k;
    }
}

static void rs_dump_image(const char *name)
{
    HANDLE h = rs_create(name);
    u32 a = g_imageBase, end = g_imageBase + g_exeImageSize;
    static u8 zero[4096];
    DWORD w;
    if (h == INVALID_HANDLE_VALUE) { blog("RESEARCH could not create %s", name); return; }
    while (a < end) {
        if (readable((void*)a, 4096)) rs_write(h, (void*)a, 4096); else WriteFile(h, zero, 4096, &w, NULL);
        a += 4096;
    }
    CloseHandle(h);
    blog("RESEARCH wrote %s (%u bytes from %08X)", name, g_exeImageSize, g_imageBase);
}

static int page_zero(const u32 *p) { u32 i; for (i = 0; i < 1024; i++) if (p[i]) return 0; return 1; }

/* "BHMEM001", then {u32 base, u32 size, u32 protect, u32 type} + data per run, size 0 = end */
static void rs_dump_memory(const char *name)
{
    HANDLE h = rs_create(name);
    MEMORY_BASIC_INFORMATION mbi;
    u32 a = 0x10000, total = 0, runs = 0;
    u32 rec[4];
    DWORD w;
    if (h == INVALID_HANDLE_VALUE) { blog("RESEARCH could not create %s", name); return; }
    WriteFile(h, "BHMEM001", 8, &w, NULL);
    while (a < 0x7FFF0000u && VirtualQuery((LPCVOID)a, &mbi, sizeof(mbi))) {
        u32 lo = (u32)mbi.BaseAddress, hi = lo + (u32)mbi.RegionSize;
        int ok = mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) && (mbi.Protect & 0xEE);
        if (ok && mbi.Type == MEM_IMAGE_ && !(lo >= g_imageBase && lo < g_imageBase + g_exeImageSize)) ok = 0;
        if (ok) {
            u32 p = lo;
            while (p < hi) {
                u32 s;
                while (p < hi && page_zero((const u32*)p)) p += 4096;
                if (p >= hi) break;
                s = p;
                while (p < hi && !page_zero((const u32*)p)) p += 4096;
                rec[0] = s; rec[1] = p - s; rec[2] = mbi.Protect; rec[3] = mbi.Type;
                WriteFile(h, rec, 16, &w, NULL);
                rs_write(h, (void*)s, p - s);
                total += p - s; runs++;
            }
        }
        if (hi <= a) break;
        a = hi;
    }
    rec[0] = rec[1] = rec[2] = rec[3] = 0;
    WriteFile(h, rec, 16, &w, NULL);
    CloseHandle(h);
    blog("RESEARCH wrote %s: %u runs, %u MB", name, runs, total >> 20);
}

static void rs_log_players(void)
{
    u32 i, now = GetTickCount();
    blog("RESEARCH recent body requests (player object / cyberface object / CFID / seconds ago):");
    for (i = 0; i < 16; i++) {
        u32 p = g_rsPlayers[i].player, off;
        if (!p) continue;
        blog("   player %08X  cf %08X  CFID %u  %u s ago", p, g_rsPlayers[i].cf, g_rsPlayers[i].cfid, (now - g_rsPlayers[i].tick) / 1000);
        if (readable((void*)p, 0x298)) {
            for (off = 0; off < 0x298; off += 32) {
                char line[160], *q = line; u32 k;
                for (k = 0; k < 32 && off + k < 0x298; k++) {
                    u8 b = ((u8*)p)[off + k];
                    *q++ = "0123456789ABCDEF"[b >> 4]; *q++ = "0123456789ABCDEF"[b & 15];
                    if ((k & 3) == 3) *q++ = ' ';
                }
                *q = 0;
                blog("      +%03X %s", off, line);
            }
        }
    }
}

typedef short (WINAPI *GetAsyncKeyState_t)(int);
static DWORD WINAPI rs_key_thread(LPVOID unused)
{
    GetAsyncKeyState_t gks = (GetAsyncKeyState_t)GetProcAddress(LoadLibraryA("user32.dll"), "GetAsyncKeyState");
    int n10 = 0, n11 = 0, prev10 = 0, prev11 = 0;
    (void)unused;
    if (!gks) { blog("RESEARCH hotkeys unavailable"); return 0; }
    blog("RESEARCH hotkeys ready: F10 = log player records, F11 = full memory dump");
    for (;;) {
        int k10 = (gks(0x79) & 0x8000) != 0, k11 = (gks(0x7A) & 0x8000) != 0;
        if (k10 && !prev10) { n10++; blog("RESEARCH ===== F10 snapshot %d =====", n10); rs_log_players(); }
        if (k11 && !prev11) {
            char nm[64], num[12];
            n11++;
            u32_to_dec((u32)n11, num);
            s_cpy(nm, "BodyHook_memdump_", 64); s_cat(nm, num, 64); s_cat(nm, ".bin", 64);
            blog("RESEARCH ===== F11 memory dump %d =====", n11);
            rs_log_players();
            rs_dump_memory(nm);
        }
        prev10 = k10; prev11 = k11;
        Sleep(40);
    }
}

typedef HANDLE (WINAPI *FindFirstFileA_t)(LPCSTR, WIN32_FIND_DATAA*);
static CreateFileA_t g_rsNextCreateFileA = 0;
static FindFirstFileA_t g_rsNextFindFirstFileA = 0;
static volatile u32 g_rsFileLines = 0;

static int rs_interesting(const char *n)
{
    int L = n ? s_len(n) : 0;
    return L > 4 && (s_ieq(n + L - 4, ".iff") || s_ieq(n + L - 4, ".ros") || s_ieq(n + L - 4, ".cmg"));
}
HANDLE WINAPI RsCreateFileA(LPCSTR name, DWORD acc, DWORD share, LPVOID sa, DWORD disp, DWORD flags, HANDLE tmpl)
{
    HANDLE h = g_rsNextCreateFileA(name, acc, share, sa, disp, flags, tmpl);
    if (rs_interesting(name) && g_rsFileLines < 20000) {
        g_rsFileLines++;
        blog("FILE open %s  acc=%08X disp=%u -> %s  (caller %08X, thread %u)", name, acc, disp,
             h == INVALID_HANDLE_VALUE ? "FAIL" : "ok", (u32)__builtin_return_address(0), GetCurrentThreadId());
    }
    return h;
}
HANDLE WINAPI RsFindFirstFileA(LPCSTR pat, WIN32_FIND_DATAA *fd)
{
    HANDLE h = g_rsNextFindFirstFileA(pat, fd);
    if (g_rsFileLines < 20000) { g_rsFileLines++; blog("FILE find %s (caller %08X)", pat, (u32)__builtin_return_address(0)); }
    return h;
}

static void rs_start(void)
{
    cfg_rsDumpImage = ini_int2("Research", "DumpImage", 0);
    cfg_rsFileLog   = ini_int2("Research", "FileLog", 0);
    cfg_rsHotkeys   = ini_int2("Research", "Hotkeys", 0);
    blog("RESEARCH BUILD: DumpImage=%d FileLog=%d Hotkeys=%d", cfg_rsDumpImage, cfg_rsFileLog, cfg_rsHotkeys);
    if (cfg_rsFileLog) {
        /* chains after the hair-style swap, which is already in the import slot */
        g_rsNextCreateFileA = hook_exe_import("KERNEL32.dll", "CreateFileA", (void*)&RsCreateFileA);
        g_rsNextFindFirstFileA = (FindFirstFileA_t)hook_exe_import("KERNEL32.dll", "FindFirstFileA", (void*)&RsFindFirstFileA);
        blog("RESEARCH file logging: CreateFileA %s, FindFirstFileA %s",
             g_rsNextCreateFileA ? "hooked" : "NOT hooked", g_rsNextFindFirstFileA ? "hooked" : "NOT hooked");
    }
    if (cfg_rsDumpImage) rs_dump_image("BodyHook_image.bin");
    if (cfg_rsHotkeys) CreateThread(NULL, 0, rs_key_thread, NULL, 0, NULL);
}
