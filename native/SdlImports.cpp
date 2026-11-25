// SDL3 API imports of the TH20 module, implemented on SDL2 (and libnx input on
// Switch). Handles crossing into the module are small integers, never host
// pointers; structures are written in SDL3's layout (all pointer-free here).
#include "Host.hpp"
#include "GlImports.hpp"
#include <SDL2/SDL.h>
#include <algorithm>
#include <array>
#include <deque>
#include <map>

namespace th20n {
namespace {
// SDL3 values (third_party/sdl3/include, checked against the module build).
constexpr std::uint32_t EV_GAMEPAD_ADDED = 1619, EV_GAMEPAD_REMOVED = 1620;
constexpr std::uint32_t EV_FINGER_DOWN = 1792, EV_FINGER_UP = 1793, EV_FINGER_MOTION = 1794;
constexpr std::uint32_t PAD_ID = 1;           // the one virtual gamepad
constexpr std::uint32_t WINDOW = 1, CONTEXT = 1;
constexpr std::uint16_t AUDIO_F32_3 = 0x8120, AUDIO_S16_3 = 0x8010;

SDL_Window* window = nullptr;
SDL_GLContext context = nullptr;
Picture current_picture;
std::deque<std::array<std::uint8_t, 128>> events;
std::uint32_t keyboard_state = 0;             // module address of a bool[512]
Gamepad pad_state;
bool pad_announced = false;
struct Stream { SDL_AudioDeviceID device = 0; };
std::map<std::uint32_t, Stream> streams;
std::uint32_t next_stream = 1;
}

Gamepad& pad() { return pad_state; }
const Picture& picture() { return current_picture; }
void update_picture() {
    int dw = 0, dh = 0;
    if (window) SDL_GL_GetDrawableSize(window, &dw, &dh);
    if (dw <= 0 || dh <= 0) { dw = 640; dh = 480; }
    int pw = dw, ph = dw * 3 / 4;
    if (ph > dh) { ph = dh; pw = dh * 4 / 3; }
    current_picture = {(dw - pw) / 2, (dh - ph) / 2, pw, ph, dw, dh};
}
void push_event(const std::uint8_t (&event)[128]) { std::array<std::uint8_t, 128> e; std::memcpy(e.data(), event, 128); events.push_back(e); }
void push_finger(int type, std::uint64_t finger, float x, float y, float dx, float dy) {
    std::uint8_t e[128]{};
    const std::uint32_t t = type == 0 ? EV_FINGER_DOWN : type == 1 ? EV_FINGER_MOTION : EV_FINGER_UP;
    const std::uint64_t now = std::uint64_t(now_ms() * 1000000.0), touch = 1;
    const float pressure = type == 2 ? 0.f : 1.f;
    std::memcpy(e, &t, 4); std::memcpy(e + 8, &now, 8); std::memcpy(e + 16, &touch, 8); std::memcpy(e + 24, &finger, 8);
    std::memcpy(e + 32, &x, 4); std::memcpy(e + 36, &y, 4); std::memcpy(e + 40, &dx, 4); std::memcpy(e + 44, &dy, 4);
    std::memcpy(e + 48, &pressure, 4); std::memcpy(e + 52, &WINDOW, 4);
    push_event(e);
}
}

using namespace th20n;
extern "C" {
// ---- init / window / GL context --------------------------------------------
std::uint32_t w2c_env_SDL_Init(struct w2c_env*, std::uint32_t flags) {
    // SDL3 and SDL2 share these flag values; gamepads come from the host.
    flags &= SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO;
    return SDL_InitSubSystem(flags) == 0;
}
std::uint32_t w2c_env_SDL_InitSubSystem(struct w2c_env* env, std::uint32_t flags) { return w2c_env_SDL_Init(env, flags); }
void w2c_env_SDL_Quit(struct w2c_env*) {}
std::uint32_t w2c_env_SDL_GL_SetAttribute(struct w2c_env*, std::uint32_t attribute, std::uint32_t value) {
    // SDL3 dropped SDL_GL_CONTEXT_EGL (19): later attributes are one lower.
    const int sdl2 = attribute >= 19 ? int(attribute) + 1 : int(attribute);
    return SDL_GL_SetAttribute(SDL_GLattr(sdl2), int(value)) == 0;
}
std::uint32_t w2c_env_SDL_CreateWindow(struct w2c_env*, std::uint32_t title, std::uint32_t, std::uint32_t, std::uint64_t) {
    if (window) return WINDOW;
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8); SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8); SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    const char* name = title ? str(title) : "Touhou 20";
#ifdef __SWITCH__
    window = SDL_CreateWindow(name, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN);
#else
    window = SDL_CreateWindow(name, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 960, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
#endif
    if (!window) { log("SDL_CreateWindow: %s", SDL_GetError()); return 0; }
    update_picture();
    return WINDOW;
}
void w2c_env_SDL_DestroyWindow(struct w2c_env*, std::uint32_t) {}
// The game sizes its output to the window; it gets the 4:3 picture, which the
// present blit then places inside the real window (GlSpecial.cpp).
std::uint32_t w2c_env_SDL_SetWindowSize(struct w2c_env*, std::uint32_t, std::uint32_t, std::uint32_t) { return 1; }
std::uint32_t w2c_env_SDL_GetWindowSizeInPixels(struct w2c_env*, std::uint32_t, std::uint32_t w, std::uint32_t h) {
    update_picture();
    if (w) store<std::int32_t>(w, current_picture.width);
    if (h) store<std::int32_t>(h, current_picture.height);
    return 1;
}
std::uint32_t w2c_env_SDL_GL_CreateContext(struct w2c_env*, std::uint32_t) {
    if (!window) return 0;
#ifndef __SWITCH__
    if (!context && std::getenv("TH20_GLPROF")) {   // the profiler needs desktop GL queries (GlProfile.cpp)
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
    }
#endif
    if (!context) context = SDL_GL_CreateContext(window);
    if (!context) { log("SDL_GL_CreateContext: %s", SDL_GetError()); return 0; }
    SDL_GL_MakeCurrent(window, context);
    glprof::start();
    gputime::start();
    return CONTEXT;
}
std::uint32_t w2c_env_SDL_GL_DestroyContext(struct w2c_env*, std::uint32_t) { return 1; }
std::uint32_t w2c_env_SDL_GL_SetSwapInterval(struct w2c_env*, std::uint32_t interval) { return SDL_GL_SetSwapInterval(int(interval)) == 0; }
std::uint32_t w2c_env_SDL_GL_SwapWindow(struct w2c_env*, std::uint32_t) {
    const std::uint64_t begin = SDL_GetPerformanceCounter();
    glprof::frame_end();
    gputime::frame_end();
    if (window) SDL_GL_SwapWindow(window);
    perf::totals.swap_ticks += SDL_GetPerformanceCounter() - begin;
    return 1;
}

// ---- events and keyboard ----------------------------------------------------
std::uint32_t w2c_env_SDL_PollEvent(struct w2c_env*, std::uint32_t event) {
    if (!pad_announced && pad_state.connected) {
        pad_announced = true;
        std::uint8_t e[128]{}; const std::uint32_t t = EV_GAMEPAD_ADDED; std::memcpy(e, &t, 4); std::memcpy(e + 16, &PAD_ID, 4); push_event(e);
    }
    if (events.empty()) return 0;
    if (event) std::memcpy(base() + event, events.front().data(), 128);
    events.pop_front();
    return 1;
}
std::uint32_t w2c_env_SDL_GetKeyboardState(struct w2c_env*, std::uint32_t numkeys) {
    if (!keyboard_state) keyboard_state = wasm_malloc(512);
    int count = 0; const Uint8* keys = SDL_GetKeyboardState(&count);
    for (int i = 0; i < 512; ++i) base()[keyboard_state + i] = (i < count && keys && keys[i]) || host_keys[i] ? 1 : 0;
    if (numkeys) store<std::int32_t>(numkeys, 512);
    return keyboard_state;
}
void w2c_env_SDL_ResetKeyboard(struct w2c_env*) {}
std::uint32_t w2c_env_SDL_GetScancodeFromName(struct w2c_env*, std::uint32_t name) { return name ? std::uint32_t(SDL_GetScancodeFromName(str(name))) : 0; }

// ---- gamepad: one virtual pad fed by the host (libnx on Switch) -------------
std::uint32_t w2c_env_SDL_GetGamepads(struct w2c_env*, std::uint32_t count) {
    const std::uint32_t n = pad_state.connected ? 1 : 0;
    const std::uint32_t ids = wasm_malloc(8);
    if (ids) { store<std::uint32_t>(ids, PAD_ID); store<std::uint32_t>(ids + 4, 0); }
    if (count) store<std::int32_t>(count, std::int32_t(n));
    return ids;
}
std::uint32_t w2c_env_SDL_OpenGamepad(struct w2c_env*, std::uint32_t id) { return id == PAD_ID ? PAD_ID : 0; }
void w2c_env_SDL_CloseGamepad(struct w2c_env*, std::uint32_t) {}
std::uint32_t w2c_env_SDL_GamepadConnected(struct w2c_env*, std::uint32_t pad) { return pad == PAD_ID && pad_state.connected; }
std::uint32_t w2c_env_SDL_GetGamepadID(struct w2c_env*, std::uint32_t pad) { return pad; }
std::uint32_t w2c_env_SDL_GetGamepadButton(struct w2c_env*, std::uint32_t pad, std::uint32_t button) {
    return pad == PAD_ID && button < 32 && (pad_state.buttons >> button & 1u);
}
std::uint32_t w2c_env_SDL_GetGamepadAxis(struct w2c_env*, std::uint32_t pad, std::uint32_t axis) {
    return pad == PAD_ID && axis < 6 ? std::uint32_t(std::int32_t(pad_state.axes[axis])) : 0;
}

// ---- audio: SDL3 push streams on SDL2 queued devices ------------------------
std::uint32_t w2c_env_SDL_OpenAudioDeviceStream(struct w2c_env*, std::uint32_t, std::uint32_t spec, std::uint32_t callback, std::uint32_t) {
    if (!spec || callback) { log("SDL_OpenAudioDeviceStream: unsupported (callback %u)", callback); return 0; }
    const auto format = load<std::uint32_t>(spec); const auto channels = load<std::int32_t>(spec + 4), freq = load<std::int32_t>(spec + 8);
    SDL_AudioSpec want{}, have{};
    want.freq = freq; want.channels = Uint8(channels); want.samples = 1024;
    want.format = format == AUDIO_F32_3 ? AUDIO_F32SYS : format == AUDIO_S16_3 ? AUDIO_S16SYS : AUDIO_F32SYS;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) { log("audio: %s", SDL_GetError()); return 0; }
    Stream s; s.device = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (!s.device) { log("SDL_OpenAudioDevice: %s", SDL_GetError()); return 0; }
    log("audio: %d Hz, %d channels", have.freq, have.channels);
    const std::uint32_t id = next_stream++; streams[id] = s;
    return id;
}
std::uint32_t w2c_env_SDL_PutAudioStreamData(struct w2c_env*, std::uint32_t stream, std::uint32_t data, std::uint32_t bytes) {
    auto it = streams.find(stream); if (it == streams.end() || !data) return 0;
    return SDL_QueueAudio(it->second.device, ptr(data), bytes) == 0;
}
std::uint32_t w2c_env_SDL_GetAudioStreamQueued(struct w2c_env*, std::uint32_t stream) {
    auto it = streams.find(stream); return it == streams.end() ? 0 : SDL_GetQueuedAudioSize(it->second.device);
}
std::uint32_t w2c_env_SDL_PauseAudioStreamDevice(struct w2c_env*, std::uint32_t stream) {
    auto it = streams.find(stream); if (it != streams.end()) SDL_PauseAudioDevice(it->second.device, 1); return 1;
}
std::uint32_t w2c_env_SDL_ResumeAudioStreamDevice(struct w2c_env*, std::uint32_t stream) {
    auto it = streams.find(stream); if (it != streams.end()) SDL_PauseAudioDevice(it->second.device, 0); return 1;
}
void w2c_env_SDL_DestroyAudioStream(struct w2c_env*, std::uint32_t stream) {
    auto it = streams.find(stream); if (it == streams.end()) return;
    SDL_CloseAudioDevice(it->second.device); streams.erase(it);
}

// ---- th20n: host services ----------------------------------------------------
double w2c_th20n_now_ms(struct w2c_th20n*) { return module_now_ms(); }
// Render density: the game draws its 640x480 scene at ceil(units * 4 / 640) x
// (upstream picks the units from the browser canvas) and checks it at every
// present, resizing its render targets when it changes. Only render targets
// scale; images (portraits, backgrounds) keep their size, so at 1x a portrait
// is drawn into a 640x480 layer and loses detail. The density is fixed for
// the run (render_density(), main.cpp): 160 units per 1x.
std::uint32_t w2c_th20n_output_units(struct w2c_th20n*) {
    return std::uint32_t(160 * render_density());
}
// The game's render targets (playfield layers) can use a lower scale than the
// backbuffer (wasm/compat/th20n_native.h); the module clamps it.
std::uint32_t w2c_th20n_target_scale(struct w2c_th20n*) { return std::uint32_t(target_density()); }
}
