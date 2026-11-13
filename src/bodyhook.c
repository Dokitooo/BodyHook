/*
 * BodyHook - per-cyberface (pngXXXX.iff) bodies, legs, uniforms and accessories for NBA 2K14 (PC, 32-bit)
 * -----------------------------------------------------------------------------------------------------
 *
 * WHAT THE GAME DOES (from the decrypted nba2k14.exe, see docs/ANALYSIS.md and HOOK_SPEC.md):
 *
 *   Every asset request goes through one resolver:
 *
 *     void* __fastcall ResolveAsset(Manager* mgr        (ecx = 0x020E3760),
 *                                   u32 sectionHash      (edx = 0xBB05A9C1),
 *                                   u32 containerHash,   // one loaded .iff, or 0 = "first container that has it"
 *                                   u32 fileId,          // CRC32 of the asset name, e.g. "hi_body"
 *                                   u32 typeHash,        // CRC32("SCNE"), CRC32("TXTR")
 *                                   u32, u32, u32);      // 0x00D94400 in the RLD build, callee pops 0x18
 *
 *   Bodies, leg skins, uniforms and accessory textures are requested with containerHash = 0, so a row
 *   placed in one player's png leaks onto everybody. BodyHook redirects those requests so each player
 *   gets assets from HIS OWN cyberface png only, otherwise from global.iff ("GLOBAL") or the cloth files.
 *
 *   Accessory normal maps: the game asks for crc32(<texture name> + "_n"), i.e. the texture FileID with
 *   "_n" appended to the CRC (elbowsleeve 04633A86 -> elbowsleeve_n 7D25FD27). BodyHook treats those
 *   exactly like the textures, so a png can replace the texture, the normal map, or both, independently.
 *
 *   Nothing in global.iff, the roster or any png is modified on disk.
 */

#if defined(BODYHOOK_MINIMAL_WINAPI)
#  include "win_min.h"
#else
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <stdarg.h>
#endif

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

#define BODYHOOK_VERSION "2.2"
#include "slam_mod.h"   /* SLAM mod: slam_glue.c has DllMain + SlamModStart, this file starts in mod_start() */

/* ------------------------------------------------------------------------------------------ */
/* no-CRT helpers                                                                             */
/* ------------------------------------------------------------------------------------------ */
#if defined(_MSC_VER) && !defined(__clang__)
#pragma function(memset)
#pragma function(memcpy)
#endif
void *__cdecl memset(void *d, int c, unsigned int n) { u8 *p = (u8*)d; while (n--) *p++ = (u8)c; return d; }
void *__cdecl memcpy(void *d, const void *s, unsigned int n) { u8 *p = (u8*)d; const u8 *q = (const u8*)s; while (n--) *p++ = *q++; return d; }

int _fltused = 0x9875;   /* float support without the CRT */

static int  s_len(const char *s) { int n = 0; while (s[n]) n++; return n; }
static void s_cpy(char *d, const char *s, int max) { int i = 0; for (; s[i] && i < max - 1; i++) d[i] = s[i]; d[i] = 0; }
static void s_cat(char *d, const char *s, int max) { int n = s_len(d); s_cpy(d + n, s, max - n); }
static char up(char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 32) : c; }
static char lo(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }
static int  s_ieq(const char *a, const char *b) { while (*a && *b) { if (up(*a) != up(*b)) return 0; a++; b++; } return *a == *b; }
static int  s_ieq_n(const char *a, const char *b, int n) { while (n--) { if (!*a || up(*a) != up(*b)) return 0; a++; b++; } return 1; }
static void s_trim(char *s)
{
    int n = s_len(s), i = 0, j;
    while (n > 0 && (s[n-1] == ' ' || s[n-1] == '\t' || s[n-1] == '\r' || s[n-1] == '\n')) s[--n] = 0;
    while (s[i] == ' ' || s[i] == '\t') i++;
    if (i) { for (j = 0; s[i + j]; j++) s[j] = s[i + j]; s[j] = 0; }
}

/* ------------------------------------------------------------------------------------------ */
/* CRC32 (zlib polynomial) - how 2K14 turns names into FileIDs                                */
/* ------------------------------------------------------------------------------------------ */
static u32 g_crcTable[256];
static void crc_init(void)
{
    u32 i, j, c;
    for (i = 0; i < 256; i++) {
        c = i;
        for (j = 0; j < 8; j++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        g_crcTable[i] = c;
    }
}
static u32 crc32_str(const char *s)
{
    u32 c = 0xFFFFFFFFu;
    while (*s) c = g_crcTable[(c ^ (u8)*s++) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}
/* crc32(name + suffix) from crc32(name): this is how the game derives "<texture>_n" normal maps */
static u32 crc32_append(u32 crc, const char *suffix)
{
    u32 c = crc ^ 0xFFFFFFFFu;
    while (*suffix) c = g_crcTable[(c ^ (u8)*suffix++) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

/* ------------------------------------------------------------------------------------------ */
/* constants                                                                                  */
/* ------------------------------------------------------------------------------------------ */
#define TYPE_SCNE     0xE26C9B5Du   /* crc32("SCNE")   */
#define TYPE_CLOTHMESH 0xA2449BBBu  /* ClothMesh (moving cloth) */
#define HASH_GLOBAL   0x18FD4C05u   /* crc32("GLOBAL") */
#define TYPE_TXTR     0x5C369069u   /* crc32("TXTR")   */
#define HASH_HIHEAD   0x47F01028u   /* crc32("hihead") - present in every cyberface png */
#define HASH_LEGS     0xFD18438Au   /* crc32("legs") */
#define SEC_CPU       0xBB05A9C1u

static const u32   kBodyIds[4]   = { 0x537BA67Bu, 0x4A610FDAu, 0x7DA47411u, 0xE9B23D51u };
static const char *kBodyNames[4] = { "hi_body_skinny", "hi_body", "hi_body_fat", "hi_body_muscular" };
static const char *kClothDefault[4] = {
    "jersey_u_reg, jersey_u_thin, jersey_u_fat",
    "jersey_v_reg, jersey_v_thin, jersey_v_fat",
    "jersey_wishbone_reg, jersey_wishbone_thin, jersey_wishbone_fat",
    "shortsreg, shortsthin, shortsfat" };
static const char *kClothKeys[4] = { "JerseyU", "JerseyV", "JerseyWishbone", "Shorts" };
static const char *kLegDefault = "legs_00_color, legs_01_color, legs_02_color, legs_03_color, legs_04_color, legs_05_color";
static const char *kAccTexDefault =
    "46A48CA9, 284C68D8, 1FC834CE, 14BFDBB5, 7E289AFF, 6FF4EC60, 6CD24D44, 38731057, 0F6B2638, 7B411638, "
    "04633A86, 6AEF61FE, B05BC4DA, B6BD4971, D646D276, 8E96C7EE, 975511F2, FBB9E735, 841EDB78, D69642A5, "
    "98DF9350, 0E46C401, C74FF2BB, E9E01C61, EBB16072, 5D691AD4, F9667DCC, F488BB8C, E1A4D030, 60FE8AD8, "
    "34A12BB6, 19C1C1B0, F5080DB7, 6430046B, C884A024, 5D507EC5, C8791905, E79D0E40, C452025A, 511CF131, "
    "9EEE8F94, 7B24B4A0, B4D6CA05, 45E26204, BC03E907, 81DA22CA";
/* normal maps that are NOT "<texture>_n": the headband uses unif_headband_normal */
static const char *kAccNrmDefault = "5779DBB0";
static const char *kSockDefault = "C452025A, 511CF131, 9EEE8F94, 7B24B4A0, B4D6CA05, 45E26204, BC03E907";

#define KNOWN_CALLSITE   0x00BF13AFu

/* ------------------------------------------------------------------------------------------ */
/* config                                                                                     */
/* ------------------------------------------------------------------------------------------ */
static char g_dir[MAX_PATH];     /* game folder: the game's png*.iff files */
static char g_modDir[MAX_PATH];  /* SLAM\BodyHook\ (where BodyHook.asi is): BodyHook's own ini / log / cache files */
static char g_ini[MAX_PATH];
static char g_logPath[MAX_PATH];

static int  cfg_enabled        = 1;
static int  cfg_log            = 1;
static int  cfg_logEvery       = 0;
static int  cfg_diagnostics    = 0;
static int  cfg_matchAnyBody   = 1;
static int  cfg_strictGlobal   = 1;
static int  cfg_waitSeconds    = 180;
static int  cfg_fastPath       = 1;
static int  cfg_liveRefresh    = 1;
static u32  cfg_loadWaitMs     = 150;
static u32  cfg_offPlayerCF    = 0x1F0;
static u32  cfg_offCFValid     = 0x10;
static u32  cfg_offCFHash      = 0x4168;
static u32  cfg_fallbackHash   = HASH_GLOBAL;

/* game profile: NBA 2K14 (default) or NBA 2K12 (k12port.inc). Player record layout differences. */
static int  g_is2k12 = 0;
static void *g_self = 0;   /* this dll */
#define BODYHOOK_HAS_2K12 1
static u32  P_offBodyWord = 0x98;    /* bodytype bitfield dword: 2K14 +0x98 bits 25..27, 2K12 +0x9C bits 9..11 */
static int  P_btShl = 4;             /* (int)(v << shl) >> 29 */
static u32  P_offUniform = 0x208;    /* uniform descriptor pointer */
static u32  P_sockBit = 0x8000;       /* uniform +4: sock colour bit (set = black). 2K12 0x4000 */
static u32  P_sockStripedA = 0x2A, P_sockStripedB = 0x32;   /* striped long socks (already team colours). 2K12 0x25/0x2D */
static u32  P_offRecCfid = 0x3C;      /* record: png CFID (short) the game builds png<CFID>.iff from. 2K12 +0x3A */

#define MAX_EXTRA 16
static u32  cfg_extraIds[MAX_EXTRA];
static int  cfg_extraCount = 0;

#define MAX_LEGS 16
static int  cfg_legs = 1;
static int  cfg_matchAnyLeg = 1;
static u32  cfg_legIds[MAX_LEGS];
static char cfg_legNames[MAX_LEGS][32];
static int  cfg_legCount = 0;

#define CLOTH_GROUPS 8
#define CLOTH_PER_GROUP 8
static int  cfg_cloth = 1;
static int  cfg_stockCloth = 1;   /* [Uniforms] StockClothSize (netcloth.inc) */
static int  cfg_perfLite = 0;     /* 2.2 [Performance] Lite */
static int  cfg_matchAnyCloth = 1;
static int  cfg_clothBridge = 1;
static int  cfg_cacheNoPng = 1;
static int  cfg_clothGroups = 0;
static u32  cfg_clothIds[CLOTH_GROUPS][CLOTH_PER_GROUP];
static char cfg_clothNames[CLOTH_GROUPS][CLOTH_PER_GROUP][32];
static int  cfg_clothCount[CLOTH_GROUPS];
static int  cfg_clothIsShorts[CLOTH_GROUPS];

/* accessories */
#define MAX_ACC 64
#define MAX_NRM 128
static int  cfg_acc = 1;
static int  cfg_accCombos = 1;
static int  cfg_keepArmElbow = 1;
static int  cfg_staticGuard = 1;           /* [Uniforms] StaticClothGuard: png static garments never drawn with moving cloth */          /* 2K12: 1 = elbow sleeves keep the arm parts (0-9 / 0-10), 2 = also elbow pads */
static int  cfg_accNormals = 1;
static u32  cfg_accTex[MAX_ACC];
static int  cfg_accTexCount = 0;
static u32  cfg_accNrm[MAX_NRM];            /* normal-map FileIDs; nrm i < accTexCount is "<tex i>_n" */
static int  cfg_accNrmCount = 0;

/* coach polo / arms */
static int  cfg_polo = 0;
static int  cfg_nbaPatchHeadbandOnly = 1;
static int  cfg_benchLive = 1, cfg_benchReplay = 2 /* 0 stock, 1 high-poly, 2 auto */, cfg_benchDiag = 0, cfg_benchUniforms = 1, cfg_benchCoaches = 0;
static int  cfg_poloArms = 0;
static u32  cfg_poloTex = 0xF0752759u;
static u32  cfg_poloNrm = 0xA5FD6D9Fu;

/* team colour socks */
static int  cfg_hair = 1;             /* [HairStyles] */
static int  cfg_hairShowAlways = 0;
static int  cfg_hairLive = 1;           /* [HairStyles] LiveRefresh: Edit Player preview rebuilds on a new pick (2K14) */
static char cfg_hairOriginal[32] = "Original";

static int  cfg_socks = 1;
static int  cfg_sockStyle = 1;
#define MAX_SOCKS 16
static u32  cfg_sockIds[MAX_SOCKS];
static int  cfg_sockCount = 0;
#define MAX_TC_ACC 64
static u32  cfg_tcAccIds[MAX_TC_ACC];      /* [TeamColorSocks] Accessories= other accessory textures */
static int  cfg_tcAccCount = 0;
static int  cfg_tcGrey = 2;                /* grey/white sock masks: 1 = whole sock Team 1, 2 = Team 2 (default), 0 = as is */
static int  cfg_tcDetect = 1;              /* stock red/blue masks are coloured like the red/green ones */
static u32  cfg_tcRB[MAX_TC_ACC], cfg_tcRG[MAX_TC_ACC];   /* forced layouts */
static int  cfg_tcRBCount = 0, cfg_tcRGCount = 0;

#define MAX_OVERRIDES 512
typedef struct { u32 cfid; u32 fileId; int disabled; char name[24]; } Override;
static Override cfg_over[MAX_OVERRIDES];
static int      cfg_overCount = 0;
#define CFID_GAME (-2)                /* read the CFID exactly like the game does (record +0x3C) */
static int      cfg_cfidOffset = CFID_GAME;
static int      g_cfidLegacy = 0;
static u32      cfg_recordSize = 0x298;

/* ------------------------------------------------------------------------------------------ */
/* logging                                                                                    */
/* ------------------------------------------------------------------------------------------ */
static CRITICAL_SECTION g_logLock;
static CRITICAL_SECTION g_clothLock;   /* 2.1: BodyHook's own cloth builds (10-25 ms) no longer hold the data lock (g_logLock) */
static HANDLE g_logFile = INVALID_HANDLE_VALUE;

/* PERF (1.11.13 / 1.19.2): the log is written by its own thread. A hook only copies its line into
 * a 256 KB queue, so no game thread ever waits for the disk (slow / USB / network drives). Before
 * the writer thread runs, after a crash and while the game closes, lines are written directly. */
#define LOGQ_SIZE (256u * 1024u)
static char g_logQ[LOGQ_SIZE];
static u32 g_logQHead = 0, g_logQTail = 0, g_logQDropped = 0;
static CRITICAL_SECTION g_logQLock;
static volatile LONG g_logDirect = 1;
static int g_logQInit = 0;
typedef HANDLE (WINAPI *BhCreateEvent_t)(LPVOID, BOOL, BOOL, LPCSTR);
typedef BOOL (WINAPI *BhSetEvent_t)(HANDLE);
typedef DWORD (WINAPI *BhWait_t)(HANDLE, DWORD);
static BhSetEvent_t g_kSetEvent = 0;
static BhWait_t g_kWait = 0;
static HANDLE g_logEvt = 0;

static void log_write_now(const char *s, u32 n)
{
    DWORD w;
    if (g_logFile != INVALID_HANDLE_VALUE && n) WriteFile(g_logFile, s, n, &w, NULL);
}

/* everything queued -> the file, on the calling thread. g_logWLock keeps the file in order; the
 * game threads only ever take g_logQLock (a memory copy), never g_logWLock, while the queue is on. */
static CRITICAL_SECTION g_logWLock;
static void log_flush(void)
{
    char chunk[2048];
    if (!g_logQInit) return;
    EnterCriticalSection(&g_logWLock);
    for (;;) {
        u32 n, i;
        EnterCriticalSection(&g_logQLock);
        n = g_logQHead - g_logQTail;
        if (n > sizeof(chunk)) n = sizeof(chunk);
        for (i = 0; i < n; i++) chunk[i] = g_logQ[(g_logQTail + i) % LOGQ_SIZE];
        g_logQTail += n;
        LeaveCriticalSection(&g_logQLock);
        if (!n) break;
        log_write_now(chunk, n);
    }
    LeaveCriticalSection(&g_logWLock);
}

static void log_raw(const char *s)
{
    u32 n, i, pend;
    if (!cfg_log || g_logFile == INVALID_HANDLE_VALUE) return;
    n = (u32)s_len(s);
    if (!g_logQInit) { log_write_now(s, n); return; }
    if (g_logDirect) {                                   /* start-up / crash / closing: straight to the file */
        log_flush();
        EnterCriticalSection(&g_logWLock); log_write_now(s, n); LeaveCriticalSection(&g_logWLock);
        return;
    }
    EnterCriticalSection(&g_logQLock);
    if (g_logQHead - g_logQTail + n > LOGQ_SIZE) g_logQDropped++;     /* queue full: drop the line, never wait */
    else {
        for (i = 0; i < n; i++) g_logQ[(g_logQHead + i) % LOGQ_SIZE] = s[i];
        g_logQHead += n;
    }
    pend = g_logQHead - g_logQTail;
    LeaveCriticalSection(&g_logQLock);
    if (pend > LOGQ_SIZE / 4 && g_kSetEvent && g_logEvt) g_kSetEvent(g_logEvt);
}

static DWORD WINAPI log_thread(LPVOID unused)
{
    (void)unused;
    g_logDirect = 0;
    for (;;) {
        if (g_kWait && g_logEvt) g_kWait(g_logEvt, 250); else Sleep(250);
        log_flush();
    }
}

/* DllMain, after the log file was opened */
static void log_start(void)
{
    HMODULE k = GetModuleHandleA("kernel32.dll");
    BhCreateEvent_t ce = (BhCreateEvent_t)GetProcAddress(k, "CreateEventA");
    InitializeCriticalSection(&g_logQLock);
    InitializeCriticalSection(&g_logWLock);
    g_kSetEvent = (BhSetEvent_t)GetProcAddress(k, "SetEvent");
    g_kWait = (BhWait_t)GetProcAddress(k, "WaitForSingleObject");
    g_logEvt = ce ? ce(NULL, FALSE, FALSE, NULL) : 0;
    g_logQInit = 1;
    if (cfg_log && g_logFile != INVALID_HANDLE_VALUE) CreateThread(NULL, 0, log_thread, NULL, 0, NULL);
}

/* crash / game closing: write what is queued and every further line directly */
static void log_go_direct(void)
{
    g_logDirect = 1;
    log_flush();
}

static void blog(const char *fmt, ...)
{
    char body[1024], line[1200];
    SYSTEMTIME t;
    va_list ap;
    if (!cfg_log) return;
    va_start(ap, fmt);
    wvsprintfA(body, fmt, ap);
    va_end(ap);
    GetLocalTime(&t);
    {
        char pre[32]; int i = 0;
        pre[i++] = '[';
        pre[i++] = (char)('0' + t.wHour / 10);   pre[i++] = (char)('0' + t.wHour % 10);   pre[i++] = ':';
        pre[i++] = (char)('0' + t.wMinute / 10); pre[i++] = (char)('0' + t.wMinute % 10); pre[i++] = ':';
        pre[i++] = (char)('0' + t.wSecond / 10); pre[i++] = (char)('0' + t.wSecond % 10); pre[i++] = '.';
        pre[i++] = (char)('0' + t.wMilliseconds / 100);
        pre[i++] = (char)('0' + (t.wMilliseconds / 10) % 10);
        pre[i++] = (char)('0' + t.wMilliseconds % 10);
        pre[i++] = ']'; pre[i++] = ' '; pre[i] = 0;
        s_cpy(line, pre, sizeof(line));
    }
    s_cat(line, body, sizeof(line));
    s_cat(line, "\r\n", sizeof(line));
    log_raw(line);                       /* own queue lock: logging never waits on the data lock */
}

/* ------------------------------------------------------------------------------------------ */
/* per-hook timing (QueryPerformanceCounter, microseconds)                                    */
/* ------------------------------------------------------------------------------------------ */
enum { T_BODY, T_LEGS, T_UNIFORM, T_SIM, T_ACC, T_COMBO, T_BENCH, T_GUARD, T_BUILD, T_COUNT };
static const char *kTimeNames[T_COUNT] = { "body", "legs", "uniform", "cloth-sim", "accessory", "acc-combo", "bench (incl. the game's own bench drawing)",
    "draw guards", "own cloth builds" };   /* 2.1: the draw-time work and BodyHook's own cloth builds are timed too */
static volatile u32 g_tCalls[T_COUNT], g_tUs[T_COUNT];
static u32 g_qpcPerUs = 0;
static u32 g_slowLogged = 0;

static u32 t_now(void)
{
    LARGE_INTEGER q; q.LowPart = 0; q.HighPart = 0;
    QueryPerformanceCounter(&q);
    return q.LowPart;
}
static void t_done(int slot, u32 start)
{
    u32 us;
    if (!g_qpcPerUs) {
        LARGE_INTEGER f; f.LowPart = 0; f.HighPart = 0;
        QueryPerformanceFrequency(&f);
        g_qpcPerUs = f.LowPart >= 1000000u ? f.LowPart / 1000000u : 1;
    }
    us = (t_now() - start) / g_qpcPerUs;
    g_tUs[slot] += us;
    g_tCalls[slot]++;
    if (cfg_log && us >= 3000 && g_slowLogged < 40) {
        g_slowLogged++;
        if (slot >= T_GUARD) blog("SLOW     %s took %u ms", kTimeNames[slot], us / 1000);
        else blog("SLOW     %s hook took %u ms (usually this is the game loading data, not BodyHook)", kTimeNames[slot], us / 1000);
    }
}

/* 2.1 (frame drops / stutter): BodyHook builds its own jersey / shorts cloth objects (bench, own cloth, static guard)
 * INSIDE the draw call - one build is the game's cloth builder, 10-12 ms, two per player. A log (97d4a686) showed three
 * players built in the same frame (~60 ms hitch) and the same players again 0.6-2 s later. Now a build that can wait
 * (the player keeps the cloth the game gave him for a few frames) waits until 14 ms after the last one ended: at most
 * one player per frame. Builds that can't wait (no cloth at all otherwise) are never held back. */
#define CLOTH_BUILD_GAP_US 14000u
static volatile u32 g_buildLastQ = 0;
static volatile u32 st_buildWaits = 0, st_clothRevived = 0;
static int cloth_budget_ok(void)
{
    u32 q = t_now();
    if (!g_qpcPerUs) { LARGE_INTEGER f; f.LowPart = 0; f.HighPart = 0; QueryPerformanceFrequency(&f); g_qpcPerUs = f.LowPart >= 1000000u ? f.LowPart / 1000000u : 1; }
    if (g_buildLastQ && (q - g_buildLastQ) / g_qpcPerUs < CLOTH_BUILD_GAP_US) { st_buildWaits++; return 0; }
    return 1;
}
static void cloth_budget_used(void) { g_buildLastQ = t_now() | 1; }

/* ------------------------------------------------------------------------------------------ */
/* safe memory reads (no SEH / no CRT: check the page first). Small cache of recent queries.  */
/* ------------------------------------------------------------------------------------------ */
#define RQ_CACHE 256    /* 2.2: was 64 searched one by one (and round-robin replaced) - now 256 slots, picked by address */
static u32 g_imgLo = 0, g_imgHi = 0;
static volatile int g_imgFast = 0;    /* set once the game code is unpacked */
static struct { volatile u32 gen, lo, hi, tick; } g_rq[RQ_CACHE];
static volatile u32 g_rqHit = 0, g_rqGen = 0;
static volatile u32 g_rqBusy[RQ_CACHE];
static volatile u32 st_vq = 0;        /* 2.2: VirtualQuery calls (PERF line) */
#define RQ_SLOT(a) ((((a) >> 16) ^ ((a) >> 24)) & (RQ_CACHE - 1))   /* the 64 KB block (Windows' allocation unit) */

/* 2.2 (slow PCs): every check is O(1) - the last hit, then the slot of this 64 KB block. A region that is
 * found stays trusted for 250 ms (as before). A slot is written gen = 0 first and a new, never-0 gen last; a reader
 * only trusts it when gen was the same before and after reading - never half of one entry and half of another. */
static int rq_in(u32 i, u32 a, u32 end, u32 now)
{
    u32 g = g_rq[i].gen, lo, hi, tk;
    if (!g) return 0;
    lo = g_rq[i].lo; hi = g_rq[i].hi; tk = g_rq[i].tick;
    return g_rq[i].gen == g && now - tk < 250 && a >= lo && end <= hi;
}
static int readable(const void *p, u32 n)
{
    MEMORY_BASIC_INFORMATION mbi;
    u32 a = (u32)p, end = a + n, now, i;
    if (a < 0x10000 || end < a) return 0;
    if (g_imgFast && a >= g_imgLo && end <= g_imgHi) return 1;     /* the exe image is always mapped */
    now = GetTickCount();
    i = g_rqHit;                                                     /* PERF 1.11.30-ML: last hit first */
    if (i < RQ_CACHE && rq_in(i, a, end, now)) return 1;
    i = RQ_SLOT(a);
    if (rq_in(i, a, end, now)) { g_rqHit = i; return 1; }
    while (a < end) {
        u32 lo, hi, k;
        st_vq++;
        if (!VirtualQuery((const void*)a, &mbi, sizeof(mbi))) return 0;
        if (mbi.State != MEM_COMMIT) return 0;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
        if (!(mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                             PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) return 0;
        lo = (u32)mbi.BaseAddress; hi = lo + (u32)mbi.RegionSize;
        k = RQ_SLOT(a);
        if (!__sync_val_compare_and_swap(&g_rqBusy[k], 0, 1)) {   /* one writer per slot; busy -> just don't cache */
            u32 ng = __sync_add_and_fetch(&g_rqGen, 1);
            if (!ng) ng = __sync_add_and_fetch(&g_rqGen, 1);
            g_rq[k].gen = 0; g_rq[k].lo = lo; g_rq[k].hi = hi; g_rq[k].tick = now; g_rq[k].gen = ng;
            g_rqHit = k;
            __sync_lock_release(&g_rqBusy[k]);
        }
        a = hi;
    }
    return 1;
}
static int read32(u32 addr, u32 *out)
{
    if (!readable((const void*)addr, 4)) return 0;
    *out = *(volatile u32*)addr;
    return 1;
}

/* ------------------------------------------------------------------------------------------ */
/* names for the log                                                                          */
/* ------------------------------------------------------------------------------------------ */
#define NAME_CACHE 512
typedef struct { u32 hash; u32 cfid; char name[24]; } NameCache;
static NameCache g_names[NAME_CACHE];
static int g_nameCount = 0;

static void u32_to_dec(u32 v, char *out)
{
    char tmp[12]; int i = 0, j = 0;
    do { tmp[i++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (i) out[j++] = tmp[--i];
    out[j] = 0;
}

static void name_set(u32 hash, const char *name, u32 cfid)
{
    int i;
    if (!hash) return;
    for (i = 0; i < g_nameCount; i++) if (g_names[i].hash == hash) { s_cpy(g_names[i].name, name, 24); g_names[i].cfid = cfid; return; }
    if (g_nameCount < NAME_CACHE) {
        g_names[g_nameCount].hash = hash; g_names[g_nameCount].cfid = cfid;
        s_cpy(g_names[g_nameCount].name, name, 24); g_nameCount++;
    }
}
static u32 name_cfid(u32 hash)
{
    int i;
    for (i = 0; i < g_nameCount; i++) if (g_names[i].hash == hash) return g_names[i].cfid;
    return 0;
}

static const char *container_name(u32 hash)
{
    static const char *known[] = { "GLOBAL", "PLAYERCREATE", 0 };
    static char ring[8][24];
    static int ri = 0;
    char *buf;
    int i, k;
    if (!hash) return "<none>";
    for (i = 0; i < g_nameCount; i++) if (g_names[i].hash == hash) return g_names[i].name;
    for (i = 0; known[i]; i++) if (crc32_str(known[i]) == hash) { name_set(hash, known[i], 0); return known[i]; }
    buf = ring[ri++ & 7];
    s_cpy(buf, "#", 24);
    for (k = 7; k >= 0; k--) { char c[2]; c[0] = "0123456789ABCDEF"[(hash >> (k * 4)) & 15]; c[1] = 0; s_cat(buf, c, 24); }
    return buf;
}

static const char *body_name(u32 id)
{
    int i;
    for (i = 0; i < 4; i++) if (kBodyIds[i] == id) return kBodyNames[i];
    return "custom-id";
}

static const char *hex8(u32 v)
{
    static char ring[8][12]; static int ri = 0;
    char *b = ring[ri++ & 7]; int k;
    for (k = 0; k < 8; k++) b[k] = "0123456789ABCDEF"[(v >> ((7 - k) * 4)) & 15];
    b[8] = 0;
    return b;
}

/* ------------------------------------------------------------------------------------------ */
/* INI                                                                                        */
/* ------------------------------------------------------------------------------------------ */
static u32 parse_u32(const char *s, int *ok)
{
    u32 v = 0; int any = 0;
    *ok = 0;
    while (*s == ' ') s++;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    for (; *s; s++) {
        char c = *s; u32 d;
        if (c == ' ') continue;
        if (c >= '0' && c <= '9') d = (u32)(c - '0');
        else if (c >= 'a' && c <= 'f') d = (u32)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = (u32)(c - 'A' + 10);
        else return 0;
        v = (v << 4) | d; any = 1;
    }
    *ok = any;
    return v;
}

/* "4A610FDA", "4A 61 0F DA", "0x4A610FDA" -> that FileID; anything else is an asset name -> crc32(lowercase) */
static u32 parse_fileid(const char *s)
{
    char tmp[128]; int ok, i, hexOnly = 1, digits = 0;
    s_cpy(tmp, s, sizeof(tmp)); s_trim(tmp);
    for (i = 0; tmp[i]; i++) {
        char c = tmp[i];
        if (c == ' ') continue;
        if (i == 1 && (c == 'x' || c == 'X') && tmp[0] == '0') continue;
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) { hexOnly = 0; break; }
        digits++;
    }
    if (hexOnly && digits == 8) { u32 v = parse_u32(tmp, &ok); if (ok) return v; }
    for (i = 0; tmp[i]; i++) tmp[i] = lo(tmp[i]);
    return crc32_str(tmp);
}

/* "a, b; c" -> ids (and optionally names) */
static int parse_list(const char *src, u32 *ids, char (*names)[32], int max)
{
    const char *p = src; int n = 0;
    while (*p && n < max) {
        char item[128]; int k = 0;
        while (*p && *p != ',' && *p != ';' && k < 127) item[k++] = *p++;
        item[k] = 0;
        if (*p) p++;
        s_trim(item);
        if (!item[0]) continue;
        if (names) s_cpy(names[n], item, 32);
        ids[n++] = parse_fileid(item);
    }
    return n;
}

static int ini_int(const char *key, int def) { return (int)GetPrivateProfileIntA("BodyHook", key, def, g_ini); }
static int ini_int2(const char *sec, const char *key, int def) { return (int)GetPrivateProfileIntA(sec, key, def, g_ini); }
static u32 ini_hex(const char *sec, const char *key, u32 def)
{
    char b[64]; int ok; u32 v;
    GetPrivateProfileStringA(sec, key, "", b, sizeof(b), g_ini);
    s_trim(b);
    if (!b[0]) return def;
    v = parse_u32(b, &ok);
    return ok ? v : def;
}
static u32 ini_fileid(const char *sec, const char *key, u32 def)
{
    char b[128];
    GetPrivateProfileStringA(sec, key, "", b, sizeof(b), g_ini);
    s_trim(b);
    return b[0] ? parse_fileid(b) : def;
}

static void load_config(void)
{
    static char buf[4096];
    static char sec[32768];
    int i;

    cfg_enabled      = ini_int("Enabled", 1);
    cfg_log          = ini_int("Log", 1);
    cfg_logEvery     = ini_int("LogEveryLookup", 0);
    cfg_diagnostics  = ini_int("Diagnostics", 0);
    cfg_matchAnyBody = ini_int("MatchAnyBodyID", 1);
    cfg_strictGlobal = ini_int("StrictGlobalFallback", 1);
    cfg_waitSeconds  = ini_int("WaitForGameSeconds", 180);
    cfg_fastPath     = ini_int("FastPngCheck", 1);
    cfg_liveRefresh  = ini_int("LivePngRefresh", 1);
    cfg_loadWaitMs   = (u32)ini_int("PngLoadWaitMs", 150);
    if (cfg_loadWaitMs > 2000) cfg_loadWaitMs = 2000;

    GetPrivateProfileStringA("BodyHook", "FallbackContainer", "GLOBAL", buf, sizeof(buf), g_ini);
    s_trim(buf);
    for (i = 0; buf[i]; i++) buf[i] = up(buf[i]);
    cfg_fallbackHash = buf[0] ? crc32_str(buf) : HASH_GLOBAL;

    cfg_offPlayerCF = ini_hex("Advanced", "PlayerCyberfaceOffset", 0x1F0);
    cfg_offCFValid  = ini_hex("Advanced", "CyberfaceValidOffset", 0x10);
    cfg_offCFHash   = ini_hex("Advanced", "CyberfaceHashOffset", 0x4168);
    cfg_recordSize  = ini_hex("Advanced", "PlayerRecordSize", 0x298);
    if (cfg_recordSize < 0x40 || cfg_recordSize > 0x2000) cfg_recordSize = 0x298;
    {
        char b[32]; int ok; u32 v;
        GetPrivateProfileStringA("Advanced", "PlayerCFIDOffset", "game", b, sizeof(b), g_ini);
        s_trim(b);
        v = parse_u32(b, &ok);
        if (!b[0] || s_ieq(b, "game") || (ok && v == 0x1C)) {
            /* 1.9.3: the CFID is the record's CF_ID (+0x3C), the field the game builds png<cfid>.iff from.
             * +0x1C is PortrID: equal in stock rosters, different for MyCareer players and custom faces.
             * The 0x1C written by older BodyHook.ini files is upgraded to this. */
            cfg_cfidOffset = CFID_GAME;
            g_cfidLegacy = ok && v == 0x1C;
        } else
            cfg_cfidOffset = (ok && !s_ieq(b, "auto") && v < cfg_recordSize) ? (int)v : -1;
    }

    GetPrivateProfileStringA("BodyHook", "ExtraBodyIDs", "", buf, sizeof(buf), g_ini);
    cfg_extraCount = parse_list(buf, cfg_extraIds, 0, MAX_EXTRA);

    cfg_legs        = ini_int("LegTattoos", 1);
    cfg_matchAnyLeg = ini_int("MatchAnyLegID", 1);
    GetPrivateProfileStringA("BodyHook", "LegTattooIDs", kLegDefault, buf, sizeof(buf), g_ini);
    s_trim(buf);
    if (!buf[0]) s_cpy(buf, kLegDefault, sizeof(buf));
    cfg_legCount = parse_list(buf, cfg_legIds, cfg_legNames, MAX_LEGS);

    cfg_cloth         = ini_int2("Uniforms", "Enabled", 1);
    cfg_matchAnyCloth = ini_int2("Uniforms", "MatchAnyInGroup", 1);
    cfg_clothBridge   = ini_int2("Uniforms", "DisableClothSimForPngModels", 1);
    cfg_cacheNoPng    = ini_int2("Uniforms", "KeepPngsOutOfSharedCache", 1);
    cfg_clothGroups = 0;
    {
        int gi;
        for (gi = 0; gi < 4; gi++) {
            int g = cfg_clothGroups;
            GetPrivateProfileStringA("Uniforms", kClothKeys[gi], kClothDefault[gi], buf, sizeof(buf), g_ini);
            s_trim(buf);
            if (!buf[0]) continue;
            cfg_clothIsShorts[g] = (gi == 3);
            cfg_clothCount[g] = parse_list(buf, cfg_clothIds[g], cfg_clothNames[g], CLOTH_PER_GROUP);
            if (cfg_clothCount[g]) cfg_clothGroups++;
        }
    }

    /* [Accessories] */
    cfg_acc        = ini_int2("Accessories", "Enabled", 1);
    cfg_accCombos  = ini_int2("Accessories", "AllowCombinations", 1);
    cfg_keepArmElbow = ini_int2("Accessories", "KeepArmUnderElbowSleeve", 1);   /* 1.11.21-ML: on by default (2K12) */
    cfg_staticGuard = ini_int2("Uniforms", "StaticClothGuard", 1);
    cfg_stockCloth = ini_int2("Uniforms", "StockClothSize", 1);
    cfg_accNormals = ini_int2("Accessories", "NormalMaps", 1);
    GetPrivateProfileStringA("Accessories", "TextureIDs", kAccTexDefault, buf, sizeof(buf), g_ini);
    s_trim(buf);
    if (!buf[0]) s_cpy(buf, kAccTexDefault, sizeof(buf));
    cfg_accTexCount = parse_list(buf, cfg_accTex, 0, MAX_ACC);
    cfg_accNrmCount = 0;
    if (cfg_accNormals) {
        for (i = 0; i < cfg_accTexCount; i++) cfg_accNrm[cfg_accNrmCount++] = crc32_append(cfg_accTex[i], "_n");
        GetPrivateProfileStringA("Accessories", "ExtraNormalIDs", kAccNrmDefault, buf, sizeof(buf), g_ini);
        cfg_accNrmCount += parse_list(buf, cfg_accNrm + cfg_accNrmCount, 0, MAX_NRM - cfg_accNrmCount);
    }

    /* [CoachPolo] */
    cfg_polo     = ini_int2("CoachPolo", "Enabled", 0);
    cfg_poloArms = ini_int2("CoachPolo", "ShowArms", 0);
    cfg_nbaPatchHeadbandOnly = ini_int2("NBAPatch", "HeadbandOnly", 1);
    cfg_benchLive = ini_int2("Bench", "HighPolyBench", 1);
    {
        char b[32];
        GetPrivateProfileStringA("Bench", "ReplayBench", "auto", b, sizeof(b), g_ini);
        s_trim(b);
        cfg_benchReplay = s_ieq(b, "highpoly") || s_ieq(b, "1") ? 1 : s_ieq(b, "stock") || s_ieq(b, "0") ? 0 : 2;
    }
    cfg_benchDiag = ini_int2("Bench", "Diagnostics", 0);
    cfg_benchUniforms = ini_int2("Bench", "BenchUniforms", 1);
    cfg_benchCoaches = ini_int2("Bench", "Coaches", 0);
    /* 2.2 [Performance] Lite=1: for weak PCs - the bench keeps the game's own models (no high-poly bench, also in
     * replays) and their uniforms don't move. Players on the floor are not changed. */
    cfg_perfLite = ini_int2("Performance", "Lite", 0);
    if (cfg_perfLite) { cfg_benchLive = 0; cfg_benchReplay = 0; cfg_benchUniforms = 0; }
    cfg_poloTex  = ini_fileid("CoachPolo", "Texture", 0xF0752759u);
    cfg_poloNrm  = ini_fileid("CoachPolo", "NormalMap", 0xA5FD6D9Fu);

    /* [TeamColorSocks] */
    cfg_hair           = ini_int2("HairStyles", "Enabled", 1);
    cfg_hairShowAlways = ini_int2("HairStyles", "ShowWithoutVariants", 0);
    cfg_hairLive       = ini_int2("HairStyles", "LiveRefresh", 1);
    GetPrivateProfileStringA("HairStyles", "OriginalLabel", "Original", buf, sizeof(buf), g_ini);
    s_trim(buf);
    s_cpy(cfg_hairOriginal, buf[0] ? buf : "Original", sizeof(cfg_hairOriginal));

    cfg_socks     = ini_int2("TeamColorSocks", "Enabled", 1);
    cfg_sockStyle = ini_int2("TeamColorSocks", "Style", 1);
    if (cfg_sockStyle != 2) cfg_sockStyle = 1;
    GetPrivateProfileStringA("TeamColorSocks", "Socks", kSockDefault, buf, sizeof(buf), g_ini);
    s_trim(buf);
    if (!buf[0]) s_cpy(buf, kSockDefault, sizeof(buf));
    cfg_sockCount = parse_list(buf, cfg_sockIds, 0, MAX_SOCKS);
    GetPrivateProfileStringA("TeamColorSocks", "Accessories", "", buf, sizeof(buf), g_ini);
    cfg_tcAccCount = parse_list(buf, cfg_tcAccIds, 0, MAX_TC_ACC);
    cfg_tcDetect = ini_int2("TeamColorSocks", "DetectStockMasks", 1);
    /* default: 2K12 (white stock socks) Team 2, 2K14 as before (grey textures used as they are) */
    cfg_tcGrey = ini_int2("TeamColorSocks", "GreyTextures", g_is2k12 ? 2 : 0);
    if (cfg_tcGrey < 0 || cfg_tcGrey > 2) cfg_tcGrey = g_is2k12 ? 2 : 0;
    GetPrivateProfileStringA("TeamColorSocks", "RedBlueTextures", "", buf, sizeof(buf), g_ini);
    cfg_tcRBCount = parse_list(buf, cfg_tcRB, 0, MAX_TC_ACC);
    GetPrivateProfileStringA("TeamColorSocks", "RedGreenTextures", "", buf, sizeof(buf), g_ini);
    cfg_tcRGCount = parse_list(buf, cfg_tcRG, 0, MAX_TC_ACC);

    /* [Players]  1612 = hi_body_custom  |  1013 = off */
    cfg_overCount = 0;
    {
        DWORD n = GetPrivateProfileSectionA("Players", sec, sizeof(sec), g_ini);
        char *q = sec;
        while (n && *q && cfg_overCount < MAX_OVERRIDES) {
            char line[256], key[64], val[160];
            int L = s_len(q), eq = -1;
            s_cpy(line, q, sizeof(line));
            q += L + 1;
            for (i = 0; line[i]; i++) if (line[i] == '=') { eq = i; break; }
            if (eq <= 0 || line[0] == ';' || line[0] == '#') continue;
            line[eq] = 0;
            s_cpy(key, line, sizeof(key)); s_trim(key);
            s_cpy(val, line + eq + 1, sizeof(val)); s_trim(val);
            for (i = 0; val[i]; i++) if (val[i] == ';') { val[i] = 0; break; }
            s_trim(val);
            if (!key[0] || !val[0]) continue;
            {
                u32 cfid = 0; int digits = 0; char *k = key;
                Override *o;
                while (*k && !(*k >= '0' && *k <= '9')) k++;
                while (*k >= '0' && *k <= '9') { cfid = cfid * 10 + (u32)(*k - '0'); k++; digits++; }
                if (!digits) continue;
                o = &cfg_over[cfg_overCount++];
                o->cfid = cfid;
                s_cpy(o->name, "PNG", sizeof(o->name));
                u32_to_dec(cfid, o->name + 3);
                o->disabled = s_ieq(val, "off") || s_ieq(val, "none") || s_ieq(val, "default") || s_ieq(val, "0");
                o->fileId = o->disabled ? 0 : parse_fileid(val);
            }
        }
    }
    if (g_is2k12) {
        /* NBA 2K12 record / cyberface layout (k12port.inc) */
        cfg_offPlayerCF = 0x1DC; cfg_offCFValid = 0x08; cfg_offCFHash = 0x6D8;
        cfg_polo = 0; cfg_poloArms = 0;                 /* coach polo / coach arms: not part of the 2K12 port */
        if (cfg_benchUniforms > 3) cfg_benchUniforms = 1;
        P_offBodyWord = 0x9C; P_btShl = 20; P_offUniform = 0x1F4;
        P_sockBit = 0x4000; P_sockStripedA = 0x25; P_sockStripedB = 0x2D; P_offRecCfid = 0x3A;
    }
}

/* index of an accessory texture (kind 1) or normal map (kind 2), -1 if not ours */
static int acc_index(u32 id, int *kind)
{
    int i;
    for (i = 0; i < cfg_accTexCount; i++) if (cfg_accTex[i] == id) { *kind = 1; return i; }
    for (i = 0; i < cfg_accNrmCount; i++) if (cfg_accNrm[i] == id) { *kind = 2; return i; }
    *kind = 0;
    return -1;
}

/* ------------------------------------------------------------------------------------------ */
/* game interface                                                                              */
/* ------------------------------------------------------------------------------------------ */
typedef void *(__fastcall *ResolveAsset_t)(void *mgr, u32 section, u32 container, u32 fileId,
                                           u32 type, u32 a4, u32 a5, u32 a6);
typedef int (__fastcall *FindInContainer_t)(void *container, u32 fileId, u32 type, void *out);

static ResolveAsset_t g_resolve = 0;
static u32 g_callSite = 0;
static volatile int g_hooked = 0;
static u32 g_mgr = 0x020E3760u;

static volatile u32 st_calls = 0, st_local = 0, st_global = 0, st_orig = 0, st_fail = 0;

/* de-duplicated lookup log */
#define SEEN_MAX 4096                    /* PERF: open-addressing hash set (was a linear scan of 2048) */
typedef struct { u32 player, cont, fileId, result, used; } Seen;
static Seen g_seen[SEEN_MAX];
static int  g_seenCount = 0;

static int seen_before(u32 player, u32 cont, u32 fileId, u32 result)
{
    u32 h = player * 0x9E3779B1u ^ cont * 0x85EBCA6Bu ^ fileId * 0xC2B2AE35u ^ result * 0x27D4EB2Fu, i, n;
    h ^= h >> 15;
    for (i = h & (SEEN_MAX - 1), n = 0; n < SEEN_MAX; n++, i = (i + 1) & (SEEN_MAX - 1)) {
        Seen *e = &g_seen[i];
        if (!e->used) {
            if (g_seenCount < SEEN_MAX * 3 / 4) {
                e->player = player; e->cont = cont; e->fileId = fileId; e->result = result; e->used = 1;
                g_seenCount++;
            }
            return 0;
        }
        if (e->player == player && e->cont == cont && e->fileId == fileId && e->result == result) return 1;
    }
    return 0;
}

/* call FindInContainer on one container node */
static int node_find(u32 node, u32 fileId, u32 type)
{
    u32 vtbl, fn, out = 0;
    if (!read32(node, &vtbl) || !read32(vtbl + 0x0C, &fn) || !fn) return 0;
    return ((FindInContainer_t)fn)((void*)node, fileId, type, &out) != 0;
}

/* the loaded container node with this hash, 0 if none */
static u32 find_node(u32 hash)
{
    u32 node, sentinel = g_mgr + 0x1C, n = 0, h = 0, next;
    if (!read32(g_mgr + 0x28, &node)) return 0;
    while (node && node != sentinel && n++ < 8192) {
        if (!read32(node + 0x28, &h)) return 0;
        if (h == hash) return node;
        if (!read32(node + 0x0C, &next)) return 0;
        node = next;
    }
    return 0;
}

static void dump_containers(void *mgr, const char *why)
{
    u32 m = (u32)mgr, node, sentinel = m + 0x1C, count = 0;
    blog("---- container list (%s) ----", why);
    if (!read32(m + 0x28, &node)) { blog("  cannot read manager at %08X", m); return; }
    while (node && node != sentinel && count < 4096) {
        u32 hash = 0, next = 0;
        char holds[160]; int i;
        holds[0] = 0;
        if (!read32(node + 0x28, &hash)) { blog("  [%u] node %08X unreadable, stop", count, node); break; }
        for (i = 0; i < 4; i++)
            if (node_find(node, kBodyIds[i], TYPE_SCNE)) {
                if (holds[0]) s_cat(holds, ", ", sizeof(holds));
                s_cat(holds, kBodyNames[i], sizeof(holds));
            }
        for (i = 0; i < cfg_extraCount; i++)
            if (node_find(node, cfg_extraIds[i], TYPE_SCNE)) {
                if (holds[0]) s_cat(holds, ", ", sizeof(holds));
                s_cat(holds, "extra-id", sizeof(holds));
            }
        blog("  [%u] %-14s hash=%08X node=%08X  body SCNE: %s", count, container_name(hash), hash, node,
             holds[0] ? holds : "-");
        if (!read32(node + 0x0C, &next)) break;
        node = next;
        count++;
    }
    blog("---- end of container list (%u containers) ----", count);
}

static u32 player_container(u8 *player, u32 *validOut)
{
    u32 cf = 0, valid = 0, hash = 0;
    *validOut = 0;
    if (!player || !read32((u32)player + cfg_offPlayerCF, &cf) || !cf) return 0;
    if (!read32(cf + cfg_offCFValid, &valid)) return 0;
    *validOut = valid;
    if (!valid) return 0;
    if (!read32(cf + cfg_offCFHash, &hash)) return 0;
    return hash;
}

static int player_bodytype(u8 *player)
{
    u32 v = 0;
    if (!player || !read32((u32)player + P_offBodyWord, &v)) return -99;
    return ((int)(v << P_btShl)) >> 29;
}

/* ------------------------------------------------------------------------------------------ */
/* loose pngXXXX.iff files: which custom rows each one has.                                   */
/* Scanned at startup and, with LivePngRefresh=1, re-read whenever the file changes on disk   */
/* (checked at most once a second per png), so rows added while the game runs are picked up.  */
/* ------------------------------------------------------------------------------------------ */
typedef struct {
    u32 cfid;
    u32 bodyMask;           /* bit0-3 standard bodies, bit4 extra id */
    u32 clothMask;          /* bit g*8+k */
    u32 legMask;
    u32 accMask[2];         /* accessory textures (index in cfg_accTex) */
    u32 nrmMask[4];         /* accessory normal maps (index in cfg_accNrm) */
    u32 unsorted;
} PngMasks;

typedef struct {
    PngMasks m;
    u32 wlo, whi, size;     /* last write time + size seen */
    u32 checked;            /* GetTickCount of the last disk check */
    char file[64];          /* file name next to the exe, e.g. png0195.iff (or a hair variant png0195_x.iff) */
    volatile u32 used;      /* GetTickCount of the last lookup by the game (the png watcher's polling fallback) */
} PngEntry;

#define MAX_PNGS 16384
static PngEntry g_pngs[MAX_PNGS];
static int g_pngCount = 0;
static u16 g_pngIndex[65536];          /* cfid -> index+1 */
static volatile int g_scanDone = 0;
static struct { u32 cfid, tick; } g_missing[64];   /* CFIDs recently checked and not found on disk */
static int g_missingNext = 0;
static volatile u32 st_refresh = 0;

static u32 rd32(const u8 *b, u32 o) { return (u32)b[o] | ((u32)b[o+1] << 8) | ((u32)b[o+2] << 16) | ((u32)b[o+3] << 24); }

static int masks_any(const PngMasks *m)
{
    return m->bodyMask || m->clothMask || m->legMask || m->accMask[0] || m->accMask[1] ||
           m->nrmMask[0] || m->nrmMask[1] || m->nrmMask[2] || m->nrmMask[3];
}

/* read the row table of one .iff and fill the masks. returns 0 if the file is not an .iff
 * PERF: reads the first 4 KB (the row table of a png is almost always inside it) and only reads on
 * up to 64 KB when the table goes further. Never called with g_logLock held (disk access). */
static CRITICAL_SECTION g_iffLock;      /* the shared read buffer */
static int iff_masks_buf(const u8 *buf, u32 got, PngMasks *m, u32 *need);
static int iff_masks(const char *path, PngMasks *m)
{
    static u8 buf[0x10000];
    DWORD got = 0, more = 0;
    u32 need = 0;
    int r;
    HANDLE f;
    u32 cf = m->cfid;
    memset(m, 0, sizeof(*m));
    m->cfid = cf;
    f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    EnterCriticalSection(&g_iffLock);
    ReadFile(f, buf, 0x1000, &got, NULL);
    r = iff_masks_buf(buf, got, m, &need);
    if (r < 0 && got == 0x1000 && need > got) {                 /* table goes past 4 KB: read on */
        ReadFile(f, buf + got, sizeof(buf) - got, &more, NULL);
        got += more;
        memset(m, 0, sizeof(*m)); m->cfid = cf;
        r = iff_masks_buf(buf, got, m, &need);
    }
    LeaveCriticalSection(&g_iffLock);
    CloseHandle(f);
    return r > 0;
}

/* 1 = ok, 0 = not an .iff, -1 = needs more than 'got' bytes (*need) */
static int iff_masks_buf(const u8 *buf, u32 got, PngMasks *m, u32 *need)
{
    u32 nfiles, fo, i, j, prev = 0;
    if (got < 0x40 || rd32(buf, 0) != 0xFF3BEF94u) return 0;
    nfiles = rd32(buf, 0x18);
    fo = 0x1C + rd32(buf, 0x1C) - 1;
    if (nfiles > 8192) return 0;
    if (fo + 4 * nfiles > got) { *need = fo + 4 * nfiles + 64; return -1; }
    for (i = 0; i < nfiles; i++) {
        u32 p = fo + 4 * i, eo = p + rd32(buf, p) - 1, fid, typ;
        if (eo + 8 > got) { if (got >= 0x10000) break; *need = eo + 8; return -1; }
        fid = rd32(buf, eo); typ = rd32(buf, eo + 4);
        if (i && fid < prev) m->unsorted = 1;
        prev = fid;
        if (typ == TYPE_TXTR) {
            for (j = 0; j < (u32)cfg_legCount; j++) if (fid == cfg_legIds[j]) m->legMask |= 1u << j;
            for (j = 0; j < (u32)cfg_accTexCount; j++) if (fid == cfg_accTex[j]) m->accMask[j >> 5] |= 1u << (j & 31);
            for (j = 0; j < (u32)cfg_accNrmCount; j++) if (fid == cfg_accNrm[j]) m->nrmMask[j >> 5] |= 1u << (j & 31);
            continue;
        }
        if (typ != TYPE_SCNE) continue;
        {
            int g, k;
            for (g = 0; g < cfg_clothGroups && g < 4; g++)
                for (k = 0; k < cfg_clothCount[g] && k < 8; k++)
                    if (fid == cfg_clothIds[g][k]) m->clothMask |= 1u << (g * 8 + k);
        }
        for (j = 0; j < 4; j++) if (fid == kBodyIds[j]) m->bodyMask |= 1u << j;
        for (j = 0; j < (u32)cfg_extraCount; j++) if (fid == cfg_extraIds[j]) m->bodyMask |= 0x10;
    }
    return 1;
}

static int bits_count(const u32 *w, int n)
{
    int c = 0, i; u32 v;
    for (i = 0; i < n; i++) for (v = w[i]; v; v &= v - 1) c++;
    return c;
}

static void log_png_rows(const char *file, const PngMasks *m, const char *how)
{
    char rows[240]; int g, k, j;
    if (m->unsorted)
        blog("   WARNING %s: its row list is NOT sorted by FileID (usually after adding a row in RED MC). "
             "The game can miss rows in it. Fix: save it once in IFF Mod Studio, or python iff_tool.py sort %s", file, file);
    if (m->bodyMask) {
        rows[0] = 0;
        for (j = 0; j < 4; j++) if (m->bodyMask & (1u << j)) { if (rows[0]) s_cat(rows, ", ", 240); s_cat(rows, kBodyNames[j], 240); }
        if (m->bodyMask & 0x10) { if (rows[0]) s_cat(rows, ", ", 240); s_cat(rows, "extra-id", 240); }
        blog("   %scustom body: %s  (CFID %u)  rows: %s", how, file, m->cfid, rows);
    }
    if (m->clothMask) {
        rows[0] = 0;
        for (g = 0; g < cfg_clothGroups && g < 4; g++)
            for (k = 0; k < cfg_clothCount[g] && k < 8; k++)
                if (m->clothMask & (1u << (g * 8 + k))) { if (rows[0]) s_cat(rows, ", ", 240); s_cat(rows, cfg_clothNames[g][k], 240); }
        blog("   %scustom uniform: %s  (CFID %u)  rows: %s", how, file, m->cfid, rows);
    }
    if (m->legMask) {
        rows[0] = 0;
        for (j = 0; j < cfg_legCount; j++) if (m->legMask & (1u << j)) { if (rows[0]) s_cat(rows, ", ", 240); s_cat(rows, cfg_legNames[j], 240); }
        blog("   %scustom legs: %s  (CFID %u)  rows: %s", how, file, m->cfid, rows);
    }
    if (m->accMask[0] || m->accMask[1] || m->nrmMask[0] || m->nrmMask[1] || m->nrmMask[2] || m->nrmMask[3])
        blog("   %scustom accessories: %s  (CFID %u)  %d texture row(s), %d normal-map row(s)", how, file, m->cfid,
             bits_count(m->accMask, 2), bits_count(m->nrmMask, 4));
}

static PngEntry *png_entry_add(u32 cfid, const char *file)
{
    PngEntry *e;
    if (cfid > 0xFFFF) return 0;
    if (g_pngIndex[cfid]) return &g_pngs[g_pngIndex[cfid] - 1];
    if (g_pngCount >= MAX_PNGS) return 0;
    e = &g_pngs[g_pngCount];
    memset(e, 0, sizeof(*e));
    e->m.cfid = cfid;
    s_cpy(e->file, file, sizeof(e->file));
    g_pngCount++;
    g_pngIndex[cfid] = (u16)g_pngCount;
    return e;
}

/* read one png into its entry; returns 1 if the file was (re)read.
 * PERF: disk access happens without any lock held; only the final copy into the entry is locked.
 * known = size / write time already known (from the folder listing), so no extra file query. */
static int png_load_ex(PngEntry *e, const WIN32_FILE_ATTRIBUTE_DATA *known)
{
    WIN32_FILE_ATTRIBUTE_DATA fa;
    char path[MAX_PATH], file[64];
    PngMasks m;
    s_cpy(file, e->file, sizeof(file));
    s_cpy(path, g_dir, MAX_PATH); s_cat(path, file, MAX_PATH);
    if (known) fa = *known;
    else if (!GetFileAttributesExA(path, 0, &fa)) return 0;
    if ((fa.ftLastWriteTime.dwLowDateTime || fa.ftLastWriteTime.dwHighDateTime) &&
        fa.ftLastWriteTime.dwLowDateTime == e->wlo && fa.ftLastWriteTime.dwHighDateTime == e->whi && fa.nFileSizeLow == e->size)
        return 0;
    m.cfid = e->m.cfid;
    if (!iff_masks(path, &m)) return 0;
    EnterCriticalSection(&g_logLock);
    if (s_ieq(e->file, file)) {                    /* still the same file (a hair change may have switched it) */
        e->m = m;
        e->wlo = fa.ftLastWriteTime.dwLowDateTime; e->whi = fa.ftLastWriteTime.dwHighDateTime; e->size = fa.nFileSizeLow;
    }
    LeaveCriticalSection(&g_logLock);
    return 1;
}
static int png_load(PngEntry *e) { return png_load_ex(e, 0); }

/* a new entry, made visible to the game threads only once its rows are known (read outside the lock).
 * fa = size / write time from a folder listing (0: ask the file system). */
static PngEntry *png_add_loaded(u32 cfid, const char *name, const WIN32_FILE_ATTRIBUTE_DATA *known, const PngMasks *cached)
{
    WIN32_FILE_ATTRIBUTE_DATA fa;
    char path[MAX_PATH];
    PngMasks m;
    PngEntry *e;
    int existed, ok;
    s_cpy(path, g_dir, MAX_PATH); s_cat(path, name, MAX_PATH);
    if (known) fa = *known;
    else if (!GetFileAttributesExA(path, 0, &fa)) memset(&fa, 0, sizeof(fa));
    if (cached) { m = *cached; ok = 1; }
    else { m.cfid = cfid; ok = iff_masks(path, &m); if (!ok) { memset(&m, 0, sizeof(m)); m.cfid = cfid; } }
    EnterCriticalSection(&g_logLock);
    existed = cfid <= 0xFFFF && g_pngIndex[cfid] != 0;
    e = existed ? 0 : png_entry_add(cfid, name);
    if (e) {
        e->m = m;
        if (ok) { e->wlo = fa.ftLastWriteTime.dwLowDateTime; e->whi = fa.ftLastWriteTime.dwHighDateTime; e->size = fa.nFileSizeLow; }
        e->checked = GetTickCount();
    }
    LeaveCriticalSection(&g_logLock);
    return e;
}

/* a png was (re)read: forget "this container already timed out" marks (see nowait_has) */
static void nowait_clear(void);

/* PERF (1.11.13 / 1.19.2): png_lookup never touches the disk. The game threads only read the table;
 * the png watcher thread (png_watch_thread) sees saved / new pngs and updates it. A CFID without an
 * entry is handed to the watcher, which looks for png<cfid>.iff in the background. */
#define WANT_MAX 64
static volatile u32 g_want[WANT_MAX];
static volatile LONG g_wantNext = 0;
static PngEntry *png_lookup(u32 cfid)
{
    PngEntry *e;
    if (!cfid || cfid > 0xFFFF) return 0;
    e = g_pngIndex[cfid] ? &g_pngs[g_pngIndex[cfid] - 1] : 0;
    if (!cfg_liveRefresh) return e;
    if (e) { e->used = GetTickCount() | 1; return e; }
    {
        int i;
        for (i = 0; i < WANT_MAX; i++) if (g_want[i] == cfid) return 0;
        g_want[(u32)(g_wantNext++) & (WANT_MAX - 1)] = cfid;
    }
    return 0;
}

/* the old on-the-spot disk check, now only used by the png watcher's polling fallback */
static PngEntry *png_lookup_disk(u32 cfid)
{
    PngEntry *e;
    u32 now;
    if (!cfid || cfid > 0xFFFF) return 0;
    e = g_pngIndex[cfid] ? &g_pngs[g_pngIndex[cfid] - 1] : 0;
    if (!cfg_liveRefresh) return e;
    now = GetTickCount();
    if (e) {
        if (now - e->checked >= 1000) {          /* watcher thread only: no lock around the disk access */
            e->checked = now;
            if (png_load(e)) {
                st_refresh++;
                blog("PNG CHANGED  %s was saved while the game runs - its custom rows were read again", e->file);
                log_png_rows(e->file, &e->m, "now ");
            }
        }
        return e;
    }
    /* not known: did a new png appear? */
    {
        int i;
        char names[2][24], num[12], path[MAX_PATH];
        for (i = 0; i < 64; i++) if (g_missing[i].cfid == cfid && now - g_missing[i].tick < 3000) return 0;
        u32_to_dec(cfid, num);
        s_cpy(names[0], "png", 24); s_cat(names[0], num, 24); s_cat(names[0], ".iff", 24);
        s_cpy(names[1], "png", 24);
        if (cfid < 1000) s_cat(names[1], cfid < 10 ? "000" : cfid < 100 ? "00" : "0", 24);
        s_cat(names[1], num, 24); s_cat(names[1], ".iff", 24);
        for (i = 0; i < 2; i++) {
            s_cpy(path, g_dir, MAX_PATH); s_cat(path, names[i], MAX_PATH);
            if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) {
                e = png_add_loaded(cfid, names[i], 0, 0);
                if (e) {
                    nowait_clear();
                    blog("PNG ADDED    %s appeared while the game runs", e->file);
                    log_png_rows(e->file, &e->m, "now ");
                }
                return e;
            }
        }
        g_missing[g_missingNext & 63].cfid = cfid;
        g_missing[g_missingNext & 63].tick = now;
        g_missingNext++;
    }
    return 0;
}

static int png_known(u32 cfid) { return cfid && cfid <= 0xFFFF && g_pngIndex[cfid] != 0; }

/* PERF (1.11.13 / 1.19.2): PngLoadWaitMs is the time a hook waits for a png that is still loading.
 * When a png's container did not answer within it, it is remembered for 60 s (or until that png
 * is saved / switched): later lookups for it use the fallback at once instead of waiting again. In
 * Edit Player a hair variant without leg rows made every lookup wait 150 ms - the stutter in the
 * reports. The first lookup still waits, so a png that is really still loading is not missed. */
static struct { u32 key, tick; } g_noWait[64];
static volatile LONG g_noWaitNext = 0;
static int nowait_has(u32 key)
{
    u32 now = GetTickCount(); int i;
    if (!key) return 0;
    for (i = 0; i < 64; i++) if (g_noWait[i].key == key && now - g_noWait[i].tick < 60000) return 1;
    return 0;
}
static void nowait_add(u32 key)
{
    u32 i = (u32)(g_noWaitNext++) & 63;
    if (!key || nowait_has(key)) return;
    g_noWait[i].tick = GetTickCount(); g_noWait[i].key = key;
}
static void nowait_clear(void) { int i; for (i = 0; i < 64; i++) g_noWait[i].key = 0; }
static u32 png_mask(u32 cfid)       { PngEntry *e = png_lookup(cfid); return e ? e->m.bodyMask : 0; }
static u32 png_leg_mask(u32 cfid)   { PngEntry *e = png_lookup(cfid); return e ? e->m.legMask : 0; }

static u32 body_bit(u32 id)
{
    int j;
    for (j = 0; j < 4; j++) if (id == kBodyIds[j]) return 1u << j;
    return 0x10;
}

/* ------------------------------------------------------------------------------------------ */
/* CFID of a player: record +0x3C (CF_ID, what the game loads png<cfid>.iff from).            */
/* ------------------------------------------------------------------------------------------ */
#define MAX_REC_WORDS 0x1000
static u8  g_alive[MAX_REC_WORDS];
static u32 g_seenVals[MAX_REC_WORDS][2];
static u8  g_distinct[MAX_REC_WORDS];
static int g_candInit = 0;
static int g_cfidOff = -1;

static void learn_cfid(u8 *player, u32 usedId)
{
    static u8 now[MAX_REC_WORDS];
    static int announced = 0;
    u32 i, n = cfg_recordSize / 2, keep = 0, bit = body_bit(usedId);
    u16 *w = (u16*)player;
    int before = g_cfidOff, best = -1, bestD = -1;
    if (cfg_cfidOffset != -1 || !g_pngCount || !readable(player, cfg_recordSize)) return;
    for (i = 0; i < n; i++) {
        u32 v = w[i];
        now[i] = (u8)(v && v <= 0xFFFF && g_pngIndex[v] && (g_pngs[g_pngIndex[v] - 1].m.bodyMask & bit));
        if (g_candInit) now[i] = (u8)(now[i] && g_alive[i]);
        keep += now[i];
    }
    if (!keep) return;
    g_candInit = 1;
    for (i = 0; i < n; i++) {
        g_alive[i] = now[i];
        if (!now[i]) continue;
        if (g_distinct[i] == 0) { g_seenVals[i][0] = w[i]; g_distinct[i] = 1; }
        else if (g_distinct[i] == 1 && g_seenVals[i][0] != w[i]) { g_seenVals[i][1] = w[i]; g_distinct[i] = 2; }
        if ((int)g_distinct[i] > bestD) { bestD = g_distinct[i]; best = (int)(i * 2); }
    }
    g_cfidOff = best;
    if (before != g_cfidOff || (bestD >= 2 && !announced)) {
        if (bestD >= 2) announced = 1;
        blog("CFID offset %s: player record +%X", bestD >= 2 ? "learned" : "guessed", (u32)g_cfidOff);
    }
}

static u32 player_cfid(u8 *player)
{
    int off = cfg_cfidOffset >= 0 ? cfg_cfidOffset : g_cfidOff;
    u32 v;
    if (cfg_cfidOffset == CFID_GAME && g_is2k12) {
        /* 2K12 0x00B6E1F0: if (!(rec[0x9C] & 0x100)) name = png{0:D4} of (u16)rec[0x3A]  (0x00C03880) */
        u16 c;
        if (!player || !readable(player + 0x3A, 0x68)) return 0;
        if (player[0x9D] & 1) return 0;
        c = *(u16*)(player + 0x3A);
        return c && c != 0xFFFF ? (u32)c : 0;
    }
    if (cfg_cfidOffset == CFID_GAME) {
        /* 0x00BE4F6F: if (!(rec[0x9B] & 1)) name = png{0:D4} of (short)rec[0x3C]  (0x00CB7800)
         * rec[0x9B] & 1 = created/generic face: no png file is loaded for him */
        short c;
        if (!player || !readable(player + 0x3C, 0x60)) return 0;
        if (player[0x9B] & 1) return 0;
        c = *(short*)(player + 0x3C);
        return c > 0 ? (u32)c : 0;
    }
    if (off < 0 || !player || !readable(player + off, 2)) return 0;
    v = *(u16*)(player + off);
    return v == 0xFFFF ? 0 : v;
}

static const char *cfid_str(u32 cfid)
{
    static char ring[8][16];
    static int ri = 0;
    char *b = ring[ri++ & 7];
    if (!cfid) return "?";
    u32_to_dec(cfid, b);
    return b;
}

/* ------------------------------------------------------------------------------------------ */
/* what custom rows does THIS player's cyberface png have?                                    */
/*  - record CFID of a loose png  -> the scan result of that file (re-read when it changes)   */
/*  - anything else (MyCareer / created players, pngs loaded from elsewhere) -> ask the        */
/*    container the game loaded for him; cached 5 s per container.                            */
/* returns 1 = known (masks filled), 0 = unknown (caller takes the full path)                  */
/* ------------------------------------------------------------------------------------------ */
#define PI_CACHE 128
static struct { u32 hash, tick; PngMasks m; } g_pi[PI_CACHE];
static u32 g_piNext = 0;
static u32 g_noted[64]; static int g_notedCount = 0;

static int player_info(u8 *player, PngMasks *out)
{
    u32 cfid, valid, hash, node, now, i;
    PngEntry *e;
    memset(out, 0, sizeof(*out));
    if (!player) return 0;
    cfid = player_cfid(player);
    e = png_lookup(cfid);
    if (e) { *out = e->m; return 1; }
    hash = player_container(player, &valid);
    if (!hash) return 0;
    now = GetTickCount();
    EnterCriticalSection(&g_logLock);
    for (i = 0; i < PI_CACHE; i++)
        if (g_pi[i].hash == hash && now - g_pi[i].tick < 5000) { *out = g_pi[i].m; LeaveCriticalSection(&g_logLock); return 1; }
    LeaveCriticalSection(&g_logLock);
    node = find_node(hash);
    if (!node || !node_find(node, HASH_HIHEAD, TYPE_SCNE)) return 0;      /* not loaded yet -> unknown */
    out->cfid = cfid;
    for (i = 0; i < 4; i++) if (node_find(node, kBodyIds[i], TYPE_SCNE)) out->bodyMask |= 1u << i;
    for (i = 0; i < (u32)cfg_extraCount; i++) if (node_find(node, cfg_extraIds[i], TYPE_SCNE)) out->bodyMask |= 0x10;
    {
        int g, k;
        for (g = 0; g < cfg_clothGroups && g < 4; g++)
            for (k = 0; k < cfg_clothCount[g] && k < 8; k++)
                if (node_find(node, cfg_clothIds[g][k], TYPE_SCNE)) out->clothMask |= 1u << (g * 8 + k);
    }
    for (i = 0; i < (u32)cfg_legCount; i++) if (node_find(node, cfg_legIds[i], TYPE_TXTR)) out->legMask |= 1u << i;
    for (i = 0; i < (u32)cfg_accTexCount; i++) if (node_find(node, cfg_accTex[i], TYPE_TXTR)) out->accMask[i >> 5] |= 1u << (i & 31);
    for (i = 0; i < (u32)cfg_accNrmCount; i++) if (node_find(node, cfg_accNrm[i], TYPE_TXTR)) out->nrmMask[i >> 5] |= 1u << (i & 31);
    EnterCriticalSection(&g_logLock);
    i = g_piNext++ % PI_CACHE;
    g_pi[i].hash = hash; g_pi[i].tick = now; g_pi[i].m = *out;
    if (masks_any(out)) {
        int k, seen = 0;
        for (k = 0; k < g_notedCount; k++) if (g_noted[k] == hash) seen = 1;
        if (!seen && g_notedCount < 64) {
            g_noted[g_notedCount++] = hash;
            blog("NOTE     player %08X: record CFID %s is not a loose png, but the cyberface the game loaded for him "
                 "(hash %08X) has custom rows (body %X, uniform %X, legs %X, accessories %d, normal maps %d). Using them.",
                 (u32)player, cfid_str(cfid), hash, out->bodyMask, out->clothMask, out->legMask,
                 bits_count(out->accMask, 2), bits_count(out->nrmMask, 4));
        }
    }
    LeaveCriticalSection(&g_logLock);
    return 1;
}

static void log_lookup(const char *what, u8 *player, u32 cont, u32 req, u32 used, u32 from, void *res)
{
    if (!cfg_log) return;
    EnterCriticalSection(&g_logLock);
    if (!cfg_logEvery && seen_before((u32)player, cont, used, (u32)res != 0)) { LeaveCriticalSection(&g_logLock); return; }
    blog("%-8s CFID=%-6s player=%08X bodytype=%d png=%s  requested=%s  used=%s from %s -> %08X",
         what, cfid_str(player_cfid(player)), (u32)player, player_bodytype(player), container_name(cont),
         body_name(req), body_name(used), container_name(from), (u32)res);
    LeaveCriticalSection(&g_logLock);
}

/* ------------------------------------------------------------------------------------------ */
/* coach polo: in every body model, the suit-jacket draw record gets the (hidden) polo's       */
/* geometry. Draw records are 0x40 bytes: {5, startIdx, nIdx, nPrim(=nIdx-2), ?, minVtx, nVtx, */
/* relptr palette, ..., +0x30 tri count, +0x38 material hash}.                                */
/* ------------------------------------------------------------------------------------------ */
#define MAT_JACKET 0xEDE2999Du
#define MAT_POLO   0xF33D9DD4u
/* models already handled: model address, its jacket record, the value the patch wrote at +0x14.
 * If the game frees a model and loads another one at the same address, the check fails and it is redone. */
static struct { u32 model, jacket, val; } g_poloDone[256];
static int g_poloDoneCount = 0, g_poloDoneNext = 0;

static int polo_done(u32 m)
{
    int i;
    for (i = 0; i < g_poloDoneCount; i++)
        if (g_poloDone[i].model == m) {
            if (!g_poloDone[i].jacket) return 1;             /* model without jacket/polo records */
            return readable((void*)g_poloDone[i].jacket, 0x40) && ((u32*)g_poloDone[i].jacket)[0] == 5 &&
                   ((u32*)g_poloDone[i].jacket)[14] == MAT_JACKET && ((u32*)g_poloDone[i].jacket)[5] == g_poloDone[i].val;
        }
    return 0;
}
static void polo_remember(u32 m, u32 jacket, u32 val)
{
    int i;
    for (i = 0; i < g_poloDoneCount; i++) if (g_poloDone[i].model == m) break;
    if (i == g_poloDoneCount) {
        if (g_poloDoneCount < 256) g_poloDoneCount++;
        else i = g_poloDoneNext++ & 255;
    }
    g_poloDone[i].model = m; g_poloDone[i].jacket = jacket; g_poloDone[i].val = val;
}
static volatile u32 st_poloModels = 0;

static void polo_patch_model(void *model)
{
    u32 m = (u32)model, a, okTo = 0, jacket = 0, polo = 0, i;
    if (!m || !cfg_enabled || !cfg_polo) return;
    (void)i;
    EnterCriticalSection(&g_logLock);
    if (polo_done(m)) { LeaveCriticalSection(&g_logLock); return; }
    for (a = m; a < m + 0x300000 && !(jacket && polo); a += 4) {
        const u32 *r = (const u32*)a;
        if (a + 0x40 > okTo) {
            if (!readable((const void*)a, 0x40)) break;
            okTo = (a | 0xFFF) + 1;
        }
        if (r[0] != 5 || !r[2] || r[2] != r[3] + 2 || !r[6]) continue;
        if (r[14] == MAT_JACKET && !jacket) jacket = a;
        else if (r[14] == MAT_POLO && !polo) polo = a;
    }
    if (jacket && polo && ((u32*)jacket)[5] != ((u32*)polo)[5]) {
        DWORD old;
        u32 *J = (u32*)jacket, *P = (u32*)polo;
        if (VirtualProtect(J, 0x40, PAGE_EXECUTE_READWRITE, &old)) {
            u32 rel = P[7];
            J[1] = P[1]; J[2] = P[2]; J[3] = P[3]; J[4] = P[4]; J[5] = P[5]; J[6] = P[6]; J[12] = P[12];
            /* +0x1C is a self-relative pointer (odd value) to the bone palette: keep it pointing at the same table */
            J[7] = (rel & 1) ? rel + ((polo + 0x1C) - (jacket + 0x1C)) : rel;
            VirtualProtect(J, 0x40, old, &old);
            st_poloModels++;
            blog("COACH POLO: model %08X - jacket part now draws the polo", m);
        }
    }
    polo_remember(m, (jacket && polo) ? jacket : 0, (jacket && polo) ? ((u32*)polo)[5] : 0);
    LeaveCriticalSection(&g_logLock);
}

/* ------------------------------------------------------------------------------------------ */
/* coach SUIT JACKET back (CoachPolo Enabled=0): bodies whose jacket draw record already holds  */
/* the polo geometry (body files edited with coach_polo.py, or saved from a game with the polo  */
/* on) get the stock jacket geometry back. Key = the polo record (first index, index count,     */
/* first vertex, vertex count); values = the stock jacket record (+04..+18, +30), read from the */
/* stock global.iff: hi_body / skinny / muscular, fat, bench body 80B3D4D1.                     */
/* ------------------------------------------------------------------------------------------ */
static const u32 kJacket[3][11] = {
    { 0x3C2F, 0x8A2, 0x180B, 0x378,  0x33E9, 0x846, 0x844, 0x0, 0x147B, 0x390, 0x4FD0000 },
    { 0x3C83, 0x870, 0x180B, 0x378,  0x33E9, 0x89A, 0x898, 0x0, 0x147B, 0x390, 0x4FD0000 },
    { 0x91B9, 0x724, 0x3A94, 0x2C8,  0x808B, 0x59D, 0x59B, 0x0, 0x33C5, 0x276, 0x32C0000 } };
static u32 g_jacketDone[64]; static int g_jacketDoneN = 0;
static volatile u32 st_jacketBack = 0;

static void jacket_restore(void *model)
{
    u32 m = (u32)model, a, okTo = 0, jacket = 0, polo = 0;
    int i;
    if (!m || !cfg_enabled || cfg_polo) return;
    EnterCriticalSection(&g_logLock);
    for (i = 0; i < g_jacketDoneN; i++) if (g_jacketDone[i] == m) { LeaveCriticalSection(&g_logLock); return; }
    for (a = m; a < m + 0x300000 && !(jacket && polo); a += 4) {
        const u32 *r = (const u32*)a;
        if (a + 0x40 > okTo) {
            if (!readable((const void*)a, 0x40)) break;
            okTo = (a | 0xFFF) + 1;
        }
        if (r[0] != 5 || !r[2] || r[2] != r[3] + 2 || !r[6]) continue;
        if (r[14] == MAT_JACKET && !jacket) jacket = a;
        else if (r[14] == MAT_POLO && !polo) polo = a;
    }
    if (jacket && polo) {
        u32 *J = (u32*)jacket, *P = (u32*)polo;
        if (J[1] == P[1] && J[2] == P[2] && J[5] == P[5]) {         /* the jacket part draws the polo */
            int k, found = 0;
            for (k = 0; k < 3; k++)
                if (P[1] == kJacket[k][0] && P[2] == kJacket[k][1] && P[5] == kJacket[k][2] && P[6] == kJacket[k][3]) {
                    DWORD old;
                    if (VirtualProtect(J, 0x40, PAGE_EXECUTE_READWRITE, &old)) {
                        J[1] = kJacket[k][4]; J[2] = kJacket[k][5]; J[3] = kJacket[k][6]; J[4] = kJacket[k][7];
                        J[5] = kJacket[k][8]; J[6] = kJacket[k][9]; J[12] = kJacket[k][10];
                        VirtualProtect(J, 0x40, old, &old);
                        st_jacketBack++;
                        found = 1;
                        blog("COACH SUIT: model %08X had the polo in its suit-jacket part (polo-edited body file) - "
                             "stock suit jacket restored (CoachPolo Enabled=0)", m);
                    }
                    break;
                }
            if (!found)
                blog("COACH SUIT: model %08X has the polo in its suit-jacket part, but its layout is not a stock body - "
                     "cannot restore the jacket (use an unedited body, or CoachPolo Enabled=1)", m);
        }
    }
    if (g_jacketDoneN < 64) g_jacketDone[g_jacketDoneN++] = m;
    LeaveCriticalSection(&g_logLock);
}

/* ------------------------------------------------------------------------------------------ */
/* BODY                                                                                        */
/* ------------------------------------------------------------------------------------------ */
static u32 g_missLogged[64]; static int g_missCount = 0;

void *__cdecl BodyLookup(void *mgr, u32 section, u8 *player, u32 retAddr,
                         u32 container, u32 fileId, u32 type, u32 a4, u32 a5, u32 a6)
{
    void *r = 0;
    u32 cf, valid, i, t0 = t_now();
    const Override *ov = 0;
    static volatile int dumped = 0, poloGlobal = 0;
    (void)retAddr;
    st_calls++;

    if (!cfg_enabled || container != 0 || type != TYPE_SCNE) {
        r = g_resolve(mgr, section, container, fileId, type, a4, a5, a6);
        goto done;
    }
    if (!poloGlobal && !g_is2k12) {                 /* the 4 stock bodies + the bench body in global.iff, once */
        void *bb;
        poloGlobal = 1;
        for (i = 0; i < 4; i++) {
            void *g = g_resolve(mgr, section, HASH_GLOBAL, kBodyIds[i], TYPE_SCNE, a4, a5, a6);
            if (cfg_polo) polo_patch_model(g); else jacket_restore(g);
        }
        bb = g_resolve(mgr, section, HASH_GLOBAL, 0x80B3D4D1u, TYPE_SCNE, a4, a5, a6);
        if (cfg_polo) polo_patch_model(bb); else jacket_restore(bb);
    }
    if (cfg_diagnostics && dumped < 2) {
        EnterCriticalSection(&g_logLock);
        if (dumped < 2) { dumped++; dump_containers(mgr, dumped == 1 ? "first body request" : "second body request"); }
        LeaveCriticalSection(&g_logLock);
    }

    cf = player_container(player, &valid);
    if (cf && cfg_fastPath && cfg_overCount == 0) {
        PngMasks pm;
        if (player_info(player, &pm) && !pm.bodyMask) cf = 0;      /* png without a body -> global.iff */
    }

    if (cf) {
        u32 used = 0, cfid, waited = 0, wantBody;
        const char *tag = "LOCAL";
        cfid = player_cfid(player);
        wantBody = png_known(cfid) ? png_mask(cfid) : 0;
        for (;;) {
            r = g_resolve(mgr, section, cf, fileId, type, a4, a5, a6);
            if (r) { used = fileId; tag = "LOCAL"; }
            for (i = 0; !r && i < (u32)cfg_extraCount; i++) {
                r = g_resolve(mgr, section, cf, cfg_extraIds[i], type, a4, a5, a6);
                if (r) { used = cfg_extraIds[i]; tag = "LOCAL+"; }
            }
            for (i = 0; !r && cfg_matchAnyBody && i < 4; i++) {
                if (kBodyIds[i] == fileId) continue;
                r = g_resolve(mgr, section, cf, kBodyIds[i], type, a4, a5, a6);
                if (r) { used = kBodyIds[i]; tag = "LOCAL~"; }
            }
            if (r || !wantBody || waited >= cfg_loadWaitMs || nowait_has(cf)) break;
            /* the png has a body on disk but its container did not answer: it is probably still loading */
            Sleep(25); waited += 25;
            {
                u32 v2, cf2 = player_container(player, &v2);
                if (cf2) cf = cf2;
            }
        }
        if (r && waited) blog("WAITED   %u ms for the png of CFID %u to finish loading", waited, cfid);
        if (!r && wantBody && waited) nowait_add(cf);
        if (!r && wantBody) {
            int k, seen = 0;
            EnterCriticalSection(&g_logLock);
            for (k = 0; k < g_missCount; k++) if (g_missLogged[k] == cfid) seen = 1;
            if (!seen && g_missCount < 64) {
                g_missLogged[g_missCount++] = cfid;
                blog("MISS     CFID=%u: png%u.iff has a body row, but the container the game gave this player (%s, hash %08X) "
                     "did not return it after %u ms", cfid, cfid, container_name(cf), cf, waited);
                dump_containers(mgr, "body MISS");
            }
            LeaveCriticalSection(&g_logLock);
        }
        if (r) {
            EnterCriticalSection(&g_logLock);
            learn_cfid(player, used);
            LeaveCriticalSection(&g_logLock);
        }
        cfid = player_cfid(player);
        if (cfid) {
            char nm[24] = "PNG"; u32_to_dec(cfid, nm + 3);
            EnterCriticalSection(&g_logLock); name_set(cf, nm, cfid); LeaveCriticalSection(&g_logLock);
            for (i = 0; i < (u32)cfg_overCount; i++)
                if (cfg_over[i].cfid == cfid) { ov = &cfg_over[i]; break; }
        }
        if (ov && ov->disabled) {
            if (r) log_lookup("SKIPPED", player, cf, fileId, used, cf, r);
            r = 0;
        } else if (ov) {
            void *r2 = g_resolve(mgr, section, cf, ov->fileId, type, a4, a5, a6);
            if (r2) { st_local++; log_lookup("LOCAL*", player, cf, fileId, ov->fileId, cf, r2); r = r2; goto done_polo; }
            blog("WARNING [Players] %s = %08X but that FileID is not a SCNE in this player's png", ov->name, ov->fileId);
            r = 0;
        } else if (r) {
            st_local++; log_lookup(tag, player, cf, fileId, used, cf, r);
            goto done_polo;
        }
    }

    if (cfg_strictGlobal) {
        r = g_resolve(mgr, section, cfg_fallbackHash, fileId, type, a4, a5, a6);
        if (r) { st_global++; log_lookup("DEFAULT", player, cf, fileId, fileId, cfg_fallbackHash, r); goto done_polo; }
        blog("WARNING %s does not contain %08X(%s) - falling back to the game's own global search",
             container_name(cfg_fallbackHash), fileId, body_name(fileId));
    }
    r = g_resolve(mgr, section, 0, fileId, type, a4, a5, a6);
    if (r) st_orig++; else st_fail++;
    log_lookup(r ? "ORIGINAL" : "FAILED", player, cf, fileId, fileId, 0, r);
    if (!cf && cfg_log && !valid && cfg_diagnostics)
        blog("  note: player %08X had no valid cyberface container at +%X/+%X", (u32)player, cfg_offPlayerCF, cfg_offCFValid);
done_polo:
    if (r && !g_is2k12) { if (cfg_polo) polo_patch_model(r); else jacket_restore(r); }
done:
    t_done(T_BODY, t0);
    return r;
}

/* ------------------------------------------------------------------------------------------ */
/* LEG TATTOOS (asset helper 0x00BE5550, mode 0 = first container that has it)                 */
/* ------------------------------------------------------------------------------------------ */
typedef void *(__fastcall *AssetHelper_t)(void *cyberface, u32 mode, u32 type, u32 fileId);
static AssetHelper_t g_assetHelper = 0;
static volatile u32 st_legCalls = 0, st_legLocal = 0, st_legGlobal = 0, st_legOrig = 0;
static int g_legSites = 0;

static const char *leg_name(u32 id)
{
    int i;
    for (i = 0; i < cfg_legCount; i++) if (cfg_legIds[i] == id) return cfg_legNames[i];
    return hex8(id);
}

static void log_leg(const char *what, u32 cfobj, u32 cont, u32 req, u32 used, const char *from, void *res)
{
    if (!cfg_log) return;
    EnterCriticalSection(&g_logLock);
    if (!cfg_logEvery && seen_before(cfobj ^ 0x80000000u, cont, used, (u32)res != 0)) { LeaveCriticalSection(&g_logLock); return; }
    blog("LEGS %-8s png=%s  requested=%s  used=%s from %s -> %08X",
         what, container_name(cont), leg_name(req), leg_name(used), from, (u32)res);
    LeaveCriticalSection(&g_logLock);
}

void *__fastcall LegLookup(void *cyberface, u32 mode, u32 type, u32 fileId)
{
    void *r;
    u32 valid = 0, cf = 0, i, t0 = t_now(), waited = 0, cfid;
    st_legCalls++;
    if (!cfg_enabled || !cfg_legs || mode != 0 || type != TYPE_TXTR || !cyberface)
        return g_assetHelper(cyberface, mode, type, fileId);

    if (read32((u32)cyberface + cfg_offCFValid, &valid) && valid)
        read32((u32)cyberface + cfg_offCFHash, &cf);
    cfid = cf ? name_cfid(cf) : 0;

    if (cf) {
        u32 want = png_known(cfid) ? png_leg_mask(cfid) : 0;
        for (;;) {
            r = g_assetHelper(cyberface, 3, type, fileId);
            if (r) { st_legLocal++; log_leg("LOCAL", (u32)cyberface, cf, fileId, fileId, container_name(cf), r); goto out; }
            for (i = 0; cfg_matchAnyLeg && i < (u32)cfg_legCount; i++) {
                if (cfg_legIds[i] == fileId) continue;
                r = g_assetHelper(cyberface, 3, type, cfg_legIds[i]);
                if (r) { st_legLocal++; log_leg("LOCAL~", (u32)cyberface, cf, fileId, cfg_legIds[i], container_name(cf), r); goto out; }
            }
            if (!want || waited >= cfg_loadWaitMs || nowait_has(cf ^ 0x08000000u)) break;
            Sleep(25); waited += 25;
        }
        if (want && waited) nowait_add(cf ^ 0x08000000u);
        if (want && cfg_log) {
            EnterCriticalSection(&g_logLock);
            if (!seen_before(cf ^ 0x08000000u, 0, 0, 0))
                blog("MISS     legs: %s has leg rows on disk but did not return them after %u ms", container_name(cf), waited);
            LeaveCriticalSection(&g_logLock);
        }
    }
    if (cfg_strictGlobal) {
        r = g_assetHelper(cyberface, 1, type, fileId);
        if (r) { st_legGlobal++; log_leg("DEFAULT", (u32)cyberface, cf, fileId, fileId, "GLOBAL", r); goto out; }
    }
    r = g_assetHelper(cyberface, 0, type, fileId);
    st_legOrig++;
    log_leg(r ? "ORIGINAL" : "FAILED", (u32)cyberface, cf, fileId, fileId, "<any>", r);
out:
    t_done(T_LEGS, t0);
    return r;
}

/* ------------------------------------------------------------------------------------------ */
/* ACCESSORY TEXTURES + NORMAL MAPS                                                            */
/* The accessory material builder (0x00BE3DD0, ebp+8 = player) asks ResolveAsset(container 0) */
/* for the texture [record+0Ch] and then for crc32(texture name + "_n"), the normal map. Both  */
/* calls come here: own png -> GLOBAL -> the game's original search. The headband (and its    */
/* normal unif_headband_normal) is fetched with the asset helper, mode 0 -> AccHelperLookup.  */
/* ------------------------------------------------------------------------------------------ */
static volatile u32 st_accCalls = 0, st_accLocal = 0, st_nrmLocal = 0, st_accShown = 0;
static int g_accSites = 0, g_accHelperSites = 0;

static void log_acc(const char *what, int kind, u8 *player, u32 cont, u32 fileId, void *res)
{
    if (!cfg_log) return;
    EnterCriticalSection(&g_logLock);
    if (cfg_logEvery || !seen_before((u32)player ^ 0x20000000u, cont, fileId, (u32)res != 0))
        blog("ACC %-6s CFID=%-6s png=%s  %s %08X -> %08X", what, cfid_str(player ? player_cfid(player) : name_cfid(cont)),
             container_name(cont), kind == 2 ? "normal map" : "texture", fileId, (u32)res);
    LeaveCriticalSection(&g_logLock);
}

static void tex_note(u32 fileId, u32 cont, void *res);   /* team colours: which texture this thread uses */
static void *acc_tex_impl(void *mgr, u32 section, u8 *player, u32 container, u32 fileId, u32 type,
                          u32 a4, u32 a5, u32 a6, u32 *used);
void *__cdecl AccTexLookup(void *mgr, u32 section, u8 *player, u32 retAddr,
                           u32 container, u32 fileId, u32 type, u32 a4, u32 a5, u32 a6)
{
    u32 used = container;
    void *r;
    (void)retAddr;
    r = acc_tex_impl(mgr, section, player, container, fileId, type, a4, a5, a6, &used);
    if (type == TYPE_TXTR && section == SEC_CPU) tex_note(fileId, used, r);
    return r;
}
static void *acc_tex_impl(void *mgr, u32 section, u8 *player, u32 container, u32 fileId, u32 type,
                          u32 a4, u32 a5, u32 a6, u32 *used)
{
    void *r;
    int kind, idx, allowed = 1;
    u32 t0 = t_now(), cf, valid;
    if (!cfg_enabled || !cfg_acc || type != TYPE_TXTR || container != 0 ||
        (idx = acc_index(fileId, &kind)) < 0)
        return g_resolve(mgr, section, container, fileId, type, a4, a5, a6);
    st_accCalls++;
    if (cfg_fastPath && g_scanDone) {
        PngMasks pm;
        if (player_info(player, &pm)) {
            const u32 *mask = kind == 1 ? pm.accMask : pm.nrmMask;
            allowed = (mask[idx >> 5] >> (idx & 31)) & 1;
        }
    }
    cf = (player && allowed) ? player_container(player, &valid) : 0;
    if (cf) {
        r = g_resolve(mgr, section, cf, fileId, type, a4, a5, a6);
        if (r) {
            if (kind == 1) st_accLocal++; else st_nrmLocal++;
            log_acc("LOCAL", kind, player, cf, fileId, r);
            *used = cf;
            goto out;
        }
    }
    if (cfg_strictGlobal) {
        r = g_resolve(mgr, section, cfg_fallbackHash, fileId, type, a4, a5, a6);
        if (r) { *used = cfg_fallbackHash; goto out; }
    }
    r = g_resolve(mgr, section, 0, fileId, type, a4, a5, a6);
out:
    t_done(T_ACC, t0);
    return r;
}

void *__fastcall AccHelperLookup(void *cyberface, u32 mode, u32 type, u32 fileId)
{
    void *r;
    int kind;
    u32 cf = 0, valid = 0;
    if (!cfg_enabled || !cfg_acc || mode != 0 || type != TYPE_TXTR || !cyberface || acc_index(fileId, &kind) < 0)
        return g_assetHelper(cyberface, mode, type, fileId);
    st_accCalls++;
    r = g_assetHelper(cyberface, 3, type, fileId);
    if (r) {
        if (read32((u32)cyberface + cfg_offCFValid, &valid) && valid) read32((u32)cyberface + cfg_offCFHash, &cf);
        if (kind == 1) st_accLocal++; else st_nrmLocal++;
        log_acc("LOCAL", kind, 0, cf, fileId, r);
        return r;
    }
    r = g_assetHelper(cyberface, 1, type, fileId);
    if (r) return r;
    return g_assetHelper(cyberface, 0, type, fileId);
}

/* ------------------------------------------------------------------------------------------ */
/* ACCESSORY COMBINATIONS                                                                      */
/* 106 accessory records (table 0x013A7800, 0x24 bytes). vtbl[2] = IsVisible(record, player). */
/* Stock hides an accessory when another slot is equipped. If the record's own slot(s) are    */
/* set, the question is asked again on a copy of the player with the other slots cleared.     */
/* If the game picked a different version of the SAME accessory (long -> short sleeves), the  */
/* game's choice stays so two versions are never drawn.                                       */
/* ------------------------------------------------------------------------------------------ */
typedef int (__fastcall *IsVisible_t)(void *record, void *player);
static CRITICAL_SECTION g_accLock;     /* PERF: the copy buffer only - never waits behind file reads / logging */
static u32 g_accVt[32], g_accVis[32]; static int g_accVtCount = 0;

#define ACC_RECORDS 106
#define ACC_SLOTS 17
static const u32 kAccOwn[ACC_RECORDS] = {
    0x8,0x8,0x8,0x8,0x8,0x10,0x10,0x100,0x100,0x100,0x100,0x100,0x100,0x100,0x200,0x200,0x200,0x200,0x200,0x200,0x200,
    0x400,0x400,0x400,0x400,0x400,0x800,0x800,0x800,0x800,0x800,0x1000,0x1000,0x4000,0x4000,
    0x2000,0x2000,0x2000,0x2000,0x2000,0x2000,0x2000,0x2000,0x2000,0x2000,0x2000,0x2000,0x2000,0x2000,0x2000,0x2000,
    0x8000,0x8000,0x8000,0x8000,0x8000,0x8000,0x18000,0x10000,0x10000,0x10000,0x10000,0x10000,0x10000,
    0x1,0x1,0x1,0x1,0x1,0x1,0x1,0x2,0x2,0x2,0x2,0x2,0x2,0x2,
    0x4,0x4,0x4,0x4,0x4,0x4,0x4,0x4,0x4,0x4,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,
    0x40,0x40,0x40,0x40,0x80,0x80,0x80,0x80 };
/* slots: elbowL elbowR wristL shirt thigh | wristR fingerL fingerR legL legR kneeL kneeR ankleL socks ankleR | armL armR */
static const u16 kSlotOff[ACC_SLOTS]   = { 0xA0,0xA0,0xA0,0xA0,0xA0, 0xA4,0xA4,0xA4,0xA4,0xA4,0xA4,0xA4,0xA4,0xA4,0xA4, 0x158,0x158 };
static const u8  kSlotShift[ACC_SLOTS] = { 9,12,15,27,29, 0,4,7,10,14,18,21,24,26,30, 26,29 };
static const u8  kSlotWidth[ACC_SLOTS] = { 3,3,4,2,2, 4,3,3,4,4,3,3,2,4,2, 3,3 };

static IsVisible_t acc_orig(u32 record)
{
    u32 vt, i;
    if (record >= g_imgLo && record + 4 <= g_imgHi) vt = *(u32*)record;   /* static table: no page query */
    else if (!read32(record, &vt)) return 0;
    for (i = 0; i < (u32)g_accVtCount; i++) if (g_accVt[i] == vt) return (IsVisible_t)g_accVis[i];
    return 0;
}

int __fastcall AccVisible(void *record, void *player)
{
    static u8 copy[0x2000];
    IsVisible_t orig = acc_orig((u32)record);
    u32 idx, own, t0;
    int mine = 0, other = 0, s, j, res;
    u8 *base;
    if (!orig) return 0;
    res = orig(record, player);
    if (res || !player || !cfg_enabled || !cfg_accCombos) return res;
    idx = ((u32*)record)[1];
    if (idx >= ACC_RECORDS) return 0;
    t0 = t_now();
    own = kAccOwn[idx];
    for (s = 0; s < ACC_SLOTS; s++) {
        u32 v = (*(u32*)((u8*)player + kSlotOff[s]) >> kSlotShift[s]) & ((1u << kSlotWidth[s]) - 1);
        if (!v) continue;
        if (own & (1u << s)) mine = 1; else other = 1;
    }
    res = 0;
    if (mine && other && cfg_recordSize <= sizeof(copy)) {
        EnterCriticalSection(&g_accLock);          /* one shared copy buffer */
        memcpy(copy, player, cfg_recordSize);
        for (s = 0; s < ACC_SLOTS; s++)
            if (!(own & (1u << s)))
                *(u32*)(copy + kSlotOff[s]) &= ~(((1u << kSlotWidth[s]) - 1) << kSlotShift[s]);
        if (orig(record, copy)) {
            res = 1;
            base = (u8*)record - idx * 0x24;
            if (readable(base, ACC_RECORDS * 0x24)) {
                for (j = 0; j < ACC_RECORDS && res; j++) {
                    IsVisible_t o2;
                    if ((u32)j == idx || kAccOwn[j] != own || ((u32*)(base + j * 0x24))[1] != (u32)j) continue;
                    o2 = acc_orig((u32)(base + j * 0x24));
                    if (o2 && o2(base + j * 0x24, player) && !o2(base + j * 0x24, copy)) res = 0;
                }
            }
        }
        LeaveCriticalSection(&g_accLock);
        if (res) st_accShown++;
    }
    t_done(T_COMBO, t0);
    return res;
}

/* ------------------------------------------------------------------------------------------ */
/* COACH ARMS (MyCareer): after the outfit filter, coaches (suit material still visible) get  */
/* every arm segment shown.                                                                   */
/* ------------------------------------------------------------------------------------------ */
typedef void (__fastcall *SegMask_t)(void *model, u32 bits, u32 show);
typedef void (__fastcall *SegMask2_t)(void *model, void *person);
static u32 g_armMaskFn = 0;
static u32 g_segMask = 0;
static volatile u32 st_arms = 0;
static int g_armSites = 0;

/* The stock arm-segment mask hides the segments in [person+0x294] (plus clothing). In MyCareer the
 * coach's +0x294 holds arm bits, so parts of his arms vanish. Only for coaches (a person without a
 * uniform descriptor at +0x208 - players always have one) whose body has the suit jacket, every arm
 * segment is shown again. Players are never touched (their +0x294 = undershirt / sleeve hiding). */
static u32 g_armLogged[32]; static int g_armLoggedN = 0;
void __fastcall ArmsMask(void *model, void *person)
{
    u32 uni = 1;
    ((SegMask2_t)g_armMaskFn)(model, person);
    if (!model || !person || !cfg_enabled || !cfg_poloArms || !g_segMask) return;
    /* 1.11.0: coach = record +28C != 0 (the flag the FLOOR build itself tests at 0x00BF1780; coaches 1,
     * players 0). 1.9.2-1.10.x used "+208 == 0 (no uniform)", but coach records DO have a uniform
     * pointer (their team's), so ShowArms never did anything. */
    if (!read32((u32)person + 0x28C, &uni) || !uni) return;          /* not a coach */
    {
        u32 mats = ((u32*)model)[0x38 / 4], n = ((u32*)model)[0x30 / 4], i;
        if (!mats || (int)n <= 0 || n > 0x200 || !readable((void*)mats, n * 0x28)) {
            static int told = 0;
            if (!told++) blog("COACH ARMS   coach %08X: model %08X has no readable material list - arms not changed", (u32)person, (u32)model);
            return;
        }
        for (i = 0; i < n; i++)
            if (((u32*)(mats + i * 0x28))[0] == MAT_JACKET) {
                int k, seen = 0;
                ((SegMask_t)g_segMask)(model, 0x1FFFF, 1);
                st_arms++;
                for (k = 0; k < g_armLoggedN; k++) if (g_armLogged[k] == (u32)person) seen = 1;
                if (!seen && g_armLoggedN < 32) {
                    g_armLogged[g_armLoggedN++] = (u32)person;
                    blog("COACH ARMS   coach %08X - all arm segments shown", (u32)person);
                }
                return;
            }
    }
}

/* ------------------------------------------------------------------------------------------ */
/* TEAM COLOUR SOCKS                                                                           */
/* ------------------------------------------------------------------------------------------ */
typedef void (__fastcall *SockPick_t)(void *record, u32 edx, u32 *out);
typedef u32 (__fastcall *TeamColor_t)(void *uniform);
static u32 g_sockPicker = 0, g_sockVtable = 0, g_sockCont = 0, g_team1 = 0, g_team2 = 0, g_linTable = 0;
static volatile u32 st_socks = 0, st_tcAcc = 0, st_tcRemap = 0;
/* every accessory class: its vtable and its own colour picker (slot 4) */
static u32 g_tcVt[32], g_tcPick[32]; static int g_tcVtCount = 0;

/* ---- texture layout detection ----------------------------------------------------------------
 * Colorize textures are channel masks: shader RED (61A6F1EA) gets colour kind0, GREEN (F97E64EB)
 * kind1, BLUE (A37598CD) the third colour. The "all RGB" sock textures are drawn green (body) +
 * red (logo); the stock ones red (body) + blue (logo). DetectStockMasks: BodyHook reads the
 * texture the game just resolved for this material (DXT1/DXT5 colour endpoints of the top of the
 * image) and, for a red+blue texture, feeds the colours so it looks exactly like the red+green one:
 * shader red <- the green colour, shader blue <- the red colour.                                    */
static struct { u32 cont, id; int fmt; } g_texFmt[96];      /* fmt: 1 = red/green (RGB), 2 = red/blue (stock 2K14), 3 = grey/white (stock 2K12), 0 = ? */
static int g_texFmtN = 0;
static struct { u32 tid, id; int fmt; } g_texLast[16];
static CRITICAL_SECTION g_texLock;    /* PERF: texture layout checks read the .iff - never under g_logLock */

/* ---- 1.11.3: the layout is read from the .iff FILE on disk ----------------------------------
 * 1.11.2 read the pixels through the game's asset lookup, but for the VRAM section that returns
 * the D3D texture object, not the pixel bytes, so every texture came out "neither". Now BodyHook
 * opens the file the texture came from (png<cfid>.iff / global.iff / <container>.iff in the game
 * folder), finds the TXTR row, reads its header from the CPU section and its top mip from the VRAM
 * section (inflating ZLIB sections on the fly with a small built-in inflater) and classifies that.
 * Every listed texture of one file is done in one pass; the result is cached.                   */
typedef struct { u16 cnt[16]; u16 sym[288]; } ZHuff;
static struct {
    HANDLE f; u32 left, ip, il, bb, bc, pos, limit; int err, stop, mode;
    u8 in[0x8000];
} Z;
static u8 Zwin[0x10000];
static ZHuff Zlt, Zdt;

typedef struct {
    u32 id, cpuOff, vramOff, hdrGot, size, step, boff, raw, r, g, b, n, fill;
    u8 hdr[0x70], blk[16];
} TexTarget;
static TexTarget g_tt[24]; static int g_ttN = 0;

static u32 z_byte(void)
{
    if (Z.ip == Z.il) {
        DWORD got = 0;
        u32 want = Z.left < sizeof(Z.in) ? Z.left : sizeof(Z.in);
        if (!want || !ReadFile(Z.f, Z.in, want, &got, NULL) || !got) { Z.err = 1; return 0; }
        Z.left -= got; Z.ip = 0; Z.il = got;
    }
    return Z.in[Z.ip++];
}
static u32 z_bits(u32 n)
{
    u32 v;
    if (!n) return 0;
    while (Z.bc < n) { Z.bb |= z_byte() << Z.bc; Z.bc += 8; }
    v = Z.bb & ((1u << n) - 1); Z.bb >>= n; Z.bc -= n;
    return v;
}
static void z_build(ZHuff *h, const u8 *len, u32 n)
{
    u16 offs[16]; u32 i;
    for (i = 0; i < 16; i++) h->cnt[i] = 0;
    for (i = 0; i < n; i++) h->cnt[len[i]]++;
    h->cnt[0] = 0;
    offs[0] = 0;
    for (i = 0; i < 15; i++) offs[i + 1] = (u16)(offs[i] + h->cnt[i]);
    for (i = 0; i < n; i++) if (len[i]) h->sym[offs[len[i]]++] = (u16)i;
}
static u32 z_sym(const ZHuff *h)
{
    int sum = 0, cur = 0, len = 0;
    do {
        cur = 2 * cur + (int)z_bits(1);
        if (++len == 16 || Z.err) { Z.err = 1; return 0; }
        sum += h->cnt[len]; cur -= h->cnt[len];
    } while (cur >= 0);
    return h->sym[sum + cur];
}

/* one chunk of section bytes [pos, pos+len) */
static void tt_sink_n(u32 pos, const u8 *p, u32 len, int k0, int k1)
{
    int k;
    for (k = k0; k < k1; k++) {
        TexTarget *t = &g_tt[k];
        u32 a = Z.mode ? t->vramOff : t->cpuOff, e = a + (Z.mode ? t->size : 0x70), s, x;
        if (pos >= e || pos + len <= a) continue;
        s = pos > a ? pos : a;
        x = pos + len < e ? pos + len : e;
        if (!Z.mode) { for (; s < x; s++) t->hdr[s - a] = p[s - pos]; t->hdrGot = x - a; continue; }
        for (; s < x; s++) {
            t->blk[t->fill++] = p[s - pos];
            if (t->fill == t->step) {
                const u8 *b = t->blk + t->boff;
                t->fill = 0; t->n++;
                if (t->raw) { t->b += b[0] >> 3; t->g += b[1] >> 2; t->r += b[2] >> 3; }
                else {
                    u32 c0 = b[0] | (b[1] << 8), c1 = b[2] | (b[3] << 8);
                    t->r += ((c0 >> 11) & 31) + ((c1 >> 11) & 31);
                    t->g += ((c0 >> 5) & 63) + ((c1 >> 5) & 63);
                    t->b += (c0 & 31) + (c1 & 31);
                }
            }
        }
    }
}
static void tt_sink(u32 pos, const u8 *p, u32 len) { tt_sink_n(pos, p, len, 0, g_ttN); }
static void z_out(u32 c)
{
    Zwin[Z.pos & 0xFFFF] = (u8)c;
    Z.pos++;
    if (!(Z.pos & 0x7FFF)) { tt_sink(Z.pos - 0x8000, Zwin + ((Z.pos - 0x8000) & 0xFFFF), 0x8000); if (Z.pos >= Z.limit) Z.stop = 1; }
}
/* inflate the raw deflate stream until Z.limit bytes were produced (or the stream ends) */
static void z_inflate(void)
{
    static const u16 lbase[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
    static const u8  lbits[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
    static const u16 dbase[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,
                                  4097,6145,8193,12289,16385,24577};
    static const u8  dbits[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
    static const u8  order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
    u8 lens[320];
    u32 last = 0, type, i;
    while (!last && !Z.err && !Z.stop) {
        last = z_bits(1); type = z_bits(2);
        if (type == 0) {
            u32 n;
            z_bits(Z.bc & 7);
            n = z_bits(16); z_bits(16);
            while (n-- && !Z.err && !Z.stop) z_out(z_bits(8));
            continue;
        }
        if (type == 1) {
            for (i = 0; i < 288; i++) lens[i] = (u8)(i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8);
            z_build(&Zlt, lens, 288);
            for (i = 0; i < 30; i++) lens[i] = 5;
            z_build(&Zdt, lens, 30);
        } else if (type == 2) {
            u32 hlit = z_bits(5) + 257, hdist = z_bits(5) + 1, hclen = z_bits(4) + 4, n = 0;
            for (i = 0; i < 19; i++) lens[i] = 0;
            for (i = 0; i < hclen; i++) lens[order[i]] = (u8)z_bits(3);
            z_build(&Zdt, lens, 19);                        /* code-length code (temporary) */
            while (n < hlit + hdist && !Z.err) {
                u32 s = z_sym(&Zdt), rep = 0; u8 v = 0;
                if (s < 16) { lens[n++] = (u8)s; continue; }
                if (s == 16) { if (!n) { Z.err = 1; break; } v = lens[n - 1]; rep = 3 + z_bits(2); }
                else if (s == 17) rep = 3 + z_bits(3);
                else rep = 11 + z_bits(7);
                if (n + rep > hlit + hdist) { Z.err = 1; break; }
                while (rep--) lens[n++] = v;
            }
            if (Z.err) break;
            z_build(&Zlt, lens, hlit);
            z_build(&Zdt, lens + hlit, hdist);
        } else { Z.err = 1; break; }
        for (;;) {
            u32 s = z_sym(&Zlt), len, dist;
            if (Z.err || Z.stop) break;
            if (s < 256) { z_out(s); continue; }
            if (s == 256) break;
            s -= 257;
            if (s >= 29) { Z.err = 1; break; }
            len = lbase[s] + z_bits(lbits[s]);
            s = z_sym(&Zdt);
            if (s >= 30) { Z.err = 1; break; }
            dist = dbase[s] + z_bits(dbits[s]);
            if (dist > Z.pos) { Z.err = 1; break; }
            while (len-- && !Z.stop) z_out(Zwin[(Z.pos - dist) & 0xFFFF]);
        }
    }
    if (!Z.stop && (Z.pos & 0x7FFF)) tt_sink(Z.pos & ~0x7FFFu, Zwin + ((Z.pos & ~0x7FFFu) & 0xFFFF), Z.pos & 0x7FFF);
}

/* feed one section (mode 0 = CPU headers, 1 = VRAM pixels) to the targets. 0 = read error */
static int tt_section(HANDLE f, const u8 *ent, u32 limit, int mode)
{
    u32 off = rd32(ent, 0x14), usize = rd32(ent, 0x0C), csize = rd32(ent, 0x18);
    u8 zh[16]; DWORD got = 0; int k;
    Z.mode = mode;
    if (SetFilePointer(f, (LONG)off, NULL, 0) == 0xFFFFFFFFu) return 0;
    if (!ReadFile(f, zh, 16, &got, NULL) || got != 16) return 0;
    if (zh[0] == 'Z' && zh[1] == 'L' && zh[2] == 'I' && zh[3] == 'B') {
        Z.f = f; Z.left = csize > 16 ? csize - 16 : 0x7FFFFFFF; Z.ip = Z.il = 0; Z.bb = Z.bc = 0;
        Z.pos = 0; Z.limit = limit; Z.err = Z.stop = 0;
        z_bits(16);                                         /* zlib header 78 xx */
        z_inflate();
        return !Z.err || Z.stop;
    }
    /* stored as it is: read just the ranges */
    for (k = 0; k < g_ttN; k++) {
        u32 a = mode ? g_tt[k].vramOff : g_tt[k].cpuOff, n = mode ? g_tt[k].size : 0x70, done = 0;
        if (a >= usize) continue;
        if (a + n > usize) n = usize - a;
        if (SetFilePointer(f, (LONG)(off + a), NULL, 0) == 0xFFFFFFFFu) return 0;
        while (done < n) {
            u32 want = n - done < sizeof(Z.in) ? n - done : sizeof(Z.in);
            if (!ReadFile(f, Z.in, want, &got, NULL) || !got) return 0;
            tt_sink_n(a + done, Z.in, got, k, k + 1);
            done += got;
        }
    }
    return 1;
}

static int tex_wanted(u32 id)
{
    int i;
    for (i = 0; i < cfg_sockCount; i++) if (cfg_sockIds[i] == id) return 1;
    for (i = 0; i < cfg_tcAccCount; i++) if (cfg_tcAccIds[i] == id) return 1;
    return 0;
}

static int tex_file_path(u32 cont, char *path)
{
    const char *nm = container_name(cont);
    u32 cfid = name_cfid(cont);
    PngEntry *e = cfid ? png_lookup(cfid) : 0;
    int n, i;
    s_cpy(path, g_dir, MAX_PATH);
    if (e) { s_cat(path, e->file, MAX_PATH); return 1; }
    if (!nm || nm[0] == '#' || nm[0] == '<') return 0;
    n = s_len(path);
    for (i = 0; nm[i] && n < MAX_PATH - 6; i++) path[n++] = (nm[i] >= 'A' && nm[i] <= 'Z') ? (char)(nm[i] + 32) : nm[i];
    path[n] = 0;
    s_cat(path, ".iff", MAX_PATH);
    return 1;
}

static void tex_store(u32 cont, u32 id, int f)
{
    int i = g_texFmtN < 96 ? g_texFmtN++ : (int)(GetTickCount() % 96);
    g_texFmt[i].cont = cont; g_texFmt[i].id = id; g_texFmt[i].fmt = f;
}

/* PERF (1.11.13 / 1.19.2): the layout found for a texture is kept in BodyHook_cache.ini, keyed by
 * file name + size + write time, so the texture pixels are read once ever, not once per session. */
static int tex_file_path(u32 cont, char *path);
static void tex_cache_key(u32 cont, char *sec)
{
    char path[MAX_PATH], num[16];
    WIN32_FILE_ATTRIBUTE_DATA fa;
    sec[0] = 0;
    if (!tex_file_path(cont, path) || !GetFileAttributesExA(path, 0, &fa)) return;
    s_cpy(sec, "TeamColor ", 128); s_cat(sec, path + s_len(g_dir), 128);
    u32_to_dec(fa.nFileSizeLow, num); s_cat(sec, " ", 128); s_cat(sec, num, 128);
    s_cat(sec, " ", 128); s_cat(sec, hex8(fa.ftLastWriteTime.dwLowDateTime), 128);
}
static void tex_cache_ini(char *p) { s_cpy(p, g_modDir, MAX_PATH); s_cat(p, "BodyHook_cache.ini", MAX_PATH); }
static int tex_cache_get(u32 cont, u32 id)
{
    char sec[128], ini[MAX_PATH], v[8];
    tex_cache_key(cont, sec);
    if (!sec[0]) return -1;
    tex_cache_ini(ini);
    GetPrivateProfileStringA(sec, hex8(id), "", v, sizeof(v), ini);
    if (v[0] < '0' || v[0] > '3') return -1;
    return v[0] - '0';
}
static void tex_store(u32 cont, u32 id, int f);
static void tex_cache_put(u32 cont)
{
    char sec[128], ini[MAX_PATH], v[4];
    int i;
    tex_cache_key(cont, sec);
    if (!sec[0]) return;
    tex_cache_ini(ini);
    for (i = 0; i < g_texFmtN; i++)
        if (g_texFmt[i].cont == cont) { v[0] = (char)('0' + g_texFmt[i].fmt); v[1] = 0; WritePrivateProfileStringA(sec, hex8(g_texFmt[i].id), v, ini); }
}

/* classify every listed texture of the file behind this container (one pass, then cached) */
static const char *g_scanWhy = "";
static void tex_scan_file(u32 cont)
{
    static u8 hdr[0x10000];
    char path[MAX_PATH];
    HANDLE f;
    DWORD got = 0;
    u32 nsec, so, nfiles, fo, i, cpuI = 99, vrI = 99, lim;
    int k;
    g_ttN = 0;
    if (!tex_file_path(cont, path)) { g_scanWhy = "the file of this container is not known"; return; }
    f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) { g_scanWhy = "its .iff is not a loose file in the game folder"; return; }
    ReadFile(f, hdr, sizeof(hdr), &got, NULL);
    g_scanWhy = "the .iff header could not be read";
    if (got < 0x40 || rd32(hdr, 0) != 0xFF3BEF94u) { CloseHandle(f); return; }
    nsec = rd32(hdr, 0x10); so = 0x14 + rd32(hdr, 0x14) - 1;
    nfiles = rd32(hdr, 0x18); fo = 0x1C + rd32(hdr, 0x1C) - 1;
    if (nsec > 16 || so + 32 * nsec > got || nfiles > 8192 || fo + 4 * nfiles > got) { CloseHandle(f); return; }
    for (i = 0; i < nsec; i++) {
        u32 h = rd32(hdr, so + 32 * i);
        if (h == 0xBB05A9C1u && cpuI == 99) cpuI = i;
        if (h == 0x411536D5u && vrI == 99) vrI = i;
    }
    if (cpuI == 99 || vrI == 99) { g_scanWhy = "the .iff has no CPU/VRAM section"; CloseHandle(f); return; }
    for (i = 0; i < nfiles && g_ttN < 24; i++) {
        u32 p = fo + 4 * i, eo = p + rd32(hdr, p) - 1, id, n;
        if (eo + 12 > got) break;
        id = rd32(hdr, eo); n = rd32(hdr, eo + 8);
        if (rd32(hdr, eo + 4) != TYPE_TXTR || !tex_wanted(id) || n <= cpuI || n <= vrI || n > 16 || eo + 12 + 4 * n > got) continue;
        memset(&g_tt[g_ttN], 0, sizeof(TexTarget));
        g_tt[g_ttN].id = id;
        g_tt[g_ttN].cpuOff = rd32(hdr, eo + 12 + 4 * cpuI);
        g_tt[g_ttN].vramOff = rd32(hdr, eo + 12 + 4 * vrI);
        g_ttN++;
    }
    g_scanWhy = "the texture is not in this .iff";
    if (!g_ttN) { CloseHandle(f); return; }
    /* pass 1: headers */
    for (lim = 0, k = 0; k < g_ttN; k++) if (g_tt[k].cpuOff + 0x70 > lim) lim = g_tt[k].cpuOff + 0x70;
    if (!tt_section(f, hdr + so + 32 * cpuI, lim, 0)) blog("TEAMCOLOR %s: CPU section could not be read", path);
    for (lim = 0, k = 0; k < g_ttN; k++) {
        TexTarget *t = &g_tt[k];
        u32 fcc = rd32(t->hdr, 8), w = t->hdr[0x68] | (t->hdr[0x69] << 8), h = t->hdr[0x6A] | (t->hdr[0x6B] << 8);
        t->size = 0;
        if (t->hdrGot < 0x70) continue;
        if (w < 4 || h < 4 || w > 4096 || h > 4096) continue;
        if (fcc == 0x31545844u) { t->step = 8; t->boff = 0; t->size = (w / 4) * (h / 4) * 8; }
        else if (fcc == 0x35545844u || fcc == 0x33545844u) { t->step = 16; t->boff = 8; t->size = (w / 4) * (h / 4) * 16; }
        else if (fcc == 21 || fcc == 22) { t->step = 4; t->boff = 0; t->raw = 1; t->size = w * h * 4; }
        if (t->size && t->vramOff + t->size > lim) lim = t->vramOff + t->size;
    }
    /* pass 2: top mip of every texture */
    if (lim && !tt_section(f, hdr + so + 32 * vrI, lim, 1)) blog("TEAMCOLOR %s: VRAM section could not be read", path);
    CloseHandle(f);
    for (k = 0; k < g_ttN; k++) {
        TexTarget *t = &g_tt[k];
        u32 rs = t->r, gs = t->g / 2, bs = t->b;
        int fmt = 0;
        const char *why;
        if (t->hdrGot < 0x70) why = "header not read";
        else if (!t->size) why = "texture format not DXT1/DXT3/DXT5/ARGB";
        else if (!t->n) why = "pixels not read";
        else if (gs > rs + rs / 4 && gs > bs) { fmt = 1; why = "mostly green"; }
        else if (rs > gs * 2 && rs > bs && rs > t->n) { fmt = 2; why = "mostly red, no green"; }
        else if (rs && gs && bs && rs * 5 < gs * 6 && gs * 5 < rs * 6 && bs * 5 < rs * 6 && rs * 5 < bs * 6 &&
                 bs * 5 < gs * 6 && gs * 5 < bs * 6) { fmt = 3; why = "grey/white, all channels equal"; }
        else why = "neither (mixed colours)";
        tex_store(cont, t->id, fmt);
        blog("TEAMCOLOR texture %08X in %s: %s  (%s; %ux%u, red %u green %u blue %u)", t->id, path + s_len(g_dir),
             fmt == 1 ? "red/green layout - used as it is" :
             fmt == 2 ? "red/blue layout -> red = Team Color 2, blue = Team Color 1" :
             fmt == 3 ? (cfg_tcGrey == 2 ? "grey/white -> whole texture Team Color 2" : cfg_tcGrey ? "grey/white -> whole texture Team Color 1" :
                         "grey/white - used as it is (GreyTextures=0)") : "layout not recognised - used as it is",
             why, t->hdr[0x68] | (t->hdr[0x69] << 8), t->hdr[0x6A] | (t->hdr[0x6B] << 8), rs, gs, bs);
    }
}

static int tex_format(u32 cont, u32 id, void *cpu)
{
    static u32 scanned[64]; static int nScanned = 0;
    int i, f = 0, seen = 0;
    (void)cpu;
    for (i = 0; i < g_texFmtN; i++) if (g_texFmt[i].cont == cont && g_texFmt[i].id == id) return g_texFmt[i].fmt;
    for (i = 0; i < cfg_tcRBCount; i++) if (cfg_tcRB[i] == id) f = 2;
    for (i = 0; i < cfg_tcRGCount; i++) if (cfg_tcRG[i] == id) f = 1;
    if (f) {
        tex_store(cont, id, f);
        blog("TEAMCOLOR texture %08X in %s: listed in %s", id, container_name(cont), f == 2 ? "RedBlueTextures" : "RedGreenTextures");
        return f;
    }
    for (i = 0; i < nScanned; i++) if (scanned[i] == cont) seen = 1;
    if (!seen && (f = tex_cache_get(cont, id)) >= 0) { tex_store(cont, id, f); return f; }
    if (!seen) {
        scanned[nScanned < 64 ? nScanned++ : 63] = cont;
        tex_scan_file(cont);
        tex_cache_put(cont);
        for (i = 0; i < g_texFmtN; i++) if (g_texFmt[i].cont == cont && g_texFmt[i].id == id) return g_texFmt[i].fmt;
    }
    tex_store(cont, id, 0);
    blog("TEAMCOLOR texture %08X in %s: layout not checked - %s; used as it is (add it to RedBlueTextures= if it is red/blue)",
         id, container_name(cont), g_scanWhy);
    return 0;
}

static int tc_listed(const u32 *rec)
{
    int i;
    if (!rec || !cfg_enabled || !cfg_socks) return 0;
    if (rec[0] == g_sockVtable) {
        if (rec[1] == P_sockStripedA || rec[1] == P_sockStripedB) return 0;   /* striped long socks: stock already */
        for (i = 0; i < cfg_sockCount; i++) if (rec[3] == cfg_sockIds[i]) return 1;
    }
    for (i = 0; i < cfg_tcAccCount; i++) if (rec[3] == cfg_tcAccIds[i]) return 2;
    return 0;
}

static void tex_note(u32 fileId, u32 cont, void *res)
{
    u32 tid, i, slot = 0;
    int f = 0;
    if (!cfg_enabled || !cfg_socks || !cfg_tcDetect || !res) return;
    for (i = 0; i < (u32)cfg_sockCount; i++) if (cfg_sockIds[i] == fileId) f = 1;
    for (i = 0; i < (u32)cfg_tcAccCount; i++) if (cfg_tcAccIds[i] == fileId) f = 1;
    if (!f) return;
    tid = GetCurrentThreadId();
    EnterCriticalSection(&g_texLock);
    f = tex_format(cont, fileId, res);
    for (i = 0; i < 16; i++) if (g_texLast[i].tid == tid || !g_texLast[i].tid) { slot = i; break; }
    if (i == 16) slot = tid & 15;
    g_texLast[slot].tid = tid; g_texLast[slot].id = fileId; g_texLast[slot].fmt = f;
    LeaveCriticalSection(&g_texLock);
}

/* layout found for this texture in any .iff (the material can be built on another thread than the lookup) */
static int tex_fmt_any(u32 id)
{
    int i, f = 0;
    for (i = 0; i < g_texFmtN; i++) if (g_texFmt[i].id == id && g_texFmt[i].fmt) f = g_texFmt[i].fmt;
    return f;
}
/* layout of the texture this thread resolved for this material (0 = not known) */
static int tex_last_fmt(u32 id)
{
    u32 tid = GetCurrentThreadId(), i;
    for (i = 0; i < 16; i++) if (g_texLast[i].tid == tid) return g_texLast[i].id == id ? g_texLast[i].fmt : 0;
    return 0;
}

/* colour kinds for shader red / green, by style; blue is SockBlue's */
void __fastcall TeamColors(void *record, u32 edx, u32 *out)
{
    const u32 *rec = (const u32*)record;
    u32 orig = 0, i;
    int listed;
    for (i = 0; i < (u32)g_tcVtCount; i++) if (g_tcVt[i] == rec[0]) { orig = g_tcPick[i]; break; }
    if (!orig && rec[0] == g_sockVtable) orig = g_sockPicker;
    if (orig) ((SockPick_t)orig)(record, edx, out);
    listed = tc_listed(rec);
    if (!listed) return;
    if (listed == 1) st_socks++; else st_tcAcc++;
    if (cfg_sockStyle == 1) { out[0] = 2; out[1] = 3; }    /* red team 1, green team 2, blue = sock colour */
    else out[1] = 2;                                          /* red sock colour, green team 1, blue team 2 */
    if (cfg_tcDetect) {
        int f = tex_last_fmt(rec[3]);
        if (!f) f = tex_fmt_any(rec[3]);
        if (!f) {                                      /* not seen by the texture lookup: forced list only */
            for (i = 0; i < (u32)cfg_tcRBCount; i++) if (cfg_tcRB[i] == rec[3]) f = 2;
            {
                static u32 told[32]; static int nt = 0; int k, seen = 0;
                for (k = 0; k < nt; k++) if (told[k] == rec[3]) seen = 1;
                if (!seen && nt < 32) {
                    told[nt++] = rec[3];
                    blog("TEAMCOLOR texture %08X: its layout was not checked (texture not seen by the lookup)%s",
                         rec[3], f == 2 ? " - RedBlueTextures list used" : " - used as it is");
                }
            }
        }
        if (f == 2) { out[0] = out[1]; st_tcRemap++; }   /* red/blue texture: red <- green's colour (Team 2) */
        else if (f == 3 && cfg_tcGrey) {               /* grey/white: like a red mask - red = the team colour, */
            out[0] = cfg_tcGrey == 2 ? 3 : 2;          /* green/blue black, so a white texel is exactly it   */
            out[1] = 1;
            st_tcRemap++;
        }
    }
    if (listed == 2 && cfg_log) {   /* 1.11.30-ML: which accessory textures really got the team colours */
        static u32 seen[32]; static int ns = 0; int k, was = 0;
        for (k = 0; k < ns; k++) if (seen[k] == rec[3]) was = 1;
        if (!was && ns < 32) { seen[ns++] = rec[3];
            blog("TEAMCOLOR accessory texture %08X (class %08X): red = %s, green = %s, blue = %s", rec[3], rec[0],
                 out[0] == 2 ? "Team 1" : out[0] == 3 ? "Team 2" : "other", out[1] == 2 ? "Team 1" : out[1] == 3 ? "Team 2" : "other",
                 rec[0] == g_sockVtable ? "sock colour" : "the accessory's own colour"); }
    }
}
void __fastcall SockColors(void *record, u32 edx, u32 *out) { TeamColors(record, edx, out); }

static void team_rgb(u32 getter, u32 uni, float *out)
{
    const float *t = (const float*)g_linTable;
    u32 c = ((TeamColor_t)getter)((void*)uni);
    out[0] = t[(c >> 16) & 0xFF]; out[1] = t[(c >> 8) & 0xFF]; out[2] = t[c & 0xFF];
}

/* blue colour for listed textures; returns 1 when handled */
int __cdecl SockBlue(const u32 *record, u8 *player, float *out)
{
    u32 uni = 0, w = 0;
    int remap;
    float sock;
    if (!tc_listed(record)) return 0;
    if (player) read32((u32)player + P_offUniform, &uni);
    sock = (uni && read32(uni + 4, &w)) ? ((w & P_sockBit) ? 0.01f : 1.0f) : 0.01f;
    remap = 0;
    if (cfg_tcDetect) {
        int f = tex_last_fmt(record[3]), i;
        if (!f) f = tex_fmt_any(record[3]);
        if (!f) for (i = 0; i < cfg_tcRBCount; i++) if (cfg_tcRB[i] == record[3]) f = 2;
        if (f == 3 && cfg_tcGrey) { out[0] = out[1] = out[2] = 0.005f; out[3] = 1.0f; return 1; }   /* grey: blue black */
        remap = f == 2;                                  /* red/blue: blue gets what red would get (Team 1) */
    }
    if (cfg_sockStyle == 1) {
        if (remap && uni && g_team1 && g_linTable) team_rgb(g_team1, uni, out);      /* team 1 */
        else if (remap) { out[0] = out[1] = out[2] = 0.26f; }
        else out[0] = out[1] = out[2] = sock;                                       /* sock colour */
    } else {
        if (remap) out[0] = out[1] = out[2] = sock;                                  /* sock colour */
        else if (uni && g_team2 && g_linTable) team_rgb(g_team2, uni, out);         /* team 2 */
        else out[0] = out[1] = out[2] = 0.26f;
    }
    out[3] = 1.0f;
    return 1;
}
/* xor eax,eax / cmp [ecx+10h],eax / je / jmp [edx*4+table] */
static int looks_like_asset_helper(u32 fn)
{
    const u8 *p = (const u8*)fn;
    if (!readable(p, 10)) return 0;
    return p[0] == 0x33 && p[1] == 0xC0 && p[2] == 0x39 && p[3] == 0x41 && p[4] == 0x10 && p[5] == 0x74 &&
           p[7] == 0xFF && p[8] == 0x24 && p[9] == 0x95;
}

static u32 call_target_of(u32 site) { return site + 5 + *(int*)(site + 1); }

static int patch_bytes(u32 addr, const void *src, u32 n, int code);
/* re-target a "call rel32" (the opcode byte stays): through patch_bytes -> SLAM */
static int patch_call(u32 site, u32 target)
{
    u8 b[5];
    b[0] = *(const u8 *)site;
    *(int *)(b + 1) = (int)(target - (site + 5));
    return patch_bytes(site, b, 5, 1);
}

/* every "mov ecx, crc32('legs')" followed (within 0x60 bytes) by a call to the asset helper */
static void install_leg_hooks(u8 *text, u32 size)
{
    u8 *p, *end = text + size - 0x70;
    int patch = cfg_enabled && cfg_legs;
    if (!patch) blog("leg tattoos: disabled in ini (the asset helper is still located for the headband)");
    for (p = text; p < end; p++) {
        if (p[0] == 0xB9 && *(u32*)(p + 1) == HASH_LEGS) {
            u8 *q;
            for (q = p + 5; q < p + 0x60; q++) {
                if (q[0] == 0xE8) {
                    u32 site = (u32)q, t = call_target_of(site);
                    if (t == (u32)&LegLookup) { q = p + 0x60; break; }          /* already patched */
                    if (looks_like_asset_helper(t)) {
                        if (!g_assetHelper) g_assetHelper = (AssetHelper_t)t;
                        if (patch && (u32)g_assetHelper == t && patch_call(site, (u32)&LegLookup)) {
                            g_legSites++;
                            blog("leg tattoo call site  : %08X -> LegLookup (asset helper %08X)", site, t);
                        }
                        break;
                    }
                }
            }
        }
    }
    if (!patch) return;
    if (!g_legSites) blog("WARNING leg tattoo call sites not found - leg tattoos stay stock");
    else blog("LEG HOOKS INSTALLED: %d call site(s). Per-cyberface leg tattoos are active.", g_legSites);
}

/* ------------------------------------------------------------------------------------------ */
/* jerseys / shorts                                                                           */
/* The game builds ONE model per jersey style x size at startup (0x00BDFFA0: names            */
/* "jersey_{style}_{thin|reg|fat}", "shorts{thin|reg|fat}", global search) and keeps them in   */
/* global arrays. The player build (0x00BF12F0) then just picks array[size + style*4] through  */
/* four tiny getters (0x00BDF8E0/0x00BDF920/0x00BDF960/0x00BDF9A0, ecx = player). Every       */
/* player therefore shares the cached model. The four calls are redirected here: the cached    */
/* model is identified (which FileID it is), and if the player's own png has that FileID -    */
/* or, with MatchAnyInGroup, another FileID of the same garment - the png's model is used.     */
/* ------------------------------------------------------------------------------------------ */
typedef void *(__fastcall *ClothGetter_t)(void *player);
static u32 g_clothGetter[4];          /* original targets, per patched site  */
static int g_clothSites = 0;
static volatile u32 st_clothCalls = 0, st_clothLocal = 0;

#define CLOTH_MAP 256
static struct { u32 res; u32 id; int g; } g_clothMap[CLOTH_MAP];
static int g_clothMapCount = 0, g_clothMapNext = 0;
static void cloth_map_add(u32 res, u32 id, int g)
{
    int i = g_clothMapCount < CLOTH_MAP ? g_clothMapCount++ : (g_clothMapNext++ % CLOTH_MAP);
    g_clothMap[i].res = res; g_clothMap[i].id = id; g_clothMap[i].g = g;
}

static const char *cloth_name(u32 id)
{
    static char ring[4][16]; static int ri = 0;
    int g, k; char *b;
    for (g = 0; g < cfg_clothGroups; g++) for (k = 0; k < cfg_clothCount[g]; k++)
        if (cfg_clothIds[g][k] == id) return cfg_clothNames[g][k];
    b = ring[ri++ & 3];
    for (k = 0; k < 8; k++) b[k] = "0123456789ABCDEF"[(id >> ((7 - k) * 4)) & 15];
    b[8] = 0;
    return b;
}

static int node_is_png(u32 node);
/* which configured FileID is this cached (global) model? returns 0 if none */
static u32 cloth_identify(u32 res, int *groupOut)
{
    int i, g, k;
    *groupOut = -1;
    for (i = 0; i < g_clothMapCount; i++) if (g_clothMap[i].res == res) { *groupOut = g_clothMap[i].g; return g_clothMap[i].id; }
    for (g = 0; g < cfg_clothGroups; g++) for (k = 0; k < cfg_clothCount[g]; k++) {
        u32 id = cfg_clothIds[g][k], node, sentinel = g_mgr + 0x1C, n = 0;
        if (!read32(g_mgr + 0x28, &node)) return 0;
        while (node && node != sentinel && n++ < 4096) {
            u32 vtbl, fn, hash, out = 0, next;
            if (!read32(node, &vtbl) || !read32(vtbl + 0x0C, &fn) || !read32(node + 0x28, &hash)) break;
            if (((FindInContainer_t)fn)((void*)node, id, TYPE_SCNE, &out) && !node_is_png(node)) {
                if ((u32)g_resolve((void*)g_mgr, SEC_CPU, hash, id, TYPE_SCNE, 0, 0, 0) == res) {
                    cloth_map_add(res, id, g);
                    if (cfg_diagnostics) blog("uniform model %08X is %s (global cache)", res, cfg_clothNames[g][k]);
                    *groupOut = g;
                    return id;
                }
            }
            if (!read32(node + 0x0C, &next)) break;
            node = next;
        }
    }
    cloth_map_add(res, 0, -1);                           /* remember "not one of ours" */
    return 0;
}

/* Is the png the game gave this player loaded yet (container valid and answering "hihead")? */
static int png_ready(u8 *player, u32 *cfOut)
{
    u32 valid, cf = player_container(player, &valid), node;
    *cfOut = cf;
    if (!cf) return 0;
    node = find_node(cf);
    return node && node_find(node, HASH_HIHEAD, TYPE_SCNE);
}

/* Prediction from the loose png on disk (no container needed): will cloth_common_ex() give this
 * player a model from his own png for this getter? Used while his png is still loading - the
 * in-game cloth instances are built for every player at once, usually BEFORE the cyberfaces load. */
static int png_predicts_cloth(u8 *player, int site, void *R)
{
    PngEntry *e = png_lookup(player_cfid(player));
    u32 id, i; int g, k, isShorts = (site >= 2);
    if (!e || !e->m.clothMask || !R) return 0;
    EnterCriticalSection(&g_logLock);
    id = cloth_identify((u32)R, &g);
    LeaveCriticalSection(&g_logLock);
    for (i = 0; i < (u32)cfg_clothGroups && i < 4; i++) {
        if (g >= 0 && (int)i != g) continue;
        if (g < 0 && cfg_clothIsShorts[i] != isShorts) continue;
        for (k = 0; k < cfg_clothCount[i] && k < 8; k++)
            if ((e->m.clothMask & (1u << (i * 8 + k))) && (cfg_clothIds[i][k] == id || cfg_matchAnyCloth)) return 1;
    }
    return 0;
}

static void *cloth_common_ex(u8 *player, int site, int *fromPng, int quiet);
static void *cloth_common(u8 *player, int site) { int f; return cloth_common_ex(player, site, &f, 0); }
static void *cloth_common_ex(u8 *player, int site, int *fromPng, int quiet)
{
    void *R = ((ClothGetter_t)g_clothGetter[site])(player);
    u32 cf, valid, id, i;
    int g, k, isShorts = (site >= 2);
    void *r;
    *fromPng = 0;
    if (!quiet) st_clothCalls++;
    if (!cfg_enabled || !cfg_cloth || !R) return R;
    if (!quiet && cfg_loadWaitMs && !nowait_has(0x40000000u | player_cfid(player)) && !png_ready(player, &cf) &&
        png_predicts_cloth(player, site, R)) {
        /* his png has this garment on disk but is still loading: wait for it (like the body does),
         * otherwise he would get the shared moving model while his cloth sim is already switched off */
        u32 waited = 0;
        while (waited < cfg_loadWaitMs && !png_ready(player, &cf)) { Sleep(25); waited += 25; }
        if (!png_ready(player, &cf)) nowait_add(0x40000000u | player_cfid(player));
        if (cfg_log) {
            EnterCriticalSection(&g_logLock);
            if (cfg_logEvery || !seen_before((u32)player ^ 0x20000000u, (u32)site, waited, 0))
                blog("UNIFORM WAIT    CFID=%-6s waited %u ms for his png to load (%s)", cfid_str(player_cfid(player)), waited,
                     png_ready(player, &cf) ? "loaded" : "still not loaded - shared model used");
            LeaveCriticalSection(&g_logLock);
        }
    }
    cf = player_container(player, &valid);
    if (!cf) return R;
    /* fast path: this player's png has no uniform rows -> nothing to do (no container walk) */
    if (cfg_fastPath && g_scanDone) {
        PngMasks pm;
        if (player_info(player, &pm) && !pm.clothMask) return R;
    }

    if (quiet == 2) {                                      /* 2.2: asked from the draw call (garment_static): never wait */
        if (!TryEnterCriticalSection(&g_logLock)) { *fromPng = -1; return R; }
    } else EnterCriticalSection(&g_logLock);
    id = cloth_identify((u32)R, &g);
    LeaveCriticalSection(&g_logLock);

    if (id) {
        r = g_resolve((void*)g_mgr, SEC_CPU, cf, id, TYPE_SCNE, 0, 0, 0);
        if (r) {
            *fromPng = 1;
            if (quiet) return r;
            st_clothLocal++;
            {
                EnterCriticalSection(&g_logLock);
                if (cfg_logEvery || !seen_before((u32)player ^ 0x40000000u, cf, id, 1))
                    blog("UNIFORM LOCAL   CFID=%-6s png=%s  %s from own png -> %08X (global model %08X)",
                         cfid_str(player_cfid(player)), container_name(cf), cloth_name(id), (u32)r, (u32)R);
                LeaveCriticalSection(&g_logLock);
            }
            return r;
        }
    }
    if (!cfg_matchAnyCloth) return R;
    /* same garment group as the cached model (or, if unknown, every group of this garment type) */
    for (i = 0; i < (u32)cfg_clothGroups; i++) {
        if (g >= 0 && (int)i != g) continue;
        if (g < 0 && cfg_clothIsShorts[i] != isShorts) continue;
        for (k = 0; k < cfg_clothCount[i]; k++) {
            u32 c = cfg_clothIds[i][k];
            if (c == id) continue;
            r = g_resolve((void*)g_mgr, SEC_CPU, cf, c, TYPE_SCNE, 0, 0, 0);
            if (r) {
                *fromPng = 1;
                if (quiet) return r;
                st_clothLocal++;
                EnterCriticalSection(&g_logLock);
                if (cfg_logEvery || !seen_before((u32)player ^ 0x40000000u, cf, c, 1))
                    blog("UNIFORM LOCAL~  CFID=%-6s png=%s  wanted %s, using %s from own png -> %08X",
                         cfid_str(player_cfid(player)), container_name(cf), id ? cloth_name(id) : "?", cloth_name(c), (u32)r);
                LeaveCriticalSection(&g_logLock);
                return r;
            }
        }
    }
    return R;                                   /* the shared model from the cloth_*.iff files */
}

static void *cloth_timed(u8 *p, int site) { u32 t0 = t_now(); void *r = cloth_common(p, site); t_done(T_UNIFORM, t0); return r; }
void *__fastcall ClothGet0(void *p) { return cloth_timed((u8*)p, 0); }
void *__fastcall ClothGet1(void *p) { return cloth_timed((u8*)p, 1); }
void *__fastcall ClothGet2(void *p) { return cloth_timed((u8*)p, 2); }
void *__fastcall ClothGet3(void *p) { return cloth_timed((u8*)p, 3); }

/* ---- cloth-simulation bridge -------------------------------------------------------------- */
/* Moving cloth: each player gets a cloth instance built from the ClothMesh (type A2449BBB) that */
/* belongs to the SHARED model (0x00BDFA80 jersey / 0x00BDFC70 shorts, array[size+style*4]).     */
/* A nonmoving png model has no ClothMesh, and the global one does not fit it (grey/broken).    */
/* When the game has no ClothMesh for a slot it creates no cloth instance and draws the static  */
/* mesh (that is how nonmoving cloth files work), so for players whose png supplies the model   */
/* the slot is presented as empty for the duration of that one call.                           */
typedef void *(__fastcall *ClothSim_t)(void *player, u32 style, void *out);
static u32 g_simFn[2];          /* builder per garment: 0 = the one paired with model getter site 0, 1 = site 2 */
static u32 g_simArr[2];         /* its ClothMesh array                                              */
static u32 g_simFetch[2];       /* address of "mov ecx,[eax*4+array]" inside the builder (patched)   */
static int g_simSites = 0;
static volatile u32 st_simBridged = 0;
static int st_simPracLog = 0;
static volatile u32 g_suppressTid = 0;     /* thread that must see an empty ClothMesh slot, 0 = none */
static const char *kGarment[2] = { "jersey", "shorts" };

/* Replaces the builder's own "mov ecx,[eax*4+array]": ecx = entry, or 0 for the suppressed thread.
 * Nothing global is ever written, so other players (and other threads) are untouched.            */
#if defined(_MSC_VER)
__declspec(naked) void ClothFetch0(void)
{
    __asm {
        mov  ecx, dword ptr fs:[24h]
        cmp  ecx, g_suppressTid
        je   empty0
        mov  ecx, g_simArr[0]
        mov  ecx, [ecx + eax*4]
        ret
    empty0:
        xor  ecx, ecx
        ret
    }
}
__declspec(naked) void ClothFetch1(void)
{
    __asm {
        mov  ecx, dword ptr fs:[24h]
        cmp  ecx, g_suppressTid
        je   empty1
        mov  ecx, g_simArr[4]
        mov  ecx, [ecx + eax*4]
        ret
    empty1:
        xor  ecx, ecx
        ret
    }
}
#endif

typedef void *(__fastcall *ClothSimP_t)(void *player, void *out);
static u32 g_simFnP[2];          /* 2K12: builders that take (player, out) and read the style themselves */
static void *sim_call(int kind, void *player, u32 style, void *out, int pfn)
{
    if (pfn) return ((ClothSimP_t)g_simFnP[kind])(player, out);
    return ((ClothSim_t)g_simFn[kind])(player, style, out);
}
/* cloth objects whose in-place rebuild for another body size was refused (sim_core): the owner guard (bench.inc) must
 * not swap them for "his own" cloth either - that cloth is made for the NEW size and the drawn model is still the old one */
static struct { u32 obj, tick; } g_clothKept[16];
static int hl_queue_idle(u32 prv, u32 rec);   /* hairrefresh.inc */
static struct { u32 out, player, model, kind, tick; } g_simOut[64];   /* cloth objects the game built, and for whom */
static void cloth_keep_note(u32 obj)
{
    int i, o = 0; u32 now = GetTickCount();
    for (i = 0; i < 16; i++) { if (g_clothKept[i].obj == obj) { o = i; break; } if (now - g_clothKept[i].tick > now - g_clothKept[o].tick) o = i; }
    g_clothKept[o].obj = obj; g_clothKept[o].tick = now;
}
static int cloth_kept(u32 obj)
{
    int i;
    if (!obj) return 0;
    for (i = 0; i < 16; i++) if (g_clothKept[i].obj == obj && GetTickCount() - g_clothKept[i].tick < 60000) return 1;
    return 0;
}
static void *sim_core(int kind, void *player, u32 style, void *out, int pfn);
static void *sim_common(int kind, void *player, u32 style, void *out) { return sim_core(kind, player, style, out, 0); }
static void *sim_core(int kind, void *player, u32 style, void *out, int pfn)
{
    int fromPng = 0;
    u32 v = 0, slotVal = 0;
    int bt, idx;
    void *r;
    int site = kind ? 2 : 0;
    if (!cfg_enabled || !cfg_cloth || !player || !g_clothGetter[site])
        return sim_call(kind, player, style, out, pfn);
    {   /* 1.11.22-ML / 1.19.10: only ask the game's getter when this record's uniform pointer is readable
         * (Edit Player can hand over a record whose uniform pointer is garbage: crash in the jersey getter) */
        u32 uni = 0;
        if (!read32((u32)player + P_offUniform, &uni) || (uni && !readable((void*)uni, 0x38)))
            return sim_call(kind, player, style, out, pfn);
    }

    if (read32((u32)player + P_offBodyWord, &v)) {
        bt = ((int)(v << P_btShl)) >> 29;
        idx = bt + (int)style * 4;
        read32(g_simArr[kind] + (u32)(idx * 4), &slotVal);
    } else idx = -99;

    /* 1.11.31-ML: Edit Player Weight / Height across the fat line. The game rebuilds the cloth IN PLACE (same object)
     * for the new body size while the drawn shorts model is still the old one until the preview is rebuilt: the fat
     * shorts (544 particles) then ran on the regular cloth (424) -> shards and strips across the screen (log 2c32871d,
     * 02:27:46). An in-place rebuild to a ClothMesh with another particle count keeps the cloth it has; the preview's
     * own rebuild (new object) gets the new cloth. Skinny / regular / muscular all have 424, so only fat was hit. */
    /* crash review: only a REAL in-place rebuild is refused - this object was built by us before for the same record and
     * the same model ([record+1E8]), and its cloth still uses a ClothMesh the game has loaded now. A new object in memory
     * that once held another cloth (freed and allocated again) is always built, else the game would run on stale data. */
    {   u32 mdl = 0; int k, hit = -1, fr = 0; u32 now = GetTickCount();
        read32((u32)player + 0x1E8, &mdl);
        for (k = 0; k < 64; k++) { if (g_simOut[k].out == (u32)out) { hit = k; break; } if (now - g_simOut[k].tick > now - g_simOut[fr].tick) fr = k; }
        if (hit >= 0 && out && slotVal && readable(out, 4) && g_simOut[hit].player == (u32)player && g_simOut[hit].model == mdl && g_simOut[hit].kind == (u32)kind) {
            const u32 CLOTH_MAGIC = g_is2k12 ? 0x019957A3u : 0xF7CB9859u;
            u32 in = *(u32*)out, om = 0, i, loaded = 0, n = kind ? 12 : 36, v;
            if (in && readable((void*)in, 0xD0) && *(u32*)(in + 0xA4) == CLOTH_MAGIC) om = *(u32*)(in + 0xA8);
            for (i = 0; om && i < n; i++) if (read32(g_simArr[kind] + i * 4, &v) && v == om) { loaded = 1; break; }
            if (loaded && readable((void*)om, 0xD0) && *(u32*)(om + 0xA4) == CLOTH_MAGIC && readable((void*)slotVal, 0xD0) &&
                (*(u32*)(in + 0xB8) & 0xFFF) != (*(u32*)(slotVal + 0xB8) & 0xFFF)) goto refuse;
        }
        if (hit < 0) hit = fr;
        g_simOut[hit].out = (u32)out; g_simOut[hit].player = (u32)player; g_simOut[hit].model = mdl; g_simOut[hit].kind = (u32)kind; g_simOut[hit].tick = now;
    }
    if (0) {
        refuse: {
            u32 in = *(u32*)out;
            static int lg;
            if (lg < 8) { lg++; blog("CLOTHSIM CFID %s %s: rebuilt in place for another body size (%u -> %u particles) while the model is still the old one - kept until the model is rebuilt",
                cfid_str(player_cfid((u8*)player)), kGarment[kind], *(u32*)(in + 0xB8) & 0xFFF, *(u32*)(slotVal + 0xB8) & 0xFFF); }
            cloth_keep_note((u32)out);
            /* 1.11.31: the game keeps asking for this rebuild until its model matches (each refused try re-placed the
             * kept shorts: flicker). The Edit Player preview is rebuilt instead (as Hair Style live refresh does), so
             * model and cloth come back together for the new body size. */
            {   static u32 lastAsk; u32 rec = 0, prv = 0, now = GetTickCount();
                if (now - lastAsk > 400 && read32(0x015DD518u, &rec) && rec > 0x10000 && read32(0x01F8CB70u, &prv) && prv &&
                    player_cfid((u8*)player) == player_cfid((u8*)rec) && hl_queue_idle(prv, rec)) {
                    lastAsk = now;
                    *(volatile u32*)(prv + 0x138) = 1;
                    { static int lg2; if (lg2 < 8) { lg2++; blog("CLOTHSIM CFID %s: Edit Player preview rebuilt for the new body size", cfid_str(player_cfid((u8*)player))); } }
                }
            }
            return out;
        }
    }

    if (cfg_clothBridge && g_simFetch[kind]) {
        u32 cf;
        if (png_ready((u8*)player, &cf)) {
            PngMasks pm;
            if (!cfg_fastPath || !g_scanDone || !player_info((u8*)player, &pm) || pm.clothMask)
                cloth_common_ex((u8*)player, site, &fromPng, 1);
            /* 1.11.30-ML: practice reuses player records - a record can still point at the previous player's png
             * container (another CFID): his moving shorts were then built static. Only his own png counts. */
            if (fromPng && *(int*)0x1E85CD4 == 4) {
                u32 owner = name_cfid(cf), me = player_cfid((u8*)player);
                if (owner && me && owner != me) fromPng = 0;
                if (st_simPracLog < 16) { st_simPracLog++;
                    blog("CLOTHSIM practice: CFID %s %s, png container %s (CFID %u) -> %s", cfid_str(me), kGarment[kind],
                         container_name(cf), owner, fromPng ? "static (his own png)" : "moving (container belongs to another player)"); }
            }
        } else if (g_clothGetter[site]) {
            /* png not loaded yet (the normal case in games): decide from the png file on disk */
            void *R = ((ClothGetter_t)g_clothGetter[site])(player);
            if (png_predicts_cloth((u8*)player, site, R)) { fromPng = 2; }
        }
    }

    if (cfg_diagnostics) {
        EnterCriticalSection(&g_logLock);
        if (cfg_logEvery || !seen_before((u32)player ^ 0x10000000u, (u32)kind, slotVal, (u32)fromPng))
            blog("CLOTHSIM %-6s CFID=%-6s %s slot %d ClothMesh=%08X -> %s",
                 fromPng ? "OFF" : "keep", cfid_str(player_cfid((u8*)player)), kGarment[kind], idx, slotVal,
                 fromPng == 2 ? "static (png still loading; its file has this garment)" :
                 fromPng ? "static (model from own png)" : (slotVal ? "moving cloth" : "static (no ClothMesh in cloth file)"));
        LeaveCriticalSection(&g_logLock);
    }
    if (!fromPng)
        return sim_call(kind, player, style, out, pfn);

    EnterCriticalSection(&g_clothLock);          /* serialises the (rare) suppressed builds (2.1: own lock, not the data lock) */
    g_suppressTid = GetCurrentThreadId();
    r = sim_call(kind, player, style, out, pfn);
    g_suppressTid = 0;
    st_simBridged++;
    LeaveCriticalSection(&g_clothLock);
    return r;
}
static void *sim_timed(int k, void *p, u32 style, void *out) { u32 t0 = t_now(); void *r = sim_common(k, p, style, out); t_done(T_SIM, t0); return r; }
void *__fastcall ClothSim0(void *p, u32 style, void *out) { return sim_timed(0, p, style, out); }
static void sl_after_build(void *player, void *out);   /* shortlen.inc: Short Length on the new shorts cloth */
void *__fastcall ClothSim1(void *p, u32 style, void *out) { void *r = sim_timed(1, p, style, out); sl_after_build(p, r); return r; }

/* push ecx/ebx/ebp/esi/edi; mov edi,ecx; mov eax,[edi+1E8h]; mov esi,[eax+18h]; xor ebx,ebx; mov ebp,edx */
static const u8 kSimProlog[] = { 0x51,0x53,0x55,0x56,0x57,0x8B,0xF9,0x8B,0x87,0xE8,0x01,0x00,0x00,0x8B,0x70,0x18,0x33,0xDB,0x8B,0xEA };

static u32 getter_array(u32 getter)       /* "mov eax,[edx*4+imm]" inside a model getter */
{
    const u8 *p = (const u8*)getter; int i;
    for (i = 0; i < 0x40; i++) if (p[i] == 0x8B && p[i+1] == 0x04 && p[i+2] == 0x95) return *(u32*)(p + i + 3);
    return 0;
}

static void install_sim_hooks(u8 *text, u32 size)
{
    u8 *p, *end = text + size - 0x100;
    u32 jA = getter_array(g_clothGetter[0]), sA = getter_array(g_clothGetter[2]);
    int nf = 0, i, kind;
    if (!cfg_enabled || !cfg_cloth) { blog("cloth-sim bridge: uniforms disabled in ini"); return; }
    /* 1. find the two builders and their ClothMesh arrays */
    for (p = text; p < end && nf < 4; p++) {
        if (p[0] != 0x51 || p[1] != 0x53) continue;
        for (i = 0; i < (int)sizeof(kSimProlog); i++) if (p[i] != kSimProlog[i]) break;
        if (i != (int)sizeof(kSimProlog)) continue;
        {
            int k; u32 arr = 0;
            int fetchOff = 0;
            for (k = 0x40; k < 0x90; k++) if (p[k] == 0x8B && p[k+1] == 0x0C && p[k+2] == 0x85 && p[k+7] == 0xE8) { arr = *(u32*)(p + k + 3); fetchOff = k; break; }
            if (!arr) continue;
            /* ClothMesh array sits between the two model arrays of the same garment */
            kind = -1;
            if (jA && arr > jA - 0x100 && arr < jA + 0x100) kind = 0;
            if (sA && arr > sA - 0x100 && arr < sA + 0x100) kind = 1;
            if (kind < 0 || g_simFn[kind]) continue;
            g_simFn[kind] = (u32)p; g_simArr[kind] = arr; g_simFetch[kind] = (u32)p + fetchOff; nf++;
            blog("cloth builder         : %08X (%s, ClothMesh array %08X, fetch at %08X)", (u32)p, kGarment[kind], arr, g_simFetch[kind]);
        }
    }
    if (!g_simFn[0] && !g_simFn[1]) { blog("WARNING cloth builders not found - moving cloth stays on for png models"); return; }
    /* patch the 7-byte "mov ecx,[eax*4+array]" into "call ClothFetchN / nop / nop" */
    for (kind = 0; kind < 2; kind++) if (g_simFetch[kind] && cfg_clothBridge) {
        DWORD old; u8 *f = (u8*)g_simFetch[kind];
        u32 stub = kind ? (u32)&ClothFetch1 : (u32)&ClothFetch0;
        u8 b7[7];
        b7[0] = 0xE8; *(int*)(b7 + 1) = (int)(stub - ((u32)f + 5)); b7[5] = 0x90; b7[6] = 0x90;
        (void)old;
        if (!patch_bytes((u32)f, b7, 7, 1)) g_simFetch[kind] = 0;
    }
    /* 2. redirect every call to them */
    for (p = text; p < end; p++) {
        u32 t;
        if (p[0] != 0xE8) continue;
        t = call_target_of((u32)p);
        for (kind = 0; kind < 2; kind++) if (g_simFn[kind] && t == g_simFn[kind]) {
            if (patch_call((u32)p, kind ? (u32)&ClothSim1 : (u32)&ClothSim0)) g_simSites++;
        }
    }
    blog("CLOTH-SIM BRIDGE INSTALLED: %d call site(s), per-thread (shared cloth data is never modified).", g_simSites);
}

/* ---- keep pngs out of the shared uniform cache ---------------------------------------------- */
/* The cache is (re)built with the global search (0x00BDF89A, 0x00BE0122, 0x00BE023A). If a png */
/* with a uniform row sits earlier in the container list than the cloth file, EVERY player would */
/* get the png's model. These three lookups skip containers that contain "hihead" (= pngs).    */
static int g_noPngSites = 0;
static volatile u32 st_cacheNoPngSkips = 0;
static int node_is_png(u32 node)
{
    u32 vtbl, fn, out = 0;
    if (!read32(node, &vtbl) || !read32(vtbl + 0x0C, &fn)) return 0;
    return ((FindInContainer_t)fn)((void*)node, HASH_HIHEAD, TYPE_SCNE, &out) != 0;
}

void *__fastcall ResolveNoPng(void *mgr, u32 section, u32 container, u32 fileId, u32 type, u32 a4, u32 a5, u32 a6)
{
    u32 node, sentinel, n = 0;
    int inPng = 0;
    /* 1.11.24-ML: ClothMesh (moving cloth) too, and a file that only a png has is NOT taken into the shared
     * cache any more (before: the global search ran again and returned the png's copy for every player -
     * and kept it after the png was unloaded = freed memory in the shared cache, Edit Player crashes) */
    if (!cfg_enabled || !cfg_cacheNoPng || container != 0 || (type != TYPE_SCNE && type != TYPE_CLOTHMESH))
        return g_resolve(mgr, section, container, fileId, type, a4, a5, a6);
    sentinel = (u32)mgr + 0x1C;
    if (read32((u32)mgr + 0x28, &node)) {
        while (node && node != sentinel && n++ < 4096) {
            u32 vtbl, fn, hash, out = 0, next;
            if (!read32(node, &vtbl) || !read32(vtbl + 0x0C, &fn) || !read32(node + 0x28, &hash)) break;
            if (((FindInContainer_t)fn)((void*)node, fileId, type, &out)) {
                if (!node_is_png(node))
                    return g_resolve(mgr, section, hash, fileId, type, a4, a5, a6);
                inPng = 1;
            }
            if (!read32(node + 0x0C, &next)) break;
            node = next;
        }
    }
    if (inPng) {
        st_cacheNoPngSkips++;
        if (cfg_log) {
            EnterCriticalSection(&g_logLock);
            if (!seen_before(fileId ^ 0x0A000000u, type, 0, 0))
                blog("SHARED-CACHE GUARD  %08X (%s) is only in a png - not put in the shared cache (players keep their own png's copy)",
                     fileId, type == TYPE_SCNE ? "model" : "ClothMesh");
            LeaveCriticalSection(&g_logLock);
        }
        return 0;
    }
    return g_resolve(mgr, section, 0, fileId, type, a4, a5, a6);
}

/* the cache builders live right next to the model getters */
static void install_cache_hooks(void)
{
    u32 lo = 0xFFFFFFFFu, hi = 0, a;
    int i;
    if (!cfg_enabled || !cfg_cloth || !cfg_cacheNoPng) return;
    for (i = 0; i < 4; i++) if (g_clothGetter[i]) { if (g_clothGetter[i] < lo) lo = g_clothGetter[i]; if (g_clothGetter[i] > hi) hi = g_clothGetter[i]; }
    if (!hi) return;
    for (a = lo - 0x300; a < hi + 0xC00; a++) {
        const u8 *p = (const u8*)a;
        /* push 0 / mov edx,BB05A9C1 / mov ecx,mgr / call ResolveAsset */
        if (p[0] == 0x6A && p[1] == 0x00 && p[2] == 0xBA && *(u32*)(p + 3) == SEC_CPU && p[7] == 0xB9 &&
            *(u32*)(p + 8) == g_mgr && p[12] == 0xE8 && call_target_of(a + 12) == (u32)g_resolve) {
            if (patch_call(a + 12, (u32)&ResolveNoPng)) {
                g_noPngSites++;
                blog("uniform cache lookup  : %08X -> skips png containers", a + 12);
            }
        }
    }
    blog("SHARED-CACHE GUARD INSTALLED: %d lookup(s).", g_noPngSites);
}

/* cmp dword [2021728h],1 / jge / xor eax,eax / ret / mov eax,[ecx+208h] */
static int looks_like_cloth_getter(u32 fn, int *isShorts)
{
    const u8 *p = (const u8*)fn;
    if (!readable(p, 0x20)) return 0;
    if (!(p[0] == 0x83 && p[1] == 0x3D && p[6] == 0x01 && p[7] == 0x7D && p[8] == 0x03 && p[9] == 0x33 &&
          p[10] == 0xC0 && p[11] == 0xC3 && p[12] == 0x8B && p[13] == 0x81 && p[14] == 0x08 && p[15] == 0x02))
        return 0;
    /* jersey style needs 9 values: 4 bits of [uniform+4] (>>9 &0xF); shorts: 2 bits of [uniform+34h] (>>1E) */
    *isShorts = (p[0x16] == 0x8B && p[0x17] == 0x40 && p[0x18] == 0x34);
    return 1;
}

/* the four getter calls sit just before the body request inside the same function */
static void install_cloth_hooks(u32 bodySite)
{
    u8 *q;
    int jerseys = 0, shorts = 0;
    if (!cfg_enabled || !cfg_cloth) { blog("uniform models: disabled in ini"); return; }
    for (q = (u8*)bodySite - 0xC0; q < (u8*)bodySite; q++) {
        int isShorts, slot;
        u32 t;
        if (q[0] != 0xE8) continue;
        t = call_target_of((u32)q);
        if (!looks_like_cloth_getter(t, &isShorts)) continue;
        slot = isShorts ? 2 + shorts : jerseys;
        if (isShorts ? shorts >= 2 : jerseys >= 2) continue;
        g_clothGetter[slot] = t;
        {
            u32 fn = slot == 0 ? (u32)&ClothGet0 : slot == 1 ? (u32)&ClothGet1 : slot == 2 ? (u32)&ClothGet2 : (u32)&ClothGet3;
            if (patch_call((u32)q, fn)) {
                g_clothSites++;
                if (isShorts) shorts++; else jerseys++;
                blog("uniform call site     : %08X -> %s model getter %08X", (u32)q, isShorts ? "shorts" : "jersey", t);
            }
        }
        q += 4;
    }
    if (g_clothSites == 4) blog("UNIFORM HOOKS INSTALLED: jerseys %d, shorts %d. Per-cyberface jersey/shorts models are active.", jerseys, shorts);
    else blog("WARNING uniform hooks: only %d of 4 call sites found (jerseys %d, shorts %d)", g_clothSites, jerseys, shorts);
}


/* ------------------------------------------------------------------------------------------ */
/* thunks (MSVC-style naked functions: cl.exe or clang-cl)                                    */
/* ------------------------------------------------------------------------------------------ */
#if !defined(_MSC_VER)
#  error "BodyHook needs an MSVC-compatible compiler (cl.exe or clang-cl) for the naked thunks."
#endif

/* body call site: ecx = manager, edx = section hash, esi = player, 6 stack args, callee pops 0x18 */
__declspec(naked) void BodyLookupThunk(void)
{
    __asm {
        push esi
        push edx
        push ecx
        call BodyLookup
        add  esp, 12
        ret  0x18
    }
}

/* accessory material builder: [ebp+8] = player (the builder has a normal ebp frame) */
__declspec(naked) void AccTexThunk(void)
{
    __asm {
        push dword ptr [ebp + 8]
        push edx
        push ecx
        call AccTexLookup
        add  esp, 12
        ret  0x18
    }
}

/* sock colour site. The game was about to do "movaps xmm0,[esp+20h]" (A) or "[esp+30h]" (B)
 * and continue at g_sockCont. All general registers and xmm1..xmm7 are preserved. */
__declspec(naked) void SockBlueA(void)
{
    __asm {
        pushad
        sub    esp, 0x80
        movups xmmword ptr [esp + 0x10], xmm1
        movups xmmword ptr [esp + 0x20], xmm2
        movups xmmword ptr [esp + 0x30], xmm3
        movups xmmword ptr [esp + 0x40], xmm4
        movups xmmword ptr [esp + 0x50], xmm5
        movups xmmword ptr [esp + 0x60], xmm6
        movups xmmword ptr [esp + 0x70], xmm7
        mov    eax, esp
        push   eax
        push   dword ptr [ebp + 8]
        push   esi
        call   SockBlue
        add    esp, 12
        movups xmm1, xmmword ptr [esp + 0x10]
        movups xmm2, xmmword ptr [esp + 0x20]
        movups xmm3, xmmword ptr [esp + 0x30]
        movups xmm4, xmmword ptr [esp + 0x40]
        movups xmm5, xmmword ptr [esp + 0x50]
        movups xmm6, xmmword ptr [esp + 0x60]
        movups xmm7, xmmword ptr [esp + 0x70]
        test   eax, eax
        jnz    useA
        movups xmm0, xmmword ptr [esp + 0xC0]
        jmp    outA
    useA:
        movups xmm0, xmmword ptr [esp]
    outA:
        add    esp, 0x80
        popad
        jmp    dword ptr [g_sockCont]
    }
}
__declspec(naked) void SockBlueB(void)
{
    __asm {
        pushad
        sub    esp, 0x80
        movups xmmword ptr [esp + 0x10], xmm1
        movups xmmword ptr [esp + 0x20], xmm2
        movups xmmword ptr [esp + 0x30], xmm3
        movups xmmword ptr [esp + 0x40], xmm4
        movups xmmword ptr [esp + 0x50], xmm5
        movups xmmword ptr [esp + 0x60], xmm6
        movups xmmword ptr [esp + 0x70], xmm7
        mov    eax, esp
        push   eax
        push   dword ptr [ebp + 8]
        push   esi
        call   SockBlue
        add    esp, 12
        movups xmm1, xmmword ptr [esp + 0x10]
        movups xmm2, xmmword ptr [esp + 0x20]
        movups xmm3, xmmword ptr [esp + 0x30]
        movups xmm4, xmmword ptr [esp + 0x40]
        movups xmm5, xmmword ptr [esp + 0x50]
        movups xmm6, xmmword ptr [esp + 0x60]
        movups xmm7, xmmword ptr [esp + 0x70]
        test   eax, eax
        jnz    useB
        movups xmm0, xmmword ptr [esp + 0xD0]
        jmp    outB
    useB:
        movups xmm0, xmmword ptr [esp]
    outB:
        add    esp, 0x80
        popad
        jmp    dword ptr [g_sockCont]
    }
}

/* ------------------------------------------------------------------------------------------ */
/* patch helpers                                                                              */
/* ------------------------------------------------------------------------------------------ */
static u8 *g_textBase = 0;
static u32 g_textSize = 0;
static u32 g_exeTimestamp = 0, g_exeImageSize = 0, g_imageBase = 0;

static int in_text(u32 a) { return a >= (u32)g_textBase && a < (u32)g_textBase + g_textSize; }

/* Every change BodyHook makes to nba2k14.exe (code, tables, vtables, import slots) goes through SLAM's
   api->patch: registered with BodyHook as owner, checked against other mods' changes, original bytes kept
   for crash reports. Memory outside the exe (game objects, Direct3D, BodyHook's own) is written directly. */
static int patch_bytes(u32 addr, const void *src, u32 n, int code)
{
    DWORD old;
    if (addr >= g_imageBase && addr < g_imageBase + g_exeImageSize) {
        int r = g_slam->patch(g_slamMe, (void *)addr, src, n);
        if (r == SLAM_ERR_CONFLICT) blog("WARNING another mod already changed %08X (%u bytes) - BodyHook leaves it alone, see SLAM\\SLAM.log", addr, n);
        return r == SLAM_OK;
    }
    if (!VirtualProtect((LPVOID)addr, n, code ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE, &old)) return 0;
    memcpy((void*)addr, src, n);
    VirtualProtect((LPVOID)addr, n, old, &old);
    if (code) FlushInstructionCache(GetCurrentProcess(), (LPCVOID)addr, n);
    return 1;
}
static int patch_jmp(u32 site, u32 target)
{
    u8 b[5];
    int rel = (int)(target - (site + 5));
    b[0] = 0xE9; memcpy(b + 1, &rel, 4);
    return patch_bytes(site, b, 5, 1);
}
static int match(const u8 *p, const u8 *pat, u32 n)
{
    u32 i;
    for (i = 0; i < n; i++) if (p[i] != pat[i]) return 0;
    return 1;
}
static u8 *find_text(const u8 *pat, u32 n, u8 *from)
{
    u8 *p = from ? from : g_textBase, *end = g_textBase + g_textSize - n;
    for (; p < end; p++) if (p[0] == pat[0] && match(p, pat, n)) return p;
    return 0;
}

/* walk readable, non-code pages of the exe image (vtables, tables) */
typedef int (*PageCb)(u32 lo, u32 hi, void *ctx);
static void for_data_pages(PageCb cb, void *ctx)
{
    u32 a = g_imageBase + 0x1000, end = g_imageBase + g_exeImageSize;
    MEMORY_BASIC_INFORMATION mbi;
    while (a < end) {
        u32 lo, hi;
        if (!VirtualQuery((LPCVOID)a, &mbi, sizeof(mbi))) break;
        lo = a; hi = (u32)mbi.BaseAddress + (u32)mbi.RegionSize;
        if (hi > end) hi = end;
        if (mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) && (mbi.Protect & 0xEE) &&
            !(lo < (u32)g_textBase + g_textSize && hi > (u32)g_textBase))
            if (cb(lo, hi, ctx)) return;
        a = hi;
    }
}

/* ------------------------------------------------------------------------------------------ */
/* install: accessories                                                                        */
/* ------------------------------------------------------------------------------------------ */
static u32 g_accBuilder = 0;

static int vt_cb(u32 lo, u32 hi, void *ctx)
{
    u32 a;
    (void)ctx;
    for (a = (lo + 3) & ~3u; a + 12 <= hi && g_accVtCount < 32; a += 4) {
        u32 *v = (u32*)a;
        if (v[1] != g_accBuilder || !in_text(v[0]) || !in_text(v[2])) continue;
        g_accVt[g_accVtCount] = a;
        g_accVis[g_accVtCount] = v[2];
        {
            u32 nv = (u32)&AccVisible;
            if (patch_bytes(a + 8, &nv, 4, 0)) g_accVtCount++;
        }
    }
    return g_accVtCount >= 32;
}

static void install_acc_hooks(void)
{
    static const u8 kProlog[] = { 0x55,0x8B,0xEC,0x83,0xE4,0xF0,0x83,0xEC,0x74,0x83,0x7D,0x0C,0x00,0x53,0x56,0x57,0x8B,0xFA,0x8B,0xF1 };
    u8 *b, *p, *q;
    if (!cfg_enabled || !cfg_acc) { blog("accessories: disabled in ini"); return; }
    b = find_text(kProlog, sizeof(kProlog), 0);
    if (b) {
        g_accBuilder = (u32)b;
        for (p = b; p < b + 0x140; p++) {
            u32 site;
            if (!(p[0] == 0x6A && p[1] == 0x00 && p[2] == 0xBA && *(u32*)(p + 3) == SEC_CPU && p[7] == 0xB9 && *(u32*)(p + 8) == g_mgr))
                continue;
            q = p + 12;
            if (q[0] == 0x89 && q[1] == 0x44 && q[2] == 0x24) q += 4;
            if (q[0] != 0xE8 || call_target_of((u32)q) != (u32)g_resolve) continue;
            site = (u32)q;
            if (patch_call(site, (u32)&AccTexThunk)) {
                g_accSites++;
                blog("accessory texture site: %08X -> AccTexLookup (material builder %08X)", site, (u32)b);
            }
        }
    } else blog("WARNING accessory material builder not found - accessory textures stay stock");

    /* headband and anything else fetched with the asset helper: push id / push TXTR / xor edx,edx / call helper */
    if (g_assetHelper) {
        u8 *end = g_textBase + g_textSize - 16;
        for (p = g_textBase; p < end; p++) {
            int kind;
            if (p[0] != 0x68 || p[5] != 0x68 || *(u32*)(p + 6) != TYPE_TXTR || p[10] != 0x33 || p[11] != 0xD2 || p[12] != 0xE8) continue;
            if (acc_index(*(u32*)(p + 1), &kind) < 0) continue;
            if (call_target_of((u32)(p + 12)) != (u32)g_assetHelper) continue;
            if (patch_call((u32)(p + 12), (u32)&AccHelperLookup)) {
                g_accHelperSites++;
                blog("accessory %s site: %08X -> AccHelperLookup (FileID %08X)", kind == 2 ? "normal-map" : "texture",
                     (u32)(p + 12), *(u32*)(p + 1));
            }
        }
    }
    blog("ACCESSORY TEXTURE HOOKS INSTALLED: %d site(s) + %d helper site(s), %d texture FileIDs, %d normal-map FileIDs%s.",
         g_accSites, g_accHelperSites, cfg_accTexCount, cfg_accNrmCount, cfg_accNormals ? "" : " (NormalMaps=0)");

    if (cfg_accCombos && g_accBuilder) {
        for_data_pages(vt_cb, 0);
        blog("ACCESSORY COMBINATIONS INSTALLED: %d accessory group(s). Accessories in different slots no longer hide each other.", g_accVtCount);
    } else blog("accessory combinations: %s", cfg_accCombos ? "not available" : "off (AllowCombinations=0)");
}

/* ------------------------------------------------------------------------------------------ */
/* install: coach polo textures, coach arms                                                    */
/* ------------------------------------------------------------------------------------------ */
static int g_poloSites = 0, g_armTable = 0;
static int arm_cb(u32 lo, u32 hi, void *ctx)
{
    static const u8 pat[] = { 0xDA,0xC9,0x53,0xD7,0x01,0,0,0, 0x81,0x78,0x34,0xEE,0x01,0,0,0 };
    u32 a, three = 3;
    (void)ctx;
    for (a = (lo + 3) & ~3u; a + 16 <= hi; a += 4)
        if (match((const u8*)a, pat, 16)) {
            if (patch_bytes(a + 4, &three, 4, 0) && patch_bytes(a + 12, &three, 4, 0)) g_armTable = 1;
            return 1;
        }
    return 0;
}

static void install_polo(void)
{
    static const u8 kTex[] = { 0x68,0xEB,0xA6,0x20,0xB1 }, kNrm[] = { 0x68,0xAE,0xFC,0x2F,0x80 };
    u8 *p;
    if (!cfg_enabled || !cfg_polo) { blog("coach polo: off (CoachPolo Enabled=0)"); return; }
    for (p = find_text(kTex, 5, 0); p; p = find_text(kTex, 5, p + 1))
        if (patch_bytes((u32)p + 1, &cfg_poloTex, 4, 1)) g_poloSites++;
    for (p = find_text(kNrm, 5, 0); p; p = find_text(kNrm, 5, p + 1))
        if (patch_bytes((u32)p + 1, &cfg_poloNrm, 4, 1)) g_poloSites++;
    if (cfg_poloArms) for_data_pages(arm_cb, 0);
    blog("COACH POLO INSTALLED: %d texture site(s) (colorize %08X, normal %08X), coach arms %s",
         g_poloSites, cfg_poloTex, cfg_poloNrm, !cfg_poloArms ? "off" : g_armTable ? "on" : "NOT FOUND");
}

static void install_arms(void)
{
    static const u8 kMask[] = { 0x83,0xEC,0x08,0x53,0x8B,0xD9,0x56,0x8B,0xF2,0x85,0xDB,0x0F,0x84,0x17,0x01,0x00,0x00,
                                0x55,0x57,0x6A,0x01,0xBA,0xFF,0xFF,0x01,0x00,0xE8 };
    u8 *m, *p, *end;
    if (!cfg_enabled || !cfg_poloArms) return;
    m = find_text(kMask, sizeof(kMask), 0);
    if (!m) { blog("WARNING coach arms: code not found"); return; }
    g_segMask = call_target_of((u32)m + 0x1A);
    g_armMaskFn = (u32)m;
    end = g_textBase + g_textSize - 0x20;
    for (p = g_textBase; p < end; p++) {
        if (p[0] != 0xE8 || call_target_of((u32)p) != (u32)m) continue;
        if (patch_call((u32)p, (u32)&ArmsMask)) g_armSites++;
    }
    /* the arm skins must also be in the coach draw pass (outfit table: D753C9DA / EE347881 bits 1 -> 3),
     * which 1.9.x only did together with the polo */
    if (!g_armTable) for_data_pages(arm_cb, 0);
    blog("COACH ARMS INSTALLED: %d site(s), arm mask %08X, arm skins in the coach pass: %s (coaches only, players untouched)",
         g_armSites, (u32)m, g_armTable ? "yes" : "NOT FOUND");
}

/* ------------------------------------------------------------------------------------------ */
/* NBA PATCH only on headbands: every other request for unif_nbapatch (0DAE36BF) - jersey /     */
/* shorts / sock materials, the sock accessory decals, menus - gets all_black (D43CE53B), the  */
/* texture the game itself uses for "no logo". The headband logo site (mov eax,0DAE36BF right   */
/* after "and edx,700h") is left alone.                                                        */
/* ------------------------------------------------------------------------------------------ */
#define TX_NBAPATCH 0x0DAE36BFu
#define TX_ALLBLACK 0xD43CE53Bu
static int nbapatch_cb(u32 lo_, u32 hi_, void *ctx)
{
    int *n = (int*)ctx;
    u32 a;
    for (a = ((lo_ + 3) & ~3u) + 0x14; a + 4 <= hi_; a += 4) {
        u32 *q = (u32*)a;
        u32 nv = TX_ALLBLACK;
        /* accessory record {vtable, index, material, texture, material row, decal...}: decal at +0x14 */
        if (q[0] != TX_NBAPATCH || q[-4] >= 106) continue;
        if (q[-5] < g_imageBase || q[-5] >= g_imageBase + g_exeImageSize || in_text(q[-5])) continue;
        if (q[-2] == 0x46A48CA9u) continue;                           /* headband record: keep */
        if (patch_bytes(a, &nv, 4, 0)) (*n)++;
    }
    return 0;
}
static void install_nbapatch(void)
{
    u8 *p, *end;
    int code = 0, data = 0, kept = 0;
    u32 nv = TX_ALLBLACK, zero = 0;
    if (!cfg_enabled || !cfg_nbaPatchHeadbandOnly) return;
    end = g_textBase + g_textSize - 16;
    for (p = g_textBase + 8; p < end; p++) {
        if (*(u32*)(p + 1) != TX_NBAPATCH && *(u32*)(p + 4) != TX_NBAPATCH && *(u32*)(p + 2) != TX_NBAPATCH) continue;
        if (p[0] == 0xB8 && *(u32*)(p + 1) == TX_NBAPATCH) {                    /* mov eax, imm */
            if (p[-6] == 0x81 && p[-5] == 0xE2 && *(u32*)(p - 4) == 0x700) { kept++; continue; }   /* headband logo */
            if (patch_bytes((u32)p + 1, &nv, 4, 1)) code++;
        } else if ((p[0] == 0x68 || p[0] == 0xBF || p[0] == 0xBE || p[0] == 0xBB) && *(u32*)(p + 1) == TX_NBAPATCH) {
            if (patch_bytes((u32)p + 1, &nv, 4, 1)) code++;                    /* push / mov edi|esi|ebx, imm */
        } else if (p[0] == 0xC7 && p[1] == 0x44 && p[2] == 0x24 && *(u32*)(p + 4) == TX_NBAPATCH) {
            if (patch_bytes((u32)p + 4, &nv, 4, 1)) code++;                    /* mov [esp+x], imm */
        } else if (p[0] == 0x81 && p[1] == 0xC3 && *(u32*)(p + 2) == TX_NBAPATCH &&
                   p[-6] == 0x81 && p[-5] == 0xE3 && *(u32*)(p - 4) == TX_ALLBLACK - TX_NBAPATCH) {
            /* ebx = flag ? all_black : nbapatch  ->  always all_black */
            if (patch_bytes((u32)p - 4, &zero, 4, 1) && patch_bytes((u32)p + 2, &nv, 4, 1)) code++;
        }
    }
    for_data_pages(nbapatch_cb, &data);
    blog("NBA PATCH HEADBAND-ONLY INSTALLED: %d code site(s) and %d accessory decal(s) now use all_black, "
         "%d headband site(s) kept", code, data, kept);
}

/* ------------------------------------------------------------------------------------------ */
/* install: team colour socks                                                                  */
/* ------------------------------------------------------------------------------------------ */
static int sockvt_cb(u32 lo, u32 hi, void *ctx)
{
    u32 a;
    (void)ctx;
    for (a = (lo + 3) & ~3u; a + 4 <= hi; a += 4)
        if (*(u32*)a == g_sockPicker && a - 0x10 >= lo) {
            u32 vt = a - 0x10;
            if (g_accBuilder && ((u32*)vt)[1] != g_accBuilder) continue;
            g_sockVtable = vt;
            return 1;
        }
    return 0;
}

static void install_socks(void)
{
    static const u8 kPick[] = { 0x8B,0x41,0x04,0x83,0xEC,0x08,0x83,0xF8,0x2A,0x74,0x42,0x83,0xF8,0x32,0x74,0x3D };
    static const u8 kCol[]  = { 0x8B,0x46,0x04,0x83,0xC0,0xD6,0x83,0xF8,0x15,0x77,0x15,0x0F,0xB6,0x80 };
    static const u8 kTeam[] = { 0x8B,0x89,0x08,0x02,0x00,0x00,0x85,0xC9,0x75,0x09,0x0F,0x28,0x05 };
    u8 *pick, *col = 0, *p, *lo, *hi;
    u32 best = 0, bestFallback = 0;
    if (!cfg_enabled || !cfg_socks) { blog("team colour socks: off (TeamColorSocks Enabled=0)"); return; }
    pick = find_text(kPick, sizeof(kPick), 0);
    for (p = find_text(kCol, sizeof(kCol), 0); p; p = find_text(kCol, sizeof(kCol), p + 1))
        if (p[0x18] == 0 && p[0x19] == 0x0F && p[0x1A] == 0x28 && p[0x1B] == 0x44 && p[0x1D] == 0x20 &&
            p[0x20] == 0x0F && p[0x21] == 0x28 && p[0x22] == 0x44 && p[0x24] == 0x30) { col = p; break; }
    if (!pick || !col) { blog("WARNING team colour socks: code not found (picker %08X, colour site %08X) - socks stay stock", (u32)pick, (u32)col); return; }
    g_sockPicker = (u32)pick;
    /* team colour 2 getter: the second "mov ecx,[ecx+208h] / test / jne / movaps xmm0,[fallback] ... call" near the colour code */
    lo = col - 0x800 > g_textBase ? col - 0x800 : g_textBase;
    hi = col + 0x800;
    for (p = find_text(kTeam, sizeof(kTeam), lo); p && p < hi; p = find_text(kTeam, sizeof(kTeam), p + 1)) {
        u32 fb, fn;
        if (p[0x13] != 0xE8 || p[0x1A] != 0xC1 || p[0x20] != 0xF3 || p[0x21] != 0x0F || p[0x22] != 0x10) continue;
        fb = *(u32*)(p + 0x0D); fn = call_target_of((u32)p + 0x13);
        if (fn == 0x00CBB280u) { g_team1 = fn; continue; }             /* team colour 1 in the RLD build */
        if (fn == 0x00CBB2E0u) { best = (u32)p; break; }              /* team colour 2 in the RLD build */
        if (!best || fb > bestFallback) { best = (u32)p; bestFallback = fb; }   /* team 2's fallback colour follows team 1's */
    }
    if (best) { g_team2 = call_target_of(best + 0x13); g_linTable = *(u32*)(best + 0x25); }
    for_data_pages(sockvt_cb, 0);
    if (!g_sockVtable) { blog("WARNING team colour socks: sock class not found - socks stay stock"); return; }
    g_sockCont = (u32)col + 0x25;
    {
        u32 nv = (u32)&SockColors;
        if (!patch_bytes(g_sockVtable + 0x10, &nv, 4, 0) || !patch_jmp((u32)col + 0x19, (u32)&SockBlueA) ||
            !patch_jmp((u32)col + 0x20, (u32)&SockBlueB)) {
            blog("WARNING team colour socks: patch failed");
            return;
        }
    }
    if (!g_team1 && g_team2 == 0x00CBB2E0u && readable((void*)0x00CBB280u, 4)) g_team1 = 0x00CBB280u;
    g_tcVt[0] = g_sockVtable; g_tcPick[0] = g_sockPicker; g_tcVtCount = 1;
    if (cfg_tcAccCount) {
        /* [TeamColorSocks] Accessories: every other accessory class gets the same colour wrapper */
        int i, done = 0;
        for (i = 0; i < g_accVtCount && g_tcVtCount < 32; i++) {
            u32 vt = g_accVt[i], pk;
            if (vt == g_sockVtable || !readable((void*)(vt + 0x10), 4)) continue;
            pk = *(u32*)(vt + 0x10);
            if (!in_text(pk)) continue;
            g_tcVt[g_tcVtCount] = vt; g_tcPick[g_tcVtCount] = pk;
            {
                u32 nv = (u32)&TeamColors;
                if (patch_bytes(vt + 0x10, &nv, 4, 0)) { g_tcVtCount++; done++; }
            }
        }
        blog("TEAM COLOUR ACCESSORIES: %d texture(s) listed, %d accessory class(es) hooked%s", cfg_tcAccCount, done,
             g_accVtCount ? "" : " (needs [Accessories] Enabled=1)");
    }
    blog("TEAM COLOUR SOCKS INSTALLED: style %d (%s), %d sock texture(s), DetectStockMasks=%d, picker %08X, class %08X, "
         "colour site %08X, team1 %08X, team2 %08X",
         cfg_sockStyle, cfg_sockStyle == 1 ? "red=team 1, green=team 2, blue=sock colour" : "red=sock colour, green=team 1, blue=team 2",
         cfg_sockCount, cfg_tcDetect, g_sockPicker, g_sockVtable, (u32)col, g_team1, g_team2);
}

/* ------------------------------------------------------------------------------------------ */
/* locating the body call site                                                                */
/* ------------------------------------------------------------------------------------------ */
static void find_text_section(void)
{
    u8 *base = (u8*)GetModuleHandleA(NULL);
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER*)base;
    IMAGE_FILE_HEADER *fh = (IMAGE_FILE_HEADER*)(base + dos->e_lfanew + 4);
    IMAGE_SECTION_HEADER *sh = (IMAGE_SECTION_HEADER*)((u8*)fh + sizeof(IMAGE_FILE_HEADER) + fh->SizeOfOptionalHeader);
    int i;
    g_imageBase = (u32)base;
    g_exeTimestamp = fh->TimeDateStamp;
    g_exeImageSize = *(u32*)((u8*)fh + sizeof(IMAGE_FILE_HEADER) + 56);
    g_imgLo = (u32)base + 0x1000; g_imgHi = (u32)base + g_exeImageSize;
    for (i = 0; i < fh->NumberOfSections; i++) {
        if (sh[i].Name[0] == '.' && sh[i].Name[1] == 't' && sh[i].Name[2] == 'e' && sh[i].Name[3] == 'x') {
            g_textBase = base + sh[i].VirtualAddress;
            g_textSize = sh[i].Misc.VirtualSize;
            return;
        }
    }
}

static const short kPattern[] = {
    0x8B,0x86,-1,-1,-1,-1, 0x6A,0x00, 0xC1,0xE0,-1, 0x6A,0x00, 0xC1,0xF8,-1,
    0x8B,0x04,0x85,-1,-1,-1,-1, 0x8B,0x0C,0x85,-1,-1,-1,-1,
    0x6A,0x00, 0x68,0x5D,0x9B,0x6C,0xE2, 0x51, 0x6A,0x00, 0xBA,0xC1,0xA9,0x05,0xBB,
    0xB9,-1,-1,-1,-1, 0xE8
};
#define PATTERN_LEN ((int)(sizeof(kPattern) / sizeof(kPattern[0])))
#define PATTERN_CALL_OFFSET (PATTERN_LEN - 1)

static int match_at(const u8 *p)
{
    int i;
    for (i = 0; i < PATTERN_LEN; i++)
        if (kPattern[i] >= 0 && p[i] != (u8)kPattern[i]) return 0;
    return 1;
}

static int looks_like_resolver(u32 fn)
{
    const u8 *p = (const u8*)fn;
    if (!readable(p, 8)) return 0;
    return p[0] == 0x56 && p[1] == 0x8B && p[2] == 0xF2 && p[3] == 0x8B && p[4] == 0x54 && p[5] == 0x24 && p[6] == 0x10;
}

static u32 locate_callsite(int fullScan)
{
    u32 site = KNOWN_CALLSITE;
    u8 *start = (u8*)(site - PATTERN_CALL_OFFSET);
    if (start >= g_textBase && (u8*)site + 5 <= g_textBase + g_textSize && readable(start, PATTERN_LEN + 4) &&
        match_at(start) && looks_like_resolver(call_target_of(site)))
        return site;
    if (fullScan && g_textBase && readable(g_textBase, 0x1000)) {
        u8 *p = g_textBase, *end = g_textBase + g_textSize - PATTERN_LEN;
        for (; p < end; p++) {
            if (p[0] == 0x8B && p[1] == 0x86 && match_at(p)) {
                u32 s = (u32)(p + PATTERN_CALL_OFFSET);
                if (looks_like_resolver(call_target_of(s))) return s;
            }
        }
    }
    return 0;
}

static int install_hook(u32 site)
{
    g_resolve = (ResolveAsset_t)call_target_of(site);
    g_callSite = site;
    if (!patch_call(site, (u32)&BodyLookupThunk)) { blog("ERROR VirtualProtect failed at %08X", site); return 0; }
    g_hooked = 1;
    return 1;
}

/* ------------------------------------------------------------------------------------------ */
/* loose png scan (startup)                                                                   */
/* ------------------------------------------------------------------------------------------ */
/* PERF (1.11.13 / 1.19.2): the row info of every png is kept in BodyHook_pngcache.bin (name, size,
 * write time, rows). At start only pngs that are new or changed since the last run are opened - the
 * folder listing already has size and time - so a big png folder no longer means reading thousands
 * of files while the game loads. The scan runs at background (low) I/O priority and holds no lock
 * while it reads. */
#define PC_MAGIC 0x31435042u     /* "BPC1" */
typedef struct { char file[64]; u32 size, wlo, whi; PngMasks m; } PcRec;
static PcRec g_pcBuf[MAX_PNGS];  /* read at start, reused to write the new cache after the scan */
static PcRec *g_pc = 0; static u32 g_pcN = 0;
static u16 g_pcIndex[65536];     /* cfid -> cache record + 1 */
typedef BOOL (WINAPI *BhSetPrio_t)(HANDLE, int);

static u32 pc_signature(void)
{
    u32 c = 0x811C9DC5u; int i, g;
#define PCMIX(v) (c = (c ^ (u32)(v)) * 0x01000193u)
    PCMIX(sizeof(PcRec)); PCMIX(cfg_legCount); PCMIX(cfg_accTexCount); PCMIX(cfg_accNrmCount); PCMIX(cfg_extraCount); PCMIX(cfg_clothGroups);
    for (i = 0; i < 4; i++) PCMIX(kBodyIds[i]);
    for (i = 0; i < cfg_legCount; i++) PCMIX(cfg_legIds[i]);
    for (i = 0; i < cfg_accTexCount; i++) PCMIX(cfg_accTex[i]);
    for (i = 0; i < cfg_accNrmCount; i++) PCMIX(cfg_accNrm[i]);
    for (i = 0; i < cfg_extraCount; i++) PCMIX(cfg_extraIds[i]);
    for (g = 0; g < cfg_clothGroups && g < 4; g++) { PCMIX(cfg_clothCount[g]); for (i = 0; i < cfg_clothCount[g] && i < 8; i++) PCMIX(cfg_clothIds[g][i]); }
#undef PCMIX
    return c;
}

/* 1.11.17-ML: own file name - the 2K14-only edition (1.19.x) keeps its own cache, so switching between the
 * two editions no longer throws the other one's cache away (a full png re-read at start) */
static void pc_path(char *p) { s_cpy(p, g_modDir, MAX_PATH); s_cat(p, "BodyHook_pngcache_ML.bin", MAX_PATH); }

static void pc_load(void)
{
    PcRec *recs = g_pcBuf;
    char p[MAX_PATH]; HANDLE f; DWORD got = 0; u32 hdr[4], i;
    g_pc = recs; g_pcN = 0;
    pc_path(p);
    f = CreateFileA(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    if (ReadFile(f, hdr, sizeof(hdr), &got, NULL) && got == sizeof(hdr) && hdr[0] == PC_MAGIC && hdr[1] == pc_signature() &&
        hdr[2] <= MAX_PNGS && ReadFile(f, recs, hdr[2] * sizeof(PcRec), &got, NULL) && got == hdr[2] * sizeof(PcRec)) {
        g_pcN = hdr[2];
        for (i = 0; i < g_pcN; i++)
            if (recs[i].m.cfid <= 0xFFFF) g_pcIndex[recs[i].m.cfid] = (u16)(i + 1);
    }
    CloseHandle(f);
}

static void pc_save(void)
{
    PcRec *out = g_pcBuf;            /* the scan is done: the old cache records are not needed any more */
    char p[MAX_PATH]; HANDLE f; DWORD w; u32 hdr[4]; int i, n = 0;
    EnterCriticalSection(&g_logLock);
    for (i = 0; i < g_pngCount && n < MAX_PNGS; i++) {
        PngEntry *e = &g_pngs[i];
        if (!e->size && !e->wlo && !e->whi) continue;
        s_cpy(out[n].file, e->file, 64); out[n].size = e->size; out[n].wlo = e->wlo; out[n].whi = e->whi; out[n].m = e->m;
        n++;
    }
    LeaveCriticalSection(&g_logLock);
    pc_path(p);
    f = CreateFileA(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    hdr[0] = 0; hdr[1] = pc_signature(); hdr[2] = (u32)n; hdr[3] = 0;
    WriteFile(f, hdr, sizeof(hdr), &w, NULL);
    WriteFile(f, out, (DWORD)n * sizeof(PcRec), &w, NULL);
    hdr[0] = PC_MAGIC;                                     /* valid only once everything was written */
    SetFilePointer(f, 0, NULL, 0);
    WriteFile(f, hdr, 4, &w, NULL);
    CloseHandle(f);
}

static void bg_io(int on)
{
    static BhSetPrio_t sp = 0;
    if (!sp) sp = (BhSetPrio_t)GetProcAddress(GetModuleHandleA("kernel32.dll"), "SetThreadPriority");
    if (sp) sp((HANDLE)(long)-2 /* current thread */, on ? 0x00010000 /* THREAD_MODE_BACKGROUND_BEGIN */ : 0x00020000 /* ..._END */);
}

static void scan_loose_pngs(void)
{
    WIN32_FIND_DATAA fd;
    char pat[MAX_PATH];
    HANDLE h;
    int total = 0, legPngs = 0, clothPngs = 0, bodyPngs = 0, accPngs = 0, read = 0, cached = 0;
    DWORD t0 = GetTickCount();
    bg_io(1);
    pc_load();
    s_cpy(pat, g_dir, MAX_PATH); s_cat(pat, "png*.iff", MAX_PATH);
    h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) { blog("loose png scan: no png*.iff files next to the exe"); g_scanDone = 1; bg_io(0); return; }
    do {
        const char *n = fd.cFileName;
        u32 cfid = 0; int k = 3, digits = 0;
        PngEntry *e;
        PcRec *c;
        total++;
        while (n[k] >= '0' && n[k] <= '9') { cfid = cfid * 10 + (u32)(n[k] - '0'); k++; digits++; }
        if (!digits || !s_ieq(n + k, ".iff") || cfid > 0xFFFF) continue;
        if (g_pngIndex[cfid]) continue;                    /* png0195.iff and png195.iff: the first one counts */
        {
            WIN32_FILE_ATTRIBUTE_DATA fa;
            memset(&fa, 0, sizeof(fa));
            fa.ftLastWriteTime = fd.ftLastWriteTime; fa.nFileSizeLow = fd.nFileSizeLow;
            c = g_pcIndex[cfid] ? &g_pc[g_pcIndex[cfid] - 1] : 0;
            if (c && s_ieq(c->file, n) && c->size == fd.nFileSizeLow &&
                c->wlo == fd.ftLastWriteTime.dwLowDateTime && c->whi == fd.ftLastWriteTime.dwHighDateTime) {
                e = png_add_loaded(cfid, n, &fa, &c->m); cached++;
            } else {
                e = png_add_loaded(cfid, n, &fa, 0); read++;
            }
        }
        if (!e) continue;
        if (e->m.bodyMask) bodyPngs++;
        if (e->m.legMask) legPngs++;
        if (e->m.clothMask) clothPngs++;
        if (e->m.accMask[0] || e->m.accMask[1] || e->m.nrmMask[0] || e->m.nrmMask[1] || e->m.nrmMask[2] || e->m.nrmMask[3]) accPngs++;
        if (masks_any(&e->m) || e->m.unsorted) log_png_rows(e->file, &e->m, "");
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    g_scanDone = 1;
    bg_io(0);
    blog("loose png scan: %d png*.iff file(s), %d with their own body, %d with leg tattoos, %d with uniform models, "
         "%d with accessory textures/normal maps. LivePngRefresh=%d", total, bodyPngs, legPngs, clothPngs, accPngs, cfg_liveRefresh);
    blog("loose png scan: %u ms, %d png(s) read, %d from BodyHook_pngcache_ML.bin (unchanged since the last run)",
         GetTickCount() - t0, read, cached);
    g_pcN = 0; g_pc = 0;
    if (read) pc_save();
}


/* ------------------------------------------------------------------------------------------ */
/* PNG WATCHER (1.11.13 / 1.19.2, LivePngRefresh=1)                                            */
/* Sees pngs saved / added while the game runs. Windows tells it which files changed            */
/* (ReadDirectoryChangesW), so it costs nothing while nothing changes. Where Windows can't do   */
/* that (some network / FAT drives) it checks, once a second, only the pngs the game used in    */
/* the last minute. Either way: in the background, never on a game thread.                      */
/* ------------------------------------------------------------------------------------------ */
typedef struct { ULONG_PTR Internal, InternalHigh; DWORD Offset, OffsetHigh; HANDLE hEvent; } BhOverlapped;
typedef BOOL (WINAPI *BhRdc_t)(HANDLE, LPVOID, DWORD, BOOL, DWORD, LPDWORD, BhOverlapped *, LPVOID);
typedef BOOL (WINAPI *BhGor_t)(HANDLE, BhOverlapped *, LPDWORD, BOOL);
typedef BOOL (WINAPI *BhResetEvent_t)(HANDLE);
static struct { char name[64]; u32 tick; } g_pend[64];

static void watch_note(const char *name)
{
    int i, free_ = -1;
    for (i = 0; i < 64; i++) {
        if (g_pend[i].name[0] && s_ieq(g_pend[i].name, name)) { g_pend[i].tick = GetTickCount(); return; }
        if (!g_pend[i].name[0] && free_ < 0) free_ = i;
    }
    if (free_ < 0) free_ = (int)(GetTickCount() & 63);
    s_cpy(g_pend[free_].name, name, 64); g_pend[free_].tick = GetTickCount();
}

/* png<digits>[_tag].iff -> cfid (0 = not a png), *plain = no hair tag */
static u32 watch_cfid(const char *n, int *plain)
{
    u32 cfid = 0; int k = 3, digits = 0;
    if (!((n[0] == 'p' || n[0] == 'P') && (n[1] == 'n' || n[1] == 'N') && (n[2] == 'g' || n[2] == 'G'))) return 0;
    while (n[k] >= '0' && n[k] <= '9') { cfid = cfid * 10 + (u32)(n[k] - '0'); k++; digits++; }
    if (!digits || cfid > 0xFFFF) return 0;
    *plain = s_ieq(n + k, ".iff");
    if (!*plain) { int l = s_len(n); if (n[k] != '_' || l < 5 || !s_ieq(n + l - 4, ".iff")) return 0; }
    return cfid;
}

/* a png changed or appeared: read it again (the old LivePngRefresh behaviour, now off the game threads) */
static void watch_refresh(const char *name)
{
    int plain;
    u32 cfid = watch_cfid(name, &plain);
    PngEntry *e;
    if (!cfid) return;
    e = g_pngIndex[cfid] ? &g_pngs[g_pngIndex[cfid] - 1] : 0;
    if (e) {
        if (!s_ieq(e->file, name)) return;          /* not the file this player uses (other hair variant) */
        if (png_load(e)) {
            st_refresh++;
            nowait_clear();
            blog("PNG CHANGED  %s was saved while the game runs - its custom rows were read again", e->file);
            log_png_rows(e->file, &e->m, "now ");
        }
        e->checked = GetTickCount();
        return;
    }
    if (!plain) return;
    {
        char path[MAX_PATH];
        s_cpy(path, g_dir, MAX_PATH); s_cat(path, name, MAX_PATH);
        if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) return;
    }
    e = png_add_loaded(cfid, name, 0, 0);
    if (!e) return;
    nowait_clear();
    blog("PNG ADDED    %s appeared while the game runs", e->file);
    log_png_rows(e->file, &e->m, "now ");
}

/* polling fallback: the pngs used in the last minute + CFIDs the game asked for without a png */
static void watch_poll(void)
{
    static u32 wantTick[WANT_MAX];
    u32 now = GetTickCount();
    int i;
    for (i = 0; i < g_pngCount; i++) {
        PngEntry *e = &g_pngs[i];
        if (!e->used || now - e->used > 60000 || now - e->checked < 1000) continue;
        e->checked = now;
        if (png_load(e)) {
            st_refresh++;
            nowait_clear();
            blog("PNG CHANGED  %s was saved while the game runs - its custom rows were read again", e->file);
            log_png_rows(e->file, &e->m, "now ");
        }
    }
    for (i = 0; i < WANT_MAX; i++) {
        u32 cfid = g_want[i];
        if (!cfid || g_pngIndex[cfid] || now - wantTick[i] < 3000) continue;
        wantTick[i] = now;
        png_lookup_disk(cfid);
    }
}

static DWORD WINAPI png_watch_thread(LPVOID unused)
{
    static DWORD buf[4096];                       /* 16 KB of FILE_NOTIFY_INFORMATION records */
    HMODULE k = GetModuleHandleA("kernel32.dll");
    BhRdc_t rdc = (BhRdc_t)GetProcAddress(k, "ReadDirectoryChangesW");
    BhGor_t gor = (BhGor_t)GetProcAddress(k, "GetOverlappedResult");
    BhCreateEvent_t ce = (BhCreateEvent_t)GetProcAddress(k, "CreateEventA");
    BhResetEvent_t re = (BhResetEvent_t)GetProcAddress(k, "ResetEvent");
    BhOverlapped ov;
    HANDLE dir = INVALID_HANDLE_VALUE;
    int notify = 0, rescan = 0;
    DWORD got;
    (void)unused;
    bg_io(1);
    memset(&ov, 0, sizeof(ov));
    if (rdc && gor && ce && re && g_kWait) {
        ov.hEvent = ce(NULL, TRUE, FALSE, NULL);
        dir = CreateFileA(g_dir, 0x0001 /* FILE_LIST_DIRECTORY */, FILE_SHARE_READ | FILE_SHARE_WRITE | 4 /* DELETE */, NULL,
                          OPEN_EXISTING, 0x02000000 /* BACKUP_SEMANTICS */ | 0x40000000 /* OVERLAPPED */, NULL);
        if (ov.hEvent && dir != INVALID_HANDLE_VALUE)
            notify = rdc(dir, buf, sizeof(buf), FALSE, 0x1 | 0x8 | 0x10 /* FILE_NAME | SIZE | LAST_WRITE */, NULL, &ov, NULL);
    }
    blog("png watcher: %s", notify ? "on (Windows reports saved / new pngs; nothing is checked while nothing changes)"
                                   : "polling (this drive can't report changes): the pngs used in the last minute, once a second");
    for (;;) {
        u32 now;
        int i;
        if (notify) {
            if (g_kWait(ov.hEvent, 250) == 0) {
                got = 0;
                if (!gor(dir, &ov, &got, FALSE)) got = 0;
                if (!got) rescan = 1;                                  /* too many changes at once: compare the folder */
                else {
                    u8 *p = (u8 *)buf;
                    for (;;) {
                        DWORD next = *(DWORD *)p, len = *(DWORD *)(p + 8) / 2, j;
                        const unsigned short *w = (const unsigned short *)(p + 12);
                        char nm[64]; int ok = len < 63;
                        for (j = 0; ok && j < len; j++) { if (w[j] > 127) ok = 0; else nm[j] = (char)w[j]; }
                        if (ok) { int plain; nm[len] = 0; if (watch_cfid(nm, &plain)) watch_note(nm); }
                        if (!next) break;
                        p += next;
                    }
                }
                re(ov.hEvent);
                if (!rdc(dir, buf, sizeof(buf), FALSE, 0x1 | 0x8 | 0x10, NULL, &ov, NULL)) {
                    notify = 0;
                    blog("png watcher: Windows stopped reporting changes - polling from now on");
                }
            }
        } else {
            Sleep(1000);
            watch_poll();
        }
        if (rescan) {                                                  /* compare the whole folder with the table */
            WIN32_FIND_DATAA fd; char pat[MAX_PATH]; HANDLE h;
            rescan = 0;
            s_cpy(pat, g_dir, MAX_PATH); s_cat(pat, "png*.iff", MAX_PATH);
            h = FindFirstFileA(pat, &fd);
            if (h != INVALID_HANDLE_VALUE) {
                do {
                    int plain; u32 cfid = watch_cfid(fd.cFileName, &plain);
                    PngEntry *e = cfid && g_pngIndex[cfid] ? &g_pngs[g_pngIndex[cfid] - 1] : 0;
                    if (!cfid) continue;
                    if (e ? (s_ieq(e->file, fd.cFileName) && (e->size != fd.nFileSizeLow || e->wlo != fd.ftLastWriteTime.dwLowDateTime))
                          : plain) watch_note(fd.cFileName);
                } while (FindNextFileA(h, &fd));
                FindClose(h);
            }
        }
        /* a file being saved sends several notices: read it 400 ms after the last one */
        now = GetTickCount();
        for (i = 0; i < 64; i++)
            if (g_pend[i].name[0] && now - g_pend[i].tick >= 400) {
                char nm[64];
                s_cpy(nm, g_pend[i].name, 64); g_pend[i].name[0] = 0;
                watch_refresh(nm);
            }
    }
}

/* ------------------------------------------------------------------------------------------ */
/* startup                                                                                    */
/* ------------------------------------------------------------------------------------------ */
#include "roster_key.inc"     /* 1.11.26-ML: player key (CFID / roster ID of generic faces) */
#include "hairstyles.inc"
#include "hairrefresh.inc"     /* 1.11.7 / 1.17.4: Hair Style live refresh in Edit Player (2K14) */
#include "wingspan.inc"        /* 1.11.25-ML: Edit Player "Wingspan" (arm length per player, ball follows) */
#include "shortlen.inc"        /* 1.19.4 / 1.11.15-ML: Edit Player "Short Length" (moving cloth_shorts) */
#include "roster.inc"          /* 1.11.26-ML: one settings file per roster */
#include "netcloth.inc"       /* 1.11.30-ML: jerseys/shorts keep the stock cloth size (net hex edits) */
#include "bench.inc"
#include "editcam.inc"        /* 1.11.10 / 1.17.6: Edit Player camera zoom */
#include "poolguard.inc"       /* cloth pool guard (practice mode) */
#include "passer.inc"          /* 1.11.30-ML: Freestyle practice rebounder */


/* ------------------------------------------------------------------------------------------ */
/* PERF: every 30 s of activity, how much time the hooks took (to tell BodyHook lag from the  */
/* game loading data)                                                                          */
/* ------------------------------------------------------------------------------------------ */
static DWORD WINAPI perf_thread(LPVOID unused)
{
    u32 lastC[T_COUNT], lastU[T_COUNT], k;
    (void)unused;
    for (k = 0; k < T_COUNT; k++) { lastC[k] = g_tCalls[k]; lastU[k] = g_tUs[k]; }
    for (;;) {
        char line[700], part[80], num[12];
        u32 tot = 0, calls = 0;
        Sleep(30000);
        line[0] = 0;
        for (k = 0; k < T_COUNT; k++) {
            u32 c = g_tCalls[k] - lastC[k], us = g_tUs[k] - lastU[k];
            lastC[k] = g_tCalls[k]; lastU[k] = g_tUs[k];
            if (!c) continue;
            if (k != T_BUILD) { calls += c; tot += us; }   /* 2.1: builds are inside "draw guards" already */
            s_cpy(part, kTimeNames[k], 80); s_cat(part, " ", 80);
            u32_to_dec(c, num); s_cat(part, num, 80); s_cat(part, "x/", 80);
            u32_to_dec(us / 1000, num); s_cat(part, num, 80); s_cat(part, "ms  ", 80);
            s_cat(line, part, sizeof(line));
        }
        if (calls) blog("PERF     last 30 s: all hooks together %u ms (%u calls)  |  %s", tot / 1000, calls, line);
        {   /* 2.2: what the draw-call checks cost on this PC */
            static u32 lvq, lnw, lsb;
            u32 vq = st_vq, nw = st_drawNoWait, sb = st_slNodeBusy;
            if (vq != lvq || nw != lnw || sb != lsb)
                blog("PERF     last 30 s: memory checks (VirtualQuery) %u, draw calls that didn't wait for a busy cloth lock %u, short-length checks put off a frame %u",
                     vq - lvq, nw - lnw, sb - lsb);
            lvq = vq; lnw = nw; lsb = sb;
        }
        {   /* 2.1 */
            static u32 lw, lr;
            if (st_buildWaits != lw || st_clothRevived != lr) {
                blog("PERF     own cloth: %u draw call(s) waited a frame for their build (one build per frame), %u object(s) reused instead of rebuilt (so far)", st_buildWaits, st_clothRevived);
                lw = st_buildWaits; lr = st_clothRevived;
            }
        }
        {
            static u32 lastDrop = 0;
            if (g_logQDropped != lastDrop) { blog("log: %u line(s) dropped so far (queue full)", g_logQDropped); lastDrop = g_logQDropped; }
        }
    }
}

static DWORD WINAPI init_thread(LPVOID unused)
{
    DWORD t0 = GetTickCount(), lastScan = 0;
    u32 site = 0;
    int scans = 0;
    (void)unused;

    for (;;) {
        DWORD now = GetTickCount();
        int full = 0;
        if (now - t0 > 3000 && now - lastScan > 1000) { full = 1; lastScan = now; scans++; }
        site = locate_callsite(full);
        if (site) break;
        if ((int)(now - t0) > cfg_waitSeconds * 1000) {
            blog("ERROR body call site not found after %d s (%d full scans). This nba2k14.exe build is not recognised.", cfg_waitSeconds, scans);
            blog("      exe TimeDateStamp=%08X SizeOfImage=%08X .text=%08X+%X", g_exeTimestamp, g_exeImageSize, (u32)g_textBase, g_textSize);
            blog("      Send this log + your nba2k14.exe so the pattern can be updated. Game runs unmodified.");
            return 0;
        }
        Sleep(full ? 5 : 2);
    }
    Sleep(500);
    if (locate_callsite(0) != site && locate_callsite(1) != site) {
        blog("ERROR call site changed while the game was unpacking - not patching.");
        return 0;
    }
    blog("game code decrypted after %u ms", GetTickCount() - t0);
    g_imgFast = 1;
    blog("body call site      : %08X %s", site, site == KNOWN_CALLSITE ? "(known RLD build)" : "(found by pattern scan)");
    blog("ResolveAsset        : %08X", call_target_of(site));
    {
        u32 mgr = *(u32*)(site - 4), tbl = *(u32*)(site - PATTERN_CALL_OFFSET + 26), i;
        blog("asset manager       : %08X", mgr);
        blog("body FileID table   : %08X", tbl);
        for (i = 0; i < 4; i++) {
            u32 v = 0;
            read32(tbl + 4 * i, &v);
            blog("   bodytype %u -> %08X %s", i, v, v == kBodyIds[i] ? kBodyNames[i] : "(CHANGED from stock!)");
        }
        g_mgr = mgr;
    }
    blog("player->cyberface   : [player+%X], valid flag +%X, container hash +%X", cfg_offPlayerCF, cfg_offCFValid, cfg_offCFHash);
    blog("fallback container  : %s (%08X)", container_name(cfg_fallbackHash), cfg_fallbackHash);

    if (!cfg_enabled) { blog("Enabled=0 in BodyHook.ini - NOT patching (logging only)."); return 0; }
    if (install_hook(site))
        blog("HOOK INSTALLED: %08X now calls BodyLookup (%08X). Per-cyberface bodies are active.", site, (u32)&BodyLookupThunk);
    install_leg_hooks(g_textBase, g_textSize);
    install_cloth_hooks(site);
    install_cache_hooks();
    install_sim_hooks(g_textBase, g_textSize);
    install_poolguard14();
    install_acc_hooks();
    install_polo();
    install_socks();
    install_arms();
    install_nbapatch();
    install_bench();
    install_netcloth();
    if (cfg_enabled && cfg_cloth && (cfg_staticGuard || ini_int2("ShortLength", "Enabled", 1)) && !g_floorOrig[16]) install_floor_wrappers();   /* static-cloth guard / Short Length need them */   /* static-cloth guard needs them */
    scan_loose_pngs();   /* after patching, so a big png folder never delays the hooks */
    if (cfg_liveRefresh) CreateThread(NULL, 0, png_watch_thread, NULL, 0, NULL);
    roster_init();          /* 1.11.26-ML: rosters\(default).ini until the game loads a roster */
    install_hair();
    install_wingspan();     /* 1.11.25-ML: before install_shortlen (which adds its Edit Player row) */
    install_shortlen();     /* after install_hair: copies the Hair Style record */
    install_roster();       /* after install_hair (CreateFileA) */
    install_editsession();  /* 2.0 fix: Edit Player Accept / not accepted */
    sl_sync_hair();
    install_editcam();      /* after the scan: saved hair choices update the png row info */
    if (cfg_log) CreateThread(NULL, 0, perf_thread, NULL, 0, NULL);
    install_passer();
    {
        int k;
        for (k = 0; k < 60; k++) {
            Sleep(1000);
            if (call_target_of(site) != (u32)&BodyLookupThunk) {
                blog("WARNING hook at %08X was overwritten (target now %08X) - re-installing", site, call_target_of(site));
                if (!looks_like_resolver(call_target_of(site)) || !install_hook(site)) { blog("ERROR could not re-install"); break; }
            }
        }
    }
    return 0;
}

#include "k12port.inc"         /* NBA 2K12 port */

/* SLAM calls this once all mods are loaded, before the game's own code runs (slam_glue.c -> SlamModStart).
   What BodyHook's DllMain did before SLAM, minus the dinput8 proxy, the mod loader, the one-copy guard
   and the crash logger: SLAM does those for every mod now. */
int mod_start(void)
{
    char self[MAX_PATH];
    int i, cut = 0;
    g_self = g_slamInst;
    InitializeCriticalSection(&g_logLock);
    InitializeCriticalSection(&g_clothLock);
    crc_init();
    GetModuleFileNameA(NULL, g_dir, MAX_PATH);
    for (i = 0; g_dir[i]; i++) if (g_dir[i] == '\\' || g_dir[i] == '/') cut = i + 1;
    g_dir[cut] = 0;
    s_cpy(g_modDir, g_slam->mod_dir(g_slamMe), MAX_PATH);       /* SLAM\BodyHook\ */
    glue_migrate("BodyHook_cache.ini");   /* 1.11.26-ML: Hair / Shorts / Wingspan are in rosters\*.ini now */
    s_cpy(g_ini, g_modDir, MAX_PATH); s_cat(g_ini, "BodyHook.ini", MAX_PATH);
    s_cpy(g_logPath, g_modDir, MAX_PATH); s_cat(g_logPath, "BodyHook.log", MAX_PATH);
    find_text_section();
    {   /* which game: NBA 2K12 (k12port.inc) or NBA 2K14 */
        char exe[MAX_PATH]; int n, j;
        GetModuleFileNameA(NULL, exe, MAX_PATH);
        n = s_len(exe);
        for (j = 0; j + 7 <= n; j++) if (s_ieq_n(exe + j, "nba2k12", 7)) g_is2k12 = 1;
        /* 1.11.21-ML: every known NBA 2K12 build, whatever the exe is called ("NBA 2K25 REMASTERED.exe", nba2k13.exe) */
        if (g_exeTimestamp == 0x4F219A21u || g_exeTimestamp == 0x4E63057Du || g_exeTimestamp == 0x4E84D35Fu) g_is2k12 = 1;
    }
    load_config();
    if (cfg_log)
        g_logFile = CreateFileA(g_logPath, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    InitializeCriticalSection(&g_accLock);
    InitializeCriticalSection(&g_texLock);
    InitializeCriticalSection(&g_iffLock);
    log_start();
    GetModuleFileNameA((HMODULE)g_slamInst, self, MAX_PATH);
    find_text_section();
    blog("BodyHook %s  (per-cyberface bodies for NBA %s)", BODYHOOK_VERSION, g_is2k12 ? "2K12" : "2K14");
    blog("loaded as %s (SLAM mod, SLAM %s)", self, g_slam->slamVersion);
    blog("settings  %s", g_ini);   /* 1.11.30-ML: which BodyHook.ini is read */
    blog("game dir  %s", g_dir);
    blog("exe TimeDateStamp=%08X SizeOfImage=%08X .text=%08X size %X", g_exeTimestamp, g_exeImageSize, (u32)g_textBase, g_textSize);
    blog("CFID: %s%s", cfg_cfidOffset == CFID_GAME ? "record CF_ID +3C, same as the game's png<cfid>.iff (MyCareer-safe)" :
         cfg_cfidOffset >= 0 ? "fixed offset from ini" : "learned automatically from custom-body pngs",
         g_cfidLegacy ? "  (old ini value PlayerCFIDOffset=0x1C = PortrID, upgraded)" : "");
    blog("FastPngCheck=%d LivePngRefresh=%d PngLoadWaitMs=%u", cfg_fastPath, cfg_liveRefresh, cfg_loadWaitMs);
    if (cfg_perfLite) blog("PERFORMANCE Lite=1: bench keeps the game's own models (no high-poly bench, also in replays), bench uniforms don't move");
    blog("config: Enabled=%d MatchAnyBodyID=%d StrictGlobalFallback=%d Diagnostics=%d LogEveryLookup=%d ExtraBodyIDs=%d Players=%d",
         cfg_enabled, cfg_matchAnyBody, cfg_strictGlobal, cfg_diagnostics, cfg_logEvery, cfg_extraCount, cfg_overCount);
    for (i = 0; i < cfg_extraCount; i++) blog("   extra body FileID %08X", cfg_extraIds[i]);
    blog("leg tattoos: %s  MatchAnyLegID=%d  (%d FileIDs)", cfg_legs ? "on" : "off", cfg_matchAnyLeg, cfg_legCount);
    blog("uniform models: %s  MatchAnyInGroup=%d  (%d groups)", cfg_cloth ? "on" : "off", cfg_matchAnyCloth, cfg_clothGroups);
    blog("accessories: %s  AllowCombinations=%d  NormalMaps=%d  (%d textures, %d normal maps)",
         cfg_acc ? "on" : "off", cfg_accCombos, cfg_accNormals, cfg_accTexCount, cfg_accNrmCount);
    if (g_is2k12) blog("team colour socks: %s  Style=%d   NBA patch headband-only: %s   high-poly bench: %s (BenchUniforms=%d)   hair styles: %s",
                       cfg_socks ? "on" : "off", cfg_sockStyle, cfg_nbaPatchHeadbandOnly ? "on" : "off",
                       cfg_benchLive ? "on" : "off", cfg_benchUniforms, cfg_hair ? "on" : "off");
    else blog("coach polo: %s  ShowArms=%d   team colour socks: %s  Style=%d",
              cfg_polo ? "on" : "off", cfg_poloArms, cfg_socks ? "on" : "off", cfg_sockStyle);
    for (i = 0; i < cfg_overCount; i++)
        blog("   [Players] CFID %u -> %s%08X", cfg_over[i].cfid, cfg_over[i].disabled ? "OFF " : "", cfg_over[i].fileId);
    if (g_is2k12) CreateThread(NULL, 0, init_thread_2k12, NULL, 0, NULL);
    else CreateThread(NULL, 0, init_thread, NULL, 0, NULL);
    return 0;
}

/* the game is closing (slam_glue.c, DLL_PROCESS_DETACH) */
void mod_stop(void)
{
    int k;
    log_go_direct();
    if (g_logQDropped) blog("log: %u line(s) were dropped because the log queue was full", g_logQDropped);
    if (g_clothSites)
        blog("uniform stats: requests=%u  from own png=%u  cloth sim switched off=%u", st_clothCalls, st_clothLocal, st_simBridged);
    if (g_accSites || g_accHelperSites)
        blog("accessory stats: requests=%u  textures from own png=%u  normal maps from own png=%u  shown by AllowCombinations=%u",
             st_accCalls, st_accLocal, st_nrmLocal, st_accShown);
    if (cfg_polo) blog("coach polo stats: %u model(s) changed, %u coach model(s) with all arm segments on", st_poloModels, st_arms);
    if (g_sockVtable) blog("team colour stats: %u sock / %u other accessory material(s) coloured, %u stock red/blue mask(s) remapped",
                           st_socks, st_tcAcc, st_tcRemap);
    if (g_legSites)
        blog("leg stats: requests=%u  from own png=%u  from GLOBAL=%u  original=%u", st_legCalls, st_legLocal, st_legGlobal, st_legOrig);
    blog("live png refresh: %u png(s) re-read after changing on disk", st_refresh);
    if (g_origCreateFileA || g_origCreateFileW) blog("hair styles: %u choice(s) made, %u png load(s) swapped to a variant", st_hairChanges, st_hairSwaps);
    if (st_benchComposites) blog("bench stats: %u player model(s) set up for the bench, %u with the high-poly model in replays, %u bench uniform draw(s)",
                                 st_benchComposites, st_benchReplayFloor, st_benchUniforms);
    if (g_hooked)
        blog("session stats: body requests=%u  from own png=%u  from %s=%u  original=%u  failed=%u",
             st_calls, st_local, container_name(cfg_fallbackHash), st_global, st_orig, st_fail);
    for (k = 0; k < T_COUNT; k++)
        if (g_tCalls[k])
            blog("time in %-9s hook: %u calls, %u us total, %u us per call", kTimeNames[k], g_tCalls[k], g_tUs[k], g_tUs[k] / g_tCalls[k]);
    if (g_logFile != INVALID_HANDLE_VALUE) CloseHandle(g_logFile);
}
