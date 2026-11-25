<img width="256" height="256" alt="Touhou 20" src="https://raw.githubusercontent.com/saekaze/th20-switch/main/platform/switch/icon.jpg" />

## Touhou 20 — 1.00a-r1 (first release)

A Nintendo Switch port of Touhou 20: Fossilized Wonders v1.00a, built on the TH20 C++ runtime from [eagler-touhou](https://github.com/Ritosa2333/eagler-touhou). Runs at full speed on stock clocks, no overclock needed.

* 🖼 **Sharp menus, portraits and text:** the screen is drawn at 1280×960, so dialogue, portraits, text and the HUD are crisp. The playfield (stage background, bullets, spell card and bomb effects) is drawn at 640×480, which keeps the heaviest effects at full speed. The picture keeps its 4:3 shape with pure black bars.
* 🧊 **No freezes from heavy frames:** one game tick per screen refresh and no catch-up ticks, so a slow frame can't snowball into a freeze.
* ⚡ **Stage loads about 70% shorter** than the first test builds (0.6–0.8 s instead of 2–3 s): TH20's PNGs are decoded 2.7× faster (63% less time) and the next images are decoded ahead on the other CPU cores.
* 🎴 **Spell card captures without a hitch:** the outline of spell card names and dialogue text is drawn 70–90% faster (it took about 30 ms per capture).
* 💣 **Heavy bombs:** the sprite layers are sorted once per change instead of being scanned for every layer drawn, for 51% less game CPU time in the heaviest bombs.
* 💾 Saves: when the game writes the same save data again, it takes 98% less time.
* 🎮 **Remappable controls** through Option → Key Config (defaults match the other ports: B shot, A bomb, L/ZL focus, R/ZR skip, + pause).
* 🔊 BGM streamed straight from `thbgm.dat` with the original loop points.
* 👆 Touch controls in handheld mode.
* 📁 Recommended folder: `sd:/switch/touhou/touhou20/`, next to the other ports.

Put `touhou20.nro`, `th20.dat`, `thbgm.dat` and `msgothic.ttc` in the folder and launch from hbmenu. If it runs out of memory from the album, start hbmenu by holding **R** while launching a game.

**Resolution options:** create an empty file with no extension next to `th20.dat`. A file named `hires` draws the playfield at 2x too: it is sharper, but it can slow down in the heaviest effects. A file named `lowres` draws everything at 1x. Delete the file to go back to the default.

**Known issue:** a few of the densest bullet patterns can still dip below 60 fps for a moment.
