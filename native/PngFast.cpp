// Fast path for the PNGs TH20 ships: 8-bit RGBA, not interlaced. The image
// data is inflated in one call with libdeflate (much faster than zlib's
// streaming inflate) and the row filters are undone in place; anything else
// (other colour types or depths, interlacing, damaged data) returns false and
// goes to libpng. The pixels are the same as stbi_load(..., 4), as BGRA.
#include "PngFast.hpp"
#include "libdeflate.h"
#include <cstring>

namespace th20n {
namespace {
std::uint32_t be32(const std::uint8_t* p) { return std::uint32_t(p[0]) << 24 | std::uint32_t(p[1]) << 16 | std::uint32_t(p[2]) << 8 | p[3]; }
inline std::uint8_t paeth(int a, int b, int c) {
    const int p = a + b - c, pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p;
    return std::uint8_t(pa <= pb && pa <= pc ? a : pb <= pc ? b : c);
}
// Undoes one row's filter in place (bpp 4). `prior` is the previous row,
// already unfiltered, or null for the first row (all zero).
bool unfilter(std::uint8_t filter, std::uint8_t* row, const std::uint8_t* prior, std::size_t n) {
    switch (filter) {
    case 0: return true;
    case 1: for (std::size_t i = 4; i < n; ++i) row[i] = std::uint8_t(row[i] + row[i - 4]); return true;
    case 2: if (prior) for (std::size_t i = 0; i < n; ++i) row[i] = std::uint8_t(row[i] + prior[i]); return true;
    case 3:
        if (prior) {
            for (std::size_t i = 0; i < 4 && i < n; ++i) row[i] = std::uint8_t(row[i] + (prior[i] >> 1));
            for (std::size_t i = 4; i < n; ++i) row[i] = std::uint8_t(row[i] + ((row[i - 4] + prior[i]) >> 1));
        } else for (std::size_t i = 4; i < n; ++i) row[i] = std::uint8_t(row[i] + (row[i - 4] >> 1));
        return true;
    case 4:
        if (prior) {
            for (std::size_t i = 0; i < 4 && i < n; ++i) row[i] = std::uint8_t(row[i] + prior[i]);   // paeth(0, b, 0) = b
            for (std::size_t i = 4; i < n; ++i) row[i] = std::uint8_t(row[i] + paeth(row[i - 4], prior[i], prior[i - 4]));
        } else for (std::size_t i = 4; i < n; ++i) row[i] = std::uint8_t(row[i] + row[i - 4]);      // paeth(a, 0, 0) = a
        return true;
    default: return false;
    }
}
}

bool decode_png_rgba8(const std::uint8_t* bytes, std::size_t size, std::vector<std::uint8_t>& out, std::uint32_t& width, std::uint32_t& height) {
    // Per thread: the image workers (Images.cpp) decode in parallel.
    thread_local libdeflate_decompressor* inflater = libdeflate_alloc_decompressor();
    thread_local std::vector<std::uint8_t> idat, raw;
    if (!inflater || size < 8 + 25 || std::memcmp(bytes, "\x89PNG\r\n\x1a\n", 8)) return false;
    // IHDR first: 8-bit (depth 8), RGBA (colour type 6), deflate, filter 0, no interlace.
    if (be32(bytes + 8) != 13 || std::memcmp(bytes + 12, "IHDR", 4)) return false;
    const std::uint32_t w = be32(bytes + 16), h = be32(bytes + 20);
    // Anything past 4096x4096 goes to libpng (TH20's images are far smaller).
    if (!w || !h || std::uint64_t(w) * h > 4096u * 4096u || bytes[24] != 8 || bytes[25] != 6 || bytes[26] || bytes[27] || bytes[28]) return false;
    // Collect the IDAT data (one span when there is a single chunk).
    const std::uint8_t* data = nullptr; std::size_t data_size = 0; idat.clear();
    for (std::size_t at = 8; ; ) {
        if (at + 12 > size) return false;
        const std::uint32_t length = be32(bytes + at);
        if (length > size - at - 12) return false;
        const std::uint8_t* type = bytes + at + 4;
        if (!std::memcmp(type, "IDAT", 4)) {
            if (!data) { data = bytes + at + 8; data_size = length; }
            else { if (idat.empty()) idat.assign(data, data + data_size); idat.insert(idat.end(), bytes + at + 8, bytes + at + 8 + length); }
        } else if (!std::memcmp(type, "IEND", 4)) break;
        else if (!std::memcmp(type, "tRNS", 4) || !std::memcmp(type, "PLTE", 4)) return false;   // not for RGBA; leave odd files to libpng
        at += 12 + std::size_t(length);
    }
    if (!data) return false;
    if (!idat.empty()) { data = idat.data(); data_size = idat.size(); }
    const std::size_t stride = std::size_t(w) * 4, expected = (stride + 1) * h;
    raw.resize(expected);
    std::size_t produced = 0;
    if (libdeflate_zlib_decompress(inflater, data, data_size, raw.data(), expected, &produced) != LIBDEFLATE_SUCCESS || produced != expected) return false;
    out.resize(std::size_t(w) * h * 4);
    const std::uint8_t* prior = nullptr;
    for (std::uint32_t y = 0; y < h; ++y) {
        std::uint8_t* row = raw.data() + std::size_t(y) * (stride + 1);
        if (!unfilter(row[0], row + 1, prior, stride)) return false;
        prior = row + 1;
    }
    for (std::uint32_t y = 0; y < h; ++y) {                       // RGBA -> BGRA
        const std::uint8_t* s = raw.data() + std::size_t(y) * (stride + 1) + 1;
        std::uint8_t* d = out.data() + std::size_t(y) * stride;
        for (std::size_t x = 0; x < stride; x += 4) {
            std::uint32_t v; std::memcpy(&v, s + x, 4);
            v = (v & 0xff00ff00u) | ((v >> 16) & 0xffu) | ((v & 0xffu) << 16);
            std::memcpy(d + x, &v, 4);
        }
    }
    width = w; height = h;
    return true;
}
}
