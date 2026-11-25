# Touhou 20: Fossilized Wonders (Nintendo Switch)

A Nintendo Switch homebrew port of 東方錦上京　～ Fossilized Wonders, built
on the TH20 C++ runtime from eagler-touhou (`ports/th20`). Companion to the
Touhou Switch ports.

**You need your own copy of the game.** No game data is included in this
repository or in a build.

## Installing

1. Create `sdmc:/switch/touhou/touhou20/` and copy into it:
   - `touhou20.nro`
   - `th20.dat` (required, version 1.00a)
   - `thbgm.dat` (music; without it the game runs silent)
   - `msgothic.ttc` (menu and dialogue text, from `C:\Windows\Fonts`)
2. Launch it from the Homebrew Menu. Saves go next to the data, like the PC
   release: `scoreth20.dat`, `th20.cfg`, `replay/`, `snapshot/`.

> 💡 The game uses about 200 MB for itself plus its textures on the GPU.
> Launch hbmenu through a game (hold **R** while starting any game) for full
> RAM; from the album it may run out of memory.

The screen is drawn at 1280x960 (2x): portraits, dialogue, text and the HUD
are sharp. The playfield layers (stage background, bullets and the
full-screen spell card and bomb effects) are drawn at 640x480 and scaled up:
they are where nearly all of the GPU's work is, and at 2x the heaviest
effects need more memory bandwidth than the Switch has. The scale never
changes while playing. To change it, create an empty file with no extension
next to `th20.dat`: `hires` draws the playfield at 2x as well (sharper, but it
can slow down in the heaviest effects), `lowres` draws everything at 1x.
Delete the file to go back to the default.

The data folder can also be `th20` or `touhou20` directly on the SD card, in
`switch/`, `touhou/`, `games/` or `roms/`, or the folder the NRO is in.

## Controls

| Switch | Game |
| --- | --- |
| B | Shot / OK |
| A | Bomb / cancel |
| L / ZL | Focus |
| R / ZR | Skip dialogue (Ctrl) |
| + | Pause |
| − | Snapshot (the game's own, into `snapshot/`) |
| D-pad / left stick | Move |
| Touch screen | Upstream touch controls (drag to move, tap menus) |

Option → Key Config can rebind shot, bomb, focus and pause. Its button
numbers are the same as in the other Switch ports: 0 B, 1 A, 2 L/ZL,
3 R/ZR, 4 +, 5 X, 6 Y.

## How it is built

TH20's runtime is compiled to a wasm32 module (clang 18, wasi-libc, libc++
with wasm exceptions) and turned into C with wabt's `wasm2c`. The C is then
compiled for the Switch with devkitA64, together with a native host that
provides what the browser used to: SDL3 window, events, gamepad and audio
on top of SDL2, the GLES calls, FreeType text, and the WASI file system
mapped onto the data folder.

- The game's memory is one block reserved at start and grown in place, so
  loading a stage never copies it. Desktop test builds bounds-check every
  memory access of the module (a stray access stops the game with a message);
  the Switch build does not.
- Images: TH20's PNGs (8-bit RGBA) are decoded with libdeflate in one pass,
  about 2.7 times faster than libpng; other images go through libpng or
  stb_image. While the game sets up one texture, two worker threads on the
  other CPU cores already decode the next PNGs of the same file; a result is
  used only if the bytes the game then asks for are the ones decoded. Fonts
  are read into memory once, not glyph by glyph from the SD card.
- Saves (score file, replays) are LZSS-encoded natively with the game's own
  algorithm, comparing eight bytes at a time; the game encodes the same save
  data several times in a row, and a repeat reuses the last result.
- Draws that cannot change the picture skip work on the GPU: plain copies
  without blending, depth-only quads without colour writes, depth tests that
  always pass.
- Two hot spots of the game code are rewritten with the same results: the
  text outline (spell card names, dialogue) works row by row instead of
  testing every pixel against every offset, and the sprite layers are sorted
  into per-layer lists once per change instead of scanning every animation
  for each of the ~50 layers drawn per frame (a third of the game's CPU time
  in heavy bombs).
- `th20-switch.log` records the clocks the console runs at, frames over
  25 ms with where the time went (game code, image decoding, text, GL, file
  reads), and the GPU time per frame and per render target.
- One game tick per screen refresh (60 Hz, vsync), no catch-up ticks after a
  slow frame.
- `thbgm.dat` is read straight from the data folder with the game's own
  `thbgm.fmt` offsets and loop points.

```sh
./scripts/fetch_upstream.sh   # eagler-touhou at UPSTREAM_COMMIT + patches/native.patch,
                              # LLVM 18.1.3 libc++abi/libunwind, SDL3 headers, zlib,
                              # libdeflate 1.22
./scripts/build_wasm.sh       # build-wasm/th20/th20.wasm -> generated/ (Ubuntu 24.04:
                              # clang-18 lld-18 wasi-libc libc++-18-dev-wasm32
                              # libc++abi-18-dev-wasm32 libclang-rt-18-dev-wasm32 wabt)
./scripts/build_switch.sh     # build-switch/touhou20.nro (devkitPro devkitA64)
```

Linux test build of the same module: `cmake -S . -B build -G Ninja && ninja -C build`,
then `./build/touhou20 /path/to/game/folder`. It can play itself for
testing: `TH20_SCRIPT` (a file of `frame mask` lines, mask in hex),
`TH20_FRAMES`, `TH20_NOVSYNC`, `TH20_SNAP_EVERY`, `TH20_TEST_FLAGS` (1: the
player is never hit, 2: every layer list is checked against a full scan). `TH20_GLPROF=file` writes the pixels every draw writes
per render target (`TH20_GLPROF_FROM`, `TH20_GLPROF_TRACE`), `TH20_FRAMELOG`
the time split of every frame.

`patches/native.patch` is what the native build changes upstream: the
DirectInput axis range of the SDL host (it reported 0..65535 where the game
expects -1000..1000, so an idle pad read as right + down held), `thbgm.dat`
from the data folder, the default pad bindings, a GLSL name Mesa rejects,
image decoding, pixel conversion and LZSS encoding through the host, the
render-target scale of the playfield layers, the text outline and the
per-layer sprite lists above, and a stage load that waited forever on the
music queue when no audio device could be opened.

Every push to `main` builds `touhou20.nro` with GitHub Actions
(`.github/workflows/build-switch.yml`); tags (`v*`) also publish it as a
release with a SHA-256 checksum.

## Credits

- Touhou Project and all game content: ZUN / Team Shanghai Alice.
- TH20 C++ runtime: eagler-touhou by Ritosa2333, MIT ([confirmed by the author](https://github.com/Ritosa2333/th10/issues/3)).
- libdeflate by Eric Biggers (MIT), fetched by `scripts/fetch_upstream.sh`.
- Switchbrew and devkitPro: libnx and the Switch toolchain.
- Switch port: Saekaze.

**Licensing:** the Switch port code in this repository (`native/`, `wasm/`,
`scripts/`, build files) is CC0 1.0 (see `LICENSE`), except the Khronos GLES
headers in `wasm/compat/GLES2`, `GLES3` and `KHR`, which keep their licence
lines (MIT / Apache-2.0). The TH20 runtime is
downloaded from eagler-touhou at build time and is Ritosa2333's code under
the MIT licence, as is what `patches/native.patch` changes in it (see
[`LICENSE-Ritosa2333.txt`](LICENSE-Ritosa2333.txt)). The libraries fetched at
build time (libdeflate, LLVM's libc++/libc++abi/libunwind, SDL3 headers,
zlib), wabt's wasm2c runtime and the devkitPro libraries keep their own
licences.
