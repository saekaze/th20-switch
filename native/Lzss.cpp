// Host side of the module's LZSS encoder (see Lzss.hpp).
#include "Host.hpp"
#include "Lzss.hpp"
#include "GlImports.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace th20n;

namespace {
std::vector<std::uint8_t> encoded;
// The game encodes the same save data several times in a row (the score file
// and its backup, and again when nothing changed). Output and final
// dictionary depend on the input alone (the encoder starts from a zeroed
// dictionary), so a repeat of the last input reuses them.
struct Last { bool valid = false; std::vector<std::uint8_t> input, output; std::uint8_t dictionary[8192]; } last;
}

extern "C" {
// Encodes `size` bytes at `input` with the module's 8 KB dictionary (left as
// the encoder leaves it); returns the encoded size for lzss_take.
std::uint32_t w2c_th20n_lzss_encode(struct w2c_th20n*, std::uint32_t input, std::uint32_t size, std::uint32_t dictionary) {
    perf::HostScope timing(perf::totals.lzss_ticks); perf::totals.lzss_bytes += size;
    encoded.clear();
    if (!dictionary || std::uint64_t(dictionary) + 8192 > memory_size() || (size && (!input || std::uint64_t(input) + size > memory_size()))) return 0;
#ifndef __SWITCH__
    if (const char* dump = std::getenv("TH20_DUMP_LZSS")) {   // encoder benchmarks (desktop)
        static unsigned dumped = 0; char name[512]; std::snprintf(name, sizeof name, "%s/%03u.bin", dump, dumped++);
        if (FILE* f = std::fopen(name, "wb")) { if (size) std::fwrite(ptr(input), 1, size, f); std::fclose(f); }
    }
#endif
    const auto* bytes = size ? static_cast<const std::uint8_t*>(ptr(input)) : nullptr;
    auto* dict = static_cast<std::uint8_t*>(ptr(dictionary));
    if (last.valid && last.input.size() == size && (!size || !std::memcmp(last.input.data(), bytes, size))) {
        std::memcpy(dict, last.dictionary, 8192);
        encoded = last.output;
        return std::uint32_t(encoded.size());
    }
    lzss::Encoder encoder(dict);
    encoded = encoder.encode(bytes, size);
    last.input.assign(bytes, bytes + size); last.output = encoded; std::memcpy(last.dictionary, dict, 8192); last.valid = true;
    return std::uint32_t(encoded.size());
}
void w2c_th20n_lzss_take(struct w2c_th20n*, std::uint32_t to) {
    if (to && !encoded.empty() && std::uint64_t(to) + encoded.size() <= memory_size()) std::memcpy(ptr(to), encoded.data(), encoded.size());
    encoded.clear(); encoded.shrink_to_fit();
}
}
