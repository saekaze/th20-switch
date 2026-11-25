// Native host for the TH20 wasm2c module: shared state and helpers.
//
// The game runs as wasm32 code converted to C (generated/th20*.c). Its
// pointers are 32-bit addresses into the module's linear memory; every import
// below receives such addresses and resolves them against the memory base,
// which is re-read on each call because memory.grow may move it.
#pragma once
#include "th20.h"
#include <cstdint>
#include <cstring>
#include <string>

struct w2c_env { w2c_th20* module; };
struct w2c_th20n { w2c_th20* module; };
struct w2c_wasi__snapshot__preview1 { w2c_th20* module; };

namespace th20n {
extern w2c_th20 module;
inline std::uint8_t* base() { return module.w2c_memory.data; }
inline std::uint64_t memory_size() { return module.w2c_memory.size; }
// Address -> host pointer (0 -> nullptr). Bounds are the module's job; the
// generated code checks its own accesses, these are the host's.
inline void* ptr(std::uint32_t address) { return address ? base() + address : nullptr; }
template<class T> inline T* at(std::uint32_t address) { return static_cast<T*>(ptr(address)); }
inline const char* str(std::uint32_t address) { return address ? reinterpret_cast<const char*>(base() + address) : nullptr; }
template<class T> inline T load(std::uint32_t address) { T v; std::memcpy(&v, base() + address, sizeof v); return v; }
template<class T> inline void store(std::uint32_t address, T v) { std::memcpy(base() + address, &v, sizeof v); }
// Allocations inside the module (its exported malloc/free).
std::uint32_t wasm_malloc(std::uint32_t bytes);
void wasm_free(std::uint32_t address);

// ---- platform ----------------------------------------------------------
struct Paths {
    std::string data = "sdmc:/switch/touhou/touhou20";   // th20.dat, thbgm.dat, msgothic.ttc
    std::string save = "sdmc:/switch/touhou/touhou20";   // th20.cfg, score, replay/, snapshot/
};
Paths& paths();
void locate_data(int argc, char** argv);
void log(const char* fmt, ...);
bool exists(const std::string& path);
bool make_directory(const std::string& path);
double now_ms();
double module_now_ms();                      // the clock given to the module (Platform.cpp)
void set_clock_frame(std::uint32_t frame);   // desktop TH20_FIXED_CLOCK: the frame the module's clock follows
#ifndef __SWITCH__
namespace sampler { void start(); void frame(std::uint32_t number); }   // Sampler.cpp (desktop profiling)
#endif
void use_spare_cores(int core);   // worker threads: run on cores 1-2, preferring `core` (Platform.cpp)
int render_density();     // 1 or 2: the scale the game draws at (main.cpp)
int target_density();     // 1 or 2: the scale of the game's own render targets (main.cpp)
int fixed_density();      // 1 or 2 when `lowres` / `hires` picks it, else 0 (Platform.cpp)

// ---- presentation (SdlImports.cpp) ----------------------------------------
// The 4:3 picture inside the window (pillarboxed), in drawable pixels.
struct Picture { int x = 0, y = 0, width = 640, height = 480, drawable_width = 640, drawable_height = 480; };
const Picture& picture();
void update_picture();
// Called by the GL layer when the game blits its frame to the window.
// Desktop tests: save the next presented frame to `path` (BMP).
void request_capture(const std::string& path);

// ---- input (main.cpp feeds, SdlImports.cpp serves) -------------------------
// buttons: SDL3 gamepad button bits. skip / snapshot: Switch R/ZR and − (the
// host turns them into the game's Ctrl and Home keys).
struct Gamepad { bool connected = true; std::uint32_t buttons = 0; std::int16_t axes[6]{}; bool skip = false, snapshot = false; };
Gamepad& pad();
void push_event(const std::uint8_t (&event)[128]);
// Keys the host holds on top of the physical keyboard (SDL scancodes).
extern bool host_keys[512];
void push_finger(int type, std::uint64_t finger, float x, float y, float dx, float dy);
}
