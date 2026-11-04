WHAT'S NEW in 2.0 (first public release; same code as test build 1.11.26-ML)
========================
PER-ROSTER SETTINGS. Hair Style, Short Length and Wingspan are now kept in ONE file per roster, like
GotoShotExt and SigExt: SLAM\BodyHook\rosters\<roster file>.ini with [Hair] [ShortLength] [Wingspan].
The file of the roster the game loads is used (a .ROS / .CMG / .FRA), only for that roster; before any
roster is loaded (the default roster) rosters\(default).ini. Saving the roster under a new name copies
the file to that name. Lines: CFID=value ; name (the name is just a label).
GENERIC / CREATED FACES. Players without their own cyberface (the default roster's old teams: CF_ID -1 or a
placeholder shared by many players, e.g. every John Brown) now get Short Length and Wingspan too, kept by
their roster ID (the ID a roster editor shows, stored in every player record):  G569=8 ; John Brown.
The name is a check: if that roster ID later holds another player, the values are not used for him.
Hair Style variants are png<CFID>_<tag>.iff files, so they stay for players with a cyberface (NBA 2K14).
OLD FILES: BodyHook_Hair.ini / BodyHook_Shorts.ini / BodyHook_Wingspan.ini (used for every roster) are copied
once into the first roster file the game loads while there is no roster file yet, then renamed *.ini.old.
SLAM's settings keeper no longer writes .ini.backup / .ini.new / "[SLAM] InUse=1" (old ones are removed).
BodyHook.log: "ROSTER  ..." lines.

1.11.25-ML
NEW: WINGSPAN (NBA 2K14). Edit Player > Body and Head gets a "Wingspan" row right under Short Length:
0..10, 5 = the player's own arms, 10 = the longest (upper arm + forearm x1.15), 0 = the shortest (x0.85).
Saved per player (CFID) in BodyHook_Wingspan.ini; shows at once in Edit Player and is used in menus,
frontend animations, games and replays. The ball moves with the longer / shorter hands (games and every
other screen); with both hands on the ball, both hands move the same way so they stay on the ball.
Players with 5 cost nothing. Ini: [Wingspan] Enabled=1, MaxPercent=15.
BodyHook.log: "WINGSPAN INSTALLED: pose job 00BF1750, skin 00BF1ECB, ball 009C5275 + 009C52A6".
If the game code is not exactly as expected (other exe / another mod), Wingspan stays off - nothing is changed.

1.11.24-ML
========================
FIX: Edit Player / random crashes in the game's memory code (nba2k14.exe+99D642). Something wrote into
memory the game had already freed; the game crashed later when it handed that memory out again:
- A png's own static uniform model (e.g. cloth_v) or ClothMesh got into the SHARED uniform cache when the
  global cloth file did not have it (the guard searched again and took the png's copy). Every player then
  wore it (overlap), and after that png was unloaded the shared cache pointed at freed memory. Now a file
  that only a png has stays out of the shared cache; that player still gets his own png's model.
- Edit Player: changing Weight / Height of a player swapped the preview's moving shorts for cloth of the
  new body size on the old model (torn shorts) and rebuilt it every 1.5 s. The preview keeps the game's cloth.
- Short Length: the 200 ms check wrote into game cloth objects that could already be freed. Game cloth now
  gets its length only when it is built or drawn. Its copies are no longer remade every 1.5 s.
- BodyHook builds its own cloth only from a valid ClothMesh that fits its memory.
- NBA 2K12: crash in the menu text code (wcslen) on the Short Length / Hair Style row text.

1.11.23-ML
========================
FIX: random crashes during games (after the 1st / 4th quarter, practice, Edit Player) since Short Length.
The game simulates cloth on its own worker thread after the player model has been drawn. BodyHook's own
cloth objects (bench players, "OWNCLOTH" own cloth, static-cloth guard) were rebuilt IN PLACE when a
record got another player or uniform - while the game's worker could still be simulating the old one:
crash in the game's cloth code (crash dump: cloth worker thread, BodyHook's bench shorts object).
Now a rebuilt object always goes into another slot; the old one is left untouched until it has not been
used for 3 seconds. A record is rebuilt at most once every 1.5 s, so the repeating "OWNCLOTH ... CFID ..."
lines (a rebuild every frame) are gone too. NBA 2K12: the bench / own-cloth check read the 2K14 record
fields (+98 / +208) - wrong fields that could change at any moment and trigger rebuilds; fixed.
Also: Edit Player crash on some players (garbage uniform pointer) - BodyHook no longer asks the game's
jersey getter about such a record.


BodyHook 2.0 - SLAM edition (NBA 2K14 / NBA 2K12)
=======================================================
This is BodyHook 2.0 as a SLAM mod (per-roster settings need SLAM's import hooks: SLAM edition only).
What changed is how it loads: SLAM (dinput8.dll + the SLAM folder next to the game's exe) loads it,
shares its game changes with the other mods and reports conflicts and crashes.

NEEDS: SLAM 1.0.0 or newer. Without SLAM a small "SLAM not installed" window appears and the game
starts without BodyHook.

INSTALL
  1. Install SLAM first (see SLAM's README).
  2. Remove the old BodyHook: dinput8.dll (the BodyHook one - SLAM's dinput8.dll replaces it) and BodyHook.asi (game folder or scripts\).
  3. Drag the folder "BodyHook" from this zip into the game's SLAM folder. That's all:
         SLAM\BodyHook\BodyHook.asi
         SLAM\BodyHook\BodyHook.ini
  4. Start the game. The mod list at the top left shows "BodyHook 2.0   SLAM".

YOUR SETTINGS
  SLAM never changes your BodyHook.ini (no "[SLAM] InUse" line, no .ini.backup, no .ini.new).
  Updating: dragging a new folder over the old one replaces BodyHook.ini with the download's default - keep a
  copy of yours first (or skip the .ini). Your Hair / Short Length / Wingspan choices are in rosters\ (not in the zip).

LOG
  SLAM\BodyHook\BodyHook.log (no longer next to the exe). SLAM's own log: SLAM\SLAM.log.

CRASH REPORTS
  BodyHook's own crash logger is gone: SLAM writes a crash report for every crash (SLAM\crash\)
  and shows a window with the likely cause. [Advanced] CrashReport in BodyHook.ini is no longer used.

NBA 2K12
  SLAM 1.0.0 recognises the 2K12 builds (dinput8.dll next to nba2k12.exe), but SLAM itself has only been
  tested with NBA 2K14. Blaze Shot Meter 12: rename Blaze's dinput8.dll to dinput8_next.dll (game folder,
  next to its BlazeShotMeter12\ folder) - SLAM loads it before the mods.

Everything below is the mod's original README. Where it talks about installing, loaders, drop-in
DLLs (dinput8 / winhttp / xinput1_3), crash reports or where the log is, the lines above apply.
=====================================================================================================

BodyHook 1.11.24-ML (NBA 2K14 + NBA 2K12)
=========================================
NBA 2K12: EVERY KNOWN EXE. The 2K12 part only worked with the exe it was made on (the 1.0x patch,
RLD, TimeDateStamp 4F219A21); with any other nba2k12.exe it stayed off, and an exe with another name
(e.g. "NBA 2K25 REMASTERED.exe") was not even seen as 2K12. Now BodyHook knows three builds and picks
the right address table itself (BodyHook.log: "using the address table of ..."):
  4F219A21  NBA 2K12 1.0x patch (RLD)                        - as before
  4E63057D  NBA 2K12 1.0 original, also renamed "NBA 2K12/2K25 REMASTERED.exe", nba2k13.exe - mapped
            from the RLD build and checked against its code (accessory combinations: same 97/97 result)
  4E84D35F  NBA 2K12 1.0 SecuROM retail (with paul.dll)      - mapped from the part of the exe that is
            not encrypted; the rest is found in the running game. Not tested in game yet.
  Every site is still checked before it is patched: anything that does not match stays stock.
  The NBA patch (headband only) and the practice cloth guard now find their code in the running exe.
NBA 2K12: SHORT LENGTH. Edit Player > Body gets the "Short Length" row under Weight (0..10, 5 = the
cloth file's shorts), as in NBA 2K14: moving cloth_shorts, saved per CFID in BodyHook_Shorts.ini,
used in menus, games, replays, practice and on the bench.
NBA 2K12: ELBOW SLEEVES no longer cut out the elbow (body parts 0-9 / 0-10 stay visible): now on by
default, [Accessories] KeepArmUnderElbowSleeve=1 (2 = elbow pads too, 0 = stock).
BLAZE SHOT METER 12 (and any other mod that only comes as dinput8.dll) works next to BodyHook:
  rename Blaze's dinput8.dll to dinput8_next.dll (keep its BlazeShotMeter12 folder in the game folder)
  and use BodyHook's dinput8.dll. BodyHook loads it before the game starts (its in-game renderer needs
  that) and passes DirectInput on to it. Or: put Blaze's dinput8.dll + BlazeShotMeter12 folder into
  scripts\ - a dinput8.dll in scripts\ (or a subfolder) is loaded like an .asi.
  BodyHook.log: "mods: loaded scripts\dinput8.dll (dinput8-only mod)" / "dinput8 proxy -> ...dinput8_next.dll".
  Blaze itself supports the 4F219A21 and 4E63057D exes.


BodyHook 1.11.20-ML (NBA 2K14 + NBA 2K12)
=========================================
FIX: a replaced moving cloth_shorts came back. When a shorts cloth is loaded again (you swapped the
cloth_shorts in a player's png - LivePngRefresh reloads it - or a mode change reloads the cloth files),
the new shorts can land at exactly the same memory addresses as the old ones. Short Length kept its
copies by address only, so after you went back to your own shorts, changing the length (anything but 5)
gave the player the length copy of the shorts you had replaced - the other shorts came back (the
Short Length 4 picture). Practice then drew him with cloth built from the old shorts' data: the grey
triangle across the floor and no shorts on the legs.
Now every length copy and every kept cloth set (bench, practice own cloth) remembers WHAT shorts it was
made from (a fingerprint of the ClothMesh's content, not only its address). When the shorts behind it
change, the copy is made again from the shorts that are loaded now and the kept cloth sets are built
again. BodyHook.log: "SHORT LENGTH  ClothMesh ... was loaded again with other shorts - its length N copy
is made again". Off-game test (source\tools\test_shortlen, part 6, ROW2=...): loading other shorts at the
same address now gives a new copy made from them (the old build gave the first shorts' copy back).


BodyHook 1.11.19-ML (NBA 2K14 + NBA 2K12)
=========================================
FIX: practice mode with a png-uniform player on the team. Practice draws its player with one moving
cloth set that the game built for someone else. With a player whose png has his own (static) shorts
on the team - 19600 with shorts_thin in his png - that set ended up made for a thin body, so every
player in live practice wore thin moving shorts at the cloth file's length (replays and games build
cloth per player, so they were right). Now, when a player is drawn with jersey / shorts cloth that
was made from another ClothMesh than his own body size + uniform style needs, BodyHook gives him his
own cloth for that draw (built exactly like the game builds his; a png static garment stays static),
and Short Length applies to it. BodyHook.log: "OWNCLOTH CFID ...: node ... had shorts cloth made for
another body size / uniform ... - his own used". Normal games: nothing changes (their cloth matches).


BodyHook 1.11.18-ML (NBA 2K14 + NBA 2K12)
=========================================
FIX: Short Length in practice mode, for real this time. Your log showed why 1.19.6 still gave 1013
the 10: practice draws its one player with a cloth set that the game built for somebody else
(19220 here) and never rebuilds it, so the length was taken from the wrong player. Now the length is
set from the player the game DRAWS with that cloth, every time the player model is used (a quick
check per model; it only does work when the player changes). Bench players with moving cloth get
their length the same way.


BodyHook 1.11.17-ML (NBA 2K14 + NBA 2K12)
=========================================
FIX: Short Length in practice mode. Practice (and some menus) keep a player's moving cloth when
another player takes over his spot, so the new player wore the old player's length (e.g. 1013 set
to 0 showed 19220's 10; live games and replays were right because they build new cloth). BodyHook
now checks the player behind every moving shorts cloth 5 times a second and gives it that player's
length as soon as he changes (5 = the cloth file's own shape).
PERFORMANCE: the png cache is now BodyHook_pngcache_ML.bin, so it no longer fights with the
2K14-only edition's BodyHook_pngcache.bin when you switch editions (that forced a full re-read of
every png at start). The first start reads all pngs once, then the cache is used again.


BodyHook 1.11.16-ML (NBA 2K14 + NBA 2K12)
=========================================
FIX: the Short Length row did not show up. Short Length looked for the Hair Style row (its template)
after the Hair Style feature had already replaced that row's text callback with BodyHook's own, so the
search no longer recognised it ("WARNING short length: Edit Player 'Hair Style' record not found" in
BodyHook.log). Now it recognises the row both ways. The off-game test now also runs that way.
BodyHook.log should show "SHORT LENGTH INSTALLED: Edit Player > Body and Head row 3 (after Weight)".


BodyHook 1.11.15-ML (NBA 2K14 + NBA 2K12)
=========================================
NEW: SHORT LENGTH (Edit Player > Body and Head)
- A new row "Short Length" right under Weight (Hair Style, when it shows, stays below it).
  0 = shortest, 10 = longest, 5 = the shorts exactly as your cloth_shorts file has them.
  Left / right changes it; each step moves the hem about 2.3 cm.
- Per player: saved by CFID in BodyHook_Shorts.ini (next to nba2k14.exe) and used everywhere:
  menus, games, replays. In Edit Player the shorts change right away.
- MOVING cloth_shorts: that player's own cloth gets longer or shorter leg tubes (the waistband and
  crotch stay where they are, the cloth physics fit the new length). Nothing shared is changed, so
  other players, jerseys and bodies are not affected.
- NONMOVING (static) shorts keep their length for now: their shape sits in one GPU buffer that every
  player shares. Turn them into moving shorts with the Jersey Cloth Editor, then Short Length works.
- Regular players only (created / MyCareer players have no CFID of their own, so no row). NBA 2K14
  only. Needs [Uniforms] Enabled=1 (the default). [ShortLength] Enabled=0 turns it off.
- Tested off-game on the game's own code: the menu list (Height, Weight, Short Length, Skin Tone ...),
  the label / help text through the game's text lookup, the row's visibility through the game's
  visibility check, the value text through the game's formatter, left/right 0..10 and the ini, and
  the shorts in the game's cloth solver at lengths 0-10 (standing, running, and changed live).
  Not tested in game yet.


BodyHook 1.11.14-ML (NBA 2K14 + NBA 2K12)
=========================================
STUTTER FIX (worse in instant replays) - every feature works as before
- Bench players' moving cloth (high-poly bench, BenchUniforms=1) was simulated twice per frame or
  more: the game runs the cloth update from two model functions, and BodyHook gave the bench players'
  jersey/shorts a time step in both. Replays run more update passes, so it got worse there. Up to
  30 bench players x jersey + shorts = a lot of extra cloth work per frame on the game thread.
  Now each bench garment is simulated once per frame (its cloth also no longer moves at double speed).
- The bench code asked the game for the cloth memory size on every bench model call (hundreds per
  frame); now once every 2 s.
- Bigger memory-check cache (16 -> 64 entries): fewer Windows VirtualQuery calls per frame.
- The PERF line in BodyHook.log now also shows "bench" (BodyHook's bench work + the game drawing and
  simulating those bench players), so a log tells whether the bench is what costs time.
If a very weak PC still stutters: [Bench] ReplayBench=stock (stock bench model in replays) or
HighPolyBench=0 (stock bench everywhere, like 1.9.x).


BodyHook 1.11.13-ML (NBA 2K14 + NBA 2K12)
=========================================
PERFORMANCE (for low-end PCs) - every feature works exactly as before
- No game thread ever waits for the disk any more. Two user logs showed why BodyHook could stutter:
  * at start BodyHook read the row table of every png*.iff (3,000-4,000 files) while holding the lock
    the hooks use - on a slow / HDD / USB drive the game froze for 1-2 s again and again in the first
    2 minutes ("SLOW acc-combo hook took 1838 ms"). Now the reading holds no lock, runs at background
    (low) disk priority, reads 4 KB per png instead of 64 KB, and the result is kept in
    BodyHook_pngcache.bin: the next starts only open pngs that are new or changed (a few ms instead
    of minutes).
  * a png whose container did not answer made EVERY lookup wait PngLoadWaitMs (150 ms) - in Edit
    Player with hair styles that was 1-2 s of waiting every 30 s ("legs 63x/1371ms"). Now a png
    that timed out once is not waited for again for 60 s (or until it is saved / switched); the first
    lookup still waits, so a png that is really still loading is not missed.
- LivePngRefresh: saved / new pngs are now found by a background watcher that Windows notifies
  (nothing is checked while nothing changes). Before, the game threads checked the files on disk
  themselves. Drives that can't notify (some network / FAT drives): it checks the pngs used in the
  last minute once a second, still in the background.
- BodyHook.log is written by its own thread; logging never makes a hook wait for the disk.
- Accessory combinations, team colour sock layouts and the log each have their own lock, so they never
  wait behind each other. The layout found for a sock texture is remembered in BodyHook_cache.ini,
  so its pixels are read once ever instead of once per session.
- Edit Player camera live reload re-reads BodyHook.ini only when it was saved (was ~60 ini reads per
  second for the whole session). The hair live-refresh helper sleeps 40 ms instead of 8 ms while idle.
- The scripts\ folder for .asi mods is created at the first start if it is missing.
New files next to nba2k14.exe: BodyHook_pngcache.bin and BodyHook_cache.ini (caches - delete them any
time, they are rebuilt).


BodyHook 1.11.12-ML (NBA 2K14 + NBA 2K12)
=========================================
1.11.12 with the MOD LOADER of 1.19.1 added - nothing else changed (no sweat / lighting / DOF;
those are 1.17+ and 2K14 only). Its dinput8.dll loads every .asi mod in the game folder, scripts\,
plugins\, asi\ and their subfolders (2 levels, e.g. scripts\SigExt\SigExt.asi), right before the
game starts. BodyHook.log lists each one ("mods: loaded ..."). Another mod's dinput8.dll: rename it
to dinput8_next.dll or dinput8_chain.dll. BodyHook loaded twice (dinput8.dll + BodyHook.asi): only
the first copy runs. BodyHook.asi (for other ASI loaders) is the hook only. See COMPATIBILITY.txt.

BodyHook 1.11.12
================
Signature camera lift: [EditPlayerCamera] View20Lift=60 moves the Signature camera and its aim 60 cm
up together, so the player sits lower and tall players' jump shots stay inside the screen. Signature
zoom default is now View20=0.6. Both can be tuned live (the ini is re-read every second).

BodyHook 1.11.11
================
Edit Player camera, per screen: Signature (camera view 20) is zoomed to twice as close
(View20=0.5); Vitals/Body (views 0/1) are back to the game's camera, just 10% closer (0.9).
Every view has its own View<N>= value in [EditPlayerCamera]; the ini is re-read every second.

BodyHook 1.11.10
================
Edit Player camera zoom: the Edit Player preview (Signature / animations, Vitals ...) no longer
shows the player tiny and far away. [EditPlayerCamera] Zoom=0.6 brings the camera 40% closer
(1.0 = the game's own camera). BodyHook.ini is re-read every second, so Zoom can be tuned while the
game runs. The log shows "EDIT CAMERA view N" for each camera view, and Views= picks which ones zoom.

BodyHook 1.11.9
===============
MyCareer crash fixed (game died ~1 s after a MyCareer cutscene with players and coaches loaded,
usually "after the 4th game"). The cutscene builds its people with the coach flag set, and the
coach rule ("coaches keep the stock bench model") switched them to the low-poly bench model -
which the game never loads in cutscenes, so drawing it read garbage and crashed
(nba2k14.exe+9B1DBE, found with the 1.11.8 crash report). The coach rule now only applies to
real game roster records; cutscene people keep the model they were built with
(log: "BENCH model ... cutscene) left as built").
The crash report from 1.11.8 stays on ([Advanced] CrashReport=1).

BodyHook 1.11.8
===============
Crash report, for tracking down crashes (MyCareer crash after a few games): BodyHook.log now
records every serious error the moment it happens (EXCEPTION lines: which file + offset, the
registers, the call stack) and, when the game dies, a CRASH block plus BodyHook_crash.dmp in
the game folder. Every minute there is a MEMORY line (address space used, largest free
block), so running out of memory over several games shows up as a trend.
It only watches - nothing in the game changes. [Advanced] CrashReport=0 switches it off.
After a crash, send BodyHook.log AND BodyHook_crash.dmp (both are in the game folder).

BodyHook 1.11.7
===============
Practice-mode stretched cloth, found with the diagnostics log: practice shows the team in a
"replay" view that draws everyone with ONE shared moving cloth set. A player whose jersey/shorts come
from his own png is static, and moving cloth on a static model = huge stretched triangles. Such a
player now gets his own static cloth for that view (log: STATIC lines). [Uniforms] StaticClothGuard=1
(0 = off). Normal games were not affected.

BodyHook 1.11.5
===============
Practice-mode cloth fix: practice mode reuses one player record for different players, and the
game only rebuilds a player's moving cloth when the RECORD changes. The previous player's moving
cloth was then simulated onto the new player's models - with a non-moving (png) jersey/shorts that
gives huge stretched triangles. BodyHook now notices when a record holds someone else and has the
game rebuild that player's cloth (the log shows "CLOTHPOOL slot ... now holds CFID ...").

BodyHook 1.11.4
===============
Bench uniform fix: a player whose bench cloth (BenchUniforms=1) was simulated at the bench and who
then came back to it after being on the court, after a quarter break or after a teleport in
practice mode had his jersey/shorts particles left where they were -> huge stretched triangles
across the court. The cloth is now placed on the body again whenever it was not drawn for a while,
moved to another render node, or has no time step; the cached cloth is rebuilt when a player record
is reused for someone else. If it ever happens again: [Bench] BenchUniforms=0 switches bench cloth off.

===============================================================================
  BodyHook  -  Per-player bodies, tattoos, uniforms and accessories
               for NBA 2K14 PC
===============================================================================

WHAT IT DOES
------------
NBA 2K14 normally shares one body, one set of leg textures, one set of
jersey/shorts models and one set of accessory textures between every player. If you put a custom body inside
one player's pngXXXX.iff, every player with the same bodytype ends up
wearing it.

BodyHook fixes that. Anything you add to a player's cyberface file
(pngXXXX.iff) is used by THAT player only. Everyone else keeps the default
from the game files. There's no overlap and no roster editing.

  * Custom BODY          - per player (hi_body rows)
  * Custom LEG TATTOOS   - per player (legs_XX_color rows)
  * Custom JERSEY/SHORTS - per player (jersey_* / shorts* rows)
  * Custom ACCESSORY TEXTURES and NORMAL MAPS - per player, each one
    independent (sleeves, pads, bands, straps, wraps, socks, headband,
    undershirt, shirts, pressure shorts...)
  * LIVE PNG REFRESH - rows you add to a png while the game runs are
    picked up; save the png and reopen Edit Player, no restart needed
  * ACCESSORY COMBINATIONS - wear accessories together that the stock game
    won't allow (arm sleeve with a shirt sleeve or elbow pad, leg sleeve
    with a knee pad or socks, ...)
  * COACH POLO - coaches wear a polo instead of the suit jacket
    (off by default, [CoachPolo] Enabled=1 to use it; ShowArms=1 shows
    coaches' whole arms - coaches only, never players)
  * NBA PATCH ON HEADBANDS ONLY - the unif_nbapatch texture is used on
    headbands and nowhere else ([NBAPatch] HeadbandOnly=1)
  * TEAM COLOUR SOCKS - every sock type takes Team 1 / Team 2 colours the
    way the striped long socks do (toggle in BodyHook.ini [TeamColorSocks])
  * HIGH-POLY BENCH - substitutes on the bench use the same high-poly
    model as on the court (hihead, own body, jersey/shorts) instead of
    the low-poly bench model. Replays: see [Bench] ReplayBench.
  * HAIR STYLES - regular players get a Hair Style row in Edit Player >
    Body & Head. It lists png<CFID>_<name>.iff copies of his cyberface;
    the one you pick is loaded instead of png<CFID>.iff (see below)
  * Works with MOVING cloth files: players with a custom uniform are drawn
    static, everyone else keeps moving cloth (in games too, where the cloth
    is set up before the cyberfaces finish loading).
  * Works in MyCareer and for any player whose face (CF_ID) differs from
    his portrait ID.

Nothing on disk is modified: not the exe, not global.iff, not the roster,
not the cloth files.


HAIR STYLES (regular players)
-----------------------------
Created players have a Hair Style list in Edit Player; players with a real
cyberface normally don't, because their hair is part of the png. BodyHook adds
one that switches between whole copies of the player's png:

  1. Copy png19600.iff and edit the copy's hair (RED MC, Blender, ...).
  2. Save it next to the original as  png19600_<name>.iff, e.g.
         png19600.iff          (original)
         png19600_hair1.iff
         png19600_braids.iff
     Use the same digits as the original file (png0195_hair1.iff for CFID 195).
  3. Edit Player > the player > Body & Head. Under Height and Weight there is
     now "Hair Style": Original, braids, hair1. Left/right to choose.
  4. The player model picks up the new file the next time it is loaded:
     back out to the player list and open him again (games and other
     menus always load the chosen file).

The choice is saved per CFID in BodyHook_Hair.ini (created next to the exe
the first time you change a hair style), so it stays after restarting the
game and does not change your roster. Delete a line there
(or pick Original) to go back to png<CFID>.iff.
Each variant is a full cyberface, so it can also carry its own body,
uniform, tattoo or accessory rows; BodyHook reads the rows of the chosen file.
Created (CAP) players are not affected: they keep the game's own list.


REQUIREMENTS
------------
  * NBA 2K14 PC (32-bit). Tested on the RLD build, exe TimeDateStamp 52202C7F.
    Other builds are found by pattern scan. If a build isn't recognised, the
    log says so and the game runs normally.
  * Custom pngXXXX.iff files placed loose in the game folder.
  * RED MC (or any IFF editor that can add rows and set FileIDs).


INSTALL
-------
1. Copy these two files into the folder that contains nba2k14.exe:
       dinput8.dll
       BodyHook.ini
   (BodyHook.asi is the same mod for people who already use an ASI loader.
    Use dinput8.dll OR the .asi, not both.)

2. If you already have another mod that is itself a dinput8.dll, rename that
   one to  dinput8_next.dll  first. BodyHook loads it, so both keep working.

3. Start the game. A file called BodyHook.log appears next to nba2k14.exe.

UNINSTALL: delete dinput8.dll (and BodyHook.ini).
DISABLE WITHOUT REMOVING: set  Enabled=0  in BodyHook.ini.


HOW TO MAKE CUSTOM CONTENT
--------------------------
See CHEATSHEET.txt for the step-by-step guide and every FileID.

Short version: open the player's pngXXXX.iff in RED MC, clone a row, import
your model or texture, give the row one of the FileIDs from the cheat sheet
and save. The player whose CFID is XXXX gets it. Nobody else does.


NOTES
-----
  * The roster BODYTYPE doesn't have to match. If the png has a body row,
    that player uses it (MatchAnyBodyID=1).
  * Jersey size (thin/reg/fat) follows the roster bodytype:
        skinny -> thin    normal -> reg    muscular -> reg    fat -> fat
    If the png only has one size, that one is used for every size.
  * Jerseys are matched by neck style (U, V, wishbone). A png with only a
    U-neck row changes the player only when his team wears a U-neck jersey.
  * Custom uniforms from a png are always drawn static (non-moving).
  * Accessory combinations are decided when the player is drawn. The Edit
    Player screen may still clear one slot when you equip another there.
    Set both slots with a roster editor and both are drawn in game.
  * If the game switches an accessory to another version of itself because
    of a second accessory (long sleeves turn into short sleeves with some
    wrist accessories), BodyHook keeps the game's version so two versions
    are never drawn on top of each other.
  * After adding rows in RED MC, check the log for "NOT sorted" warnings and
    run  python iff_tool.py sort pngXXXX.iff  if you see one (IFF Mod Studio
    saves rows sorted).
  * Live refresh: when a png in the game folder is saved while the game runs,
    the log shows "PNG CHANGED" and the new rows are used the next time the
    game loads that player (close and reopen Edit Player). If the game itself
    keeps an old copy of a texture it already loaded, it may still need the
    player to be reloaded (switch to another player and back).
  * Keep the roster bodytype between 0 and 3. Values 4-7 ("OTHER") are
    unsafe in the stock game as well.


SETTINGS (BodyHook.ini)
-----------------------
  Enabled=1                      master switch
  Log=1                          write BodyHook.log
  Diagnostics=0                  1 = detailed troubleshooting lines
  MatchAnyBodyID=1               any body row in the png is used
  LegTattoos=1                   per-player leg tattoos on/off
  MatchAnyLegID=1                any legs_XX row in the png is used
  [Uniforms] Enabled=1           per-player jerseys/shorts on/off
  MatchAnyInGroup=1              one size per garment is enough
  DisableClothSimForPngModels=1  png uniforms drawn static
  KeepPngsOutOfSharedCache=1     png uniforms never leak to others
  FastPngCheck=1                 skip players without custom rows (speed)
  LivePngRefresh=1               re-read a png when it is saved while playing
  PngLoadWaitMs=150              wait this long for a png that is still loading
  [Accessories] Enabled=1        per-player accessory textures on/off
  AllowCombinations=1            accessories in different slots all drawn
  TextureIDs=...                 accessory texture FileIDs (all stock ones)
  NormalMaps=1                   per-player accessory normal maps
                                 (texture FileID + "_n", worked out automatically)
  ExtraNormalIDs=5779DBB0        other normal maps (headband: unif_headband_normal)
  [CoachPolo] Enabled=0          1 = coaches wear the polo (default 0 = suit jacket)
  Texture= / NormalMap=          polo textures (default coach_suit_colorize/_normal)
  ShowArms=0                     1 = coaches in a suit show whole arms (players never)
  [NBAPatch] HeadbandOnly=1      NBA patch only on headbands (0 = stock)
  [TeamColorSocks] Enabled=1     socks use team colours (0 = stock socks)
  Accessories=...                other accessory textures that get team colours
  DetectStockMasks=1             stock red/blue textures coloured like red/green ones
  Style=1                        1: red=Team 1, green=Team 2, blue=sock colour
                                 2: red=sock colour, green=Team 1, blue=Team 2
  Socks=...                      sock texture FileIDs that get team colours
  [HairStyles] Enabled=1        Hair Style row for regular players (0 = off)
  ShowWithoutVariants=0          1 = show the row even without png<CFID>_*.iff
  OriginalLabel=Original         name shown for png<CFID>.iff itself
  [Bench] HighPolyBench=1        high-poly substitutes on the bench (0 = stock)
  ReplayBench=auto               auto / highpoly / stock for bench players in replays
  BenchUniforms=1                moving-cloth jersey/shorts for bench players (2/3 = static)
  [Players] 1612=off             ignore the body in png1612 (example)


TROUBLESHOOTING
---------------
Open BodyHook.log. A working install shows near the top:
    HOOK INSTALLED ...
    LEG HOOKS INSTALLED: 2 call site(s)
    UNIFORM HOOKS INSTALLED: jerseys 2, shorts 2
    CLOTH-SIM BRIDGE INSTALLED ...
    ACCESSORY TEXTURE HOOKS INSTALLED: 2 site(s) + 1 helper site(s) ... normal-map FileIDs
    ACCESSORY COMBINATIONS INSTALLED: 17 accessory group(s) ...
    TEAM COLOUR SOCKS INSTALLED: style 1 ...
    NBA PATCH HEADBAND-ONLY INSTALLED: 7 code site(s) and 14 accessory decal(s) ...
    (COACH POLO / COACH ARMS lines only when turned on in the ini)
    HAIR STYLES INSTALLED: item ... (4/4 callbacks) ...
    HIGH-POLY BENCH INSTALLED: composite ... Live bench: high-poly ...
    BENCH    cloth set is MOVING/NONMOVING -> replays use ...  (in game)
and a line for every png with custom rows:
    custom body: png1612.iff (CFID 1612) rows: hi_body

While playing, lines like these confirm what each player got:
    LOCAL    CFID=1612 ... from PNG1612       body from his own png
    DEFAULT  CFID=1285 ... from GLOBAL        stock body
    LEGS LOCAL~ png=PNG195 ...                leg tattoo from his png
    UNIFORM LOCAL CFID=195 ...                jersey/shorts from his png
    UNIFORM WAIT  CFID=19600 waited 50 ms ... his png was still loading
    ACC LOCAL  CFID=195 ... texture 7E289AFF     accessory texture from his png
    ACC LOCAL  CFID=195 ... normal map 550EE63B  accessory normal map from his png
    PNG CHANGED  png0195.iff was saved ...     rows re-read while playing
    HAIR STYLE   CFID 19600 -> hair1           you picked a hair style
    HAIR STYLE   png19600.iff -> png19600_hair1.iff   the variant was loaded
    PERF     last 30 s: all hooks together N ms ...  time BodyHook itself took

Nothing changes in game?
  - Is the png in the game folder, and is it listed in the log at startup?
  - Is the row TYPE right (SCNE for models, TXTR for leg/accessory textures)?
  - Is the FileID typed exactly as in the cheat sheet?
  - Does the player's CFID in the roster match the png number?
    (BodyHook reads CF_ID - the same field the game loads png<CFID>.iff
    from - not PortrID. The log line "CFID: record CF_ID +3C" confirms it.)

Lag when a player appears?
  The log writes a PERF line every 30 s with the time BodyHook itself used.
  If it is only a few ms, the pause is the game reading the player's png
  from disk (big cyberfaces - 10 MB and more - take longest). Log=0 in the
  ini also saves a little time.

Any ERROR or WARNING line in the log: send the whole log to the author.


CHANGES IN 1.9.3
----------------
  * Fixed: png uniform + MOVING cloth files exploded in games (stretched
    shards). The game builds every player's moving cloth at the start of a
    game, before the cyberfaces finish loading, so BodyHook could not see
    the png yet and left the cloth simulation on. It now decides from the
    png file on disk, and the uniform request waits for the png to load.
  * Fixed: independent features not working in MyCareer. BodyHook read the
    player's PortrID (portrait) instead of his CF_ID. They are equal in
    stock rosters, but not for MyCareer players (NOCRASH.CMG: portrait 1919,
    face png2594). It now reads CF_ID exactly like the game does.


CHANGES IN 1.10.0
-----------------
  * New: high-poly bench ([Bench] HighPolyBench=1). Live bench players use
    their court model. In replays: high-poly with NONMOVING cloth files, the
    stock bench model with MOVING cloth files (ReplayBench=auto), so the
    jerseys never disappear.

CHANGES IN 1.10.1
-----------------
  * Fixed: ReplayBench=auto took the stock MOVING cloth files for nonmoving,
    so bench players in replays had no jersey or shorts. It now checks the
    game's own cloth data (the log says "BENCH cloth set is MOVING").

CHANGES IN 1.10.2
-----------------
  * Fixed: bench players on the high-poly model had no jersey and shorts.
    The game only gives cloth to the 10 players on the court, and the court
    model draws a jersey/shorts only when the player has cloth. BodyHook now
    builds every other player his own static jersey/shorts ([Bench]
    BenchUniforms=1), so replays can use the high-poly bench model with
    MOVING cloth too.

CHANGES IN 1.10.3
-----------------
  * Bench players now get REAL moving cloth (their own cloth simulation),
    so the jersey and shorts follow the body instead of hanging in their
    rest shape ([Bench] BenchUniforms=1). The 1.10.2 static version is
    still there as BenchUniforms=2.

CHANGES IN 1.10.4
-----------------
  * Bench cloth now actually moves: the game only hands the cloth time step
    to the 10 players on the court, so bench cloth stayed frozen in its
    rest shape (sideways shorts). BodyHook now gives bench players the same
    time step.

CHANGES IN 1.11.0
-----------------
  * Fixed: coaches on the bench wore the polo outfit without arms. Coaches
    keep the stock bench model again ([Bench] Coaches=0).
  * Fixed: [CoachPolo] ShowArms=1 did nothing since 1.9.2 (it looked for
    "no uniform", but coaches have one). Coaches are now recognised by the
    game's own coach flag, and their arm skin is drawn too.
  * New: [TeamColorSocks] Accessories= - give team colours to ANY accessory
    texture (wristbands, sleeves, ...). Empty by default (socks only).
  * New: [TeamColorSocks] DetectStockMasks=1 - stock-painted textures
    (red main colour + blue logo) are coloured like the "all RGB" ones
    (green main colour + red logo), so they do not need repainting.

CHANGES IN 1.11.1
-----------------
  * Fixed: coaches wore the polo even with [CoachPolo] Enabled=0. The body
    files in use already had the polo built into the suit-jacket part
    (bodies edited with coach_polo.py), so the game drew the polo there.
    With Enabled=0 BodyHook now puts the stock suit jacket (part 0-17) back
    in every stock-layout body, including the bench body; the jacket then
    uses suitjacket_colorize B120A6EB. Enabled=1 = polo (0-18) with
    coach_suit_colorize F0752759, as before. The log says "COACH SUIT".

CHANGES IN 1.11.2
-----------------
  * Team colour textures: the layout check now samples the whole texture
    (1.11.x only looked at its first rows) and reads ARGB textures too.
    Red/blue textures get red = Team 2, blue = Team 1 - the same look as a
    red/green texture and as the striped long socks. [TeamColorSocks]
    RedBlueTextures= / RedGreenTextures= force a layout per FileID.

CHANGES IN 1.11.3
-----------------
  * Team colour textures: the red/blue check now really works. 1.11.2
    asked the game for the pixels, but the game hands back its texture
    object instead, so every texture came out "layout not recognised".
    BodyHook now reads the texture straight from the .iff file on disk
    (png<CFID>.iff, global.iff - compressed or not), once per file.
    Red/blue texture (red base, blue logo/accents): red = Team Color 2,
    blue = Team Color 1. Example: Team Color 1 red, Team Color 2 black ->
    black sock, red logo. Red/green textures are used as they are.
    The log shows e.g.
      TEAMCOLOR texture 7B24B4A0 in png195.iff: red/blue layout -> red =
      Team Color 2, blue = Team Color 1 (mostly red, no green; 1024x512 ...)
  * Note: most stock sock masks in global.iff are red/blue too, so they
    get the same swap. Want a texture used as it is? Put its FileID in
    RedGreenTextures=.

CHANGES IN 1.11.7
- Hair Styles: live refresh in Edit Player (NBA 2K14). Picking another Hair Style now rebuilds the
  preview player immediately, so the new png<CFID>_<tag>.iff shows without saving and re-opening
  the player. The head camera is held while the new head loads and re-framed when it is ready.
  This is the HairRefresh tool's behaviour built into BodyHook (no extra exe, no ASI loader, no
  ini polling). [HairStyles] LiveRefresh=1 (default) / 0 = off. Remove HairRefresh if you used it.
  Needs the RLD nba2k14.exe build; on anything else BodyHook logs that it is unavailable and
  picks apply on the next model load as before. NBA 2K12: unchanged.

