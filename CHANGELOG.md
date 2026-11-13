# Changelog

## 2.2.1

Two crash fixes. Nothing removed, nothing looks different.

- **Crash late in a game** (cloth, `nba2k14.exe+5AC55`, often in the 3rd or 4th quarter, a moment after a transition screen): the game replaces its jersey and shorts cloth data during a game, and BodyHook's own cloth objects (high-poly bench uniforms, own cloth) could be used for a moment after their data was gone. Now every one is checked before the game gets it; one whose data is gone is never used again and is rebuilt right away from the cloth the game has now. Costs a few millionths of a second per frame.
- **Crash 1 second after starting the game** (`BodyHook.asi+15157`): the start-up search could read memory outside `nba2k14.exe` that had just been freed. It now only looks inside the game's own code.
- **More room for cloth objects** (bench 64, own 32), so a whole bench can be rebuilt at once.
- `BodyHook.log`: a "PERF cloth safety" line says how often cloth data was replaced.
- `BodyHook.ini` from 2.2 keeps working unchanged.

## 2.2

For weak PCs. Nothing removed, nothing looks different.

- **No more waiting in the draw:** BodyHook checks every player model it draws. Sometimes that check had to wait for another part of BodyHook (a cloth being built, the Short Length watcher), a 3-14 ms freeze each time, several times a second on slow PCs. Now it never waits: it uses the answer from the last frame and checks again next frame.
- **Short Length** does nothing until someone actually has a length other than 5.
- **Cheaper memory-safety checks** behind every read (O(1) cache, 256 entries instead of searching 64).
- **`[Performance] Lite=1`** in `BodyHook.ini`: "potato" mode. The bench keeps the game's own models and its uniforms don't move. Off by default.
- `BodyHook.log` PERF lines also show memory checks and how often a draw didn't wait.
- `BodyHook.ini` from 2.1 keeps working (Lite is off when the line is missing).

## 2.1

Smoother. Nothing removed, nothing looks different.

- **Less stutter:** BodyHook builds its own jersey and shorts cloth for some players (bench, practice, players drawn with cloth of another body size). Each build takes the game 10-25 ms and ran while a frame was drawn, so several players at once could freeze a frame for about 60 ms. Now at most one player is built per frame; the others keep the game's cloth for a frame or two.
- **No rebuilds:** a player who comes back with the same body and uniform gets the cloth BodyHook already made for him.
- Cloth builds no longer hold the lock the other BodyHook hooks use.
- **Long-net exes:** the cloth-size check keeps 256 answers instead of 16.
- `BodyHook.log` PERF lines also show "draw guards" and "own cloth builds", and how many builds waited a frame or were reused.
- `BodyHook.ini` is unchanged: your 2.0 ini keeps working.

## 2.0

- **Runs under [SLAM](https://github.com/Dokitooo/SLAM)**, in its own folder `SLAM\BodyHook\` (asi, ini, log, `rosters\`).
- **MyCareer compatibility**, tested.
- **Short Length:** a "Short Length" row in Edit Player > Body and Head (0 shortest to 10 longest). Per player, used everywhere. Needs moving `cloth_shorts`.
- **Wingspan:** a "Wingspan" row (0 to 10, 5 = his own arms). Upper arm and forearm get longer or shorter; the hands keep their shape and the ball stays in his hands.
- **Choices saved per roster file:** Hair Style, Short Length and Wingspan are kept in `SLAM\BodyHook\rosters\<roster>.ini`.
- **Saved with the game:** those Edit Player choices are written when the game saves the roster or save file, like the game's own Edit Player values. Load a roster or quit without saving and they are gone. The new value still shows and plays right away. They also follow Edit Player's Accept: leave a player without accepting and his Hair Style, Short Length and Wingspan go back too.
- **Edit Player camera:** each preview camera view can be zoomed and lifted (`[EditPlayerCamera]`).
- **Freestyle practice passers:** in Freestyle practice one or two players rebound and pass to you (pass modifier + shoot = alley-oop).
- **Moving cloth in practice and on the bench:** every player gets his own jersey/shorts cloth, so shorts no longer disappear or swap sizes.
- **Long-net exes:** jerseys and shorts keep the stock cloth size when the exe's net cloth was hex-edited longer.
- **Edit Player fix:** stepping Height / Weight across the fat body size no longer tears the shorts.
- Faster loading of players and crash-safety checks throughout.

## 1.1

- **High Poly Bench** model fix.
- **Moving Cloth compatibility:** issues with cyberfaces that use non-moving cloth are fixed. My Wembanyama should work fine regardless of the tights you use.
- **Real-time Hair Style and cyberface switching**, thanks to Blessie 2K.
- **Two team colours on accessory textures**, set in `BodyHook.ini`. For modders.
- **MyCareer** incompatibility fixes.
- Optimization fixes.

## 1.0

- **Custom bodies:** each player uses the body in his own png, whatever his body type.
- **Custom leg tattoos.**
- **Custom jerseys and shorts:** works with non-moving cloth models for now.
- **Custom accessory textures and normal maps:** sleeves, pads, bands, straps, socks, headbands and more.
- **Accessory combinations**, e.g. an arm sleeve with a shirt sleeve, or a leg sleeve with a knee pad.
- **Switch Hair Styles for players:** save copies of a cyberface as `png19600_hair1.iff`, `png19600_braids.iff` and so on, then pick one under Edit Player > Body & Head > Hair Style. Your choice is remembered even after closing the game.
- **Compatibility with game modes like MyCareer** (needs further feedback).
- **NBA logo patch on headbands only.**
- **Team-colour socks.**
- **Coach polo** (optional, off by default): coaches wear a polo instead of the suit jacket. Not final yet.
