# Changelog

## 1.00a-r1

- First Switch release of Touhou 20 on the eagler-touhou TH20 runtime
  (`ports/th20` at the commit in `UPSTREAM_COMMIT`), compiled to wasm32 and
  turned into C with wasm2c.
- Screen at 1280x960, playfield layers at 640x480 (`hires` / `lowres` files
  in the game folder for all 2x / all 1x).
- One game tick per vsync; no catch-up bursts after slow frames.
- PNGs decoded with libdeflate, the next ones ahead on two worker threads;
  fonts kept in memory; saves LZSS-encoded natively.
- Faster text outline and per-layer sprite lists (same results as the
  original code).
- GPU shortcuts for draws that cannot change the picture.
- `th20-switch.log` records the clocks, frames over 25 ms with where the time
  went, and the GPU time per frame and per render target.
