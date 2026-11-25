// th20::web::fonts for the wasm2c build: upstream's FreeType host
// (web/cpp/sdl/FontHost.cpp) runs natively instead (native/FontHost.cpp, with
// the platform's FreeType), and these forward to it. Pointers are addresses in
// the module's memory; the native side resolves them.
#include "../../upstream/ports/th20/web/cpp/platform/Fonts.hpp"
#include <cstdint>

#define TH20N_IMPORT(name) __attribute__((import_module("th20n"), import_name(name)))
extern "C" {
TH20N_IMPORT("font_initialize") std::int32_t th20n_font_initialize();
TH20N_IMPORT("font_measure") void th20n_font_measure(const char* cp932, std::int32_t height_px, std::int32_t family, std::int32_t* cx, std::int32_t* cy);
TH20N_IMPORT("font_draw") void th20n_font_draw(std::uint8_t* bgra, std::int32_t pitch, std::int32_t rows, std::int32_t x, std::int32_t y, const char* cp932,
    std::int32_t height_px, std::int32_t family, std::uint32_t text_color, std::int32_t spacing);
}

namespace th20::web::fonts {
namespace { std::int32_t families = -1; }
bool initialize() { if (families < 0) families = th20n_font_initialize(); return (families & (1 << gothic)) != 0; }
bool available(int family) { initialize(); return family == mincho ? (families & (1 << mincho)) != 0 : (families & (1 << gothic)) != 0; }
void measure(const char* cp932, int height_px, int family, int& cx, int& cy) {
    std::int32_t w = 0, h = 0; th20n_font_measure(cp932, height_px, family, &w, &h); cx = w; cy = h;
}
void draw(std::uint8_t* bgra, int pitch, int rows, int x, int y, const char* cp932,
          int height_px, int family, std::uint32_t text_color, int spacing) {
    th20n_font_draw(bgra, pitch, rows, x, y, cp932, height_px, family, text_color, spacing);
}
}
