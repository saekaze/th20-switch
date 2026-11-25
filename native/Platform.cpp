// Switch host: data/save folders, the log file and the monotonic clock.
#include "Host.hpp"
#include <SDL2/SDL.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>
#include <vector>
#ifdef __SWITCH__
#include <switch.h>
#endif

namespace th20n {
Paths& paths() { static Paths p; return p; }
namespace {
FILE* log_file = nullptr;
std::string directory_of(const char* path) {
    std::string s = path ? path : ""; const auto slash = s.find_last_of('/');
    return slash == std::string::npos ? std::string() : s.substr(0, slash);
}
}
void locate_data(int argc, char** argv) {
    std::vector<std::string> candidates;
#ifdef __SWITCH__
    // hbmenu passes the NRO path as argv[0]; the folder next to it wins. Then
    // the usual Touhou port folders on the SD card.
    if (argc > 0 && argv[0]) { const auto dir = directory_of(argv[0]); if (!dir.empty()) candidates.push_back(dir); }
    for (const char* root : {"sdmc:/switch/", "sdmc:/", "sdmc:/touhou/", "sdmc:/switch/touhou/", "sdmc:/games/", "sdmc:/roms/"})
        for (const char* name : {"th20", "touhou20", "touhou 20"}) candidates.push_back(std::string(root) + name);
#else
    if (argc > 1 && argv[1]) candidates.push_back(argv[1]);
    candidates.push_back(".");
#endif
    for (const auto& c : candidates) if (exists(c + "/th20.dat")) { paths().data = paths().save = c; break; }
    if (!exists(paths().data + "/th20.dat") && !candidates.empty()) paths().data = paths().save = candidates.front();
    make_directory(paths().save);
    log_file = std::fopen((paths().save + "/th20-switch.log").c_str(), "w");
}
void log(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); char line[1024]; std::vsnprintf(line, sizeof line, fmt, ap); va_end(ap);
    std::fprintf(stderr, "%s\n", line);
    if (log_file) { std::fprintf(log_file, "%s\n", line); std::fflush(log_file); }
}
bool exists(const std::string& path) { struct stat st; return stat(path.c_str(), &st) == 0; }
bool make_directory(const std::string& path) { return mkdir(path.c_str(), 0777) == 0 || exists(path); }
double now_ms() {
    static const double frequency = double(SDL_GetPerformanceFrequency());
    static const Uint64 start = SDL_GetPerformanceCounter();
    return double(SDL_GetPerformanceCounter() - start) * 1000.0 / frequency;
}
// The clock the module sees. Desktop test builds with TH20_FIXED_CLOCK run
// it from the frame count (1/60 s per frame, plus 1 us per reading so loops
// that wait for time still end): the game's random seed and timing are then
// the same on every run, so two runs can be compared frame for frame.
namespace { std::uint32_t clock_frame = 0; std::uint64_t clock_reads = 0; }
void set_clock_frame(std::uint32_t frame) { clock_frame = frame; }
double module_now_ms() {
#ifndef __SWITCH__
    static const bool fixed = std::getenv("TH20_FIXED_CLOCK") != nullptr;
    if (fixed) return double(clock_frame) * (1000.0 / 60.0) + double(++clock_reads) * 0.001;
#endif
    return now_ms();
}
// Render scale (see main.cpp, density): an empty `hires` or `lowres` file in
// the game folder draws everything at 2x or at 1x (desktop test builds:
// TH20_HIRES / TH20_LOWRES); 0 means the default (screen 2x, playfield 1x).
int fixed_density() {
    if (std::getenv("TH20_HIRES") || exists(paths().data + "/hires")) return 2;
    if (std::getenv("TH20_LOWRES") || exists(paths().data + "/lowres")) return 1;
    return 0;
}
}

// Desktop test hooks for the module (wasm/compat/th20n_native.h).
extern "C" std::uint32_t w2c_th20n_test_flags(struct w2c_th20n*) {
#ifdef __SWITCH__
    return 0;
#else
    static const std::uint32_t flags = [] { const char* v = std::getenv("TH20_TEST_FLAGS"); return v ? std::uint32_t(std::strtoul(v, nullptr, 0)) : 0u; }();
    return flags;
#endif
}

namespace th20n {
// Moves the calling worker thread off the game thread's core (core 0) onto
// cores 1 and 2, preferring `core`. Nothing to do on the desktop.
void use_spare_cores(int core) {
#ifdef __SWITCH__
    if (core != 1 && core != 2) core = 2;
    const Result rc = svcSetThreadCoreMask(CUR_THREAD_HANDLE, core, (1u << 1) | (1u << 2));
    if (R_FAILED(rc)) log("worker thread: core mask not set (0x%x)", rc);
#else
    (void)core;
#endif
}
}
