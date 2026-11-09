/* ------------------------------------------------------------------------------------------ */
/* 1.11.26-ML: WHO A PLAYER IS (for Hair Style / Short Length / Wingspan)                      */
/*                                                                                             */
/* A real cyberface: his CFID (record +0x3C, as before). A generic / created face (record      */
/* +0x9B bit 0) has no CFID of his own (CF_ID -1, or a placeholder shared by dozens of players: */
/* the default roster's John Browns are 2219 / 2220 / 2221). His key is then his roster ID:     */
/* record +0x154 = the 16-bit ID every player record keeps in the roster file (RED MC /         */
/* Players.csv "ID"; checked in game: Curry 380, the John Browns 569 / 579 / 616). In the       */
/* file: G<ID>=value ; First Last. The name is a check: if that roster ID now holds a          */
/* different player, the saved values are not used for him (NBA 2K14 only).                    */
/* ------------------------------------------------------------------------------------------ */
#define GEN_BIT    0x80000000u
#define GEN_NAMES  4096

static char g_rosterIni[MAX_PATH];              /* SLAM\BodyHook\rosters\<roster>.ini (Hair, ShortLength, Wingspan) */
static char g_rosterKey[96] = "(default)";
static CRITICAL_SECTION g_gnLock;
static int  g_gnInit = 0;
static struct { u32 id; char name[40]; } g_gn[GEN_NAMES];   /* roster ID -> name saved with his values */
static int  g_gnCount = 0;

static int w16_to_a(const u16 *w, char *out, int max)          /* UTF-16 name -> ASCII ('?' for other letters) */
{
    int n = 0;
    out[0] = 0;
    if (!w || (u32)w < 0x10000 || !readable((void*)w, 2)) return 0;
    {   /* one memory check for the whole name when it fits (this runs at draw time for generic players) */
        int fast = readable((void*)w, 2 * (u32)max);
        while (n < max - 1 && (fast || readable((void*)(w + n), 2)) && w[n]) { u16 c = w[n]; out[n] = (char)(c >= 0x20 && c < 0x7F ? c : '?'); n++; }
    }
    out[n] = 0;
    return n;
}
/* "First Last" from the record's name pointers (+4 first, +0 last) */
static void player_name(u8 *p, char *out, int max)
{
    char a[40], b[40];
    out[0] = 0;
    if (g_is2k12 || !p || !readable(p, 8)) return;
    w16_to_a(*(const u16 *const *)(p + 4), a, 40);
    w16_to_a(*(const u16 *const *)p, b, 40);
    s_cpy(out, a, max); if (a[0] && b[0]) s_cat(out, " ", max); s_cat(out, b, max);
}
static int name_eq(const char *a, const char *b)
{
    while (*a && lo(*a) == lo(*b)) { a++; b++; }
    return lo(*a) == lo(*b);
}

/* the key without the name check (Edit Player: the row is shown, a change claims the slot) */
static u32 player_key_raw(u8 *p)
{
    u32 c = player_cfid(p), id;
    if (c) return c;
    /* 1.11.28-ML: MyPLAYER (MyCareer) has no CFID and not always the generic-face bit: any player without a CFID
     * is keyed by his roster ID (it was: generic bit required -> MyPLAYER had no key, no Short Length / Wingspan) */
    if (g_is2k12 || !p || !readable(p, 0x158)) return 0;
    id = *(u32*)(p + 0x154);
    return id < 0x10000 ? (GEN_BIT | id) : 0;
}
static void nck_clear(void);
static const char *gn_get(u32 id)
{
    int i;
    for (i = 0; i < g_gnCount; i++) if (g_gn[i].id == id) return g_gn[i].name;
    return 0;
}
static void gn_set(u32 id, const char *name)
{
    int i;
    if (!name || !name[0]) return;
    if (g_gnInit) EnterCriticalSection(&g_gnLock);
    for (i = 0; i < g_gnCount; i++) if (g_gn[i].id == id) break;
    if (i == g_gnCount && g_gnCount < GEN_NAMES) g_gn[g_gnCount++].id = id;
    if (i < GEN_NAMES) s_cpy(g_gn[i].name, name, 40);
    nck_clear();
    if (g_gnInit) LeaveCriticalSection(&g_gnLock);
}
/* the key with the name check: 0 when the roster ID now holds someone else.
 * 1.11.27-ML: the result is cached per record + name pointers + key (this runs at draw time for every player). */
static struct { u32 rec, n0, n1, key; int ok; } g_nck[64];
static volatile u32 g_nckNext;
static void nck_clear(void) { int i; for (i = 0; i < 64; i++) g_nck[i].rec = 0; }
static u32 player_key(u8 *p)
{
    u32 k = player_key_raw(p);
    if (k & GEN_BIT) {
        const char *saved = gn_get(k & 0xFFFF);
        if (saved && saved[0]) {
            u32 n0 = *(u32*)p, n1 = *(u32*)(p + 4), j;
            for (j = 0; j < 64; j++) if (g_nck[j].rec == (u32)p && g_nck[j].n0 == n0 && g_nck[j].n1 == n1 && g_nck[j].key == k) return g_nck[j].ok ? k : 0;
            {
                char nm[40]; int ok;
                player_name(p, nm, 40);
                ok = !nm[0] || name_eq(nm, saved);
                j = g_nckNext++ & 63;
                g_nck[j].rec = 0; g_nck[j].n0 = n0; g_nck[j].n1 = n1; g_nck[j].key = k; g_nck[j].ok = ok; g_nck[j].rec = (u32)p;
                if (!ok) return 0;
            }
        }
    }
    return k;
}
static void key_str(u32 key, char *out)            /* "1612" / "G569" (ini key) */
{
    if (key & GEN_BIT) { out[0] = 'G'; u32_to_dec(key & 0xFFFF, out + 1); }
    else u32_to_dec(key, out);
}
static const char *key_what(u32 key) { return (key & GEN_BIT) ? "roster ID" : "CFID"; }
/* parse "1612" / "G569" (spaces allowed); 0 = not a key */
static u32 key_parse(const char *k)
{
    u32 v = 0; int gen = 0, ok = 0;
    while (*k == ' ') k++;
    if (*k == 'G' || *k == 'g') { gen = 1; k++; }
    for (; *k >= '0' && *k <= '9' && v <= 0xFFFF; k++) { v = v * 10 + (u32)(*k - '0'); ok = 1; }
    while (*k == ' ') k++;
    if (!ok || *k || v > 0xFFFF) return 0;
    if (gen) return GEN_BIT | v;
    return v ? v : 0;
}
/* value + " ; name" (the name is a label for CFID players, the check for generic ones) */
static void val_named(char *out, int max, const char *val, u32 key, u8 *rec)
{
    char nm[40];
    s_cpy(out, val, max);
    nm[0] = 0;
    if (rec) player_name(rec, nm, 40);
    if (!nm[0] && (key & GEN_BIT)) { const char *s = gn_get(key & 0xFFFF); if (s) s_cpy(nm, s, 40); }
    if (nm[0]) { s_cat(out, " ; ", max); s_cat(out, nm, max); }
}
/* a line's value without the " ; name" part; a generic key's name goes to the name table */
static void val_split(char *v, u32 key)
{
    int i;
    for (i = 0; v[i]; i++) if (v[i] == ';') {
        int j = i;
        const char *n = v + i + 1;
        while (*n == ' ') n++;
        if (key & GEN_BIT) {
            char nm[40]; int l;
            s_cpy(nm, n, 40); l = s_len(nm);
            while (l && nm[l - 1] == ' ') nm[--l] = 0;
            if (nm[0] && !gn_get(key & 0xFFFF)) gn_set(key & 0xFFFF, nm);
        }
        while (j > 0 && v[j - 1] == ' ') j--;
        v[j] = 0;
        return;
    }
}
