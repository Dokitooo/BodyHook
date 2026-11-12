/* edithub.h - Edit Player "Accept", shared by SigExt / GotoShotExt / BodyHook (the SAME file in all three).
 *
 * Edit Player edits a COPY of the player: [015DD518] = 015DD520 (the copy), [015DD51C] = the roster's own record.
 *   - opening a player copies him into the copy        (call D958B0 at 005042A8)
 *   - "Accept" copies the copy back into the roster    (call D958B0 at 00501F07)
 *   - backing out ("You are about to lose any changes") copies nothing back.
 * The game's own Edit Player values follow that. A mod that keeps its own per-player values (SigExt's extended IDs /
 * Upper Release 2 / Animation Blending, GotoShotExt's Go-To Shot, BodyHook's Hair Style / Short Length / Wingspan)
 * has to do the same: keep a change when the player is accepted, put the old value back when he is not.
 *
 * Only ONE mod can re-target those two calls (SLAM refuses different bytes on the same spot). So: the first mod that
 * starts hooks both calls; every mod - that one included - registers what to do on "accept" and on "open" in a small
 * shared block (named mapping Local\NBA2K14_EditHub_<pid>). The hook calls all of them. Load order does not matter.
 * A mod that hooked the calls on its own (BodyHook 2.0) leaves them taken: then eh_join() says so and returns 0.
 *
 * The including file provides: g_slam / g_slamMe (slam_mod.h), LoadLibraryA, GetProcAddress, Sleep.
 */
#ifndef EDITHUB_H
#define EDITHUB_H

#define EH_ACCEPT_SITE 0x00501F07u
#define EH_OPEN_SITE   0x005042A8u
#define EH_COPY        0x00D958B0u
#define EH_EDITPTR     0x015DD518u
#define EH_REALPTR     0x015DD51Cu
#define EH_COPYREC     0x015DD520u
#define EH_MAGIC       0x42554845u          /* 'EHUB' */
#define EH_VER         1u
#define EH_MAX         8

typedef void (__cdecl *EhFn)(void);
typedef struct {
    unsigned magic, ver;
    volatile long lock;
    volatile unsigned hooked;               /* 1 = the two calls go through EhAccept / EhOpen of the owner */
    volatile unsigned n;                    /* registered mods */
    char owner[16];
    struct { EhFn accept, open; char name[16]; } cb[EH_MAX];
} EhHub;
static EhHub *g_eh;
static int g_ehJoined;

typedef void *(__fastcall *EhCopy_t)(void *dst, const void *src, unsigned n);
static void eh_call(int accept)
{
    unsigned i, n = g_eh ? g_eh->n : 0;
    for (i = 0; i < n && i < EH_MAX; i++) { EhFn f = accept ? g_eh->cb[i].accept : g_eh->cb[i].open; if (f) f(); }
}
/* 00501F07: Accept - the copy goes back into the roster's player, then every mod keeps its changes */
static void *__fastcall EhAccept(void *dst, const void *src, unsigned n)
{
    void *r = ((EhCopy_t)EH_COPY)(dst, src, n);
    eh_call(1);
    return r;
}
/* 005042A8: a player is opened - every mod first undoes what the previous one did not accept */
static void *__fastcall EhOpen(void *dst, const void *src, unsigned n)
{
    eh_call(0);
    return ((EhCopy_t)EH_COPY)(dst, src, n);
}

static unsigned eh_target(unsigned site) { return site + 5u + (unsigned)*(const int *)(site + 1); }
static int eh_same(unsigned a, const char *hex)
{
    const unsigned char *p = (const unsigned char *)a;
    while (*hex) {
        unsigned v = 0; int k;
        while (*hex == ' ') hex++;
        if (!*hex) break;
        for (k = 0; k < 2; k++) { char c = *hex++; v = v * 16 + (unsigned)(c <= '9' ? c - '0' : (c | 32) - 'a' + 10); }
        if (*p++ != (unsigned char)v) return 0;
    }
    return 1;
}
/* is Edit Player editing a copy right now (and do we get its Accept)? */
static int eh_editing(void)
{
    unsigned e = 0, r = 0;
    if (!g_ehJoined) return 0;
    if (!g_slam->read((const void *)EH_EDITPTR, &e, 4) || !g_slam->read((const void *)EH_REALPTR, &r, 4)) return 0;
    return e == EH_COPYREC && r != 0;
}

typedef void *(__stdcall *EhCFM_t)(void *, void *, unsigned, unsigned, unsigned, const char *);
typedef void *(__stdcall *EhMVF_t)(void *, unsigned, unsigned, unsigned, unsigned);
typedef unsigned (__stdcall *EhGCP_t)(void);
/* register this mod; returns 1 = accept / open reach it. *how: "hooked" (this mod hooked the calls), "joined"
 * (another mod did), or why not */
static int eh_join(const char *me, EhFn on_accept, EhFn on_open, const char **how)
{
    void *k = (void *)LoadLibraryA("kernel32.dll");
    EhCFM_t cfm = k ? (EhCFM_t)GetProcAddress(k, "CreateFileMappingA") : 0;
    EhMVF_t mvf = k ? (EhMVF_t)GetProcAddress(k, "MapViewOfFile") : 0;
    EhGCP_t gcp = k ? (EhGCP_t)GetProcAddress(k, "GetCurrentProcessId") : 0;
    static const char pre[] = "Local\\NBA2K14_EditHub_";
    char name[48]; unsigned pid, i; int l = 0, ok = 0;
    *how = "kernel32 functions missing";
    if (!cfm || !mvf || !gcp) return 0;
    while (pre[l]) { name[l] = pre[l]; l++; }          /* no memcpy: GotoShotExt has no CRT */
    pid = gcp();
    for (i = 0; i < 8; i++) { unsigned d = (pid >> (28 - 4 * i)) & 15; name[l++] = (char)(d < 10 ? '0' + d : 'A' + d - 10); }
    name[l] = 0;
    if (!g_eh) {
        void *h = cfm((void *)-1, 0, 0x04 /* PAGE_READWRITE */, 0, sizeof(EhHub), name);
        if (!h) { *how = "shared block not created"; return 0; }
        g_eh = (EhHub *)mvf(h, 0x0006 /* FILE_MAP_READ | FILE_MAP_WRITE */, 0, 0, sizeof(EhHub));
        if (!g_eh) { *how = "shared block not mapped"; return 0; }
    }
    while (__sync_val_compare_and_swap(&g_eh->lock, 0, 1)) Sleep(0);
    if (!g_eh->magic) { g_eh->magic = EH_MAGIC; g_eh->ver = EH_VER; }
    if (g_eh->magic != EH_MAGIC) { *how = "shared block of an unknown version"; goto out; }
    if (!g_eh->hooked) {
        unsigned char b[5];
        if (!eh_same(0x00501EF6u, "8B 15 18 D5 5D 01 8B 0D 1C D5 5D 01 68 E4 01 00 00 E8") ||
            !eh_same(0x00504290u, "B9 20 D5 5D 01 68 E4 01 00 00 8B D6")) { *how = "game code not as expected"; goto out; }
        if (*(const unsigned char *)EH_ACCEPT_SITE != 0xE8 || *(const unsigned char *)EH_OPEN_SITE != 0xE8 ||
            eh_target(EH_ACCEPT_SITE) != EH_COPY || eh_target(EH_OPEN_SITE) != EH_COPY) {
            *how = "another mod hooked Edit Player Accept on its own (an older BodyHook 2.0?) - update it"; goto out;
        }
        b[0] = 0xE8; *(int *)(b + 1) = (int)((unsigned)&EhAccept - (EH_ACCEPT_SITE + 5));
        if (g_slam->patch(g_slamMe, (void *)EH_ACCEPT_SITE, b, 5) != SLAM_OK) { *how = "Accept call could not be patched"; goto out; }
        b[0] = 0xE8; *(int *)(b + 1) = (int)((unsigned)&EhOpen - (EH_OPEN_SITE + 5));
        if (g_slam->patch(g_slamMe, (void *)EH_OPEN_SITE, b, 5) != SLAM_OK) {
            g_slam->unpatch(g_slamMe, (void *)EH_ACCEPT_SITE); *how = "open call could not be patched"; goto out;
        }
        for (i = 0; i < 15 && me[i]; i++) g_eh->owner[i] = me[i];
        g_eh->owner[i] = 0;
        g_eh->hooked = 1;
        *how = "hooked";
    } else *how = "joined";
    if (g_eh->n >= EH_MAX) { *how = "too many mods in the shared block"; goto out; }
    i = g_eh->n;
    g_eh->cb[i].accept = on_accept; g_eh->cb[i].open = on_open;
    { unsigned j; for (j = 0; j < 15 && me[j]; j++) g_eh->cb[i].name[j] = me[j]; g_eh->cb[i].name[j] = 0; }
    __sync_synchronize();
    g_eh->n = i + 1;                        /* published last: the hook only calls complete entries */
    ok = g_ehJoined = 1;
out:
    __sync_lock_release(&g_eh->lock);
    return ok;
}
#endif
