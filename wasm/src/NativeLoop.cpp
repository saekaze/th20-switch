// Native frame loop for the TH20 wasm2c build. Replaces upstream's
// web/cpp/sdl/ApplicationHost.cpp (requestAnimationFrame + JS callbacks): the
// native host calls th20n_frame once per display refresh, and each call runs
// exactly one game tick through the same recovered main-loop step
// (game_tick below is upstream's, unchanged). A late frame therefore slows
// the game down for that frame; there are no catch-up ticks, which is what
// kept heavy frames from snowballing into freezes in the other Switch ports.
#include "../../upstream/ports/th20/src/program_entry/program_entry.hpp"
#include "../../upstream/ports/th20/src/program_entry/unrecovered_dependencies.hpp"
#include "../../upstream/ports/th20/web/cpp/platform/Audio.hpp"
#include <emscripten.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>

extern "C" void sdl_native_input();

namespace pe = th20::source::program_entry;

namespace {
bool running = false, suspended = false;
double elapsed = 0, audio_remainder = 0, frame_begin_ms = 0;

// Upstream ApplicationHost.cpp game_tick: one poll_message iteration of the
// recovered main loop. 0 continue, nonzero session finished, -1 exception.
int game_tick() try {
    auto& w = pe::window_state;
    auto& g = pe::graphics_state;
    auto& config = g.configuration;
    if (w.quit_requested != 0) return 1;
    if (pe::device(g) == nullptr) return 0;
    int status;
    if ((w.flags & 4u) != 0) status = pe::run_unlimited_frame(w);
    else if (g.presentation.PresentationInterval == 1 && config.frame_skip == 0) status = pe::run_present_paced_frame(w);
    else status = pe::run_timed_frame(w);
    if (status != 0) return status;
    pe::graphics_event_flags &= ~8u;
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "game_tick: exception: %s\n", e.what());
    return -1;
} catch (...) {
    std::fprintf(stderr, "game_tick: unknown exception\n");
    return -1;
}
}

extern "C" {
// The game clock: frame-paced time, plus the time already spent inside the
// current frame (as upstream does), so a loop waiting on the clock ends.
double sdl_loop_time() {
    return elapsed + (running && !suspended ? std::max(0., emscripten_get_now() - frame_begin_ms) / 1000. : 0.);
}
__attribute__((export_name("sdl_loop_tick"))) int sdl_loop_tick(double seconds, std::uint32_t milliseconds) {
    if (running) return -1;
    elapsed += seconds;
    th20::web::audio::advance(milliseconds);
    sdl_native_input();
    return game_tick();
}
__attribute__((export_name("sdl_loop_pause"))) void sdl_loop_pause(std::uint32_t pause) {
    suspended = pause != 0;
    th20::web::audio::pause(pause != 0);
}
__attribute__((export_name("sdl_loop_start"))) void sdl_loop_start() {
    elapsed = audio_remainder = 0;
    frame_begin_ms = emscripten_get_now();
    running = true;
}
__attribute__((export_name("sdl_loop_stop"))) void sdl_loop_stop() {
    if (!running) return;
    running = false;
    th20::web::audio::pause(true);
}
// One display refresh. `seconds` is the frame period the host paces at
// (1/60 on Switch); returns game_tick's status, or -2 when not running.
__attribute__((export_name("th20n_frame"))) int th20n_frame(double seconds) try {
    if (!running) return -2;
    frame_begin_ms = emscripten_get_now();
    if (suspended) { th20::web::audio::pause(true); return 0; }
    th20::web::audio::pause(false);
    elapsed += seconds;
    audio_remainder += seconds * 1000;
    const auto milliseconds = std::uint32_t(std::floor(audio_remainder));
    audio_remainder -= milliseconds;
    th20::web::audio::advance(milliseconds);
    sdl_native_input();
    const int result = game_tick();
    th20::web::audio::pump();
    return result;
} catch (const std::exception& e) {
    std::fprintf(stderr, "frame: exception: %s\n", e.what());
    return -1;
} catch (...) {
    std::fprintf(stderr, "frame: unknown exception\n");
    return -1;
}
}
