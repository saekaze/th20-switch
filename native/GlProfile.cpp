// Desktop GPU work profiler (TH20_GLPROF=file): for every draw, the pixels it
// wrote (GL_SAMPLES_PASSED) and the fragment shader runs, with the render
// target, program kind, blend mode and texture; blits and clears by area.
// Needs a desktop GL context (made instead of GLES when profiling; the game's
// "#version 300 es" shaders build there through ARB_ES3_compatibility).
// Per frame it writes the totals, the work per render target / program /
// blend group and the heaviest draws. Not built for the Switch.
#ifndef __SWITCH__
#include "GlImports.hpp"
#include <GL/gl.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#ifndef GL_SAMPLES_PASSED
#define GL_SAMPLES_PASSED 0x8914
#endif
#ifndef GL_FRAGMENT_SHADER_INVOCATIONS_ARB
#define GL_FRAGMENT_SHADER_INVOCATIONS_ARB 0x82F4
#endif
#ifndef GL_TEXTURE_WIDTH
#define GL_TEXTURE_WIDTH 0x1000
#define GL_TEXTURE_HEIGHT 0x1001
#endif
extern "C" void glGetTexLevelParameteriv(GLenum, GLint, GLenum, GLint*);

namespace th20n::glprof {
namespace {
FILE* out = nullptr;
bool on = false, invocations = false;
std::uint32_t frame = 0, from = 0;     // TH20_GLPROF_FROM: first frame profiled
std::uint32_t trace = ~0u;              // TH20_GLPROF_TRACE: a frame whose draws are all listed, in order
struct Draw {
    GLuint fbo, program, texture; int fbo_w, fbo_h, tex_w, tex_h; GLint blend_src, blend_dst, depth_func; bool blend, depth, depth_write, color_write;
    GLuint q_samples, q_fs; std::uint32_t vertices, instances; char kind;
};
std::vector<Draw> draws;
std::vector<GLuint> pool; size_t pool_used = 0;
std::unordered_map<GLuint, char> kinds;       // program -> g(eneric) s(pecialised) r(esample) i(mgui) ?
std::uint64_t blit_px = 0, clear_px = 0; std::uint32_t blits = 0, clears = 0;
struct Clear { GLuint fbo; GLbitfield bits; std::uint64_t px; };
std::vector<Clear> clear_list;
std::map<GLuint, GLuint> attachments;   // framebuffer -> colour texture, as last printed

GLuint query() {
    if (pool_used == pool.size()) { GLuint q = 0; glGenQueries(1, &q); pool.push_back(q); }
    return pool[pool_used++];
}
char kind_of(GLuint program) {
    auto it = kinds.find(program);
    if (it != kinds.end()) return it->second;
    char k = '?';
    GLuint shaders[4]{}; GLsizei n = 0; glGetAttachedShaders(program, 4, &n, shaders);
    for (GLsizei i = 0; i < n; ++i) {
        GLint type = 0; glGetShaderiv(shaders[i], GL_SHADER_TYPE, &type);
        if (type != GL_FRAGMENT_SHADER) continue;
        GLint length = 0; glGetShaderiv(shaders[i], GL_SHADER_SOURCE_LENGTH, &length);
        std::string source(size_t(std::max(length, 1)), '\0'); glGetShaderSource(shaders[i], length, nullptr, source.data());
        k = source.find("uniform int colorOp") != std::string::npos ? 'g' : source.find("sourceRect") != std::string::npos ? 'r'
          : source.find("Frag_UV") != std::string::npos ? 'i' : source.find("const int colorOp") != std::string::npos ? 's' : '?';
    }
    return kinds[program] = k;
}
void size_of(GLuint fbo, int& w, int& h) {
    w = h = 0;
    if (!fbo) { GLint vp[4]{}; glGetIntegerv(GL_VIEWPORT, vp); w = vp[2]; h = vp[3]; return; }
    GLint name = 0, type = 0;
    glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &type);
    glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &name);
    if (type != GL_TEXTURE || !name) return;
    GLint bound = 0; glGetIntegerv(GL_TEXTURE_BINDING_2D, &bound);
    glBindTexture(GL_TEXTURE_2D, GLuint(name));
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w); glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
    glBindTexture(GL_TEXTURE_2D, GLuint(bound));
}
}
bool enabled() { return on; }
void start() {
    const char* path = std::getenv("TH20_GLPROF");
    if (!path || on) return;
    if (const char* f = std::getenv("TH20_GLPROF_FROM")) from = std::uint32_t(std::strtoul(f, nullptr, 10));
    if (const char* t = std::getenv("TH20_GLPROF_TRACE")) trace = std::uint32_t(std::strtoul(t, nullptr, 10));
    out = std::fopen(path, "w");
    if (!out) return;
    on = true;
    GLint n = 0; glGetIntegerv(GL_NUM_EXTENSIONS, &n);
    for (GLint i = 0; i < n; ++i) if (std::string(reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, GLuint(i)))) == "GL_ARB_pipeline_statistics_query") invocations = true;
    std::fprintf(out, "# %s / %s, fragment shader invocations: %s\n", reinterpret_cast<const char*>(glGetString(GL_RENDERER)),
                 reinterpret_cast<const char*>(glGetString(GL_VERSION)), invocations ? "yes" : "no");
}
void draw_begin() {
    if (!on || frame < from) return;
    Draw d{};
    GLint v = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &v); d.fbo = GLuint(v);
    glGetIntegerv(GL_CURRENT_PROGRAM, &v); d.program = GLuint(v); d.kind = kind_of(d.program);
    GLint active = 0; glGetIntegerv(GL_ACTIVE_TEXTURE, &active); glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &v); d.texture = GLuint(v);
    if (d.texture) { glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &d.tex_w); glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &d.tex_h); }
    glActiveTexture(GLenum(active));
    size_of(d.fbo, d.fbo_w, d.fbo_h);
    d.blend = glIsEnabled(GL_BLEND); d.depth = glIsEnabled(GL_DEPTH_TEST);
    GLboolean b = 0; glGetBooleanv(GL_DEPTH_WRITEMASK, &b); d.depth_write = b; glGetIntegerv(GL_DEPTH_FUNC, &d.depth_func);
    GLboolean m[4]{}; glGetBooleanv(GL_COLOR_WRITEMASK, m); d.color_write = m[0] || m[1] || m[2] || m[3];
    if (d.fbo && !attachments.count(d.fbo)) {
        GLint name = 0; glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &name);
        attachments[d.fbo] = GLuint(name);
        std::fprintf(out, "A %u fbo %u texture %u %dx%d\n", frame, d.fbo, GLuint(name), d.fbo_w, d.fbo_h);
    }
    glGetIntegerv(GL_BLEND_SRC_RGB, &d.blend_src); glGetIntegerv(GL_BLEND_DST_RGB, &d.blend_dst);
    d.q_samples = query(); glBeginQuery(GL_SAMPLES_PASSED, d.q_samples);
    if (invocations) { d.q_fs = query(); glBeginQuery(GL_FRAGMENT_SHADER_INVOCATIONS_ARB, d.q_fs); }
    draws.push_back(d);
}
void draw_end(std::uint32_t vertices, std::uint32_t instances) {
    if (!on || frame < from || draws.empty()) return;
    glEndQuery(GL_SAMPLES_PASSED);
    if (invocations) glEndQuery(GL_FRAGMENT_SHADER_INVOCATIONS_ARB);
    draws.back().vertices = vertices; draws.back().instances = instances;
}
void blit(int w, int h) { if (on && frame >= from) { ++blits; blit_px += std::uint64_t(std::abs(w)) * std::uint64_t(std::abs(h)); } }
void clear(GLbitfield bits) {
    if (!on || frame < from) return;
    GLint box[4]{}, fbo = 0; glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fbo);
    if (glIsEnabled(GL_SCISSOR_TEST)) glGetIntegerv(GL_SCISSOR_BOX, box);
    else size_of(GLuint(fbo), box[2], box[3]);
    const std::uint64_t px = std::uint64_t(std::max(box[2], 0)) * std::uint64_t(std::max(box[3], 0));
    ++clears; clear_px += px; clear_list.push_back({GLuint(fbo), bits, px});
}
void frame_end() {
    if (!on) return;
    if (frame < from) { ++frame; return; }
    struct Group { std::uint64_t samples = 0, fs = 0; std::uint32_t draws = 0; };
    std::map<std::tuple<GLuint, int, int, char, int, int, int>, Group> groups;
    std::uint64_t total = 0, total_fs = 0;
    struct Heavy { std::uint64_t samples, fs; const Draw* d; };
    std::vector<Heavy> heavy;
    for (const auto& d : draws) {
        GLuint s = 0, f = 0; glGetQueryObjectuiv(d.q_samples, GL_QUERY_RESULT, &s);
        if (invocations) glGetQueryObjectuiv(d.q_fs, GL_QUERY_RESULT, &f);
        total += s; total_fs += f;
        // depth: 0 off, else func - GL_NEVER + 1, +16 when it writes; blend -2 when colour writes are off
        const int depth = d.depth ? int(d.depth_func - GL_NEVER) + 1 + (d.depth_write ? 16 : 0) : 0;
        auto& g = groups[{d.fbo, d.fbo_w, d.fbo_h, d.kind, !d.color_write ? -2 : d.blend ? d.blend_src : -1, d.blend ? d.blend_dst : -1, depth}];
        g.samples += s; g.fs += f; ++g.draws;
        heavy.push_back({s, f, &d});
    }
    std::fprintf(out, "F %u draws %zu px %llu fs %llu blits %u/%llu clears %u/%llu\n", frame, draws.size(), (unsigned long long)total,
                 (unsigned long long)total_fs, blits, (unsigned long long)blit_px, clears, (unsigned long long)clear_px);
    for (const auto& [k, g] : groups)
        std::fprintf(out, "G %u fbo %u %dx%d %c blend %d/%d depth %d draws %u px %llu fs %llu\n", frame, std::get<0>(k), std::get<1>(k), std::get<2>(k),
                     std::get<3>(k), std::get<4>(k), std::get<5>(k), std::get<6>(k), g.draws, (unsigned long long)g.samples, (unsigned long long)g.fs);
    for (const auto& c : clear_list)
        std::fprintf(out, "C %u fbo %u %s%s%s px %llu\n", frame, c.fbo, c.bits & GL_COLOR_BUFFER_BIT ? "c" : "", c.bits & GL_DEPTH_BUFFER_BIT ? "d" : "",
                     c.bits & GL_STENCIL_BUFFER_BIT ? "s" : "", (unsigned long long)c.px);
    if (frame == trace && std::getenv("TH20_GLPROF_DUMP")) {   // each render target's picture (PPM) at the traced frame
        GLint read = 0; glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
        for (const auto& [fbo, texture] : attachments) {
            int w = 0, h = 0; GLint draw = 0; glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo); size_of(fbo, w, h); glBindFramebuffer(GL_DRAW_FRAMEBUFFER, GLuint(draw));
            if (w <= 0 || h <= 0) continue;
            std::vector<std::uint8_t> rgba(size_t(w) * h * 4);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo); glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
            char name[512]; std::snprintf(name, sizeof name, "%s/fbo%u.ppm", std::getenv("TH20_GLPROF_DUMP"), fbo);
            if (FILE* f = std::fopen(name, "wb")) {
                std::fprintf(f, "P6\n%d %d\n255\n", w, h);
                for (int y = h - 1; y >= 0; --y) for (int x = 0; x < w; ++x) std::fwrite(&rgba[(size_t(y) * w + x) * 4], 1, 3, f);
                std::fclose(f);
            }
        }
        glBindFramebuffer(GL_READ_FRAMEBUFFER, GLuint(read));
    }
    if (frame == trace)
        for (size_t i = 0; i < heavy.size(); ++i) {
            const auto& d = *heavy[i].d;
            std::fprintf(out, "T %u #%zu fbo %u %c tex %u %dx%d blend %d %d/%d colour %d depth %d func %d write %d verts %u inst %u px %llu\n", frame, i, d.fbo, d.kind,
                         d.texture, d.tex_w, d.tex_h, int(d.blend), d.blend_src, d.blend_dst, int(d.color_write), int(d.depth), d.depth_func, int(d.depth_write),
                         d.vertices, d.instances, (unsigned long long)heavy[i].samples);
        }
    std::sort(heavy.begin(), heavy.end(), [](const Heavy& a, const Heavy& b) { return a.fs + a.samples > b.fs + b.samples; });
    for (size_t i = 0; i < heavy.size() && i < 8; ++i) {
        const auto& d = *heavy[i].d;
        std::fprintf(out, "D %u fbo %u %c tex %u %dx%d blend %d %d/%d colour %d depth %d func %d write %d verts %u inst %u px %llu fs %llu\n", frame, d.fbo, d.kind, d.texture, d.tex_w, d.tex_h,
                     int(d.blend), d.blend_src, d.blend_dst, int(d.color_write), int(d.depth), d.depth_func, int(d.depth_write), d.vertices, d.instances,
                     (unsigned long long)heavy[i].samples, (unsigned long long)heavy[i].fs);
    }
    draws.clear(); clear_list.clear(); pool_used = 0; blits = clears = 0; blit_px = clear_px = 0; ++frame;
    if (frame % 60 == 0) std::fflush(out);
}
}
#endif
