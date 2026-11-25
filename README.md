# Touhou 20: Fossilized Wonders — Nintendo Switch Port
*(東方錦上京　～ Fossilized Wonders)*

![Platform](https://img.shields.io/badge/Platform-Nintendo%20Switch-e60012?style=for-the-badge&logo=nintendoswitch&logoColor=white)
![Status](https://img.shields.io/badge/Status-Playable-brightgreen?style=for-the-badge)
![License](https://img.shields.io/badge/Port%20Code-CC0%201.0-blue?style=for-the-badge)

A homebrew port of ZUN's 2025 bullet hell **Touhou 20: Fossilized Wonders** for the **Nintendo Switch** (Horizon OS).

This build runs the TH20 C++ runtime from [eagler-touhou](https://github.com/Ritosa2333/eagler-touhou) (`ports/th20`). The runtime is compiled to WebAssembly, turned into C with wasm2c and compiled for ARM64, driven by a native SDL2 host on an OpenGL ES 3 context — no Linux, Box64 or Wine involved.

Companion to the [Touhou Switch ports](https://github.com/saekaze/touhou-switch), with the same `touhou20.nro` + `sd:/switch/touhou/touhou20/` layout.

---

## 🆕 First release (1.00a-r1)

* 🖥 **Resolution:** menus, portraits, dialogue and the HUD are drawn at 1280×960 (2x); the playfield at 640×480 (1x), so the heaviest effects run at full speed on stock clocks.
* 🧊 One game tick per screen refresh, no catch-up ticks after a slow frame.
* ⚡ Stage loads take 0.6–0.8 s: images are decoded ahead on the other CPU cores.
* 🔊 BGM streamed straight from `thbgm.dat` with the original loop points.

---

## ✨ Key Features

* ⬛ **OLED-Friendly Pillarboxing:** the 4:3 picture is scaled to the screen height with pure black bars.
* 🖥 **Resolution options:** an empty file named `hires` (no extension) next to `th20.dat` draws the playfield at 2x too (sharper, but it can slow down in the heaviest effects); `lowres` draws everything at 1x.
* 🔊 **Full Audio:** sound effects plus BGM from your `thbgm.dat` (loop points from the game's own `thbgm.fmt`).
* 🎮 **Sane, Remappable Controls:** Joy-Con (handheld, grip, detached) and Pro Controller with the same default layout as every other Touhou Switch port; the in-game **Key Config** can rebind them.
* 👆 **Touch Screen:** in handheld mode, drag to move and tap menus (upstream's touch controller).
* 🈂️ **Japanese Text Without Windows:** text is drawn from `msgothic.ttc`, read once and kept in memory.
* 🌏 **Language-Aware Title:** hbmenu shows `東方錦上京　～ Fossilized Wonders` on consoles set to 日本語 and the romanised title everywhere else.
* 💾 **Saves Next to the Data:** `th20.cfg`, `scoreth20.dat`, replays (`replay/`) and snapshots are written into the SD folder the game loaded from, like the PC release.

---

## 📥 Installation Guide

> ⚠️ **Disclaimer:** In compliance with ZUN's guidelines and copyright law, this repository contains **ONLY the homebrew engine code**. No game assets are distributed. You must legally own a copy of *Touhou 20: Fossilized Wonders v1.00a*.

### 1. SD Card File Structure

1. Ensure your Nintendo Switch is running custom firmware (Atmosphère CFW).
2. Download the latest `touhou20.nro` from the [Releases](../../releases) tab (or build from source).
3. Create the folder `sd:/switch/touhou/touhou20/` and copy the following into it:

```text
sd:/switch/touhou/touhou20/
    ├── touhou20.nro          # Nintendo Switch homebrew executable
    ├── th20.dat              # Main game archive (v1.00a)
    ├── thbgm.dat             # Background music archive
    └── msgothic.ttc          # MS Gothic (C:\Windows\Fonts) - menu and dialogue text
```

`thbgm.dat` is optional (the game runs silent without it).

**Recommended place: `sd:/switch/touhou/touhou20/`.** Keeping every Touhou port in one `sd:/switch/touhou/` folder (`touhou6`, `touhou7`, `touhou8` …) is much tidier than a separate folder per game. Other places still work: the port first looks in its own folder (wherever the NRO is), then for a `th20` / `touhou20` folder directly on the SD card, in `switch/`, `touhou/`, `games/` or `roms/`.

> 💡 TH20 uses about 200 MB for itself plus its textures on the GPU. If it runs out of memory from the album, launch hbmenu through a game (hold **R** while starting any game) for full RAM.

### 2. Launching

Run `touhou20.nro` from the **Homebrew Menu (hbmenu)**, **Sphaira launcher**, or a home screen forwarder. If `th20.dat` is missing, the port says so and shows the folder it used. `th20-switch.log` is written next to the saves: it records start-up, the clocks, any frame that took longer than 25 ms and the GPU time per frame.

---

## 🕹 Controls

| Nintendo Switch Button | Action |
| :--- | :--- |
| **Left Stick / D-Pad** | Character Movement |
| **B** | Shoot / Confirm |
| **A** | Bomb / Cancel |
| **L / ZL** | Focus (Precision Slow-Motion Movement) |
| **R / ZR** | Skip Dialogue (hold, Ctrl) |
| **+ (Plus)** | Pause / In-Game Menu |
| **− (Minus)** | Snapshot (saved to `snapshot/` next to the game data) |
| **Touch** (handheld) | Drag to move, tap menus |

**The same default layout in every Touhou Switch port:** B shoots, A bombs, L/ZL focuses, R/ZR skips dialogue, + pauses. These are only defaults — the in-game **Option → Key Config** can rebind shot, bomb, focus and pause. Key Config numbers: 0 B, 1 A, 2 L/ZL, 3 R/ZR, 4 +, 5 X, 6 Y.

---

## 🛠 Building from Source

### Automated Build (GitHub Actions)

Every push builds `touhou20.nro` (`.github/workflows/build-switch.yml`): clang 18 compiles the runtime to wasm32 and wasm2c turns it into C on Ubuntu 24.04, then the `devkitpro/devkita64` container compiles the NRO. Tags (`v*`) also publish it as a release with a SHA-256 checksum.

### Local Build (Linux)

The TH20 runtime is not in this repository: `scripts/fetch_upstream.sh` downloads it from eagler-touhou at the commit pinned in `UPSTREAM_COMMIT` and applies `patches/native.patch` on top, along with LLVM 18.1.3 libc++abi/libunwind, SDL3 headers, zlib and libdeflate 1.22.

```sh
./scripts/fetch_upstream.sh
./scripts/build_wasm.sh       # generated/ (Ubuntu 24.04: clang-18 lld-18 wasi-libc libc++-18-dev-wasm32
                              # libc++abi-18-dev-wasm32 libclang-rt-18-dev-wasm32 wabt)
./scripts/build_switch.sh     # build-switch/touhou20.nro (devkitPro devkitA64)
```

Linux test build of the same module:

```sh
cmake -S . -B build -G Ninja && ninja -C build
./build/touhou20 /path/to/game/folder
```

The Linux build can play itself for testing: `TH20_SCRIPT` (a file of `frame mask` lines, mask in hex), `TH20_FRAMES`, `TH20_NOVSYNC`, `TH20_SNAP_EVERY`, `TH20_TEST_FLAGS` (1: the player is never hit, 2: every layer list is checked against a full scan), `TH20_GLPROF` (pixels written per draw and render target) and `TH20_FRAMELOG` (the time split of every frame).

---

## 📂 How It Works

* **wasm2c:** the runtime is built as a wasm32 module (clang 18, wasi-libc, libc++ with wasm exceptions) and translated to C. `native/` provides what the browser used to: the SDL3 window, events, gamepad and audio on top of SDL2, the GLES calls, FreeType text and the WASI file system mapped onto the data folder.
* **Resolution:** the screen is 1280×960; the playfield layers (stage background, bullets, spell card and bomb effects) are 640×480, because they are nearly all of the GPU's work and at 2x the heaviest effects need more memory bandwidth than the Switch has. The scale never changes while playing.
* **One tick per vsync:** one game tick per 60 Hz refresh, no catch-up ticks after a slow frame.
* **Images:** TH20's PNGs are decoded with libdeflate, and two worker threads decode the next images of the same file while the game sets up the current one.
* **Saves:** LZSS encoding runs natively with the game's own algorithm.
* **Game code:** the text outline (spell card names, dialogue) and the selection of sprites per layer are rewritten natively with the same results.
* **`patches/native.patch`** is everything the native build changes upstream: the gamepad axis range, `thbgm.dat` from the data folder, the default pad bindings, a GLSL name Mesa rejects, image decoding, pixel conversion and LZSS encoding through the host, the render-target scale of the playfield, the text outline and per-layer sprite lists, and a stage load that waited forever on the music queue without an audio device.

---

## 🤝 Credits & Acknowledgments

* **ZUN / Team Shanghai Alice** — original creator and developer of the Touhou Project series.
* **[Ritosa2333](https://github.com/Ritosa2333/eagler-touhou)** — the TH20 C++ runtime (eagler-touhou, MIT) this port is built on.
* **Eric Biggers** — libdeflate (MIT).
* **Switchbrew & devkitPro Team** — the open-source `libnx` SDK and Switch toolchain.

**Licensing:** the Switch port code in this repository (`native/`, `wasm/`, `scripts/`, build files) is CC0 1.0 (see `LICENSE`), except the Khronos GLES headers in `wasm/compat/GLES2`, `GLES3` and `KHR`, which keep their licence lines (MIT / Apache-2.0). The TH20 runtime is downloaded from eagler-touhou at build time and is Ritosa2333's code under the **MIT** licence ([confirmed by the author](https://github.com/Ritosa2333/th10/issues/3)), as is what `patches/native.patch` changes in it (see [`LICENSE-Ritosa2333.txt`](LICENSE-Ritosa2333.txt)). The libraries fetched at build time (libdeflate, LLVM's libc++/libc++abi/libunwind, SDL3 headers, zlib), wabt's wasm2c runtime and the devkitPro libraries keep their own licences.
