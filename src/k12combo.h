/* ------------------------------------------------------------------------------------------ */
/* NBA 2K12 accessory combinations - probing core (shared by the DLL and the test harness)    */
/*                                                                                            */
/* 2K12 has 97 accessory records (table 0x0124C54C, 0x1C bytes: vtable, index, model,          */
/* texture, material row, decal, flag) in 17 classes (vtables 0x00E5FC24 + k*0x10:            */
/* dtor, colour picker, IsVisible(record, player)). The slot fields are packed into the       */
/* player record at +0xA0..+0xB3 in a different layout from 2K14, so instead of a table the   */
/* hook asks every record's own IsVisible once at start-up which bits make it appear (the    */
/* smallest pattern that shows it on an empty player). The records of one class are the      */
/* values of one slot, so the bits of all their patterns are that slot's field. The union of  */
/* all slot fields is the region that gets cleared (outside the record's own slot) on a copy  */
/* of the player when the game hid a record that the player does have equipped.             */
/* ------------------------------------------------------------------------------------------ */
#define K12_ACC_N       97
#define K12_ACC_TABLE   g_k12->acc_table
#define K12_ACC_STRIDE  0x1C
#define K12_ACC_VT0     g_k12->acc_vt0
#define K12_ACC_NVT     17
#define K12_PREC        0x204           /* player record size */
#define K12_SLOT_OFF    0xA0            /* slot words +A0, +A4, +A8, +AC, +B0 */
#define K12_SLOT_WORDS  5

typedef int (__fastcall *K12Vis_t)(void *record, void *player);

static u32 k12_own[K12_ACC_N][K12_SLOT_WORDS];
static u32 k12_region[K12_SLOT_WORDS];
static u32 k12_visOrig[K12_ACC_NVT];
static u32 k12_pat[K12_ACC_N]; static u8 k12_patWord[K12_ACC_N];   /* smallest pattern that shows the record */

static K12Vis_t k12_vis_of(const u32 *rec)
{
    u32 k = rec[0] - K12_ACC_VT0;
    if ((k & 0xF) || k / 0x10 >= K12_ACC_NVT) return 0;
    return (K12Vis_t)k12_visOrig[k / 0x10];
}

/* smallest bit pattern that shows the record on an otherwise empty player; 1 = found */
static int k12_probe_record(int idx, u8 *buf)
{
    const u32 *rec = (const u32*)(K12_ACC_TABLE + idx * K12_ACC_STRIDE);
    K12Vis_t vis = k12_vis_of(rec);
    u32 *w = (u32*)(buf + K12_SLOT_OFF);
    int width, d, s;
    u32 v;
    if (!vis || rec[1] != (u32)idx) return 0;
    memset(buf, 0, K12_PREC);
    if (vis((void*)rec, buf)) return 0;                     /* shown with nothing equipped: no slot */
    for (width = 1; width <= 4; width++)
        for (d = 0; d < K12_SLOT_WORDS; d++)
            for (s = 0; s + width <= 32; s++)
                for (v = 1; v < (1u << width); v++) {
                    if (!(v & 1) || !(v >> (width - 1))) continue;   /* each pattern once: lowest and highest bit set */
                    w[d] = v << s;
                    if (vis((void*)rec, buf)) { w[d] = 0; k12_pat[idx] = v << s; k12_patWord[idx] = (u8)d; return 1; }
                    w[d] = 0;
                }
    return 0;
}

/* A class (vtable) is one accessory slot; its records are the values of that slot. The slot field
 * is the bit range covering the patterns of all its records (values 1..n fill it). Gaps are filled
 * only for fields up to 4 bits; anything wider keeps the exact bits. */
static int k12_probe_all(u8 *buf)
{
    int i, c, d, n = 0;
    memset(k12_own, 0, sizeof(k12_own));
    memset(k12_region, 0, sizeof(k12_region));
    memset(k12_pat, 0, sizeof(k12_pat));
    for (i = 0; i < K12_ACC_N; i++) if (k12_probe_record(i, buf)) n++;
    for (c = 0; c < K12_ACC_NVT; c++) {
        u32 uni[K12_SLOT_WORDS];
        memset(uni, 0, sizeof(uni));
        for (i = 0; i < K12_ACC_N; i++)
            if (k12_pat[i] && *(u32*)(K12_ACC_TABLE + i * K12_ACC_STRIDE) == K12_ACC_VT0 + c * 0x10)
                uni[k12_patWord[i]] |= k12_pat[i];
        for (d = 0; d < K12_SLOT_WORDS; d++) {
            int lo, hi;
            if (!uni[d]) continue;
            for (lo = 0; !(uni[d] >> lo & 1); lo++) ;
            for (hi = 31; !(uni[d] >> hi & 1); hi--) ;
            if (hi - lo < 4) uni[d] = (hi == 31 ? 0xFFFFFFFFu : ((1u << (hi + 1)) - 1)) & ~((1u << lo) - 1);
        }
        for (i = 0; i < K12_ACC_N; i++)
            if (k12_pat[i] && *(u32*)(K12_ACC_TABLE + i * K12_ACC_STRIDE) == K12_ACC_VT0 + c * 0x10)
                memcpy(k12_own[i], uni, sizeof(uni));
        for (d = 0; d < K12_SLOT_WORDS; d++) k12_region[d] |= uni[d];
    }
    return n;
}

static int k12_same_own(int a, int b)
{
    int d;
    for (d = 0; d < K12_SLOT_WORDS; d++) if (k12_own[a][d] != k12_own[b][d]) return 0;
    return 1;
}

/* the decision, given a hidden record: 1 = show it. copy = scratch player buffer (K12_PREC bytes) */
static int k12_combo_decide(int idx, const u8 *player, u8 *copy)
{
    const u32 *pw = (const u32*)(player + K12_SLOT_OFF);
    u32 *cw = (u32*)(copy + K12_SLOT_OFF);
    const u32 *own = k12_own[idx];
    const u32 *rec = (const u32*)(K12_ACC_TABLE + idx * K12_ACC_STRIDE);
    int d, j, mine = 0, other = 0;
    K12Vis_t vis = k12_vis_of(rec);
    if (!vis) return 0;
    for (d = 0; d < K12_SLOT_WORDS; d++) {
        if (pw[d] & own[d]) mine = 1;
        if (pw[d] & k12_region[d] & ~own[d]) other = 1;
    }
    if (!mine || !other) return 0;
    memcpy(copy, player, K12_PREC);
    for (d = 0; d < K12_SLOT_WORDS; d++) cw[d] &= ~(k12_region[d] & ~own[d]);
    if (!vis((void*)rec, copy)) return 0;
    /* the game picked another version of the same accessory (same slot): keep its choice */
    for (j = 0; j < K12_ACC_N; j++) {
        const u32 *r2 = (const u32*)(K12_ACC_TABLE + j * K12_ACC_STRIDE);
        K12Vis_t v2;
        if (j == idx || !k12_same_own(j, idx)) continue;
        v2 = k12_vis_of(r2);
        if (v2 && v2((void*)r2, (void*)player) && !v2((void*)r2, copy)) return 0;
    }
    return 1;
}
