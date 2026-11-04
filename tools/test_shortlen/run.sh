#!/bin/sh
# run.sh IMAGE ROW OUT NFRAMES MOVE CFID LEN LIVE_AT LIVE_LEN
#   IMAGE = memory image of the decrypted RLD nba2k14.exe (BodyHook_image.bin), ROW = a cloth_shorts ClothMesh row
#   HAIRFLAG=-DHAIR_ON: test as in game, with the Hair Style feature installed first
#   e.g. ./run.sh BodyHook_image.bin s_reg.row out.bin 300 1 1612 10 0 0   (length 10, running)
cd "$(dirname "$0")"
clang -m32 -O1 -nostdlib -static -no-pie -fno-pic -fno-stack-protector -msse2 -fms-extensions -fasm-blocks -fno-builtin -w \
  $HAIRFLAG -DIMAGE="\"$1\"" -DROWFILE="\"$2\"" -DOUTFILE="\"$3\"" -DNFRAMES=$4 -DMOVE=$5 -DCFID_T=$6 -DLEN_T=$7 -DLIVE_AT=$8 -DLIVE_LEN=$9 ${ROW2:+-DROWFILE2="\"$ROW2\""} \
  -Wl,-Ttext-segment=0x8000000 -o /tmp/t_sl t_sl.c && /tmp/t_sl
