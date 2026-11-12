/* ------------------------------------------------------------------------------------------ */
/* 1.11.26-ML: PER-ROSTER SETTINGS                                                             */
/*                                                                                             */
/* Hair Style, Short Length and Wingspan live in ONE file per roster / save file, like         */
/* GotoShotExt and SigExt:  SLAM\BodyHook\rosters\<roster file>.ini  ([Hair] [ShortLength]      */
/* [Wingspan], CFID=value ; name   or   G<roster ID>=value ; First Last). The file of the       */
/* roster the game loads is used; before any roster is loaded (the default roster):            */
/* rosters\(default).ini. Saving the roster under a new name copies the file to that name.     */
/* Which roster: the game's own CreateFileA / ReadFile / CloseHandle / MoveFileA (through       */
/* SLAM's import hooks): a .ROS / .CMG / .FRA read past 256 KB is a load (not the file list).   */
/* Old files (1.11.25 and before: BodyHook_Hair.ini, BodyHook_Shorts.ini, BodyHook_Wingspan.ini, */
/* used for every roster): copied once into the first roster file (.ROS / .CMG / .FRA) loaded   */
/* while no roster file exists yet, then renamed to *.ini.old (kept, never read again).         */
/* ------------------------------------------------------------------------------------------ */
typedef BOOL (WINAPI *RosterRF_t)(HANDLE, LPVOID, DWORD, LPDWORD, LPVOID);
typedef BOOL (WINAPI *RosterCH_t)(HANDLE);
typedef BOOL (WINAPI *RosterMF_t)(LPCSTR, LPCSTR);
static RosterRF_t g_rosterRF; static RosterCH_t g_rosterCH; static RosterMF_t g_rosterMF;
static struct { HANDLE h; u32 bytes; int done; char key[96]; } g_rosterTr[16];
static volatile long g_rosterTrLock;
static int g_rosterOn;   /* (declared in roster_key.inc) */
#define ROSTER_LOAD_BYTES (256u * 1024u)

static void rt_lock(void)   { while (__sync_lock_test_and_set(&g_rosterTrLock, 1)) Sleep(0); }
static void rt_unlock(void) { __sync_lock_release(&g_rosterTrLock); }

static void roster_path(const char *key, char *out)
{
    s_cpy(out, g_modDir, MAX_PATH); s_cat(out, "rosters", MAX_PATH);
    CreateDirectoryA(out, NULL);
    s_cat(out, "\\", MAX_PATH); s_cat(out, key, MAX_PATH); s_cat(out, ".ini", MAX_PATH);
}
static int roster_save_key(const char *path, char *key)   /* "...\Name.ROS" -> "Name.ROS"; 0 if not a roster / save */
{
    int i, b = 0, n = s_len(path); char e[4];
    if (n < 5 || path[n - 4] != '.') return 0;
    for (i = 0; i < 3; i++) { char c = path[n - 3 + i]; e[i] = (char)(c >= 'a' && c <= 'z' ? c - 32 : c); } e[3] = 0;
    if (!((e[0] == 'R' && e[1] == 'O' && e[2] == 'S') || (e[0] == 'C' && e[1] == 'M' && e[2] == 'G') || (e[0] == 'F' && e[1] == 'R' && e[2] == 'A'))) return 0;
    for (i = 0; i < n; i++) if (path[i] == '\\' || path[i] == '/') b = i + 1;
    s_cpy(key, path + b, 96);
    return 1;
}
static int roster_any_file(void)
{
    char pat[MAX_PATH]; WIN32_FIND_DATAA fd; HANDLE h;
    s_cpy(pat, g_modDir, MAX_PATH); s_cat(pat, "rosters\\*.ini", MAX_PATH);
    h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do { if (!s_ieq(fd.cFileName, "(default).ini")) { FindClose(h); return 1; } } while (FindNextFileA(h, &fd));
    FindClose(h);
    return 0;
}
/* the old global files -> this roster file (once) */
static void roster_migrate_old(const char *into)
{
    static const char *const files[3] = { "BodyHook_Hair.ini", "BodyHook_Shorts.ini", "BodyHook_Wingspan.ini" };
    static const char *const secs[3]  = { "Hair", "ShortLength", "Wingspan" };
    static char buf[32768];
    int f, moved = 0;
    for (f = 0; f < 3; f++) {
        char path[MAX_PATH], old[MAX_PATH]; const char *p; DWORD n;
        s_cpy(path, g_modDir, MAX_PATH); s_cat(path, files[f], MAX_PATH);
        if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) continue;
        n = GetPrivateProfileSectionA(secs[f], buf, sizeof(buf), path);
        for (p = buf; n && *p; p += s_len(p) + 1) {
            char k[32], v[160]; int i = 0;
            while (p[i] && p[i] != '=' && i < 31) { k[i] = p[i]; i++; }
            k[i] = 0;
            if (p[i] != '=') continue;
            s_trim(k); s_cpy(v, p + i + 1, 160);
            WritePrivateProfileStringA(secs[f], k, v, into);
            moved++;
        }
        s_cpy(old, path, MAX_PATH); s_cat(old, ".old", MAX_PATH);
        MoveFileExA(path, old, MOVEFILE_REPLACE_EXISTING);
    }
    if (moved) blog("ROSTER  %d saved choice(s) from the old BodyHook_Hair / Shorts / Wingspan.ini moved into %s (old files kept as *.ini.old)", moved, into);
}

/* 2.0 fix: Edit Player was left without accepting -> the remembered values go back (see roster_key.inc) */
static int es_undo(void)
{
    static EsUndo cp[ES_MAX]; int n, i;
    pend_lock(); n = g_nesU; for (i = 0; i < n; i++) cp[i] = g_esU[i]; g_nesU = 0; g_nesW = 0; pend_unlock();
    for (i = 0; i < n; i++) {
        u32 k = cp[i].key;
        if (cp[i].kind == ES_WS) {
            if (g_wsReady) EnterCriticalSection(&g_wsLock);
            ws_set(k, cp[i].oldv);
            if (g_wsReady) LeaveCriticalSection(&g_wsLock);
        } else if (cp[i].kind == ES_SL) {
            EnterCriticalSection(&g_slLock); sl_set(k, cp[i].oldv); sl_live(k, cp[i].oldv); LeaveCriticalSection(&g_slLock);
        } else {
            EnterCriticalSection(&g_hairLock);
            hair_sel_set(k, cp[i].oldtag);
            g_hairCache[0].tick = g_hairCache[1].tick = g_hairCache[2].tick = g_hairCache[3].tick = 0;
            LeaveCriticalSection(&g_hairLock);
            hair_png_entry_update(k, cp[i].oldtag);
            hair_live_queue(k);
        }
    }
    return n;
}
/* 2.0.1 (user 11-12 20:36): the two Edit Player calls are shared with SigExt / GotoShotExt (edithub.h): whichever mod
   starts first hooks them, every mod registers what to do on Accept / open. 2.0 hooked them on its own - then the
   other two could not follow Accept. */
#include "edithub.h"
static void __cdecl es_cb_accept(void)     /* Accept copied the player back */
{
    int k = es_accept();
    if (k) blog("EDIT PLAYER  player accepted: %d Hair Style / Short Length / Wingspan change(s) kept (written when the game saves)", k);
}
static void __cdecl es_cb_open(void)       /* a player is opened: what the last one did not accept goes */
{
    int k = es_undo();
    if (k) blog("EDIT PLAYER  left without accepting: %d Hair Style / Short Length / Wingspan change(s) undone", k);
}
static DWORD WINAPI es_thread(LPVOID unused)    /* Edit Player left (no copy any more) without accepting */
{
    int off = 0;
    (void)unused;
    for (;;) {
        Sleep(200);
        if (!g_nesU || es_on()) { off = 0; continue; }
        if (++off < 2) continue;
        { int k = es_undo(); if (k) blog("EDIT PLAYER  left without accepting: %d Hair Style / Short Length / Wingspan change(s) undone", k); }
        off = 0;
    }
}
static void install_editsession(void)
{
    const char *how = "";
    if (g_is2k12) return;
    if (!eh_join("BodyHook", es_cb_accept, es_cb_open, &how)) {
        blog("WARNING Edit Player Accept not followed (%s) - Hair Style / Short Length / Wingspan changes are kept even when a player is not accepted", how);
        return;
    }
    g_esHooks = 1;
    CreateThread(NULL, 0, es_thread, NULL, 0, NULL);
    blog("EDIT PLAYER  Hair Style / Short Length / Wingspan follow Accept: kept when the player is accepted, undone when he is not (shared with SigExt / GotoShotExt: %s)", how);
}

/* (re)read everything from g_rosterIni */
static void roster_reload(void)
{
    int i;
    EnterCriticalSection(&g_gnLock); g_gnCount = 0; nck_clear(); LeaveCriticalSection(&g_gnLock);
    {   /* hair: previous choices back to the original png first (png row info), then the new roster's */
        static u32 prev[MAX_HAIR_SEL]; int np = 0;
        EnterCriticalSection(&g_hairLock);
        for (i = 0; i < g_hairSelCount; i++) prev[np++] = g_hairSel[i].cfid;
        hair_load_selections();
        g_hairCache[0].tick = g_hairCache[1].tick = g_hairCache[2].tick = g_hairCache[3].tick = 0;
        LeaveCriticalSection(&g_hairLock);
        for (i = 0; i < np; i++) hair_png_entry_update(prev[i], hair_sel_get(prev[i]));
        for (i = 0; i < g_hairSelCount; i++) hair_png_entry_update(g_hairSel[i].cfid, g_hairSel[i].tag);
    }
    EnterCriticalSection(&g_slLock); sl_load(); LeaveCriticalSection(&g_slLock);
    if (g_wsReady || cfg_ws) { EnterCriticalSection(&g_wsLock); ws_load(); LeaveCriticalSection(&g_wsLock); }
}
static void roster_count(int *h, int *s, int *w)
{
    int i; *h = g_hairSelCount; *s = g_slSelCount; *w = 0;
    for (i = 0; i < 65536; i++) { if (g_wsVal[i] != 5) (*w)++; if (g_wsGen[i] != 5) (*w)++; }
}

static void roster_load(const char *key)
{
    int h, s, w;
    char path[MAX_PATH];
    roster_path(key, path);
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES && key[0] != '(' && !roster_any_file()) roster_migrate_old(path);
    es_undo();                  /* 2.0 fix: changes of a player that was not accepted */
    {   int nd = pend_drop();   /* 2.1: not saved by the game -> gone (roster_reload reads the file's values) */
        if (nd) blog("ROSTER  %d Edit Player change(s) not saved by the game - dropped (loading \"%s\")", nd, key); }
    s_cpy(g_rosterKey, key, sizeof(g_rosterKey));
    s_cpy(g_rosterIni, path, MAX_PATH);
    roster_reload();
    roster_count(&h, &s, &w);
    blog("ROSTER  \"%s\" loaded -> rosters\\%s.ini: %d hair style(s), %d short length(s), %d wingspan(s)", key, key, h, s, w);
}
static void roster_saveas(const char *key)
{
    char path[MAX_PATH]; int n;
    { int u = es_undo(); if (u) blog("EDIT PLAYER  %d change(s) of a player that was not accepted undone before the save", u); }   /* 2.0 fix */
    if (s_ieq(key, g_rosterKey)) {          /* 2.1: the game saved this roster -> the Edit Player changes are written now */
        n = pend_commit(g_rosterIni);
        if (n) blog("ROSTER  game saved \"%s\": %d Edit Player change(s) written", key, n);
        return;
    }
    roster_path(key, path);
    if (GetFileAttributesA(g_rosterIni) != INVALID_FILE_ATTRIBUTES) CopyFileA(g_rosterIni, path, FALSE);
    else DeleteFileA(path);
    n = pend_commit(path);                   /* 2.1: plus the changes not saved before */
    blog("ROSTER  saved as \"%s\": rosters\\%s.ini (choices of \"%s\" copied, %d new change(s))", key, key, g_rosterKey, n);
    s_cpy(g_rosterKey, key, sizeof(g_rosterKey));
    s_cpy(g_rosterIni, path, MAX_PATH);
}

/* called by HairCreateFileA (hairstyles.inc) for every file the game opens */
static void roster_track_open(LPCSTR name, DWORD acc, HANDLE h)
{
    char key[96]; int i;
    if (!g_rosterOn || h == INVALID_HANDLE_VALUE || !name || !roster_save_key(name, key)) return;
    if (acc & GENERIC_WRITE) { roster_saveas(key); return; }
    rt_lock();
    for (i = 0; i < 16; i++) if (!g_rosterTr[i].h) { g_rosterTr[i].h = h; g_rosterTr[i].bytes = 0; g_rosterTr[i].done = 0; s_cpy(g_rosterTr[i].key, key, 96); break; }
    rt_unlock();
}
BOOL WINAPI RosterReadFile(HANDLE h, LPVOID b, DWORD n, LPDWORD rd, LPVOID ov)
{
    BOOL r = g_rosterRF(h, b, n, rd, ov); int i, fire = 0; char key[96];
    if (r && h && g_rosterOn) {
        rt_lock();
        for (i = 0; i < 16; i++) if (g_rosterTr[i].h == h) {
            g_rosterTr[i].bytes += rd ? *rd : n;
            if (!g_rosterTr[i].done && g_rosterTr[i].bytes >= ROSTER_LOAD_BYTES) { g_rosterTr[i].done = 1; s_cpy(key, g_rosterTr[i].key, 96); fire = 1; }
            break;
        }
        rt_unlock();
        if (fire) roster_load(key);
    }
    return r;
}
BOOL WINAPI RosterCloseHandle(HANDLE h)
{
    int i;
    if (h && g_rosterOn) { rt_lock(); for (i = 0; i < 16; i++) if (g_rosterTr[i].h == h) { g_rosterTr[i].h = 0; break; } rt_unlock(); }
    return g_rosterCH(h);
}
BOOL WINAPI RosterMoveFileA(LPCSTR a, LPCSTR b)
{
    BOOL r = g_rosterMF(a, b); char key[96];
    if (r && b && g_rosterOn && roster_save_key(b, key)) roster_saveas(key);
    return r;
}

/* before install_hair / install_wingspan / install_shortlen: they read g_rosterIni */
static void roster_init(void)
{
    InitializeCriticalSection(&g_gnLock); g_gnInit = 1;
    roster_path("(default)", g_rosterIni);
}
/* after install_hair (it hooks CreateFileA) */
static void install_roster(void)
{
    if (!g_origCreateFileA) g_origCreateFileA = hook_exe_import("KERNEL32.dll", "CreateFileA", (void*)&HairCreateFileA);
    g_rosterRF = (RosterRF_t)hook_exe_import("KERNEL32.dll", "ReadFile", (void*)&RosterReadFile);
    g_rosterCH = (RosterCH_t)hook_exe_import("KERNEL32.dll", "CloseHandle", (void*)&RosterCloseHandle);
    g_rosterMF = (RosterMF_t)hook_exe_import("KERNEL32.dll", "MoveFileA", (void*)&RosterMoveFileA);
    if (!g_rosterCH) g_rosterCH = (RosterCH_t)GetProcAddress(GetModuleHandleA("kernel32.dll"), "CloseHandle");
    if (!g_rosterMF) g_rosterMF = (RosterMF_t)GetProcAddress(GetModuleHandleA("kernel32.dll"), "MoveFileA");
    g_rosterOn = g_origCreateFileA && g_rosterRF;
    if (g_rosterOn) blog("ROSTER  per-roster settings on: rosters\\<roster>.ini follows the .ROS / .CMG / .FRA the game loads and saves (now: %s)", g_rosterKey);
    else blog("WARNING per-roster settings: roster files not followed (import hooks missing) - everything stays in rosters\\(default).ini");
}
