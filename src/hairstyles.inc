/* ------------------------------------------------------------------------------------------ */
/* HAIR STYLES (cyberface variants)                                                            */
/*                                                                                             */
/* For a regular (non-created) player, Edit Player > Body & Head shows a "Hair Style" row      */
/* listing  Original  plus every  png<CFID>_<tag>.iff  next to the exe. Picking a tag makes     */
/* the game load png<CFID>_<tag>.iff instead of png<CFID>.iff for that player, everywhere.     */
/* Created players (CAP) keep the stock hair style list.                                       */
/*                                                                                             */
/* How: the Edit Player "Hair Style" menu item (a static item record in .data, found by its    */
/* text hash ABED212B) has three callbacks we replace:                                         */
/*   visibility  (functor vtable slot 1)  stock: visible only for created players              */
/*   value text  (item +0x28)             stock: "{0:HAIR_STYLE}" from record +0x9C            */
/*   left/right  (item +0x34 -> +4/+8)    stock: record +0x9C bits 0..5                        */
/* The file swap is done in the exe's CreateFileA import (the game opens png files by name     */
/* every time it builds a player model).                                                       */
/* ------------------------------------------------------------------------------------------ */
#define HAIR_ITEM_HASH   0xABED212Bu   /* "Hair Style" (Edit Player, Body & Head)          */
#define STRING_FMT_KEY   0x159D2468u   /* loc text "{0:STRING}" (low bit cleared as the game passes it) */
#define MAX_HAIR_VARIANTS 32
#define HAIR_TAG_LEN     48
#define MAX_HAIR_SEL     2048

#define g_hairIni g_rosterIni     /* 1.11.26-ML: SLAM\BodyHook\rosters\<roster>.ini [Hair] */
static CRITICAL_SECTION g_hairLock;

typedef int  (__cdecl    *HairVis_t)(void);
typedef void (__fastcall *HairText_t)(u8 *rec, void *sink, u32 unused);
typedef int  (__fastcall *HairStep_t)(u8 *rec, u32 unused);
typedef void (__fastcall *Fmt_t)(void *sink, u32 key, void *args);

static HairVis_t  g_hairOrigVis = 0;
static HairText_t g_hairOrigText = 0;
static HairStep_t g_hairOrigInc = 0, g_hairOrigDec = 0;
static Fmt_t      g_hairFmt = 0;
static u32        g_hairStrHandler = 0;
static u32        g_hairFmtKey = STRING_FMT_KEY;   /* 2K12: the item's own key ("{0:HAIR_STYLE}"), our handler */
static u32        g_hairEditRec = 0;     /* address of the global that points at the record being edited */
static volatile u32 st_hairSwaps = 0, st_hairChanges = 0;

/* saved choices: cfid -> tag ("" = original) */
static struct { u32 cfid; char tag[HAIR_TAG_LEN]; } g_hairSel[MAX_HAIR_SEL];
static int g_hairSelCount = 0;

/* variant list cache per CFID */
typedef struct { u32 cfid, tick; int count; char tags[MAX_HAIR_VARIANTS][HAIR_TAG_LEN]; } HairList;
static HairList g_hairCache[4];
static u32 g_hairCacheNext = 0;

static void cfid_digits(u32 cfid, char *out)   /* same as the game's png{0:D4} */
{
    char num[12];
    u32_to_dec(cfid, num);
    out[0] = 0;
    if (cfid < 1000) s_cat(out, cfid < 10 ? "000" : cfid < 100 ? "00" : "0", 12);
    s_cat(out, num, 12);
}

static const char *hair_sel_get(u32 cfid)
{
    int i;
    for (i = 0; i < g_hairSelCount; i++) if (g_hairSel[i].cfid == cfid) return g_hairSel[i].tag;
    return "";
}

static void hair_sel_set(u32 cfid, const char *tag)
{
    int i;
    for (i = 0; i < g_hairSelCount; i++) if (g_hairSel[i].cfid == cfid) break;
    if (i == g_hairSelCount) {
        if (g_hairSelCount >= MAX_HAIR_SEL) return;
        g_hairSelCount++;
        g_hairSel[i].cfid = cfid;
    }
    s_cpy(g_hairSel[i].tag, tag, HAIR_TAG_LEN);
}

/* list png<digits>_*.iff (cached for 2 s per CFID) */
static HairList *hair_list(u32 cfid)
{
    u32 now = GetTickCount(), i;
    HairList *L = 0;
    WIN32_FIND_DATAA fd;
    HANDLE h;
    char pat[MAX_PATH], dig[12];
    int plen;
    for (i = 0; i < 4; i++)
        if (g_hairCache[i].cfid == cfid && now - g_hairCache[i].tick < 2000) return &g_hairCache[i];
    for (i = 0; i < 4; i++) if (g_hairCache[i].cfid == cfid) { L = &g_hairCache[i]; break; }
    if (!L) L = &g_hairCache[g_hairCacheNext++ & 3];
    L->cfid = cfid; L->tick = now; L->count = 0;
    cfid_digits(cfid, dig);
    s_cpy(pat, g_dir, MAX_PATH); s_cat(pat, "png", MAX_PATH); s_cat(pat, dig, MAX_PATH); s_cat(pat, "_*.iff", MAX_PATH);
    plen = 3 + s_len(dig) + 1;                  /* "png" + digits + "_" */
    h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return L;
    do {
        const char *n = fd.cFileName;
        int L2 = s_len(n), k, taglen = L2 - plen - 4, j;
        char tag[HAIR_TAG_LEN];
        if (taglen <= 0 || taglen >= HAIR_TAG_LEN || !s_ieq(n + L2 - 4, ".iff")) continue;
        if (fd.dwFileAttributes & 0x10) continue;      /* directory */
        for (k = 0; k < taglen; k++) tag[k] = n[plen + k];
        tag[taglen] = 0;
        /* keep the list sorted (case-insensitive) so the order never depends on the disk */
        if (L->count >= MAX_HAIR_VARIANTS) break;
        for (j = L->count; j > 0; j--) {
            const char *a = L->tags[j - 1], *b = tag;
            while (*a && up(*a) == up(*b)) { a++; b++; }
            if (up(*a) <= up(*b)) break;
            s_cpy(L->tags[j], L->tags[j - 1], HAIR_TAG_LEN);
        }
        s_cpy(L->tags[j], tag, HAIR_TAG_LEN);
        L->count++;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return L;
}

/* index of the current choice in the list: 0 = original, 1..count = tags */
static int hair_current(u32 cfid, HairList *L)
{
    const char *t = hair_sel_get(cfid);
    int i;
    if (!t[0]) return 0;
    for (i = 0; i < L->count; i++) if (s_ieq(L->tags[i], t)) return i + 1;
    return 0;                                   /* chosen file was removed: original */
}

/* png CFID of the record the menu is editing (the game builds the png name from record +0x3C,
 * 2K12 +0x3A) */
static u32 hair_rec_cfid(u8 *rec)
{
    short v;
    if (!rec || !readable(rec, 0x40)) return 0;
    v = *(short*)(rec + P_offRecCfid);
    return v > 0 ? (u32)v : 0;
}
static u8 *hair_edit_rec(u8 *fromEcx)
{
    u32 p = 0;
    if (g_hairEditRec && read32(g_hairEditRec, &p) && p && readable((void*)p, 0x40)) return (u8*)p;
    return fromEcx;
}
/* the stock visibility answers 0 (shown) for created players */
static int hair_is_created(void) { return g_hairOrigVis ? g_hairOrigVis() == 0 : 1; }

static void hair_png_entry_update(u32 cfid, const char *tag);
static void hair_live_queue(u32 cfid);
static void install_hair_live(void);
static int g_hlInstalled;

/* ---- menu callbacks ---- */
int __cdecl HairVisible(void)
{
    int r = g_hairOrigVis();
    u8 *rec;
    u32 cfid;
    HairList *L;
    if (r == 0) return 0;                      /* created player: stock behaviour */
    rec = hair_edit_rec(0);
    cfid = hair_rec_cfid(rec);
    if (!cfid) return r;
    EnterCriticalSection(&g_hairLock);
    L = hair_list(cfid);
    if (L->count || cfg_hairShowAlways) r = 0;
    LeaveCriticalSection(&g_hairLock);
    return r;
}

typedef int (__cdecl *GameMode_t)(void);
int __cdecl HairMcVisible(void)
{
    if (((GameMode_t)0x0048EF10u)() != 3) return 0;          /* not MyCareer: 004FDC70 answered 0 here too */
    return hair_rec_cfid(hair_edit_rec(0)) ? 0 : 3;
}

void __fastcall HairText(u8 *rec, void *sink, u32 unused)
{
    static u16 wbuf[HAIR_TAG_LEN + 8];
    /* 1.11.24-ML: the game's own string formatter (2K12, type 959F7918) reads the value object at +24h as
     * a pointer to {text, 0}: it read stack garbage there (crash in wcslen, nba2k12 C6D8F0). +24h = itself. */
    struct { const u16 *w; const char *a; u32 pad[7]; void *self; u32 pad2[6]; } sv;
    void *vals[1];
    struct { u32 zero, count; void **values; u32 handler; } args;
    u32 cfid;
    int cur, i;
    const char *txt;
    if (hair_is_created() || !g_hairFmt || !g_hairStrHandler) { g_hairOrigText(rec, sink, unused); return; }
    cfid = hair_rec_cfid(hair_edit_rec(rec));
    EnterCriticalSection(&g_hairLock);
    {
        HairList *L = hair_list(cfid);
        cur = hair_current(cfid, L);
        txt = cur ? L->tags[cur - 1] : cfg_hairOriginal;
        for (i = 0; txt[i] && i < HAIR_TAG_LEN + 6; i++) wbuf[i] = (u8)txt[i];
        wbuf[i] = 0;
    }
    LeaveCriticalSection(&g_hairLock);
    memset(&sv, 0, sizeof(sv));
    sv.w = wbuf; sv.a = 0; sv.self = &sv;
    vals[0] = &sv;
    args.zero = 0; args.count = 1; args.values = vals; args.handler = g_hairStrHandler;
    g_hairFmt(sink, g_hairFmtKey, &args);
}

static int hair_step(u8 *rec, int dir)
{
    u32 cfid = hair_rec_cfid(hair_edit_rec(rec));
    HairList *L;
    int cur, n;
    char tag[HAIR_TAG_LEN], key[12];
    if (!cfid) return 0;
    es_remember(ES_HAIR, cfid);                /* 2.0 fix: undone if Edit Player is left without accepting */
    EnterCriticalSection(&g_hairLock);
    g_hairCache[0].tick = g_hairCache[1].tick = g_hairCache[2].tick = g_hairCache[3].tick = 0;   /* re-read the folder */
    L = hair_list(cfid);
    n = L->count + 1;
    cur = hair_current(cfid, L);
    cur = (cur + dir + n) % n;
    s_cpy(tag, cur ? L->tags[cur - 1] : "", HAIR_TAG_LEN);
    hair_sel_set(cfid, tag);
    LeaveCriticalSection(&g_hairLock);
    u32_to_dec(cfid, key);
    { char v[HAIR_TAG_LEN + 48]; val_named(v, sizeof(v), tag, cfid, hair_edit_rec(rec));
      edit_write("Hair", key, tag[0] ? v : NULL); }   /* 2.1: written when the game saves (2.0 fix: once accepted) */
    hair_png_entry_update(cfid, tag);
    st_hairChanges++;
    blog("HAIR STYLE   CFID %u -> %s  (%s)", cfid, tag[0] ? tag : cfg_hairOriginal,
         g_hlInstalled ? "live refresh" : "applies the next time his model is loaded");
    hair_live_queue(cfid);
    return 1;
}
int __fastcall HairNext(u8 *rec, u32 unused) { return hair_is_created() ? g_hairOrigInc(rec, unused) : hair_step(rec, +1); }
int __fastcall HairPrev(u8 *rec, u32 unused) { return hair_is_created() ? g_hairOrigDec(rec, unused) : hair_step(rec, -1); }

/* 2K12 has no "{0:STRING}" handler like 2K14's, so the item's own format key is used with this
 * handler: the formatter calls it for {0:...} with the value slot in edx, and it appends the wide
 * text through the sink (vtable +18h: ecx = sink, edx = text, push length - same in both games). */
typedef void (__fastcall *SinkW_t)(void *sink, const u16 *w, u32 n);
void __fastcall HairStr12(void *sink, u32 *val, void *spec)
{
    const u16 *w;
    u32 n = 0, vt;
    (void)spec;
    if (!sink || !val || !val[0]) return;
    w = *(const u16**)val[0];
    if (!w) return;
    while (w[n] && n < 256) n++;
    if (!read32((u32)sink, &vt) || !read32(vt + 0x18, &vt) || !in_text(vt)) return;
    ((SinkW_t)vt)(sink, w, n);
}

/* ---- file swap (exe import CreateFileA) ---- */
typedef HANDLE (WINAPI *CreateFileA_t)(LPCSTR, DWORD, DWORD, LPVOID, DWORD, DWORD, HANDLE);
static CreateFileA_t g_origCreateFileA = 0;
static u32 g_hairLogged[64]; static int g_hairLoggedN = 0;

static HANDLE hair_cfa(LPCSTR name, DWORD acc, DWORD share, LPVOID sa, DWORD disp, DWORD flags, HANDLE tmpl)
{
    if (name && g_hairSelCount) {
        int L = s_len(name), b = 0, i, d = 0;
        u32 cfid = 0;
        for (i = 0; i < L; i++) if (name[i] == '\\' || name[i] == '/' || name[i] == ':') b = i + 1;
        if (L - b >= 8 && lo(name[b]) == 'p' && lo(name[b + 1]) == 'n' && lo(name[b + 2]) == 'g') {
            for (i = b + 3; name[i] >= '0' && name[i] <= '9' && d < 6; i++, d++) cfid = cfid * 10 + (u32)(name[i] - '0');
            if (d >= 1 && s_ieq(name + i, ".iff") && cfid <= 0xFFFF) {
                char tag[HAIR_TAG_LEN], path[MAX_PATH];
                EnterCriticalSection(&g_hairLock);
                s_cpy(tag, hair_sel_get(cfid), HAIR_TAG_LEN);
                LeaveCriticalSection(&g_hairLock);
                if (tag[0] && b + d + 3 + 1 + s_len(tag) + 5 < MAX_PATH) {
                    HANDLE h;
                    for (i = 0; i < b + 3 + d; i++) path[i] = name[i];
                    path[i] = 0;
                    s_cat(path, "_", MAX_PATH); s_cat(path, tag, MAX_PATH); s_cat(path, ".iff", MAX_PATH);
                    h = g_origCreateFileA(path, acc, share, sa, disp, flags, tmpl);
                    if (h != INVALID_HANDLE_VALUE) {
                        int k, seen = 0;
                        st_hairSwaps++;
                        for (k = 0; k < g_hairLoggedN; k++) if (g_hairLogged[k] == cfid) seen = 1;
                        if (!seen) {
                            if (g_hairLoggedN < 64) g_hairLogged[g_hairLoggedN++] = cfid;
                            blog("HAIR STYLE   %s -> %s", name + b, path + b);
                        }
                        return h;
                    }
                }
            }
        }
    }
    return g_origCreateFileA(name, acc, share, sa, disp, flags, tmpl);
}
static void roster_track_open(LPCSTR name, DWORD acc, HANDLE h);   /* roster.inc: which roster the game loads / saves */
HANDLE WINAPI HairCreateFileA(LPCSTR name, DWORD acc, DWORD share, LPVOID sa, DWORD disp, DWORD flags, HANDLE tmpl)
{
    HANDLE h = hair_cfa(name, acc, share, sa, disp, flags, tmpl);
    roster_track_open(name, acc, h);
    return h;
}

/* NBA 2K12 opens files with CreateFileW */
typedef HANDLE (WINAPI *CreateFileW_t)(const u16*, DWORD, DWORD, LPVOID, DWORD, DWORD, HANDLE);
static CreateFileW_t g_origCreateFileW = 0;
HANDLE WINAPI HairCreateFileW(const u16 *name, DWORD acc, DWORD share, LPVOID sa, DWORD disp, DWORD flags, HANDLE tmpl)
{
    if (name && g_hairSelCount) {
        int L = 0, b = 0, i, d = 0;
        u32 cfid = 0;
        while (name[L] && L < MAX_PATH) L++;
        for (i = 0; i < L; i++) if (name[i] == '\\' || name[i] == '/' || name[i] == ':') b = i + 1;
        if (L < MAX_PATH && L - b >= 8 && (name[b] | 32) == 'p' && (name[b + 1] | 32) == 'n' && (name[b + 2] | 32) == 'g') {
            for (i = b + 3; name[i] >= '0' && name[i] <= '9' && d < 6; i++, d++) cfid = cfid * 10 + (u32)(name[i] - '0');
            if (d >= 1 && L - i == 4 && name[i] == '.' && (name[i + 1] | 32) == 'i' && (name[i + 2] | 32) == 'f' &&
                (name[i + 3] | 32) == 'f' && cfid <= 0xFFFF) {
                char tag[HAIR_TAG_LEN];
                static u16 path[MAX_PATH + HAIR_TAG_LEN + 8];
                EnterCriticalSection(&g_hairLock);
                s_cpy(tag, hair_sel_get(cfid), HAIR_TAG_LEN);
                if (tag[0] && i + 1 + s_len(tag) + 5 < MAX_PATH) {
                    HANDLE h;
                    int k, n = 0;
                    for (k = 0; k < i; k++) path[n++] = name[k];
                    path[n++] = '_';
                    for (k = 0; tag[k]; k++) path[n++] = (u8)tag[k];
                    path[n++] = '.'; path[n++] = 'i'; path[n++] = 'f'; path[n++] = 'f'; path[n] = 0;
                    h = g_origCreateFileW(path, acc, share, sa, disp, flags, tmpl);
                    LeaveCriticalSection(&g_hairLock);
                    if (h != INVALID_HANDLE_VALUE) {
                        int seen = 0;
                        st_hairSwaps++;
                        for (k = 0; k < g_hairLoggedN; k++) if (g_hairLogged[k] == cfid) seen = 1;
                        if (!seen) {
                            if (g_hairLoggedN < 64) g_hairLogged[g_hairLoggedN++] = cfid;
                            blog("HAIR STYLE   png%u.iff -> png%u_%s.iff", cfid, cfid, tag);
                        }
                        return h;
                    }
                } else LeaveCriticalSection(&g_hairLock);
            }
        }
    }
    return g_origCreateFileW(name, acc, share, sa, disp, flags, tmpl);
}

/* BodyHook's own per-png row info (bodies, uniforms, accessories) follows the chosen file */
static void hair_png_entry_update(u32 cfid, const char *tag)
{
    PngEntry *e;
    char dig[12], file[64];
    if (!cfid || cfid > 0xFFFF || !g_pngIndex[cfid]) return;
    e = &g_pngs[g_pngIndex[cfid] - 1];
    cfid_digits(cfid, dig);
    s_cpy(file, "png", 64); s_cat(file, dig, 64);
    if (tag[0]) { s_cat(file, "_", 64); s_cat(file, tag, 64); }
    s_cat(file, ".iff", 64);
    EnterCriticalSection(&g_logLock);
    if (!s_ieq(e->file, file)) {
        s_cpy(e->file, file, sizeof(e->file));
        e->wlo = e->whi = e->size = 0;
        e->checked = GetTickCount();
        LeaveCriticalSection(&g_logLock);
        png_load(e);                     /* PERF: the file is read without holding the lock */
        nowait_clear();
        return;
    }
    LeaveCriticalSection(&g_logLock);
}

static void hair_load_selections(void)
{
    static char buf[32768];
    char *p;
    DWORD n = GetPrivateProfileSectionA("Hair", buf, sizeof(buf), g_hairIni);
    g_hairSelCount = 0;
    for (p = buf; n && *p; p += s_len(p) + 1) {
        char k[16]; int i = 0, ok = 0;
        const char *v;
        u32 cfid;
        while (p[i] && p[i] != '=' && i < 15) { k[i] = p[i]; i++; }
        k[i] = 0;
        if (p[i] != '=') continue;
        v = p + i + 1;
        s_trim(k);
        cfid = key_parse(k);                   /* decimal CFID (parse_u32 would read it as hex) */
        ok = cfid && !(cfid & GEN_BIT);        /* hair variants are png files: CFID players only */
        if (!ok || !v[0]) continue;
        { char t[HAIR_TAG_LEN]; s_cpy(t, v, HAIR_TAG_LEN); val_split(t, cfid); s_trim(t); if (t[0]) hair_sel_set(cfid, t); }
    }
}

/* ---- install ---- */
static int hair_bytes(u32 a, const char *hex)   /* "56 8B F1 ??" style compare */
{
    const u8 *p = (const u8*)a;
    int i = 0;
    if (!in_image(a, 32) || !readable(p, 32)) return 0;   /* 2.2.1: game code only */
    while (*hex) {
        int hi, lo2;
        if (*hex == ' ') { hex++; continue; }
        if (hex[0] == '?') { hex += 2; i++; continue; }
        hi = hex[0] <= '9' ? hex[0] - '0' : (hex[0] | 32) - 'a' + 10;
        lo2 = hex[1] <= '9' ? hex[1] - '0' : (hex[1] | 32) - 'a' + 10;
        if (p[i] != (u8)(hi * 16 + lo2)) return 0;
        hex += 2; i++;
    }
    return 1;
}

typedef struct { u32 item; } HairFind;
static int hair_item_cb(u32 lo_, u32 hi_, void *ctx)
{
    HairFind *f = (HairFind*)ctx;
    u32 a;
    for (a = ((lo_ + 3) & ~3u) + 4; a + 0x4C <= hi_; a += 4) {
        const u32 *q = (const u32*)a;
        if (q[0] != HAIR_ITEM_HASH) continue;
        /* item record starts 4 bytes before the text hash: type 1, flags +0x10 = 4 */
        if (q[-1] != 1 || q[3] != 4) continue;
        if (!in_text(q[9]) || !q[11] || !q[12]) continue;
        f->item = a - 4;
        return 1;
    }
    return 0;
}

static u32 find_str_handler(void)
{
    /* the game's "{0:STRING}" argument handler:
       cmp [esp+4],0 / mov eax,[edx] / push ebx / push esi / mov esi,[eax+4] / push edi / mov edi,[eax] / mov ebx,ecx */
    static const u8 pat[] = { 0x83,0x7C,0x24,0x04,0x00, 0x8B,0x02, 0x53, 0x56, 0x8B,0x70,0x04, 0x57, 0x8B,0x38, 0x8B,0xD9 };
    u8 *p = find_text(pat, sizeof(pat), 0);
    while (p) {
        /* it compares the tag with crc "STRING" (6E46752F) a little further on */
        if (readable(p, 0x40)) {
            int k;
            for (k = 0x10; k < 0x30; k++)
                if (p[k] == 0x3D && *(u32*)(p + k + 1) == 0x6E46752Fu) return (u32)p;
        }
        p = find_text(pat, sizeof(pat), p + 1);
    }
    return 0;
}

/* the game's own calls of an imported function, through SLAM's shared import hooks (SigExt also follows
   CreateFileA: both are chained). Returns what to call next. */
static CreateFileA_t hook_exe_import(const char *dllName, const char *fn, void *repl)
{
    void *next = 0; int r;
    if (!SLAM_HAS(g_slam, hook_import)) { blog("WARNING this SLAM build has no import hooks - %s!%s not hooked (update SLAM's dinput8.dll)", dllName, fn); return 0; }
    r = g_slam->hook_import(g_slamMe, dllName, fn, repl, &next);
    if (r == SLAM_ERR_CONFLICT) blog("WARNING another mod blocks the import hook %s!%s - see SLAM\\SLAM.log", dllName, fn);
    return r == SLAM_OK ? (CreateFileA_t)next : 0;
}

static void install_hair(void)
{
    HairFind f;
    u32 visObj, visVt, visFn, textFn, stepObj, incFn, decFn, fmtSite, visSlot = 4;
    int n = 0;
    InitializeCriticalSection(&g_hairLock);
    if (!cfg_hair) { blog("hair styles: off (HairStyles Enabled=0)"); return; }
    hair_load_selections();
    {
        int i;
        for (i = 0; i < g_hairSelCount; i++) blog("   hair style: CFID %u -> %s (saved choice)", g_hairSel[i].cfid, g_hairSel[i].tag);
    }

    f.item = 0;
    for_data_pages(hair_item_cb, &f);
    if (!f.item) { blog("WARNING hair styles: Edit Player 'Hair Style' item not found - feature off"); return; }
    textFn  = *(u32*)(f.item + 0x28);
    visObj  = *(u32*)(f.item + 0x30);
    stepObj = *(u32*)(f.item + 0x34);
    visVt   = readable((void*)visObj, 8) ? *(u32*)(visObj + 4) : 0;
    visFn   = visVt && readable((void*)visVt, 12) ? *(u32*)(visVt + 4) : 0;
    /* NBA 2K12: the visibility check is the functor's first slot */
    if (g_is2k12 && visVt && readable((void*)visVt, 8) && hair_bytes(*(u32*)visVt, "8B 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 85 C0 75 ?? A1")) {
        visFn = *(u32*)visVt; visSlot = 0;
    }
    incFn   = readable((void*)stepObj, 12) ? *(u32*)(stepObj + 4) : 0;
    decFn   = readable((void*)stepObj, 12) ? *(u32*)(stepObj + 8) : 0;

    /* stock visibility: mov ecx,[editRec] / call IsCreated / test eax,eax / jne / mov eax,[editRec] */
    if (!in_text(visFn) || !hair_bytes(visFn, "8B 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 85 C0 75 ?? A1")) {
        blog("WARNING hair styles: visibility check at %08X not recognised - feature off", visFn); return;
    }
    g_hairEditRec = *(u32*)(visFn + 2);
    /* stock value text: sub esp,14h / mov eax,edx / mov [esp],ecx ... call Format (E8 at +33h) */
    if (!in_text(textFn) || !hair_bytes(textFn, "83 EC 14 8B C2 89 0C 24")) {
        blog("WARNING hair styles: value text at %08X not recognised - feature off", textFn); return;
    }
    fmtSite = textFn + 0x33;
    if (*(u8*)fmtSite != 0xE8 || !hair_bytes(call_target_of(fmtSite), "8B 44 24 04 89 44 24 04 85 C0")) {
        blog("WARNING hair styles: text formatter not found - feature off"); return;
    }
    g_hairFmt = (Fmt_t)call_target_of(fmtSite);
    /* stock left/right: push esi / mov esi,ecx / mov eax,[esi+9Ch] */
    /* NBA 2K12: push ebx/esi/edi, mov edi|esi,ecx, movzx reg,word [reg+9Eh] (style = +9C bits 16..21) */
    if (!in_text(incFn) || !in_text(decFn) ||
        (!g_is2k12 && (!hair_bytes(incFn, "56 8B F1 8B 86 9C 00 00 00") || !hair_bytes(decFn, "56 8B F1 8B 86 9C 00 00 00"))) ||
        (g_is2k12 && (!hair_bytes(incFn, "53 56 57 8B F9 0F B7 B7 9E 00 00 00") || !hair_bytes(decFn, "53 56 57 8B F1 0F B7 BE 9E 00 00 00")))) {
        blog("WARNING hair styles: left/right handlers not recognised - feature off"); return;
    }
    if (g_is2k12) {
        /* the item's own key "{0:HAIR_STYLE}" (mov edx,imm at +14h) with our handler for {0:...} */
        if (*(u8*)(textFn + 0x14) != 0xBA) { blog("WARNING hair styles: format key not found - feature off"); return; }
        g_hairFmtKey = *(u32*)(textFn + 0x15);
        g_hairStrHandler = (u32)&HairStr12;
    } else g_hairStrHandler = find_str_handler();
    if (!g_hairStrHandler) { blog("WARNING hair styles: string argument handler not found - feature off"); return; }

    g_origCreateFileA = hook_exe_import("KERNEL32.dll", "CreateFileA", (void*)&HairCreateFileA);
    if (g_is2k12) g_origCreateFileW = (CreateFileW_t)hook_exe_import("KERNEL32.dll", "CreateFileW", (void*)&HairCreateFileW);
    if (!g_origCreateFileA && !g_origCreateFileW) { blog("WARNING hair styles: CreateFileA import not found - feature off"); return; }

    g_hairOrigVis = (HairVis_t)visFn;
    g_hairOrigText = (HairText_t)textFn;
    g_hairOrigInc = (HairStep_t)incFn;
    g_hairOrigDec = (HairStep_t)decFn;
    {
        u32 v1 = (u32)&HairVisible, v2 = (u32)&HairText, v3 = (u32)&HairNext, v4 = (u32)&HairPrev;
        n += patch_bytes(visVt + visSlot, &v1, 4, 0);
        /* 1.11.29-ML: MyCareer. The row's first visibility function 004FDC70 hides it in MyCareer (game mode 3,
           0x48EF10); in its place: MyCareer -> shown for a MyPLAYER with a CFID, other modes -> as before (shown). */
        if (!g_is2k12 && visSlot != 0 && *(u32*)visVt == 0x004FDC70u && hair_bytes(0x004FDC70u, "E8 ?? ?? ?? ?? 83 E8 03")) {
            u32 v0 = (u32)&HairMcVisible;
            if (patch_bytes(visVt, &v0, 4, 0)) blog("HAIR STYLES  MyCareer: row shown for a MyPLAYER with a CFID");
        }
        n += patch_bytes(f.item + 0x28, &v2, 4, 0);
        n += patch_bytes(stepObj + 4, &v3, 4, 0);
        n += patch_bytes(stepObj + 8, &v4, 4, 0);
    }
    /* BodyHook's row info follows saved choices */
    {
        int i;
        for (i = 0; i < g_hairSelCount; i++) hair_png_entry_update(g_hairSel[i].cfid, g_hairSel[i].tag);
    }
    blog("HAIR STYLES INSTALLED: item %08X (%d/4 callbacks), edit record [%08X], %d saved choice(s) in rosters\\%s.ini%s",
         f.item, n, g_hairEditRec, g_hairSelCount, g_rosterKey,
         g_is2k12 ? (g_origCreateFileW ? ", file swap CreateFileW + CreateFileA" : ", file swap CreateFileA only") : "");
    if (!g_is2k12) install_hair_live();
}
