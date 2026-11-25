// Desktop sampling profiler for the game thread (TH20_SAMPLE=file,
// TH20_SAMPLE_FRAMES=first-last): every 0.25 ms inside those frames, the game
// thread's program counter is recorded; the file gets
// one address per sample and the process's memory map (map them with nm).
// Not built for the Switch.
#ifndef __SWITCH__
#include "Host.hpp"
#include <atomic>
#include <csignal>
#include <pthread.h>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <sys/syscall.h>
#include <ucontext.h>
#include <unistd.h>
#include <vector>

namespace th20n::sampler {
namespace {
constexpr size_t capacity = 1 << 18;
constexpr int depth = 32;               // program counter plus return addresses (builds with frame pointers)
std::uintptr_t* samples = nullptr;      // capacity * depth, zero-terminated stacks
std::uintptr_t stack_low = 0, stack_high = 0;
std::atomic<size_t> count{0};
std::uint32_t* sample_frames = nullptr;
std::atomic<std::uint32_t> current_frame{0};
std::atomic<bool> active{false};
pid_t game_thread = 0;
timer_t timer{};
std::uint32_t first = 1, last = 0;
FILE* out = nullptr;

void handler(int, siginfo_t*, void* context) {
    if (!active.load(std::memory_order_relaxed)) return;
    const size_t at = count.fetch_add(1, std::memory_order_relaxed);
    if (at >= capacity) return;
    const auto* uc = static_cast<const ucontext_t*>(context);
    std::uintptr_t* stack = samples + at * depth;
    for (int i = 0; i < depth; ++i) stack[i] = 0;
    sample_frames[at] = current_frame.load(std::memory_order_relaxed);
#if defined(__x86_64__)
    stack[0] = std::uintptr_t(uc->uc_mcontext.gregs[REG_RIP]);
    std::uintptr_t fp = std::uintptr_t(uc->uc_mcontext.gregs[REG_RBP]);
    for (int i = 1; i < depth; ++i) {   // frame pointer chain, only inside the game thread's stack
        if (fp < stack_low || fp + 16 > stack_high || (fp & 7)) break;
        const auto* frame = reinterpret_cast<const std::uintptr_t*>(fp);
        stack[i] = frame[1];
        if (frame[0] <= fp) break;
        fp = frame[0];
    }
#elif defined(__aarch64__)
    stack[0] = std::uintptr_t(uc->uc_mcontext.pc);
#endif
}
}
// On the game thread, before the first frame.
void start() {
    const char* path = std::getenv("TH20_SAMPLE");
    if (!path) return;
    if (const char* range = std::getenv("TH20_SAMPLE_FRAMES")) std::sscanf(range, "%u-%u", &first, &last);
    out = std::fopen(path, "w");
    if (!out) return;
    samples = new std::uintptr_t[capacity * depth];
    {   // the game thread's stack, to walk frame pointers safely
        pthread_attr_t attr; void* base = nullptr; size_t size = 0;
        if (!pthread_getattr_np(pthread_self(), &attr)) { pthread_attr_getstack(&attr, &base, &size); pthread_attr_destroy(&attr); }
        stack_low = std::uintptr_t(base); stack_high = stack_low + size;
    }
    sample_frames = new std::uint32_t[capacity];
    game_thread = pid_t(syscall(SYS_gettid));
    struct sigaction sa{}; sa.sa_sigaction = handler; sa.sa_flags = SA_SIGINFO | SA_RESTART; sigemptyset(&sa.sa_mask);
    sigaction(SIGPROF, &sa, nullptr);
    // A wall-clock timer (thread CPU timers tick at the kernel's HZ only),
    // signalled to this thread only: samples include time it waits in calls.
    sigevent ev{}; ev.sigev_notify = SIGEV_THREAD_ID; ev.sigev_signo = SIGPROF; ev._sigev_un._tid = game_thread;
    if (timer_create(CLOCK_MONOTONIC, &ev, &timer) != 0) { std::fclose(out); out = nullptr; return; }
    itimerspec spec{}; spec.it_interval.tv_nsec = spec.it_value.tv_nsec = 250000;   // every 0.25 ms
    timer_settime(timer, 0, &spec, nullptr);
    log("sampler: frames %u-%u", first, last);
}
void frame(std::uint32_t number) {
    if (!out) return;
    current_frame.store(number, std::memory_order_relaxed);
    active.store(number >= first && number <= last, std::memory_order_relaxed);
    if (number > last) {
        const size_t n = std::min(count.load(), capacity);
        for (size_t i = 0; i < n; ++i) {
            std::fprintf(out, "%lx %u", (unsigned long)samples[i * depth], sample_frames[i]);
            for (int k = 1; k < depth && samples[i * depth + k]; ++k) std::fprintf(out, " %lx", (unsigned long)samples[i * depth + k]);
            std::fputc('\n', out);
        }
        if (FILE* maps = std::fopen("/proc/self/maps", "r")) {          // to place samples in shared libraries
            char line[1024]; while (std::fgets(line, sizeof line, maps)) std::fprintf(out, "map %s", line);
            std::fclose(maps);
        }
        std::fclose(out); out = nullptr;
        itimerspec stop{}; timer_settime(timer, 0, &stop, nullptr);
        log("sampler: %zu samples", n);
    }
}
}
#endif
