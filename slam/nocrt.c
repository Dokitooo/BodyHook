/* nocrt.c - memset/memcpy for mods built without a C runtime (the compiler may call them for
   struct copies and byte loops). Link it only into mods that don't define them already. */
#include <stddef.h>
#if defined(_MSC_VER) && !defined(__clang__)
#pragma function(memset, memcpy)
#endif
void *__cdecl memset(void *d, int c, size_t n) { unsigned char *p = (unsigned char *)d; while (n--) *p++ = (unsigned char)c; return d; }
void *__cdecl memcpy(void *d, const void *s, size_t n) { unsigned char *p = (unsigned char *)d; const unsigned char *q = (const unsigned char *)s; while (n--) *p++ = *q++; return d; }
