#!/bin/sh
# Builds the plain .asi (no SLAM) BodyHook: bin/plain/BodyHook.asi - same code, slam/slam_lite.c stands in for SLAM.
set -e
export PATH="/usr/lib/llvm-18/bin:$PATH"
cd "$(dirname "$0")/.."
: "${MGW:?}"; : "${GEN:?}"
OUT=bin/plain; OBJ=build/objp; mkdir -p $OUT $OBJ
for d in build/linux_plain/*.def; do llvm-dlltool -m i386 -k -d $d -l $OBJ/$(basename $d .def).lib; done
CF="$EXTRA --target=i686-pc-windows-msvc /c /O2 /GS- /Gs999999 /Zl /W3 -fms-extensions -Islam"
clang-cl $CF /DBODYHOOK_MINIMAL_WINAPI /DMOD_HAS_STOP /Fo$OBJ/bodyhook.obj src/bodyhook.c
clang-cl $CF -imsvc $GEN -imsvc $MGW/crt -imsvc $MGW/include -imsvc $MGW/defaults/include -D_X86_ -fgnuc-version=4.2.1 /Oi- /DMOD_HAS_STOP \
  /Fo$OBJ/slam_lite.obj slam/slam_lite.c
lld-link /brepro /dll /nodefaultlib /entry:DllMain@12 /subsystem:windows,5.01 /machine:x86 /safeseh:no /opt:ref /dynamicbase /nxcompat \
  /noimplib /out:$OUT/BodyHook.asi $OBJ/bodyhook.obj $OBJ/slam_lite.obj $OBJ/*.lib
ls -l $OUT/BodyHook.asi
