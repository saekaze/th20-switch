// GL state the module sets, tracked so a draw can skip GPU work that cannot
// change the picture (the result is the same pixel for pixel):
//  - blending with ONE, ZERO (add) is a plain copy: blending off, so the GPU
//    does not read the destination first;
//  - blending with ZERO, ONE (add) keeps the colour as it is (the game's
//    depth-only quads): colour writes off for that draw;
//  - a depth test that always passes and writes nothing: depth test off.
// The state is put back right after the draw. TH20_NOOPT=1 (desktop) turns
// it off for comparisons.
#include "GlImports.hpp"
#include <cstdlib>

namespace th20n::glstate {
namespace {
struct State {
    bool blend = false, depth_test = false, depth_write = true;
    GLenum src_rgb = GL_ONE, dst_rgb = GL_ZERO, src_alpha = GL_ONE, dst_alpha = GL_ZERO;
    GLenum eq_rgb = GL_FUNC_ADD, eq_alpha = GL_FUNC_ADD, depth_func = GL_LESS;
    bool mask[4] = {true, true, true, true};
} s;
bool enabled() {
#ifdef __SWITCH__
    return true;
#else
    static const bool on = std::getenv("TH20_NOOPT") == nullptr;
    return on;
#endif
}
}
void enable(GLenum cap, bool on) {
    if (cap == GL_BLEND) s.blend = on;
    else if (cap == GL_DEPTH_TEST) s.depth_test = on;
}
void blend_func(GLenum src_rgb, GLenum dst_rgb, GLenum src_alpha, GLenum dst_alpha) {
    s.src_rgb = src_rgb; s.dst_rgb = dst_rgb; s.src_alpha = src_alpha; s.dst_alpha = dst_alpha;
}
void blend_equation(GLenum rgb, GLenum alpha) { s.eq_rgb = rgb; s.eq_alpha = alpha; }
void color_mask(bool r, bool g, bool b, bool a) { s.mask[0] = r; s.mask[1] = g; s.mask[2] = b; s.mask[3] = a; }
void depth_func(GLenum func) { s.depth_func = func; }
void depth_mask(bool write) { s.depth_write = write; }

DrawFix before_draw() {
    gputime::draw();
    DrawFix fix;
    if (!enabled()) return fix;
    const bool add = s.eq_rgb == GL_FUNC_ADD && s.eq_alpha == GL_FUNC_ADD;
    if (s.blend && add && s.src_rgb == GL_ONE && s.dst_rgb == GL_ZERO && s.src_alpha == GL_ONE && s.dst_alpha == GL_ZERO) {
        glDisable(GL_BLEND); fix.blend_off = true;
    } else if (s.blend && add && s.src_rgb == GL_ZERO && s.dst_rgb == GL_ONE && s.src_alpha == GL_ZERO && s.dst_alpha == GL_ONE &&
               (s.mask[0] || s.mask[1] || s.mask[2] || s.mask[3])) {
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE); fix.mask_off = true;
    }
    if (s.depth_test && s.depth_func == GL_ALWAYS && !s.depth_write) { glDisable(GL_DEPTH_TEST); fix.depth_off = true; }
    return fix;
}
void after_draw(const DrawFix& fix) {
    if (fix.blend_off) glEnable(GL_BLEND);
    if (fix.mask_off) glColorMask(s.mask[0], s.mask[1], s.mask[2], s.mask[3]);
    if (fix.depth_off) glEnable(GL_DEPTH_TEST);
}
}
