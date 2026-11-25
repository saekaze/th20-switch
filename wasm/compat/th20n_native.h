// Host functions the native (wasm2c) build gives the TH20 module for work that
// is much faster outside the module: image decoding and pixel conversion.
// Pointers are module addresses; the host resolves them.
#pragma once
#include <cstdint>
#define TH20N_NATIVE_IMPORT(name) __attribute__((import_module("th20n"), import_name(name)))
extern "C" {
// Decodes an image file (PNG with libpng and zlib, other formats with
// stb_image) with the same results as stbi_load(..., 4). Returns 1 and the
// size, and keeps the pixels for th20n_image_take_bgra; 0 if not decodable.
TH20N_NATIVE_IMPORT("image_open") std::int32_t th20n_image_open(const std::uint8_t* bytes, std::uint32_t size,
                                                                 std::uint32_t* width, std::uint32_t* height);
// Writes the kept image as BGRA (width * height * 4 bytes) and releases it.
TH20N_NATIVE_IMPORT("image_take_bgra") void th20n_image_take_bgra(std::uint8_t* to);
// BGRA rows (pitch bytes apart) -> packed RGBA; opaque: alpha 255 (BGRX).
TH20N_NATIVE_IMPORT("bgra_to_rgba") void th20n_bgra_to_rgba(const std::uint8_t* from, std::uint32_t pitch,
                                                             std::uint32_t width, std::uint32_t height,
                                                             std::uint32_t opaque, std::uint8_t* to);
}
extern "C" {
// TH20's LZSS encoder (score file and replays) run by the host: same output
// and the same final dictionary. Returns the size for th20n_lzss_take.
TH20N_NATIVE_IMPORT("lzss_encode") std::uint32_t th20n_lzss_encode(const std::uint8_t* input, std::uint32_t size, std::uint8_t* dictionary);
TH20N_NATIVE_IMPORT("lzss_take") void th20n_lzss_take(std::uint8_t* to);
}
extern "C" {
// Pixel scale of the game's own render targets (the playfield layers): at
// most the backbuffer's (output_units); lower keeps the GPU work of the
// playfield effects down while the backbuffer (HUD, text) stays sharp.
TH20N_NATIVE_IMPORT("target_scale") std::uint32_t th20n_target_scale();
}
extern "C" {
// Test hooks of the desktop build (TH20_TEST_FLAGS); always 0 on the Switch.
// Bit 0: the player is never hit.
TH20N_NATIVE_IMPORT("test_flags") std::uint32_t th20n_test_flags();
}
