// OpenGL ES imports: the generated forwarders (GlImports.cpp) and the calls
// that need more than address translation (GlSpecial.cpp).
#pragma once
#include "Host.hpp"
#include <GLES3/gl3.h>
#include <SDL2/SDL_timer.h>
#include <cstdint>
// Per-frame cost accounting for th20-switch.log: time inside GL calls, the
// number of calls and draws, bytes uploaded, and the swap (vsync) wait. The
// rest of a frame is the game itself.
namespace th20n::perf {
struct Totals {
    std::uint64_t gl_ticks = 0, swap_ticks = 0, upload_bytes = 0; std::uint32_t gl_calls = 0, draws = 0;
    std::uint64_t io_ticks = 0, io_bytes = 0; std::uint32_t io_calls = 0;   // module file reads / seeks
    std::uint32_t mapped_appends = 0, synced_updates = 0;                    // vertex streaming (GlSpecial.cpp)
    std::uint64_t image_ticks = 0, image_pixels = 0; std::uint32_t images = 0; // image decoding (Images.cpp)
    std::uint64_t text_ticks = 0; std::uint32_t glyphs = 0;                   // text (FontHost.cpp)
    std::uint64_t lzss_ticks = 0, lzss_bytes = 0;                            // score / replay encoding (Lzss.cpp)
};
extern Totals totals;
// Time spent in a host service the module calls (part of the game's time).
struct HostScope {
    std::uint64_t& ticks; std::uint64_t begin = SDL_GetPerformanceCounter();
    explicit HostScope(std::uint64_t& t) : ticks(t) {}
    ~HostScope() { ticks += SDL_GetPerformanceCounter() - begin; }
};
struct GlScope {
    std::uint64_t begin = SDL_GetPerformanceCounter();
    ~GlScope() { totals.gl_ticks += SDL_GetPerformanceCounter() - begin; ++totals.gl_calls; }
};
}
// Desktop GPU work profiler (GlProfile.cpp, TH20_GLPROF); nothing on the Switch.
namespace th20n::glprof {
#ifdef __SWITCH__
inline void start() {} inline void draw_begin() {} inline void draw_end(std::uint32_t, std::uint32_t) {}
inline void blit(int, int) {} inline void clear(GLbitfield) {} inline void frame_end() {}
#else
void start(); void draw_begin(); void draw_end(std::uint32_t vertices, std::uint32_t instances);
void blit(int width, int height); void clear(GLbitfield bits); void frame_end();
#endif
}
// Tracked GL state and per-draw shortcuts that leave the picture unchanged (GlOptimize.cpp).
namespace th20n::glstate {
struct DrawFix { bool blend_off = false, mask_off = false, depth_off = false; };
void enable(GLenum cap, bool on);
void blend_func(GLenum src_rgb, GLenum dst_rgb, GLenum src_alpha, GLenum dst_alpha);
void blend_equation(GLenum rgb, GLenum alpha);
void color_mask(bool r, bool g, bool b, bool a);
void depth_func(GLenum func);
void depth_mask(bool write);
DrawFix before_draw();
void after_draw(const DrawFix& fix);
}
// GPU time per frame and per render target (GpuTimer.cpp).
namespace th20n::gputime {
struct Result {
    static constexpr int max_parts = 8;
    struct Part { GLuint framebuffer; double ms; };
    Part parts[max_parts]{}; int count = 0; double total_ms = 0; std::uint32_t frame = 0; bool valid = false;
};
void start(); bool available(); void framebuffer(GLuint framebuffer); void draw(); void frame_end();
const Result& latest();   // the newest frame the GPU has finished (four frames back)
}
namespace th20n::gl {
void glVertexAttribPointer(u32 index, u32 size, u32 type, u32 normalized, u32 stride, u32 offset);
void glDrawElements(u32 mode, u32 count, u32 type, u32 offset);
void glShaderSource(u32 shader, u32 count, u32 strings, u32 lengths);
void glBindFramebuffer(u32 target, u32 framebuffer);
void glBlitFramebuffer(u32 sx0, u32 sy0, u32 sx1, u32 sy1, u32 dx0, u32 dy0, u32 dx1, u32 dy1, u32 mask, u32 filter);
void glBufferData(u32 target, u32 size, u32 data, u32 usage);
void glBufferSubData(u32 target, u32 offset, u32 size, u32 data);
}
