// 8-bit RGBA PNG decoder on libdeflate (PngFast.cpp).
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
namespace th20n {
// BGRA pixels of an 8-bit, non-interlaced RGBA PNG; false for anything else.
bool decode_png_rgba8(const std::uint8_t* bytes, std::size_t size, std::vector<std::uint8_t>& out, std::uint32_t& width, std::uint32_t& height);
}
