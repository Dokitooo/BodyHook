static int hair_bytes(u32 a, const char *hex)   /* "56 8B F1 ??" style compare */
{
    const u8 *p = (const u8*)a;
    int i = 0;
    if (!readable(p, 32)) return 0;
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
