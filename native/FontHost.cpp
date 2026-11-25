// TH20's text rasterizer on the native side: upstream's
// web/cpp/sdl/FontHost.cpp (FreeType, GDI-style coverage blend), unchanged
// except that the strings and the BGRA target live in the module's memory and
// the face files are looked up in the data folder. The module reaches it
// through the th20n.font_* imports (wasm/src/FontImports.cpp).
#include "Host.hpp"
#include "GlImports.hpp"
#include "../upstream/ports/th20/web/cpp/platform/Cp932.hpp"
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include <cstdio>
#include <string>
#include <vector>

namespace {
using namespace th20n;
FT_Library library = nullptr;
FT_Face faces[2]{};
bool tried = false;
constexpr int gothic = 0, mincho = 1;

// The face files are read into memory once: FreeType reading glyph outlines
// straight from the SD card costs a few small file reads per character.
std::vector<std::vector<std::uint8_t>> font_files;
FT_Face open(std::initializer_list<const char*> names) {
    for (const char* name : names) {
        const std::string path = paths().data + "/" + name;
        FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) continue;
        std::vector<std::uint8_t> bytes;
        if (!std::fseek(f, 0, SEEK_END)) {
            const long size = std::ftell(f);
            if (size > 0 && !std::fseek(f, 0, SEEK_SET)) {
                bytes.resize(size_t(size));
                if (std::fread(bytes.data(), 1, bytes.size(), f) != bytes.size()) bytes.clear();
            }
        }
        std::fclose(f);
        if (bytes.empty()) continue;
        FT_Face face = nullptr;
        if (!FT_New_Memory_Face(library, bytes.data(), FT_Long(bytes.size()), 0, &face) && face) {
            font_files.push_back(std::move(bytes));   // the buffer moves, its heap block (and face->stream) stays
            log("font: %s", path.c_str());
            return face;
        }
    }
    return nullptr;
}
void size_for(FT_Face face, int height_px) {
    if (face->size->metrics.y_ppem != height_px) FT_Set_Pixel_Sizes(face, 0, FT_UInt(height_px));
}
struct Pen { std::uint8_t* bgra; int pitch, rows, y; std::uint32_t r, g, b; };
void blend_glyph(Pen& pen, FT_Face face, int left) {
    FT_GlyphSlot slot = face->glyph;
    const int ascender = int(face->size->metrics.ascender >> 6);
    const int base_y = pen.y + ascender - slot->bitmap_top;
    for (unsigned row = 0; row < slot->bitmap.rows; ++row) {
        const int y = base_y + int(row);
        if (y < 0 || y >= pen.rows) continue;
        for (unsigned col = 0; col < slot->bitmap.width; ++col) {
            const int x = left + int(col);
            if (x < 0 || x >= pen.pitch / 4) continue;
            const unsigned coverage = slot->bitmap.buffer[row * slot->bitmap.pitch + col];
            if (!coverage) continue;
            auto* dst = pen.bgra + y * pen.pitch + x * 4;
            const unsigned keep = 255 - coverage;
            const unsigned channels[4]{pen.b, pen.g, pen.r, 0};
            for (int c = 0; c < 4; ++c) dst[c] = std::uint8_t((channels[c] * coverage + dst[c] * keep + 127) / 255);
        }
    }
}
bool initialize() {
    if (tried) return faces[gothic] != nullptr;
    tried = true;
    if (FT_Init_FreeType(&library)) return false;
    faces[gothic] = open({"msgothic.ttc", "msgothic.ttf", "meiryo.ttc", "simhei.ttf", "unifont.otf", "NotoSansJP-Regular.otf", "unifont.ttf"});
    faces[mincho] = open({"msmincho.ttc", "YuMincho.ttf", "NotoSerifJP-Regular.otf"});
    if (!faces[gothic]) log("font: no msgothic.ttc in %s - text will be empty", paths().data.c_str());
    return faces[gothic] != nullptr;
}
}

extern "C" {
std::uint32_t w2c_th20n_font_initialize(struct w2c_th20n*) {
    initialize();
    return (faces[gothic] ? 1u << gothic : 0u) | (faces[mincho] ? 1u << mincho : 0u);
}
void w2c_th20n_font_measure(struct w2c_th20n*, std::uint32_t text, std::uint32_t height_px, std::uint32_t family, std::uint32_t cx_out, std::uint32_t cy_out) {
    perf::HostScope timing(perf::totals.text_ticks);
    int cx = 0, cy = 0;
    initialize();
    FT_Face face = faces[family == 1 ? mincho : gothic] ? faces[family == 1 ? mincho : gothic] : faces[gothic];
    if (face && text) {
        size_for(face, int(height_px));
        cy = int(face->size->metrics.height >> 6);
        const char* p = str(text);
        while (*p) {
            const std::uint16_t code = th20::web::cp932::map(p);
            if (!code) continue;
            if (!FT_Load_Char(face, code, FT_LOAD_DEFAULT | FT_LOAD_NO_BITMAP)) cx += int(face->glyph->advance.x >> 6);
        }
    }
    if (cx_out) store<std::int32_t>(cx_out, cx);
    if (cy_out) store<std::int32_t>(cy_out, cy);
}
void w2c_th20n_font_draw(struct w2c_th20n*, std::uint32_t bgra, std::uint32_t pitch, std::uint32_t rows, std::uint32_t x, std::uint32_t y, std::uint32_t text,
        std::uint32_t height_px, std::uint32_t family, std::uint32_t text_color, std::uint32_t spacing) {
    perf::HostScope timing(perf::totals.text_ticks);
    initialize();
    FT_Face face = faces[family == 1 ? mincho : gothic] ? faces[family == 1 ? mincho : gothic] : faces[gothic];
    if (!face || !text || !bgra) return;
    size_for(face, int(height_px));
    Pen pen{at<std::uint8_t>(bgra), int(pitch), int(rows), std::int32_t(y), (text_color >> 16) & 0xff, (text_color >> 8) & 0xff, text_color & 0xff};
    int pen_x = std::int32_t(x);
    const char* p = str(text);
    while (*p) {
        const std::uint16_t code = th20::web::cp932::map(p);
        if (!code) continue;
        if (!FT_Load_Char(face, code, FT_LOAD_DEFAULT | FT_LOAD_NO_BITMAP)) {
            FT_GlyphSlot slot = face->glyph;
            FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL); ++perf::totals.glyphs;
            blend_glyph(pen, face, pen_x + slot->bitmap_left);
            pen_x += std::int32_t(spacing) > 0 ? std::int32_t(spacing) : int(slot->advance.x >> 6);
        }
    }
}
}
