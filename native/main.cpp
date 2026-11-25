// Touhou 20 for Nintendo Switch: native entry point of the wasm2c build.
//
// The game (eagler-touhou ports/th20, compiled to wasm32 and converted to C)
// runs on its own thread with a large stack. Each loop iteration feeds input,
// then calls the module's th20n_frame: exactly one game tick, whose present
// blocks on vsync. No catch-up ticks after a slow frame.
#include "Host.hpp"
#include "GlImports.hpp"
#include "wasm-rt-impl.h"
#include <SDL2/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>
#ifdef __SWITCH__
#include <switch.h>
#else
#include <pthread.h>
#endif

namespace th20n {
w2c_th20 module;
std::uint32_t wasm_malloc(std::uint32_t bytes) { return w2c_th20_malloc(&module, bytes); }
void wasm_free(std::uint32_t address) { if (address) w2c_th20_free(&module, address); }
}
using namespace th20n;

namespace {
w2c_env env{&module};
w2c_th20n services{&module};
w2c_wasi__snapshot__preview1 wasi{&module};
int exit_code = 0;
bool module_exited = false;

// ---- input ------------------------------------------------------------------
// SDL3 gamepad buttons. The module's InputHost turns them into DirectInput
// button numbers in this order: 0 south, 1 east, 2 west, 3 north, 4 left
// shoulder, 5 right shoulder, 6 back, 7 start, 8 / 9 stick clicks.
enum : std::uint32_t { B_SOUTH = 0, B_EAST, B_WEST, B_NORTH, B_BACK, B_GUIDE, B_START, B_LSTICK, B_RSTICK, B_LSHOULDER, B_RSHOULDER,
    B_UP, B_DOWN, B_LEFT, B_RIGHT };
// Key Config button numbers are the same as in the other Switch ports:
// 0 B, 1 A, 2 L/ZL, 3 R/ZR, 4 +, 5 X, 6 Y (the game's default pad bindings
// are patched to shot 0, bomb 1, focus 2, pause 4). Each Switch button is
// sent as the SDL button that lands on its number.
constexpr std::uint32_t PORT_B = 1u << B_SOUTH, PORT_A = 1u << B_EAST, PORT_L = 1u << B_WEST, PORT_R = 1u << B_NORTH,
    PORT_PLUS = 1u << B_LSHOULDER, PORT_X = 1u << B_RSHOULDER, PORT_Y = 1u << B_BACK;
#ifdef __SWITCH__
::PadState npad;
void read_switch_pad(Gamepad& out) {
    padUpdate(&npad);
    out.connected = padIsConnected(&npad);
    const u64 held = padGetButtons(&npad);
    std::uint32_t b = 0;
    if (held & HidNpadButton_B) b |= PORT_B;
    if (held & HidNpadButton_A) b |= PORT_A;
    if (held & (HidNpadButton_L | HidNpadButton_ZL)) b |= PORT_L;
    if (held & (HidNpadButton_R | HidNpadButton_ZR)) b |= PORT_R;
    if (held & HidNpadButton_Plus) b |= PORT_PLUS;
    if (held & HidNpadButton_X) b |= PORT_X;
    if (held & HidNpadButton_Y) b |= PORT_Y;
    out.skip = held & (HidNpadButton_R | HidNpadButton_ZR);
    out.snapshot = held & HidNpadButton_Minus;
    if (held & HidNpadButton_StickL) b |= 1u << B_LSTICK;
    if (held & HidNpadButton_StickR) b |= 1u << B_RSTICK;
    if (held & HidNpadButton_Up) b |= 1u << B_UP;
    if (held & HidNpadButton_Down) b |= 1u << B_DOWN;
    if (held & HidNpadButton_Left) b |= 1u << B_LEFT;
    if (held & HidNpadButton_Right) b |= 1u << B_RIGHT;
    out.buttons = b;
    const auto l = padGetStickPos(&npad, 0), r = padGetStickPos(&npad, 1);
    out.axes[0] = std::int16_t(std::clamp<int>(l.x, -32768, 32767)); out.axes[1] = std::int16_t(std::clamp<int>(-l.y, -32768, 32767));
    out.axes[2] = std::int16_t(std::clamp<int>(r.x, -32768, 32767)); out.axes[3] = std::int16_t(std::clamp<int>(-r.y, -32768, 32767));
    out.axes[4] = (held & HidNpadButton_ZL) ? 32767 : 0; out.axes[5] = (held & HidNpadButton_ZR) ? 32767 : 0;
}
#else
SDL_GameController* desktop_pad = nullptr;
void read_desktop_pad(Gamepad& out) {
    if (!desktop_pad) for (int i = 0; i < SDL_NumJoysticks(); ++i) if (SDL_IsGameController(i)) { desktop_pad = SDL_GameControllerOpen(i); break; }
    out = {};
    out.connected = true;   // the module always sees one pad; idle when unplugged
    if (!desktop_pad) return;
    // The same positions as on the Switch (SDL2's A is the bottom button,
    // like the Switch's B), so the defaults work on a desktop pad too.
    const auto held = [](SDL_GameControllerButton b) { return SDL_GameControllerGetButton(desktop_pad, b) != 0; };
    if (held(SDL_CONTROLLER_BUTTON_A)) out.buttons |= PORT_B;
    if (held(SDL_CONTROLLER_BUTTON_B)) out.buttons |= PORT_A;
    if (held(SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) out.buttons |= PORT_L;
    if (held(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) out.buttons |= PORT_R;
    if (held(SDL_CONTROLLER_BUTTON_START)) out.buttons |= PORT_PLUS;
    if (held(SDL_CONTROLLER_BUTTON_Y)) out.buttons |= PORT_X;
    if (held(SDL_CONTROLLER_BUTTON_X)) out.buttons |= PORT_Y;
    out.skip = held(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    out.snapshot = held(SDL_CONTROLLER_BUTTON_BACK);
    for (int b = SDL_CONTROLLER_BUTTON_DPAD_UP; b <= SDL_CONTROLLER_BUTTON_DPAD_RIGHT; ++b)
        if (held(SDL_GameControllerButton(b))) out.buttons |= 1u << b;   // same numbers in SDL3
    for (int a = 0; a < 6; ++a) out.axes[a] = SDL_GameControllerGetAxis(desktop_pad, SDL_GameControllerAxis(a));
}
#endif

// Desktop test hook: TH20_SCRIPT=file of "frame mask" lines, the mask in hex.
// Mask bits 0-15 are keyboard keys (Z X C LShift Up Down Left Right Esc LCtrl
// Enter), bits 16-31 SDL3 gamepad buttons 0-15 (e.g. 10000 = B, 2000000 = +).
struct Script { std::vector<std::pair<std::uint32_t, std::uint32_t>> steps; size_t next = 0; std::uint32_t held = 0; bool active = false; } script;
constexpr SDL_Scancode script_keys[] = {SDL_SCANCODE_Z, SDL_SCANCODE_X, SDL_SCANCODE_C, SDL_SCANCODE_LSHIFT, SDL_SCANCODE_UP, SDL_SCANCODE_DOWN,
    SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT, SDL_SCANCODE_ESCAPE, SDL_SCANCODE_LCTRL, SDL_SCANCODE_RETURN};
}
namespace th20n {
// Keys the host holds on top of the physical keyboard (script, R -> Ctrl).
bool host_keys[512]{};

// Render scale, fixed for the whole run (SdlImports.cpp, output_units). The
// screen is drawn at 2x: portraits, dialogue, text and the HUD go straight
// into it. The playfield layers (stage background, bullets, the full-screen
// spell and bomb effects) are drawn into the game's own render targets at 1x
// and scaled up: that is where nearly all the GPU work is, and at 2x the
// heaviest effects need more memory bandwidth than the Switch has. `hires`
// in the game folder draws those at 2x too, `lowres` draws everything at 1x.
namespace { int density = 2, targets = 1; }
int render_density() { return density; }
int target_density() { return std::min(targets, density); }
}
namespace {
void touch_event(const SDL_Event& e) {
    // SDL2 reports touches normalised to the whole screen; the game wants the
    // 4:3 picture inside it.
    const auto& p = picture(); if (p.width <= 0 || p.height <= 0) return;
    const float x = (e.tfinger.x * p.drawable_width - p.x) / p.width, y = (e.tfinger.y * p.drawable_height - p.y) / p.height;
    const int type = e.type == SDL_FINGERDOWN ? 0 : e.type == SDL_FINGERMOTION ? 1 : 2;
    if (type == 0 && (x < 0 || x > 1 || y < 0 || y > 1)) return;
    push_finger(type, std::uint64_t(e.tfinger.fingerId), std::clamp(x, 0.f, 1.f), std::clamp(y, 0.f, 1.f),
                e.tfinger.dx * p.drawable_width / p.width, e.tfinger.dy * p.drawable_height / p.height);
}

// ---- running the module --------------------------------------------------------
// Calls into the module go through here: a wasm trap (bounds, unreachable,
// call stack exhausted, ...) ends the game with a message instead of a crash.
template<class F> bool guarded(const char* what, F&& call) {
    const wasm_rt_trap_t trap = wasm_rt_trap_t(wasm_rt_impl_try());
    if (trap == WASM_RT_TRAP_NONE) { call(); return !module_exited; }
    if (module_exited) return false;
    log("%s: wasm trap: %s", what, wasm_rt_strerror(trap));
    exit_code = 2;
    return false;
}

std::string fatal_message;
void run_game() {
    wasm_rt_init();
    wasm2c_th20_instantiate(&module, &env, &services, &wasi);
    if (!guarded("initialize", [] { w2c_th20_0x5Finitialize(&module); })) { fatal_message = "module start-up failed"; return; }
    const double begin = now_ms();
    std::uint32_t opened = 0;
    if (!guarded("sdl_game_open", [&] { opened = w2c_th20_sdl_game_open(&module, 0, 0); }) || !opened) {
        fatal_message = "game start-up failed (see th20-switch.log)"; return;
    }
    log("game opened in %.1fs", (now_ms() - begin) / 1000);
    guarded("sdl_loop_start", [] { w2c_th20_sdl_loop_start(&module); });

#ifndef __SWITCH__
    const char* frames_env = std::getenv("TH20_FRAMES"); const std::uint32_t frame_limit = frames_env ? std::uint32_t(std::strtoul(frames_env, nullptr, 10)) : 0;
    if (std::getenv("TH20_NOVSYNC")) SDL_GL_SetSwapInterval(0);
    sampler::start();
    // TH20_FRAMELOG=file: every frame's split (ms): frame tick game images text gl io swap lzss
    FILE* frame_log = std::getenv("TH20_FRAMELOG") ? std::fopen(std::getenv("TH20_FRAMELOG"), "w") : nullptr;
    const char* snap_env = std::getenv("TH20_SNAP_EVERY"); const std::uint32_t snap_every = snap_env ? std::uint32_t(std::strtoul(snap_env, nullptr, 10)) : 0;
    if (snap_every) make_directory(paths().save + "/snapshot");
    if (const char* file = std::getenv("TH20_SCRIPT")) {
        if (FILE* f = std::fopen(file, "r")) { unsigned frame = 0, mask = 0; while (std::fscanf(f, "%u %x", &frame, &mask) == 2) script.steps.push_back({frame, mask}); std::fclose(f); script.active = true; }
        log("script %s: %zu steps", file, script.steps.size());
    }
#endif
    if (const int fixed = fixed_density()) { density = fixed; targets = fixed; }
#ifndef __SWITCH__
    if (const char* t = std::getenv("TH20_TARGET_SCALE")) targets = std::atoi(t);
#endif
    log("resolution: %dx (%dx%d), playfield layers %dx", density, 640 * density, 480 * density, std::min(targets, density));
#ifndef __SWITCH__
    // TH20_HIRES_AT=frame: 1x up to that frame, then 2x (gets a profiling run to a late scene sooner).
    const std::uint32_t hires_at = std::getenv("TH20_HIRES_AT") ? std::uint32_t(std::strtoul(std::getenv("TH20_HIRES_AT"), nullptr, 10)) : 0;
    if (hires_at) density = 1;
#endif
    std::uint32_t frame = 0, slow = 0; double worst = 0, total = 0, cpu_total = 0, spike_window = 0; std::uint32_t spike_lines = 0;
    std::uint32_t gpu_seen = ~0u, gpu_frames = 0, gpu_slow = 0, gpu_lines = 0; double gpu_total = 0, gpu_worst = 0, gpu_window = 0;
    bool running = true;
    while (running
#ifdef __SWITCH__
           && appletMainLoop()
#endif
    ) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            else if (e.type == SDL_FINGERDOWN || e.type == SDL_FINGERUP || e.type == SDL_FINGERMOTION) touch_event(e);
        }
#ifdef __SWITCH__
        read_switch_pad(pad());
#else
        read_desktop_pad(pad());
#endif
        std::memset(host_keys, 0, sizeof host_keys);
        if (pad().skip) host_keys[SDL_SCANCODE_LCTRL] = true;         // R/ZR: skip dialogue (Ctrl)
        if (pad().snapshot) host_keys[SDL_SCANCODE_HOME] = true;      // −: the game's snapshot key
        if (script.active) {
            while (script.next < script.steps.size() && script.steps[script.next].first <= frame) script.held = script.steps[script.next++].second;
            for (unsigned k = 0; k < sizeof script_keys / sizeof *script_keys; ++k) if (script.held & (1u << k)) host_keys[script_keys[k]] = true;
            pad().buttons |= script.held >> 16;
        }
#ifndef __SWITCH__
        if (snap_every && frame % snap_every == 0) { char name[64]; std::snprintf(name, sizeof name, "/snapshot/f%06u.bmp", frame); request_capture(paths().save + name); }
#endif
#ifndef __SWITCH__
        if (hires_at && frame == hires_at) { density = 2; log("frame %u: resolution 2x", frame); }
        sampler::frame(frame);
        set_clock_frame(frame);
#endif
        const double tick_begin = now_ms();
        const perf::Totals before = perf::totals;
#ifndef __SWITCH__
        timespec cpu0{}; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &cpu0);
#endif
        std::uint32_t status = 0;
        if (!guarded("frame", [&] { status = w2c_th20_th20n_frame(&module, 1.0 / 60.0); })) break;
#ifndef __SWITCH__
        timespec cpu1{}; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &cpu1);
        cpu_total += (cpu1.tv_sec - cpu0.tv_sec) * 1000.0 + (cpu1.tv_nsec - cpu0.tv_nsec) / 1e6;
#endif
        const double tick = now_ms() - tick_begin;
        {   // GPU time of the frames the GPU has finished (four frames back)
            const auto& g = gputime::latest();
            if (g.valid && g.frame != gpu_seen && g.frame >= 2) {   // frames 0-1 also time the start-up
                gpu_seen = g.frame; gpu_total += g.total_ms; gpu_worst = std::max(gpu_worst, g.total_ms); ++gpu_frames;
                if (g.total_ms > 1000.0 / 60) {
                    ++gpu_slow;
                    if (now_ms() - gpu_window > 60000) { gpu_window = now_ms(); gpu_lines = 0; }
                    if (gpu_lines++ < 20) {
                        char parts[256]; int at = 0; parts[0] = 0;
                        for (int i = 0; i < g.count && at < int(sizeof parts) - 32; ++i)
                            at += std::snprintf(parts + at, sizeof parts - at, "%sfb %u %.1f", i ? ", " : "", g.parts[i].framebuffer, g.parts[i].ms);
                        log("GPU frame %u: %.1fms (%s)", g.frame, g.total_ms, parts);
                    }
                }
            }
        }
        total += tick; worst = std::max(worst, tick); if (tick > 1000.0 / 60) ++slow;
#ifndef __SWITCH__
        if (frame_log) {
            const auto& t = perf::totals; const double ms = 1000.0 / double(SDL_GetPerformanceFrequency());
            const double gl = double(t.gl_ticks - before.gl_ticks) * ms, swap = double(t.swap_ticks - before.swap_ticks) * ms, io = double(t.io_ticks - before.io_ticks) * ms;
            std::fprintf(frame_log, "%u %.2f %.2f %.2f %.2f %.2f %.2f %.2f %.2f\n", frame, tick, std::max(0.0, tick - gl - swap - io),
                         double(t.image_ticks - before.image_ticks) * ms, double(t.text_ticks - before.text_ticks) * ms, gl, io, swap,
                         double(t.lzss_ticks - before.lzss_ticks) * ms);
            if (frame % 300 == 0) std::fflush(frame_log);
        }
#endif
        if (std::int32_t(status) == -1) log("frame %u: game tick raised an exception", frame);
        else if (status != 0) { log("game finished (status %d)", std::int32_t(status)); running = false; }
        ++frame;
        // Slow frames (over 25 ms, vsync wait included) go to the log, at most 20 a minute.
        if (tick > 25.0) {
            if (now_ms() - spike_window > 60000) { spike_window = now_ms(); spike_lines = 0; }
            if (spike_lines++ < 20) {
                const auto& t = perf::totals; const double ms = 1000.0 / double(SDL_GetPerformanceFrequency());
                const double gl = double(t.gl_ticks - before.gl_ticks) * ms, swap = double(t.swap_ticks - before.swap_ticks) * ms;
                const double io = double(t.io_ticks - before.io_ticks) * ms;
                const double images = double(t.image_ticks - before.image_ticks) * ms, text = double(t.text_ticks - before.text_ticks) * ms;
                const double lzss = double(t.lzss_ticks - before.lzss_ticks) * ms;
                log("slow frame %u: %.1fms (game %.1f [images %.1f: %u, %llu Kpx; text %.1f: %u glyphs; save encoding %.1f: %llu KB], GL %.1f in %u calls / %llu KB, file I/O %.1f in %u calls / %llu KB, swap %.1f)",
                    frame, tick, std::max(0.0, tick - gl - swap - io), images, t.images - before.images,
                    (unsigned long long)((t.image_pixels - before.image_pixels) / 1000), text, t.glyphs - before.glyphs,
                    lzss, (unsigned long long)((t.lzss_bytes - before.lzss_bytes) / 1024),
                    gl, t.gl_calls - before.gl_calls,
                    (unsigned long long)((t.upload_bytes - before.upload_bytes) / 1024), io, t.io_calls - before.io_calls,
                    (unsigned long long)((t.io_bytes - before.io_bytes) / 1024), swap);
            }
        }
        if (frame % 600 == 0) {
            log("frame %u: tick avg %.2fms worst %.2fms slow %u, game memory %llu MB", frame, total / 600, worst, slow,
                (unsigned long long)(module.w2c_memory.size >> 20));
            {   // where the frames went: GL calls, vsync wait, and the game itself
                auto& t = perf::totals; const double ms = 1000.0 / double(SDL_GetPerformanceFrequency()) / 600;
                const double gl = double(t.gl_ticks) * ms, swap = double(t.swap_ticks) * ms;
                log("frame %u: per frame: game %.2fms, GL %.2fms (%u calls, %u draws, %llu KB uploaded, %u+%u vertex appends), swap %.2fms",
                    frame, std::max(0.0, total / 600 - gl - swap), gl, t.gl_calls / 600, t.draws / 600,
                    (unsigned long long)(t.upload_bytes / 600 / 1024), t.mapped_appends / 600, t.synced_updates / 600, swap);
                t = {};
            }
            if (gpu_frames) log("frame %u: GPU avg %.2fms worst %.2fms, over 16.7ms: %u", frame, gpu_total / gpu_frames, gpu_worst, gpu_slow);
            gpu_total = gpu_worst = 0; gpu_frames = gpu_slow = 0;
#ifndef __SWITCH__
            log("frame %u: game thread cpu avg %.2fms", frame, cpu_total / 600); cpu_total = 0;
#endif
            total = worst = 0; slow = 0;
        }
#ifndef __SWITCH__
        if (frame_limit && frame >= frame_limit) running = false;
#endif
    }
    guarded("application_save", [] { w2c_th20_application_save(&module, 0); });
    guarded("sdl_game_close", [] { w2c_th20_sdl_game_close(&module); });
}

#ifdef __SWITCH__
void thread_entry(void*) { run_game(); }
#else
void* thread_entry(void*) { run_game(); return nullptr; }
#endif

#ifdef __SWITCH__
// The clocks the console runs at (read only): the GPU clock and the memory
// clock decide how much the GPU can draw per frame. Docked runs the GPU at
// 768 MHz, handheld at 307 or 384 MHz.
void log_clocks() {
    u32 cpu = 0, gpu = 0, memory = 0;
    bool read = false;
    if (hosversionAtLeast(8, 0, 0)) {
        if (R_SUCCEEDED(clkrstInitialize())) {
            const struct { PcvModuleId id; u32* out; } modules[] = {{PcvModuleId_CpuBus, &cpu}, {PcvModuleId_GPU, &gpu}, {PcvModuleId_EMC, &memory}};
            for (const auto& m : modules) {
                ClkrstSession session;
                if (R_SUCCEEDED(clkrstOpenSession(&session, m.id, 3))) { read |= R_SUCCEEDED(clkrstGetClockRate(&session, m.out)); clkrstCloseSession(&session); }
            }
            clkrstExit();
        }
    } else if (R_SUCCEEDED(pcvInitialize())) {
        read = R_SUCCEEDED(pcvGetClockRate(PcvModule_CpuBus, &cpu)) | R_SUCCEEDED(pcvGetClockRate(PcvModule_GPU, &gpu)) |
               R_SUCCEEDED(pcvGetClockRate(PcvModule_EMC, &memory));
        pcvExit();
    }
    ApmPerformanceMode mode = ApmPerformanceMode_Invalid; u32 configuration = 0;
    if (R_SUCCEEDED(apmInitialize())) {
        if (R_SUCCEEDED(apmGetPerformanceMode(&mode)) && mode != ApmPerformanceMode_Invalid) apmGetPerformanceConfiguration(mode, &configuration);
        apmExit();
    }
    if (read) log("clocks: CPU %.1f MHz, GPU %.1f MHz, memory %.1f MHz (performance mode %d, configuration 0x%08x)",
                  cpu / 1e6, gpu / 1e6, memory / 1e6, int(mode), configuration);
    else log("clocks: not readable (performance mode %d, configuration 0x%08x)", int(mode), configuration);
}
#endif

void fatal(const std::string& message) {
    log("fatal: %s", message.c_str());
    SDL_Quit();
#ifdef __SWITCH__
    consoleInit(nullptr);
    std::printf("\n  Touhou 20: Fossilized Wonders - Switch port\n\n  Error: %s\n\n", message.c_str());
    std::printf("  Data folder: %s\n  Needed: th20.dat and msgothic.ttc. Optional: thbgm.dat\n\n  Press + to exit.\n", paths().data.c_str());
    consoleUpdate(nullptr);
    while (appletMainLoop()) { padUpdate(&npad); if (padGetButtonsDown(&npad) & HidNpadButton_Plus) break; consoleUpdate(nullptr); }
    consoleExit(nullptr);
#else
    std::fprintf(stderr, "%s\n", message.c_str());
#endif
}
}

extern "C" [[noreturn]] void th20n_module_exit(int code) {
    // proc_exit from the module: unwind to guarded() through the trap path.
    module_exited = true; exit_code = code;
    log("module exit(%d)", code);
    wasm_rt_trap(WASM_RT_TRAP_UNREACHABLE);
}

int main(int argc, char** argv) {
    locate_data(argc, argv);
#ifdef __SWITCH__
    padConfigureInput(1, HidNpadStyleSet_NpadStandard); padInitializeDefault(&npad);
    appletSetFocusHandlingMode(AppletFocusHandlingMode_SuspendHomeSleep);
#endif
    log("th20-switch: data %s", paths().data.c_str());
#ifdef __SWITCH__
    // Docked, the GPU runs about twice as fast as handheld: it matters for 2x.
    log("console: %s, %s", appletGetOperationMode() == AppletOperationMode_Console ? "docked" : "handheld",
        appletGetAppletType() == AppletType_Application || appletGetAppletType() == AppletType_SystemApplication ? "full RAM (title takeover)" : "applet mode (album)");
    log_clocks();
#endif
    if (!exists(paths().data + "/th20.dat")) { fatal("th20.dat not found"); return 1; }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) { fatal(SDL_GetError()); return 1; }
    // The game thread: wasm2c turns the game's call depth into native stack.
    constexpr size_t stack = 32 * 1024 * 1024;
#ifdef __SWITCH__
    Thread thread;
    if (R_FAILED(threadCreate(&thread, thread_entry, nullptr, nullptr, stack, 0x2C, 0)) || R_FAILED(threadStart(&thread))) { fatal("cannot start the game thread"); return 1; }
    threadWaitForExit(&thread); threadClose(&thread);
#else
    pthread_attr_t attr; pthread_attr_init(&attr); pthread_attr_setstacksize(&attr, stack);
    pthread_t thread; if (pthread_create(&thread, &attr, thread_entry, nullptr) != 0) { fatal("cannot start the game thread"); return 1; }
    pthread_join(thread, nullptr);
#endif
    if (!fatal_message.empty()) { fatal(fatal_message); return 1; }
    if (exit_code == 2) { fatal("the game stopped (wasm trap, see th20-switch.log)"); return 1; }
    SDL_Quit();
    return 0;
}
