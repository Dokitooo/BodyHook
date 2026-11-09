# Changelog

## 2.0

BodyHook is now a [SLAM](https://github.com/Dokitooo/SLAM) mod, in its own folder `SLAM\BodyHook\` (asi, ini, log, `rosters\`).

- **Short Length:** a "Short Length" row in Edit Player > Body and Head (0 shortest to 10 longest). Per player, used everywhere. Needs moving `cloth_shorts`.
- **Wingspan:** a "Wingspan" row (0 to 10, 5 = his own arms). Upper arm and forearm get longer or shorter; the hands keep their shape and the ball stays in his hands.
- **Per-roster settings:** Hair Style, Short Length and Wingspan are saved per roster in `SLAM\BodyHook\rosters\<roster>.ini`. Saving the roster under a new name copies them along.
- **Edit Player camera:** each preview camera view can be zoomed and lifted (`[EditPlayerCamera]`).
- **Hair Style live refresh:** picking a hair style rebuilds the preview player right away.
- **Freestyle practice passers:** in Freestyle practice one or two players rebound and pass to you (pass modifier + shoot = alley-oop).
- **Moving cloth in practice and on the bench:** every player gets his own jersey/shorts cloth, so shorts no longer disappear or swap sizes.
- **Long-net exes:** jerseys and shorts keep the stock cloth size when the exe's net cloth was hex-edited longer.
- **Edit Player fix:** stepping Height / Weight across the fat body size no longer tears the shorts.
- Faster png scanning, fewer stalls when players load, crash-safety checks throughout.

## 1.1

- **Accessory textures and normal maps** per player, each one independent: sleeves, pads, bands, straps, wraps, socks, headband, undershirt, shirts, pressure shorts.
- **Live png refresh:** rows added to a png while the game runs are picked up; save the png and reopen Edit Player.
- **Accessory combinations** the stock game won't allow: arm sleeve with a shirt sleeve or elbow pad, leg sleeve with a knee pad or socks.
- **Coach Polo:** coaches wear a polo instead of the suit jacket (off by default).
- **NBA patch on headbands only** (`[NBAPatch] HeadbandOnly=1`).
- **Team colour socks:** every sock type takes the Team 1 / Team 2 colours.
- **High-poly bench:** substitutes use the same high-poly model as on the court.
- **Hair Styles:** regular players get a Hair Style row in Edit Player that switches between `png<CFID>_<name>.iff` copies of their cyberface.
- Moving cloth also in games; works in MyCareer and when a player's face ID differs from his portrait ID.

## 1.0

- Per-player **body**, **leg tattoos** and **jersey/shorts**, read from each player's cyberface file (`pngXXXX.iff`). Everyone else keeps the game's defaults.
- Works with moving cloth files: players with a custom uniform are drawn static, everyone else keeps moving cloth.
- Installs as `dinput8.dll` (or `BodyHook.asi` with an ASI loader) plus `BodyHook.ini` in the game folder.
