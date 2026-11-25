// GPU time per frame (GL_EXT_disjoint_timer_query, in Mesa for GLES when the
// driver has timestamps, as nouveau does). Each frame is split where the draw
// framebuffer changes, so th20-switch.log can say which render target the
// GPU spent its time on. A frame is timed from its first framebuffer bind or
// draw to the swap, so the time the GPU waits for the game between frames is
// not counted. Results are read four frames later, when the GPU is long done
// with them: nothing here waits for the GPU.
#include "GlImports.hpp"
#include <algorithm>
#include <cstring>
#include <vector>

#ifndef GL_TIME_ELAPSED_EXT
#define GL_TIME_ELAPSED_EXT 0x88BF
#endif
#ifndef GL_GPU_DISJOINT_EXT
#define GL_GPU_DISJOINT_EXT 0x8FBB
#endif

namespace th20n::gputime {
namespace {
struct Part { GLuint query; GLuint framebuffer; };
GLuint bound = 0;                       // the draw framebuffer bound now
struct Slot { std::vector<Part> parts; std::uint32_t frame = 0; bool used = false; };
constexpr int depth = 4;
Slot slots[depth];
std::vector<GLuint> pool;
int current = 0;
bool supported = false, active = false;
std::uint32_t frame_number = 0;
Result last{};

GLuint take_query() {
    if (pool.empty()) { GLuint q[16]{}; glGenQueries(16, q); pool.assign(q, q + 16); }
    const GLuint q = pool.back(); pool.pop_back(); return q;
}
void begin(GLuint framebuffer) {
    Part part{take_query(), framebuffer};
    glBeginQuery(GL_TIME_ELAPSED_EXT, part.query);
    slots[current].parts.push_back(part);
    active = true;
}
void end() { if (active) { glEndQuery(GL_TIME_ELAPSED_EXT); active = false; } }
// Reads a finished slot into `last` and gives its queries back to the pool.
void collect(Slot& slot) {
    if (!slot.used) return;
    GLint disjoint = 0; glGetIntegerv(GL_GPU_DISJOINT_EXT, &disjoint);
    Result r{}; r.frame = slot.frame; r.valid = !disjoint;
    for (const auto& part : slot.parts) {
        GLuint ns = 0; glGetQueryObjectuiv(part.query, GL_QUERY_RESULT, &ns);
        const double ms = ns / 1e6;
        r.total_ms += ms;
        auto it = std::find_if(r.parts, r.parts + r.count, [&](const Result::Part& p) { return p.framebuffer == part.framebuffer; });
        if (it != r.parts + r.count) it->ms += ms;
        else if (r.count < Result::max_parts) r.parts[r.count++] = {part.framebuffer, ms};
        pool.push_back(part.query);
    }
    slot.parts.clear(); slot.used = false;
    if (r.valid) last = r;
}
}
void start() {
    const char* extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    supported = extensions && std::strstr(extensions, "GL_EXT_disjoint_timer_query");
    log("GPU timer: %s", supported ? "on" : "not available");
    while (glGetError() != GL_NO_ERROR) {}
}
bool available() { return supported; }
void framebuffer(GLuint framebuffer) {
    const bool same = framebuffer == bound;
    bound = framebuffer;
    if (!supported || (active && same)) return;
    end();
    begin(framebuffer);
}
// A draw on whatever is bound starts the frame's timing if no bind did.
void draw() { if (supported && !active) begin(bound); }
void frame_end() {
    if (!supported) return;
    end();
    slots[current].frame = frame_number++; slots[current].used = true;
    current = (current + 1) % depth;
    collect(slots[current]);            // the slot about to be reused: four frames old
}
const Result& latest() { return last; }
}
