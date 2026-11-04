#!/usr/bin/env python3
"""
iff_tool.py - inspect NBA 2K14 .iff containers (Python 3, no extra packages)

  python iff_tool.py info  png1612.iff        list every row (FileID / type / name) + body rows
  python iff_tool.py scan  "C:\\Games\\NBA 2K14"  list every pngXXXX.iff that carries a body SCNE
  python iff_tool.py hash  hi_body_custom      FileID for an asset name (use in ExtraBodyIDs / RED MC)
  python iff_tool.py sort  png1018.iff         re-sort the row list by FileID (writes png1018.iff, keeps .bak)

Format notes (reverse-engineered from the files in this project):
  header  u32 magic FF3BEF94, u32 headerSize, u32 (BE) headerSize+compressed, u32 0,
          u32 sectionCount, relptr sectionTable, u32 fileCount, relptr fileTable
          relptr = address_of_field + value - 1
  section 32 bytes: hash, hash, align, uncompressedSize, ?, fileOffset, compressedSize, relptr extra
          section hashes: BB05A9C1 (CPU data), 411536D5 = crc32("VRAM")
          data at fileOffset = "ZLIB" + BE usize + BE csize + u32, then a zlib stream
  file    u32 FileID, u32 typeHash, u32 n, u32 offset[n] (offset into each section)
  FileID  = crc32(lowercase asset name)   e.g. crc32("hi_body") = 4A610FDA
  type    = crc32("SCNE") = E26C9B5D, crc32("TXTR") = 5C369069
  RED MC shows FileIDs big-endian ("4A 61 0F DA"); the file stores them little-endian ("DA 0F 61 4A").
  The game identifies a loaded container by crc32(UPPERCASE file name without .iff): PNG1612, GLOBAL...
"""
import os, sys, struct, zlib

def crc(s): return zlib.crc32(s.encode()) & 0xFFFFFFFF

KNOWN = {}
LEG_IDS = {crc('legs_%02d_color' % i): 'legs_%02d_color' % i for i in range(6)}
CLOTH_IDS = {crc(n): n for n in ['jersey_%s_%s' % (s, z) for s in ('u', 'v', 'wishbone') for z in ('thin', 'reg', 'fat')] +
             ['shorts%s' % z for z in ('thin', 'reg', 'fat')]}
for n in ['hi_body', 'hi_body_skinny', 'hi_body_fat', 'hi_body_muscular', 'hihead', 'lohead',
          'face_color', 'face_normal', 'face_occlusion'] + list(LEG_IDS.values()) + list(CLOTH_IDS.values()):
    KNOWN[crc(n)] = n
ACC_IDS = {0x46A48CA9: 'acc: headband', 0x284C68D8: 'acc: undershirt', 0x1FC834CE: 'acc: short-sleeve shirt',
    0x14BFDBB5: 'acc: long-sleeve shirt', 0x7E289AFF: 'acc: arm sleeve', 0x6FF4EC60: 'acc: padded arm sleeve',
    0x6CD24D44: 'acc: short sleeve', 0x38731057: 'acc: shoulder sleeve', 0x0F6B2638: 'acc: PowerWEB sleeve',
    0x7B411638: 'acc: elbow pad/brace', 0x04633A86: 'acc: elbow sleeve', 0x6AEF61FE: 'acc: bands',
    0xB05BC4DA: 'acc: high band', 0xB6BD4971: 'acc: strap', 0xD646D276: 'acc: forearm band',
    0x8E96C7EE: 'acc: protective wristband', 0x975511F2: 'acc: rubber band', 0xFBB9E735: 'acc: I promise band',
    0x841EDB78: 'acc: power band', 0xD69642A5: 'acc: wrist wrap', 0x98DF9350: 'acc: hand wrap',
    0x0E46C401: 'acc: finger strap', 0xC74FF2BB: 'acc: long finger strap', 0xE9E01C61: 'acc: pressure shorts',
    0xEBB16072: 'acc: high thigh pad', 0x5D691AD4: 'acc: leg sleeve', 0xF9667DCC: 'acc: calf sleeve',
    0xF488BB8C: 'acc: padded calf sleeve', 0xE1A4D030: 'acc: half/mid calf sleeve', 0x60FE8AD8: 'acc: hex half calf',
    0x34A12BB6: 'acc: Jordan calf sleeve', 0x19C1C1B0: 'acc: knee pad', 0xF5080DB7: 'acc: hex pad',
    0x6430046B: 'acc: knee brace', 0xC884A024: 'acc: knee sleeve', 0x5D507EC5: 'acc: knee strap',
    0xC8791905: 'acc: ankle brace', 0xE79D0E40: 'acc: ankle wrap', 0xC452025A: 'acc: ankle socks',
    0x511CF131: 'acc: short socks', 0x9EEE8F94: 'acc: short double socks', 0x7B24B4A0: 'acc: medium socks',
    0xB4D6CA05: 'acc: medium double socks', 0x45E26204: 'acc: medium long socks', 0xBC03E907: 'acc: long socks',
    0x81DA22CA: 'acc: striped long socks'}
KNOWN.update(ACC_IDS)
TYPES = {crc('SCNE'): 'SCNE', crc('TXTR'): 'TXTR'}
BODY_IDS = {crc('hi_body_skinny'): 0, crc('hi_body'): 1, crc('hi_body_fat'): 2, crc('hi_body_muscular'): 3}
BODY_ENUM = {0: 'skinny', 1: 'normal', 2: 'fat', 3: 'muscular'}

def u32(d, o): return struct.unpack_from('<I', d, o)[0]
def rel(d, o):
    v = u32(d, o)
    return o + v - 1 if v else None

def parse(path):
    d = open(path, 'rb').read()
    if u32(d, 0) != 0xFF3BEF94:
        raise ValueError('not a 2K IFF (magic %08X)' % u32(d, 0))
    nsec, nfiles = u32(d, 0x10), u32(d, 0x18)
    so, fo = rel(d, 0x14), rel(d, 0x1C)
    secs = []
    for i in range(nsec):
        h1, h2, align, usize, f5, off, csize, ext = struct.unpack_from('<8I', d, so + 32 * i)
        secs.append(dict(hash=h1, usize=usize, off=off, csize=csize))
    files = []
    for i in range(nfiles):
        p = fo + 4 * i
        eo = p + u32(d, p) - 1
        fid, typ, n = struct.unpack_from('<3I', d, eo)
        offs = struct.unpack_from('<%dI' % n, d, eo + 12)
        files.append(dict(id=fid, type=typ, offs=offs))
    # sizes: distance to the next used offset in each section
    for si, s in enumerate(secs):
        starts = sorted(set(f['offs'][si] for f in files if si < len(f['offs'])))
        for f in files:
            if si < len(f['offs']):
                o = f['offs'][si]
                nxt = [x for x in starts if x > o]
                f.setdefault('sizes', []).append((nxt[0] if nxt else s['usize']) - o)
    return dict(path=path, header=u32(d, 4), redmc=b'EDITED WITH RED MC' in d[:0x200], secs=secs, files=files)

def be(v): return ' '.join('%02X' % b for b in struct.pack('>I', v))

def container_of(path):
    return os.path.splitext(os.path.basename(path))[0].upper()

def cmd_info(path):
    info = parse(path)
    cont = container_of(path)
    print('%s' % path)
    print('  container name : %s  (hash %08X - this is what BodyHook matches)' % (cont, crc(cont)))
    print('  saved by RED MC: %s   header 0x%X bytes' % ('yes' if info['redmc'] else 'no', info['header']))
    for i, s in enumerate(info['secs']):
        print('  section %d: hash %08X  %s  %d bytes (zlib %d)' % (i, s['hash'], 'VRAM' if s['hash'] == crc('VRAM') else 'CPU ',
                                                                s['usize'], s['csize']))
    print('  %-12s %-5s %-26s %s' % ('FileID(REDMC)', 'Type', 'Name', 'bytes per section'))
    bodies = []
    for f in info['files']:
        name = KNOWN.get(f['id'], '?')
        t = TYPES.get(f['type'], '%08X' % f['type'])
        mark = ''
        if t == 'SCNE' and f['id'] in BODY_IDS:
            mark = '  <== BODY (bodytype %d / %s)' % (BODY_IDS[f['id']], BODY_ENUM[BODY_IDS[f['id']]])
            bodies.append(f)
        if t == 'TXTR' and f['id'] in LEG_IDS:
            mark = '  <== LEG TATTOO'
        if t == 'SCNE' and f['id'] in CLOTH_IDS:
            mark = '  <== UNIFORM MODEL'
        print('  %-12s %-5s %-26s %s%s' % (be(f['id']), t, name, f.get('sizes'), mark))
    if bodies:
        print('\n  -> With BodyHook, players whose CFID loads %s get this body; nobody else does.' % cont)
    else:
        print('\n  -> No standard body SCNE row: players using this cyberface keep the global.iff body.')
    return info

def cmd_scan(folder):
    hits = 0
    for fn in sorted(os.listdir(folder)):
        low = fn.lower()
        if not (low.startswith('png') and low.endswith('.iff')):
            continue
        try:
            info = parse(os.path.join(folder, fn))
        except Exception as ex:
            print('  %-20s  ERROR %s' % (fn, ex)); continue
        b = [KNOWN.get(f['id']) for f in info['files'] if f['type'] == crc('SCNE') and f['id'] in BODY_IDS]
        l = [KNOWN.get(f['id']) for f in info['files'] if f['type'] == crc('TXTR') and f['id'] in LEG_IDS]
        c = [KNOWN.get(f['id']) for f in info['files'] if f['type'] == crc('SCNE') and f['id'] in CLOTH_IDS]
        if b or l or c:
            hits += 1
            print('  %-20s CFID %-6s body: %-14s legs: %-14s uniform: %s' % (fn, fn[3:-4], ', '.join(b) or '-', ', '.join(l) or '-', ', '.join(c) or '-'))
    print('%d cyberface file(s) with their own body, leg tattoos and/or uniform models.' % hits)

def cmd_hash(name):
    print('asset FileID for %r      : %08X   (RED MC: %s)' % (name.lower(), crc(name.lower()), be(crc(name.lower()))))
    print('container hash for %r    : %08X' % (name.upper(), crc(name.upper())))

def cmd_sort(path):
    d = bytearray(open(path, 'rb').read())
    n, fo = u32(d, 0x18), rel(d, 0x1C)
    ents = []
    for i in range(n):
        p = fo + 4 * i
        eo = p + u32(d, p) - 1
        ents.append((u32(d, eo), eo))
    if [e[0] for e in ents] == sorted(e[0] for e in ents):
        print('%s: already sorted, nothing to do' % path); return
    ents.sort()
    for i, (fid, eo) in enumerate(ents):
        p = fo + 4 * i
        struct.pack_into('<I', d, p, (eo - p + 1) & 0xFFFFFFFF)
    open(path + '.bak', 'wb').write(open(path, 'rb').read())
    open(path, 'wb').write(bytes(d))
    print('%s: row list sorted (%d rows). Original saved as %s.bak' % (path, n, path))

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(__doc__); sys.exit(1)
    c, a = sys.argv[1], sys.argv[2]
    {'info': cmd_info, 'scan': cmd_scan, 'hash': cmd_hash, 'sort': cmd_sort}[c](a)
