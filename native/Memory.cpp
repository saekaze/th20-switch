// The game's wasm linear memory (wasm2c runtime hooks, see scripts/build_wasm.sh).
//
// wasm2c grows the memory with realloc, which copies the whole block: on the
// Switch that is 200+ MB, a freeze of a good fraction of a second each time
// the game asks for more (stage loads), and twice the memory while it copies.
// So one block is reserved when the module starts and the memory grows inside
// it. Only a game that outgrows the reservation falls back to realloc.
#include "Host.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#ifdef __SWITCH__
#include <malloc.h>
extern "C" char* fake_heap_start;
extern "C" char* fake_heap_end;
#endif

namespace {
std::uint8_t* block = nullptr;
std::uint64_t capacity = 0, logged = 0;
constexpr std::uint64_t MB = 1ull << 20, granule = 64 * 1024;

// How much to reserve: half of what the heap still has (the GPU's textures
// and buffers come from the same heap), at most 768 MB.
std::uint64_t reservation(std::uint64_t needed) {
    std::uint64_t free_heap = 1024 * MB;
#ifdef __SWITCH__
    const struct mallinfo heap = mallinfo();
    free_heap = std::uint64_t(fake_heap_end - fake_heap_start) - heap.arena + heap.fordblks;
#endif
    std::uint64_t want = std::min<std::uint64_t>(768 * MB, free_heap / 2);
    return std::max(want, needed) / granule * granule + (std::max(want, needed) % granule ? granule : 0);
}
}

extern "C" void* th20n_memory_reserve(std::uint64_t bytes) {
    for (std::uint64_t want = reservation(bytes); want >= bytes && !block; want = want / 4 * 3 / granule * granule) {
        block = static_cast<std::uint8_t*>(std::malloc(want ? want : 1));
        if (block) capacity = want;
        if (want <= granule) break;
    }
    if (!block) { th20n::log("game memory: cannot reserve %llu MB", (unsigned long long)(bytes / MB)); return nullptr; }
    std::memset(block, 0, bytes);
    th20n::log("game memory: %llu MB reserved", (unsigned long long)(capacity / MB));
    return block;
}

extern "C" void* th20n_memory_resize(void* data, std::uint64_t old_size, std::uint64_t new_size) {
    (void)old_size;
    if (data == block && new_size <= capacity) {
        if (new_size >= logged + 32 * MB) { logged = new_size / (32 * MB) * (32 * MB); th20n::log("game memory: %llu MB", (unsigned long long)(new_size / MB)); }
        return data;   // the caller zeroes the new pages
    }
    const double begin = th20n::now_ms();
    void* moved = std::realloc(data, new_size);
    if (moved && data == block) { block = static_cast<std::uint8_t*>(moved); capacity = new_size; }
    th20n::log("game memory: grew past the reservation to %llu MB (%s, %.0f ms)", (unsigned long long)(new_size / MB),
               moved ? "moved" : "failed", th20n::now_ms() - begin);
    return moved;
}

extern "C" void th20n_memory_release(void* data) {
    if (data == block) { block = nullptr; capacity = 0; }
    std::free(data);
}
