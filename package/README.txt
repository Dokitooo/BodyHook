===============================================================================
  BodyHook 2.0  -  Per-player bodies, tattoos, uniforms and accessories
                   for NBA 2K14 PC
===============================================================================

NEW IN 2.0
----------
  * SLAM mod: BodyHook now runs under SLAM (the NBA 2K14 mod loader) with its
    own folder SLAM\BodyHook\ (asi, ini, log, rosters\). A plain .asi for any
    ASI loader is included too.
  * SHORT LENGTH - Edit Player > Body and Head has a "Short Length" row
    (0 shortest .. 10 longest, 5 = the shorts as your cloth file has them).
    Per player, used everywhere (menus, games, replays). Needs MOVING
    cloth_shorts.
  * WINGSPAN - a "Wingspan" row under Short Length (0 .. 10, 5 = his own
    arms): upper arm and forearm get longer or shorter, the hands keep their
    shape, the ball stays in his hands.
  * PER-ROSTER SETTINGS - Hair Style, Short Length and Wingspan choices are
    saved per roster in BodyHook\rosters\<roster>.ini (when the game saves
    the roster); saving the roster under a new name copies them along.
  * SAVES WITH THE GAME - those Edit Player choices are written when the
    GAME saves the roster / save file, like the game's own Edit Player
    values: load a roster or quit without saving and they are gone. The new
    value still shows and plays right away. (NBA 2K12: written right away.)
  * EDIT PLAYER CAMERA - each preview camera view can be zoomed and lifted
    ([EditPlayerCamera]); the ini is re-read while the game runs.
  * HAIR STYLE LIVE REFRESH - picking a hair style rebuilds the preview
    player right away.
  * FREESTYLE PRACTICE PASSERS - in Freestyle practice one or two players
    rebound and pass to you (pass modifier + shoot = alley-oop); no
    violations, dead balls or control swaps in Freestyle ([Practice]).
  * MOVING CLOTH in practice and on the bench: every player gets his own
    jersey/shorts cloth, so shorts no longer disappear or swap sizes.
  * LONG-NET EXES: jerseys/shorts keep the stock cloth size when the exe's
    net cloth was hex-edited longer (nets stay long).
  * EDIT PLAYER: stepping Height / Weight across the fat body size no longer
    tears the shorts; the preview is rebuilt for the new body size.
  * NBA 2K12 (plain .asi): the 1.0x patch, 1.0 original and SecuROM retail
    exes are supported; elbow sleeves keep the arm under them visible
    ([Accessories] KeepArmUnderElbowSleeve).
  * Faster png scanning (cache), fewer stalls when players load, crash-safety
    checks throughout.


INSTALL
-------
SLAM (recommended): copy SLAM\BodyHook\ into the game's SLAM folder
  (needs SLAM 1.0: the SLAM folder + dinput8.dll in the nba2k14.exe folder).
  Log: SLAM\BodyHook\BodyHook.log

Plain .asi (any ASI loader, no SLAM): put plain\BodyHook.asi where your loader
  reads .asi files (game folder or scripts\) and the plain\BodyHook\ folder
  (BodyHook.ini) next to it. Log: BodyHook\BodyHook.log
  Use ONE of the two, not both.

Already using BodyHook? Keep your own BodyHook.ini and rosters\ folder.
UNINSTALL: delete the BodyHook folder (and the plain .asi if you used it).
DISABLE WITHOUT REMOVING: Enabled=0 in BodyHook.ini.


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
  4. The preview updates right away (LiveRefresh=1); games, menus and
     replays always load the chosen file.

The choice is saved (when the game saves the roster) per CFID and per roster in BodyHook\rosters\<roster>.ini,
so it stays after restarting the game and does not change your roster.
Pick Original to go back to png<CFID>.iff.
Each variant is a full cyberface, so it can also carry its own body,
uniform, tattoo or accessory rows; BodyHook reads the rows of the chosen file.
Created (CAP) players are not affected: they keep the game's own list.


REQUIREMENTS
------------
  * NBA 2K14 PC (32-bit), RLD build (exe TimeDateStamp 52202C7F). Other
    builds: the log says what it could not find and the game runs normally.
  * Custom pngXXXX.iff files placed loose in the game folder.
  * RED MC (or any IFF editor that can add rows and set FileIDs).


SETTINGS
--------
Every setting is explained inside BodyHook.ini.


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


TROUBLESHOOTING
---------------
Open BodyHook.log. (SLAM\BodyHook\BodyHook.log, or BodyHook\BodyHook.log next to the plain .asi)
A working install shows near the top:
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
    SHORT LENGTH ClothMesh ... length 7          a player's shorts length
    ROSTER  "MyRoster.ROS" loaded -> rosters\MyRoster.ROS.ini   per-roster settings
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