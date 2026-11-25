#include "GlImports.hpp"
#include <cstring>
#include <unordered_map>
#include <SDL2/SDL.h>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>

namespace th20n {
namespace { std::string capture_path; }
void request_capture(const std::string& path) { capture_path = path; }
}
namespace th20n::gl {
namespace {
GLuint draw_framebuffer = 0;
// Saves the 4:3 picture of the frame being presented as a BMP (desktop test
// snapshots; the game's own snapshot key writes PNGs itself).
void capture(const std::string& path) {
    const auto& p = picture();
    GLint read = 0; glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
    ::glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    std::vector<std::uint8_t> rgba(size_t(p.width) * p.height * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(p.x, p.y, p.width, p.height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    ::glBindFramebuffer(GL_READ_FRAMEBUFFER, GLuint(read));
    const std::uint32_t w = p.width, h = p.height, row = w * 3, pad = (4 - row % 4) % 4, size = 54 + (row + pad) * h;
    std::vector<std::uint8_t> bmp(size, 0);
    auto put = [&](std::uint32_t at, std::uint32_t v, std::uint32_t n) { for (std::uint32_t i = 0; i < n; ++i) bmp[at + i] = std::uint8_t(v >> (8 * i)); };
    bmp[0] = 'B'; bmp[1] = 'M'; put(2, size, 4); put(10, 54, 4); put(14, 40, 4); put(18, w, 4); put(22, h, 4); put(26, 1, 2); put(28, 24, 2); put(34, size - 54, 4);
    for (std::uint32_t y = 0; y < h; ++y) for (std::uint32_t x = 0; x < w; ++x) {   // GL rows are bottom-up, like BMP
        const auto* s = rgba.data() + (size_t(y) * w + x) * 4; auto* d = bmp.data() + 54 + y * (row + pad) + x * 3; d[0] = s[2]; d[1] = s[1]; d[2] = s[0];
    }
    if (FILE* f = std::fopen(path.c_str(), "wb")) { std::fwrite(bmp.data(), 1, bmp.size(), f); std::fclose(f); }
}
}
void glVertexAttribPointer(u32 index, u32 size, u32 type, u32 normalized, u32 stride, u32 offset) {
    // WebGL2/GLES: always an offset into the bound array buffer.
    ::glVertexAttribPointer(index, GLint(size), type, GLboolean(normalized), GLsizei(stride), reinterpret_cast<const void*>(std::uintptr_t(offset)));
}
void glDrawElements(u32 mode, u32 count, u32 type, u32 offset) {
    ::glDrawElements(mode, GLsizei(count), type, reinterpret_cast<const void*>(std::uintptr_t(offset)));
}
void glShaderSource(u32 shader, u32 count, u32 strings, u32 lengths) {
    std::vector<const GLchar*> sources(count);
    for (u32 i = 0; i < count; ++i) sources[i] = str(load<std::uint32_t>(strings + i * 4));
    ::glShaderSource(shader, GLsizei(count), sources.data(), lengths ? at<const GLint>(lengths) : nullptr);
}
void glBindFramebuffer(u32 target, u32 framebuffer) {
    if (target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER) {
        draw_framebuffer = framebuffer;
        gputime::framebuffer(framebuffer);
    }
    ::glBindFramebuffer(target, framebuffer);
}
void glBlitFramebuffer(u32 sx0, u32 sy0, u32 sx1, u32 sy1, u32 dx0, u32 dy0, u32 dx1, u32 dy1, u32 mask, u32 filter) {
    glprof::blit(int(std::int32_t(dx1) - std::int32_t(dx0)), int(std::int32_t(dy1) - std::int32_t(dy0)));
    if (draw_framebuffer != 0) {
        ::glBlitFramebuffer(GLint(sx0), GLint(sy0), GLint(sx1), GLint(sy1), GLint(dx0), GLint(dy0), GLint(dx1), GLint(dy1), mask, filter);
        return;
    }
    // The game's present: its frame goes to the window. Scale it into the 4:3
    // picture (the game was told the window is that size) with pure black
    // bars around it, like the other Switch ports.
    update_picture();
    const auto& p = picture();
    // Upstream's renderer caches GL state (viewport, masks, clear colour):
    // touch nothing it does not expect, and restore what the bars need.
    GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST), mask_rgba[4]; GLfloat clear[4];
    glGetBooleanv(GL_COLOR_WRITEMASK, mask_rgba); glGetFloatv(GL_COLOR_CLEAR_VALUE, clear);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0, 0, 0, 1);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT);
    glColorMask(mask_rgba[0], mask_rgba[1], mask_rgba[2], mask_rgba[3]);
    glClearColor(clear[0], clear[1], clear[2], clear[3]);
    const auto map_x = [&](u32 v) { return p.x + int((long long)std::int32_t(v) * p.width / (p.width ? p.width : 1)); };
    const auto map_y = [&](u32 v) { return p.y + int((long long)std::int32_t(v) * p.height / (p.height ? p.height : 1)); };
    const GLenum smooth = (std::int32_t(sx1) - std::int32_t(sx0)) == p.width ? filter : GLenum(GL_LINEAR);
    ::glBlitFramebuffer(GLint(sx0), GLint(sy0), GLint(sx1), GLint(sy1), map_x(dx0), map_y(dy0), map_x(dx1), map_y(dy1), mask, smooth);
    if (scissor) glEnable(GL_SCISSOR_TEST);
    if (!capture_path.empty()) { capture(capture_path); capture_path.clear(); }
}

// Vertex streaming. The renderer appends each batch to one buffer that is
// orphaned (glBufferData with no data) once per frame or when full, so every
// glBufferSubData after the orphan lands in space no queued draw reads. A plain
// glBufferSubData does not know that and makes the driver wait for the GPU
// (or stage a copy) on every batch; those appends are written through an
// unsynchronised mapping instead. Anything else (an update below what was
// already appended, a buffer filled with data) keeps the normal path.
namespace {
std::unordered_map<GLuint, std::uint32_t> appended;   // orphaned buffer -> end of the appended bytes
GLuint bound(u32 target) {
    GLint id = 0;
    glGetIntegerv(target == GL_ELEMENT_ARRAY_BUFFER ? GL_ELEMENT_ARRAY_BUFFER_BINDING : GL_ARRAY_BUFFER_BINDING, &id);
    return GLuint(id);
}
}
void glBufferData(u32 target, u32 size, u32 data, u32 usage) {
    ::glBufferData(target, GLsizeiptr(size), data ? ptr(data) : nullptr, usage);
    if (target != GL_ARRAY_BUFFER && target != GL_ELEMENT_ARRAY_BUFFER) return;
    const GLuint id = bound(target);
    if (data) appended.erase(id); else appended[id] = 0;
}
void glBufferSubData(u32 target, u32 offset, u32 size, u32 data) {
    if ((target == GL_ARRAY_BUFFER || target == GL_ELEMENT_ARRAY_BUFFER) && size) {
        const GLuint id = bound(target);
        const auto it = appended.find(id);
        if (it != appended.end() && offset >= it->second) {
            void* to = glMapBufferRange(target, GLintptr(offset), GLsizeiptr(size),
                                        GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
            if (to) {
                std::memcpy(to, ptr(data), size);
                glUnmapBuffer(target);
                it->second = offset + size;
                ++perf::totals.mapped_appends;
                return;
            }
        }
        if (it != appended.end()) appended.erase(it);
    }
    ++perf::totals.synced_updates;
    ::glBufferSubData(target, GLintptr(offset), GLsizeiptr(size), ptr(data));
}
}

extern "C" {
// EXT_clip_control through the extension loader; absent -> the shader keeps
// its [-1,1] depth remap (same as upstream's WebGL path).
std::uint32_t w2c_th20n_clip_control(struct w2c_th20n*) {
    const char* list = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    if (!list || !std::strstr(list, "GL_EXT_clip_control")) return 0;
    using ClipControl = void (*)(GLenum, GLenum);
    auto clip = reinterpret_cast<ClipControl>(SDL_GL_GetProcAddress("glClipControlEXT"));
    if (!clip) return 0;
    clip(0x8CA1 /*LOWER_LEFT*/, 0x935F /*ZERO_TO_ONE*/);
    return glGetError() == GL_NO_ERROR;
}
}
