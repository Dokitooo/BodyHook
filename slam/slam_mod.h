/* slam_mod.h - what a ported mod's own code sees of SLAM (no <windows.h> needed) */
#ifndef SLAM_MOD_H
#define SLAM_MOD_H
#include "slam.h"
extern const SLAM_API *g_slam;
extern SlamMod *g_slamMe;
extern void *g_slamInst;      /* this .asi (HINSTANCE) */
int mod_start(void);
void mod_stop(void);          /* only if built with MOD_HAS_STOP: called when the game closes */
void glue_migrate(const char *file);     /* a settings file in the mod's folder: keeps the player's own (see slam_glue.c) */
#endif
