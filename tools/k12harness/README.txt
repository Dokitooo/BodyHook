k12harness - runs NBA 2K12's own accessory IsVisible functions on Linux (no Windows, no emulator)

Needs a decrypted image of nba2k12.exe: img12.bin = build A (4F219A21, the 2K12 dump build writes
BodyHook_image_2k12_4F219A21.bin), img12_B.bin = build B (4E63057D, not encrypted: the exe mapped at
0x400000). Compile with -DK12B=0 (A) or -DK12B=1 (B); the addresses come from ../../src/k12builds.h. The image is mapped at 0x400000 in a 32-bit static process and the
probing core of the DLL (../../src/k12combo.h) is run on it.

  clang -m32 -O1 -nostdlib -static -fno-pic -fno-builtin -fno-stack-protector combo_probe.c -o combo_probe \
        -fuse-ld=lld -Wl,-e,_start -Wl,--image-base=0x30000000
  ./combo_probe     slot field of every accessory record + every two-accessory pair the game hides
  (same for combo_pairs.c: counts per class, stock vs. with AllowCombinations)
