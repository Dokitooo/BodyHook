#!/bin/sh
# Builds BodyHook.asi (SLAM mod) on Linux with clang-cl + lld-link (LLVM 18).
# slam/slam_glue.c (the SLAM side: DllMain, SlamModStart) needs Windows headers: set MGW to the MinGW-w64
# "mingw-w64-headers" folder and GEN to a folder with a generated _mingw.h (see SLAM's source\build).
set -e
export PATH="/usr/lib/llvm-18/bin:$PATH"
cd "$(dirname "$0")/.."
: "${MGW:?set MGW to the mingw-w64-headers folder}"; : "${GEN:?set GEN to the folder with _mingw.h}"
OUT=bin; OBJ=build/obj; mkdir -p $OUT $OBJ
for d in build/linux/*.def; do llvm-dlltool -m i386 -k -d $d -l $OBJ/$(basename $d .def).lib; done
CF="$EXTRA --target=i686-pc-windows-msvc /c /O2 /GS- /Gs999999 /Zl /W3 -fms-extensions -Islam"
OBJS=""
for s in src/bodyhook.c; do o=$OBJ/$(basename $s .c).obj; clang-cl $CF /DBODYHOOK_MINIMAL_WINAPI /DMOD_HAS_STOP /Fo$o $s; OBJS="$OBJS $o"; done
clang-cl $CF -imsvc $GEN -imsvc $MGW/crt -imsvc $MGW/include -imsvc $MGW/defaults/include -D_X86_ -fgnuc-version=4.2.1 /DMOD_HAS_STOP \
  '-DMOD_NAME="BodyHook"' "-DMOD_VERSION=\"${VER:-2.2}\"" /Fo$OBJ/slam_glue.obj slam/slam_glue.c
lld-link /brepro /dll /nodefaultlib /entry:DllMain@12 /subsystem:windows,5.01 /machine:x86 /safeseh:no /opt:ref /dynamicbase /nxcompat \
  /noimplib /out:$OUT/BodyHook.asi $OBJS $OBJ/slam_glue.obj $OBJ/*.lib
ls -l $OUT/BodyHook.asi
