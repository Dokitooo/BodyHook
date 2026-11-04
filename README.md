# BodyHook 2.0 — per-player bodies for NBA 2K14 (PC)

BodyHook gives every player his own body, leg tattoos, jersey/shorts models and accessory
textures from his cyberface file (`pngXXXX.iff`), and adds **Hair Style**, **Short Length** and
**Wingspan** rows to Edit Player, saved per roster. It is a [SLAM](https://github.com/Dokitooo/SLAM) mod.

**Download:** [BodyHook.zip](https://github.com/Dokitooo/BodyHook/releases/latest/download/BodyHook.zip) (latest release) ·
[all releases](https://github.com/Dokitooo/BodyHook/releases)

## Requirements

- NBA 2K14 PC (also runs on the supported NBA 2K12 builds, see `package/COMPATIBILITY.txt`)
- [SLAM](https://github.com/Dokitooo/SLAM/releases/latest/download/SLAM.zip) 1.0.0 or newer

## Install

1. Install SLAM first (see its README).
2. Remove any old BodyHook: the BodyHook `dinput8.dll` (SLAM's replaces it) and `BodyHook.asi`
   from the game folder or `scripts\`.
3. Copy the `BodyHook` folder from the zip into the game's `SLAM` folder:
   ```
   SLAM\BodyHook\BodyHook.asi
   SLAM\BodyHook\BodyHook.ini
   ```
4. Start the game. The mod list at the top left shows `BodyHook 2.0   SLAM`.

The log is `SLAM\BodyHook\BodyHook.log`. Per-roster choices (hair, shorts, wingspan) live in
`SLAM\BodyHook\rosters\`. The full manual is [`package/README.txt`](package/README.txt); the
custom-content reference is [`package/CHEATSHEET.txt`](package/CHEATSHEET.txt).

## Repository layout

| Path | Contents |
| --- | --- |
| `src/` | The mod (`bodyhook.c` and its `.inc` parts) |
| `slam/` | SLAM SDK header and glue |
| `build/` | Build scripts (MSVC, clang-cl on Linux) |
| `tools/` | `iff_tool.py`, test harnesses, NBA 2K12 address mapping |
| `package/` | The README, cheat sheet, compatibility notes and default `.ini` shipped in the zip |

## Build

From an *x86 Native Tools Command Prompt for VS*: `build\build_msvc.bat`, or on Linux with
clang-cl + lld-link: `build/build_linux_clang.sh`. Output: `bin\BodyHook.asi`.
`src/bodyhook.rc` holds the version resource used for release binaries.

## Verify your download

Every release lists the SHA-256 of each zip in `SHA256SUMS.txt` (also attached to the release).
Compare it with the hash of the file you downloaded:

```powershell
Get-FileHash .\BodyHook.zip -Algorithm SHA256
```

```bash
sha256sum -c SHA256SUMS.txt
```

## About antivirus warnings

The DLL/ASI files in this mod hook into `nba2k14.exe` at runtime, which some antivirus heuristics
flag on unknown files. To make the release easy to check:

- The full source is in this repository; the release binaries are built from it.
- Binaries are not packed, compressed or obfuscated, and the zip has no password.
- Every binary carries version information (right-click > Properties > Details).
- SHA-256 checksums are published with each release.

If your antivirus still flags a file, you can submit it as a false positive to the vendor, or
build it yourself from the source.
