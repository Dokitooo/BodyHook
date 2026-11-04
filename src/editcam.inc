/* ------------------------------------------------------------------------------------------ */
/* EDIT PLAYER CAMERA (1.11.10 / 1.17.6, NBA 2K14)                                              */
/*                                                                                             */
/* The Edit Player preview camera picks one of 22 "views" from a static table (0x0109EED0,     */
/* 0x50 bytes each); the active view is [0x0109DE90], the table pointer [0x0109DE98], and the  */
/* framing code (0x004FB130) rebuilds the camera from the view every frame:                    */
/*   +00 bone to follow (-2 = the whole body, -1 = a fixed spot)  +10 target offset (x,y,z)     */
/*   +20 u16 angle around the player   +24 distance (cm)   +30 camera offset (x,y,z), y = height */
/* From the first test log: Signature (the shot previews) uses view 20 (whole body, 1500 cm,   */
/* 400 cm up); Vitals/Body use views 0/1 (450 cm, 80 cm up); 4 = head. [EditPlayerCamera]      */
/* View<N>=factor scales distance and camera height of view N (defaults View20=0.5,           */
/* View0=View1=0.9). The ini is re-read every second, so the values can be tuned live.         */
/* LogViews=1 writes "EDIT CAMERA view N" when the view changes.                               */
/* 1.11.12: View<N>Lift=cm raises the camera and the point it aims at by the same amount, so   */
/* the player sits lower on screen (default View20Lift=60 for Signature).                      */
/* ------------------------------------------------------------------------------------------ */
#define EC_TABLE     0x0109EED0u
#define EC_VIEW      0x0109DE90u
#define EC_TABLE_PTR 0x0109DE98u
#define EC_COUNT     22
static float g_ecOrigDist[EC_COUNT], g_ecOrigCamY[EC_COUNT], g_ecOrigTgtY[EC_COUNT];
static int   g_ecInstalled = 0;
static int   cfg_ecEnabled = 1, cfg_ecLog = 1, cfg_ecLive = 1;
static float cfg_ecZoom[EC_COUNT];           /* per view, 1.0 = the game's own */
static float cfg_ecLift[EC_COUNT];           /* 1.11.12: per view, cm: camera and its aim move up = the player sits lower */
static float g_ecAppliedLift[EC_COUNT];
static float g_ecApplied[EC_COUNT];
static int   g_ecFirst = 1;
static u32   cfg_ecViews = 0;                /* views with a zoom != 1 (for the log) */

static void ec_fmt(char *out, const char *fmt, ...) { va_list ap; va_start(ap, fmt); wvsprintfA(out, fmt, ap); va_end(ap); }

static float ec_atof(const char *s)
{
    float v = 0.0f, f = 0.1f; int neg = 0;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') v = v * 10.0f + (float)(*s++ - '0');
    if (*s == '.' || *s == ',') { s++; while (*s >= '0' && *s <= '9') { v += f * (float)(*s++ - '0'); f *= 0.1f; } }
    return neg ? -v : v;
}

/* 1.11.10b: one zoom per view: View<N>=factor. Signature (the animation previews) is view 20 (1500 cm
 * away, 400 cm up), Vitals/Body are views 0/1 (450 cm). Defaults: View20=0.5, View0=View1=0.9. */
static void ec_read_ini(void)
{
    char b[64], key[16]; int i;
    cfg_ecEnabled = ini_int2("EditPlayerCamera", "Enabled", 1);
    cfg_ecLog     = ini_int2("EditPlayerCamera", "LogViews", 1);
    cfg_ecLive    = ini_int2("EditPlayerCamera", "LiveReload", 1);
    cfg_ecViews = 0;
    for (i = 0; i < EC_COUNT; i++) {
        float z;
        ec_fmt(key, "View%d", i);
        GetPrivateProfileStringA("EditPlayerCamera", key, i == 20 ? "0.5" : (i <= 1 ? "0.9" : "1.0"), b, sizeof(b), g_ini);
        z = ec_atof(b);
        if (z < 0.1f) z = 0.1f;
        if (z > 2.0f) z = 2.0f;
        cfg_ecZoom[i] = z;
        ec_fmt(key, "View%dLift", i);
        GetPrivateProfileStringA("EditPlayerCamera", key, i == 20 ? "60" : "0", b, sizeof(b), g_ini);
        cfg_ecLift[i] = ec_atof(b);
        if (cfg_ecLift[i] < -300.0f) cfg_ecLift[i] = -300.0f;
        if (cfg_ecLift[i] > 300.0f) cfg_ecLift[i] = 300.0f;
        if (z != 1.0f || cfg_ecLift[i] != 0.0f) cfg_ecViews |= 1u << i;
    }
}

static void ec_apply(void)
{
    int i, changed = 0;
    char line[400], part[64];
    for (i = 0; i < EC_COUNT; i++) {
        float z = cfg_ecEnabled ? cfg_ecZoom[i] : 1.0f, up = cfg_ecEnabled ? cfg_ecLift[i] : 0.0f;
        u8 *v = (u8*)(EC_TABLE + 0x50 * i);
        float d, y, t;
        if (!g_ecFirst && z == g_ecApplied[i] && up == g_ecAppliedLift[i]) continue;
        /* camera and the point it aims at go up together: same view, the player sits lower */
        d = g_ecOrigDist[i] * z; y = g_ecOrigCamY[i] * z + up; t = g_ecOrigTgtY[i] + up;
        patch_bytes((u32)(v + 0x24), &d, 4, 0);
        patch_bytes((u32)(v + 0x34), &y, 4, 0);
        patch_bytes((u32)(v + 0x14), &t, 4, 0);   /* aim point: camera and target move up together */
        g_ecApplied[i] = z; g_ecAppliedLift[i] = up; changed = 1;
    }
    g_ecFirst = 0;
    if (!changed) return;
    line[0] = 0;
    for (i = 0; i < EC_COUNT; i++)
        if (g_ecApplied[i] != 1.0f || g_ecAppliedLift[i] != 0.0f) {
            ec_fmt(part, "  view %d: %d%% (%d cm), lift %d cm", i, (int)(g_ecApplied[i] * 100.0f + 0.5f), (int)(g_ecOrigDist[i] * g_ecApplied[i]),
                   (int)g_ecAppliedLift[i]);
            s_cat(line, part, sizeof(line));
        }
    blog("EDIT CAMERA  zoom%s", line[0] ? line : "  off (the game's own camera)");
}

static DWORD WINAPI ec_thread(LPVOID unused)
{
    u32 lastView = 0xFFFFFFFFu, lastTable = 0, t = 0;
    (void)unused;
    for (;;) {
        u32 view = 0xFFFFFFFFu, table = 0;
        Sleep(100);
        read32(EC_VIEW, &view); read32(EC_TABLE_PTR, &table);
        if (cfg_ecLog && table && (view != lastView || table != lastTable)) {
            if (table == EC_TABLE && view < EC_COUNT) {
                int bone = *(int*)(EC_TABLE + 0x50 * view);
                blog("EDIT CAMERA  view %u (follows %s%d, %d cm away)%s", view, bone == -2 ? "whole body " : bone == -1 ? "fixed spot " : "bone ",
                     bone, (int)*(float*)(EC_TABLE + 0x50 * view + 0x24), (cfg_ecEnabled && (cfg_ecViews >> view) & 1) ? "  - zoomed" : "");
            } else
                blog("EDIT CAMERA  view %u of another table %08X (not changed)", view, table);
        }
        if (table) { lastView = view; lastTable = table; }
        if (cfg_ecLive && ++t >= 10) {
            /* PERF (1.11.13 / 1.19.2): re-read [EditPlayerCamera] only when BodyHook.ini was saved
             * (was ~60 ini reads every second for the whole session) */
            static u32 lo = 0, hi = 0, sz = 0;
            WIN32_FILE_ATTRIBUTE_DATA fa;
            t = 0;
            if (GetFileAttributesExA(g_ini, 0, &fa) &&
                (fa.ftLastWriteTime.dwLowDateTime != lo || fa.ftLastWriteTime.dwHighDateTime != hi || fa.nFileSizeLow != sz)) {
                int first = !lo && !hi && !sz;
                lo = fa.ftLastWriteTime.dwLowDateTime; hi = fa.ftLastWriteTime.dwHighDateTime; sz = fa.nFileSizeLow;
                if (!first) { ec_read_ini(); ec_apply(); }
            }
        }
    }
}

static void install_editcam(void)
{
    int i;
#ifdef BODYHOOK_HAS_2K12
    if (g_is2k12) return;
#endif
    ec_read_ini();
    /* check the table is what this was made for: views 0/1 450 cm, view 1 whole body, view 4 head (bone 15, 75 cm) */
    if (!readable((void*)EC_TABLE, 0x50 * EC_COUNT) ||
        *(float*)(EC_TABLE + 0x24) != 450.0f || *(int*)(EC_TABLE + 0x50) != -2 || *(float*)(EC_TABLE + 0x50 + 0x24) != 450.0f ||
        *(int*)(EC_TABLE + 0x50 * 4) != 15 || *(float*)(EC_TABLE + 0x50 * 4 + 0x24) != 75.0f) {
        blog("EDIT CAMERA  view table not found (other nba2k14.exe build) - Edit Player camera unchanged");
        return;
    }
    for (i = 0; i < EC_COUNT; i++) {
        g_ecOrigDist[i] = *(float*)(EC_TABLE + 0x50 * i + 0x24);
        g_ecOrigCamY[i] = *(float*)(EC_TABLE + 0x50 * i + 0x34);
        g_ecOrigTgtY[i] = *(float*)(EC_TABLE + 0x50 * i + 0x14);
    }
    g_ecInstalled = 1;
    ec_apply();
    blog("EDIT CAMERA INSTALLED: %s, LiveReload=%d (edit [EditPlayerCamera] View<N>= in BodyHook.ini while playing)",
         cfg_ecEnabled ? "on" : "off (Enabled=0)", cfg_ecLive);
    CreateThread(NULL, 0, ec_thread, NULL, 0, NULL);
}
