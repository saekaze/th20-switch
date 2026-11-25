// Image decoding and pixel conversion for the TH20 module (wasm/compat/th20n_native.h).
//
// Stage loads decode ~100 PNG / JPEG textures and convert ~80 MB of pixels.
// In the module that is stb_image's bit-by-bit inflate plus two per-pixel
// swizzle loops; here 8-bit RGBA PNGs (all of TH20's) go through the
// libdeflate fast path (PngFast.cpp), other PNGs through libpng, other
// formats through the same stb_image compiled natively, and the swizzles are
// plain loops.
// The pixels are the same as stbi_load(..., 4): palette and tRNS expanded,
// 16-bit samples reduced to their high byte, gray expanded, no gamma.
// TH20_CHECK_IMAGES=1 (desktop) decodes every PNG both ways and logs any
// difference.
#include "Host.hpp"
#include "GlImports.hpp"
#include "PngFast.hpp"
#include <png.h>
#include <condition_variable>
#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_TGA
#define STBI_ONLY_BMP
#define STBI_ONLY_JPEG
#include "stb_image.h"

using namespace th20n;

namespace {
std::vector<std::uint8_t> kept;   // BGRA, waiting for image_take_bgra
std::uint32_t kept_w = 0, kept_h = 0;

struct Reader { const std::uint8_t* data; std::size_t size, at; };
void read_bytes(png_structp png, png_bytep out, png_size_t n) {
    auto* r = static_cast<Reader*>(png_get_io_ptr(png));
    if (r->at + n > r->size) png_error(png, "truncated");
    std::memcpy(out, r->data + r->at, n); r->at += n;
}

bool decode_png(const std::uint8_t* bytes, std::size_t size, std::vector<std::uint8_t>& out, std::uint32_t& w, std::uint32_t& h) {
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) return false;
    png_infop info = png_create_info_struct(png);
    if (!info) { png_destroy_read_struct(&png, nullptr, nullptr); return false; }
    // Not an automatic variable: it changes after setjmp (libpng errors longjmp back).
    static std::vector<png_bytep> rows;
    if (setjmp(png_jmpbuf(png))) { png_destroy_read_struct(&png, &info, nullptr); return false; }
    Reader reader{bytes, size, 0};
    png_set_read_fn(png, &reader, read_bytes);
    png_read_info(png, info);
    const int type = png_get_color_type(png, info), depth = png_get_bit_depth(png, info);
    if (type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (type == PNG_COLOR_TYPE_GRAY && depth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (depth == 16) png_set_strip_16(png);
    if (type == PNG_COLOR_TYPE_GRAY || type == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(png);
    png_set_filler(png, 0xff, PNG_FILLER_AFTER);   // no effect when there is alpha
    png_set_bgr(png);
    png_set_interlace_handling(png);
    png_read_update_info(png, info);
    w = png_get_image_width(png, info); h = png_get_image_height(png, info);
    if (!w || !h || png_get_rowbytes(png, info) != std::size_t(w) * 4) { png_destroy_read_struct(&png, &info, nullptr); return false; }
    out.resize(std::size_t(w) * h * 4);
    rows.resize(h);
    for (std::uint32_t y = 0; y < h; ++y) rows[y] = out.data() + std::size_t(y) * w * 4;
    png_read_image(png, rows.data());
    png_read_end(png, nullptr);
    png_destroy_read_struct(&png, &info, nullptr);
    return true;
}

bool decode_stb(const std::uint8_t* bytes, std::size_t size, std::vector<std::uint8_t>& out, std::uint32_t& w, std::uint32_t& h) {
    int iw = 0, ih = 0, channels = 0;
    stbi_uc* rgba = stbi_load_from_memory(bytes, int(size), &iw, &ih, &channels, 4);
    if (!rgba || iw <= 0 || ih <= 0) { if (rgba) stbi_image_free(rgba); return false; }
    w = std::uint32_t(iw); h = std::uint32_t(ih);
    out.resize(std::size_t(w) * h * 4);
    for (std::size_t i = 0, n = out.size(); i < n; i += 4) {
        out[i] = rgba[i + 2]; out[i + 1] = rgba[i + 1]; out[i + 2] = rgba[i]; out[i + 3] = rgba[i + 3];
    }
    stbi_image_free(rgba);
    return true;
}

bool is_png(const std::uint8_t* b, std::size_t n) { return n >= 8 && !std::memcmp(b, "\x89PNG\r\n\x1a\n", 8); }

// Decoding ahead. The images of an ANM file lie one after another in the
// module's memory, a few hundred bytes apart, and the game decodes them in
// that order. After each image the next two PNGs after it are copied and
// decoded by two worker threads on the other CPU cores, so the game thread
// mostly finds its image done. A result is used only if the game asks for
// the same address and size and the bytes there still equal the copy that
// was decoded; anything else is decoded on the spot as before.
struct Job {
    std::uint32_t address = 0, size = 0;
    std::vector<std::uint8_t> bytes, pixels;   // the PNG as copied; the decoded BGRA
    std::uint32_t width = 0, height = 0;
    bool started = false, done = false, ok = false;
    void decode() { ok = decode_png_rgba8(bytes.data(), bytes.size(), pixels, width, height); }
};
class Ahead {
public:
    ~Ahead() {
        { std::lock_guard<std::mutex> lock(m_); stop_ = true; }
        cv_.notify_all();
        for (auto& w : workers_) if (w.joinable()) w.join();
    }
    // The job for this image, decoded, or null; drops the finished jobs at or
    // before it or far past it (guesses the game did not follow). A job no
    // worker has started yet is decoded right here.
    std::shared_ptr<Job> take(std::uint32_t address, std::uint32_t size) {
        std::unique_lock<std::mutex> lock(m_);
        std::shared_ptr<Job> found;
        for (auto it = jobs_.begin(); it != jobs_.end();) {
            if (!found && (*it)->address == address && (*it)->size == size) { found = *it; it = jobs_.erase(it); }
            else if ((*it)->done && ((*it)->address <= address || (*it)->address - address > (8u << 20))) it = jobs_.erase(it);
            else ++it;
        }
        if (!found) return nullptr;
        if (!found->started) {
            found->started = true;
            lock.unlock();
            found->decode();
            found->done = true;   // no worker can see it: it left jobs_ above
            return found;
        }
        done_.wait(lock, [&] { return found->done; });
        return found;
    }
    // Queues the next PNGs after [address, address + size).
    void after(std::uint32_t address, std::uint32_t size) {
        std::uint32_t from = address + size;
        for (int depth = 0; depth < 2; ++depth) {
            std::uint32_t next = 0, length = 0;
            if (!find_png(from, next, length)) return;
            queue(next, length);
            from = next + length;
        }
    }
private:
    std::mutex m_; std::condition_variable cv_, done_;
    std::vector<std::shared_ptr<Job>> jobs_;      // queued, running or done
    std::vector<std::thread> workers_;
    bool stop_ = false;

    // The next PNG signature within 64 KB after `from`, and its length up to IEND.
    static bool find_png(std::uint32_t from, std::uint32_t& at, std::uint32_t& length) {
        const std::uint64_t memory = memory_size();
        if (from >= memory) return false;
        const auto* base = static_cast<const std::uint8_t*>(th20n::base());
        const std::uint64_t end = std::min<std::uint64_t>(memory, std::uint64_t(from) + 65536);
        for (std::uint64_t i = from; i + 8 <= end; ++i) {
            if (base[i] != 0x89 || std::memcmp(base + i, "\x89PNG\r\n\x1a\n", 8)) continue;
            std::uint64_t chunk = i + 8;
            while (chunk + 12 <= memory && chunk - i < (64u << 20)) {
                const std::uint8_t* c = base + chunk;
                const std::uint64_t n = std::uint64_t(c[0]) << 24 | std::uint64_t(c[1]) << 16 | std::uint64_t(c[2]) << 8 | c[3];
                if (chunk + 12 + n > memory) return false;
                chunk += 12 + n;
                if (!std::memcmp(c + 4, "IEND", 4)) { at = std::uint32_t(i); length = std::uint32_t(chunk - i); return true; }
            }
            return false;
        }
        return false;
    }
    void queue(std::uint32_t address, std::uint32_t length) {
        std::lock_guard<std::mutex> lock(m_);
        for (const auto& j : jobs_) if (j->address == address) return;
        if (jobs_.size() >= 4) return;   // the game went elsewhere; wait until these are taken or dropped
        auto job = std::make_shared<Job>();
        job->address = address; job->size = length;
        const auto* data = static_cast<const std::uint8_t*>(ptr(address));
        job->bytes.assign(data, data + length);
        jobs_.push_back(job);
        if (workers_.empty()) for (int core : {2, 1}) workers_.emplace_back([this, core] { use_spare_cores(core); run(); });
        cv_.notify_one();
    }
    void run() {
        std::unique_lock<std::mutex> lock(m_);
        for (;;) {
            std::shared_ptr<Job> job;
            cv_.wait(lock, [&] {
                if (stop_) return true;
                for (const auto& j : jobs_) if (!j->started) { job = j; return true; }
                return false;
            });
            if (stop_) return;
            job->started = true;
            lock.unlock();
            job->decode();        // only this worker touches the job until done is set
            lock.lock();
            job->done = true;
            done_.notify_all();
        }
    }
};
Ahead ahead;
}

extern "C" {
std::uint32_t w2c_th20n_image_open(struct w2c_th20n*, std::uint32_t bytes, std::uint32_t size, std::uint32_t width, std::uint32_t height) {
    perf::HostScope timing(perf::totals.image_ticks);
    kept.clear(); kept_w = kept_h = 0;
    if (!bytes || !size || std::uint64_t(bytes) + size > memory_size()) return 0;
    const auto* data = static_cast<const std::uint8_t*>(ptr(bytes));
    // TH20's PNGs are 8-bit RGBA: the libdeflate fast path; libpng for any other kind.
    bool ok = false;
    if (is_png(data, size)) {
        const auto job = ahead.take(bytes, size);
        if (job && job->ok && job->bytes.size() == size && !std::memcmp(job->bytes.data(), data, size)) {
            kept.swap(job->pixels); kept_w = job->width; kept_h = job->height; ok = true;
        } else ok = decode_png_rgba8(data, size, kept, kept_w, kept_h) || decode_png(data, size, kept, kept_w, kept_h);
        ahead.after(bytes, size);
    } else ok = decode_stb(data, size, kept, kept_w, kept_h);
#ifndef __SWITCH__
    if (static const char* dump = std::getenv("TH20_DUMP_IMAGES"); dump) {   // decoder benchmarks (desktop)
        static unsigned dumped = 0; char name[512]; std::snprintf(name, sizeof name, "%s/%05u.img", dump, dumped++);
        if (FILE* f = std::fopen(name, "wb")) { std::fwrite(data, 1, size, f); std::fclose(f); }
    }
    static const bool check = std::getenv("TH20_CHECK_IMAGES") != nullptr;
    if (check && is_png(data, size)) {
        std::vector<std::uint8_t> reference; std::uint32_t rw = 0, rh = 0;
        const bool rok = decode_stb(data, size, reference, rw, rh);
        static unsigned checked = 0, mismatched = 0; ++checked;
        if (rok != ok || rw != kept_w || rh != kept_h || reference != kept) {
            ++mismatched;
            log("image check: PNG %u (%ux%u) differs from stb_image (%u of %u)", checked, kept_w, kept_h, mismatched, checked);
        } else if (checked % 25 == 0) log("image check: %u PNGs identical to stb_image so far", checked);
    }
#endif
    if (!ok) { kept.clear(); return 0; }
    ++perf::totals.images; perf::totals.image_pixels += std::uint64_t(kept_w) * kept_h;
    store<std::uint32_t>(width, kept_w);
    store<std::uint32_t>(height, kept_h);
    return 1;
}

void w2c_th20n_image_take_bgra(struct w2c_th20n*, std::uint32_t to) {
    perf::HostScope timing(perf::totals.image_ticks);
    if (to && !kept.empty() && std::uint64_t(to) + kept.size() <= memory_size()) std::memcpy(ptr(to), kept.data(), kept.size());
    kept.clear(); kept.shrink_to_fit(); kept_w = kept_h = 0;
}

void w2c_th20n_bgra_to_rgba(struct w2c_th20n*, std::uint32_t from, std::uint32_t pitch, std::uint32_t width, std::uint32_t height,
                            std::uint32_t opaque, std::uint32_t to) {
    if (!from || !to || !width || !height) return;
    perf::HostScope timing(perf::totals.image_ticks);
    if (std::uint64_t(from) + std::uint64_t(pitch) * (height - 1) + std::uint64_t(width) * 4 > memory_size() ||
        std::uint64_t(to) + std::uint64_t(width) * height * 4 > memory_size()) return;
    const std::uint8_t* src = static_cast<const std::uint8_t*>(ptr(from));
    std::uint8_t* dst = static_cast<std::uint8_t*>(ptr(to));
    const std::uint32_t alpha = opaque ? 0xff000000u : 0u;
    for (std::uint32_t y = 0; y < height; ++y) {
        const std::uint8_t* row = src + std::size_t(y) * pitch;
        std::uint8_t* out = dst + std::size_t(y) * width * 4;
        for (std::uint32_t x = 0; x < width; ++x) {
            std::uint32_t v; std::memcpy(&v, row + x * 4, 4);           // little endian: 0xAARRGGBB in BGRA
            v = (v & 0xff00ff00u) | ((v >> 16) & 0xffu) | ((v & 0xffu) << 16) | alpha;
            std::memcpy(out + x * 4, &v, 4);
        }
    }
}
}
