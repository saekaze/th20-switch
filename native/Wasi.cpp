// WASI (wasi_snapshot_preview1) for the TH20 module: the subset wasi-libc
// calls for TH20's file streams, directory listing and clock.
//
// One preopened directory "/" (fd 3). The game's absolute paths come in
// relative to it and are mapped onto the data folder:
//   game/...            -> data folder (th20.dat, thbgm.dat)
//   savesth20/jp/...    -> save folder (th20.cfg, score, replay/, snapshot/)
//   savesth20/chs/...   -> save folder/chs
//   fonts/...           -> data folder (msgothic.ttc, ...)
//   anything else       -> data folder
#include "Host.hpp"
#include "GlImports.hpp"
#include <algorithm>
#include <cerrno>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
#ifdef __SWITCH__
// libnx (switch/kernel/random.h); not <switch.h>, whose Handle type would
// clash with this file's.
extern "C" void randomGet(void* buf, size_t len);
#endif

namespace {
using namespace th20n;
// WASI errno values
enum : std::uint32_t { E_SUCCESS = 0, E_ACCES = 2, E_BADF = 8, E_EXIST = 20, E_INVAL = 28, E_IO = 29, E_ISDIR = 31,
    E_NOENT = 44, E_NOSYS = 52, E_NOTDIR = 54, E_NOTEMPTY = 55 };
enum : std::uint8_t { T_UNKNOWN = 0, T_CHAR = 2, T_DIRECTORY = 3, T_REGULAR = 4 };
constexpr int ROOT_FD = 3;

std::uint32_t errno_to_wasi(int e) {
    switch (e) {
    case ENOENT: return E_NOENT; case EEXIST: return E_EXIST; case EACCES: case EPERM: return E_ACCES;
    case ENOTDIR: return E_NOTDIR; case EISDIR: return E_ISDIR; case EINVAL: return E_INVAL; case ENOTEMPTY: return E_NOTEMPTY;
    case EBADF: return E_BADF; default: return E_IO;
    }
}
std::string host_path(const std::string& relative) {
    std::string p = relative;
    while (!p.empty() && p.front() == '/') p.erase(0, 1);
    const auto starts = [&](const char* prefix) { const size_t n = std::strlen(prefix); return p.compare(0, n, prefix) == 0 && (p.size() == n || p[n] == '/'); };
    const auto rest = [&](const char* prefix) { const size_t n = std::strlen(prefix); return p.size() > n ? p.substr(n) : std::string(); };
    if (starts("savesth20/jp")) return paths().save + rest("savesth20/jp");
    if (starts("savesth20/chs")) return paths().save + "/chs" + rest("savesth20/chs");
    if (starts("savesth20")) return paths().save + rest("savesth20");
    if (starts("game")) return paths().data + rest("game");
    if (starts("fonts")) return paths().data + rest("fonts");
    if (p.empty() || p == ".") return paths().data;
    return paths().data + "/" + p;
}
struct Entry { std::string name; std::uint8_t type; };
// Read-only files are read through a buffer that a worker thread keeps filled
// ahead of the reader. The module reads th20.dat in 1 KB and 3 KB pieces
// (~2,400 reads per stage load) and the music in pieces of a few KB, and every
// read is a request to the SD card on the Switch: 1 MB at a time is fast, and
// with the worker the game thread does not wait for it at all while reading
// sequentially (before, refilling a 1 MB chunk stopped the game for 14-18 ms
// every ~6 s of music). Only a jump to another place reads synchronously.
class ReadAhead {
public:
    ReadAhead(std::string path, std::uint64_t size) : path_(std::move(path)), size_(size) {}
    ~ReadAhead() {
        { std::lock_guard<std::mutex> lock(m_); stop_ = true; }
        cv_.notify_all();
        if (worker_.joinable()) worker_.join();
        if (fd_ >= 0) ::close(fd_);
    }
    // Copies up to n bytes at pos (fewer at the end of the file; 0 past it).
    // `fd` is the caller's descriptor, used for a synchronous fill.
    std::int64_t read(int fd, std::uint64_t pos, std::uint8_t* to, std::uint32_t n) {
        if (pos >= size_ || !n) return 0;
        const std::uint64_t want = std::min<std::uint64_t>(n, size_ - pos);
        std::unique_lock<std::mutex> lock(m_);
        // Right at the end of the buffer while the worker reads it: wait.
        if (busy_ && pos >= start() && pos <= end() && pos + want > end()) done_.wait(lock, [&] { return !busy_ || stop_; });
        if (!(pos >= start() && pos + want <= end())) {
            ++generation_;   // the worker's read in flight (if any) is for the old place
            const std::uint64_t fill = std::min<std::uint64_t>(std::max<std::uint64_t>(want, piece), size_ - pos);
            data_.resize(std::size_t(fill)); head_ = 0; at_ = pos;
            if (::lseek(fd, off_t(pos), SEEK_SET) < 0) { data_.clear(); return -1; }
            std::size_t got = 0;
            while (got < fill) {
                const ssize_t r = ::read(fd, data_.data() + got, std::size_t(fill - got));
                if (r < 0) { data_.clear(); return -1; }
                if (r == 0) break;
                got += std::size_t(r);
            }
            data_.resize(got);
        }
        const std::uint64_t copied = std::min<std::uint64_t>(want, end() - pos);
        std::memcpy(to, data_.data() + head_ + (pos - at_), std::size_t(copied));
        // Drop what the reader has passed (in large steps, not per read).
        const std::uint64_t consumed = pos + copied - at_;
        if (consumed >= piece) { data_.erase(data_.begin() + std::ptrdiff_t(head_), data_.begin() + std::ptrdiff_t(head_ + consumed)); at_ += consumed; }
        reader_ = pos + copied;
        if (!busy_ && end() < size_ && end() - reader_ < ahead) {
            request_ = true;
            if (!worker_.joinable()) worker_ = std::thread([this] { run(); });
            cv_.notify_one();
        }
        return std::int64_t(copied);
    }
private:
    static constexpr std::uint64_t piece = 256 * 1024, ahead = 1024 * 1024;
    std::uint64_t start() const { return at_; }
    std::uint64_t end() const { return at_ + (data_.size() - head_); }
    void run() {
        std::vector<std::uint8_t> chunk(piece);
        std::unique_lock<std::mutex> lock(m_);
        for (;;) {
            cv_.wait(lock, [&] { return stop_ || request_; });
            if (stop_) return;
            request_ = false; busy_ = true;
            const std::uint64_t from = end(), generation = generation_;
            const std::size_t length = std::size_t(std::min<std::uint64_t>(piece, size_ - from));
            lock.unlock();
            if (fd_ < 0) fd_ = ::open(path_.c_str(), O_RDONLY);
            ssize_t n = -1;
            if (fd_ >= 0 && ::lseek(fd_, off_t(from), SEEK_SET) >= 0) n = ::read(fd_, chunk.data(), length);
            lock.lock();
            busy_ = false;
            if (n > 0 && generation == generation_ && from == end()) data_.insert(data_.end(), chunk.begin(), chunk.begin() + n);
            if (n > 0 && end() < size_ && end() - reader_ < ahead) request_ = true;
            done_.notify_all();
        }
    }
    std::string path_; std::uint64_t size_; int fd_ = -1;
    std::mutex m_; std::condition_variable cv_, done_;
    std::vector<std::uint8_t> data_; std::size_t head_ = 0;
    std::uint64_t at_ = 0, reader_ = 0, generation_ = 0;
    bool request_ = false, busy_ = false, stop_ = false;
    std::thread worker_;   // started on the first request
};

struct Handle {
    int fd = -1; bool directory = false; std::string path;
    std::vector<Entry> entries;   // directory listing, read on open
    // Read-only regular files: position and seeks are kept here, reads go
    // through `buffer`.
    bool cached = false;
    std::uint64_t pos = 0, size = 0;
    std::unique_ptr<ReadAhead> buffer;
};
std::map<std::uint32_t, std::unique_ptr<Handle>> handles;
std::uint32_t next_fd = 4;
Handle* handle(std::uint32_t fd) { auto it = handles.find(fd); return it == handles.end() ? nullptr : it->second.get(); }
std::string guest_path(std::uint32_t address, std::uint32_t length) { return std::string(str(address), length); }
std::uint8_t file_type(mode_t mode) { return S_ISDIR(mode) ? T_DIRECTORY : S_ISREG(mode) ? T_REGULAR : T_UNKNOWN; }
}

extern "C" {
std::uint32_t w2c_wasi__snapshot__preview1_fd_write(struct w2c_wasi__snapshot__preview1*, std::uint32_t fd, std::uint32_t iovs, std::uint32_t count, std::uint32_t written) {
    std::uint32_t total = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto buffer = load<std::uint32_t>(iovs + i * 8), length = load<std::uint32_t>(iovs + i * 8 + 4);
        if (!length) continue;
        if (fd == 1 || fd == 2) {
            // stdout/stderr: the module's diagnostics go to the log, line by line.
            static std::string line[3];
            for (std::uint32_t k = 0; k < length; ++k) {
                const char c = char(base()[buffer + k]);
                if (c == '\n') { log("[th20] %s", line[fd].c_str()); line[fd].clear(); } else line[fd] += c;
            }
            total += length; continue;
        }
        auto* h = handle(fd); if (!h || h->directory) return E_BADF;
        const ssize_t n = ::write(h->fd, base() + buffer, length);
        if (n < 0) return errno_to_wasi(errno);
        total += std::uint32_t(n);
        if (std::uint32_t(n) < length) break;
    }
    store<std::uint32_t>(written, total);
    return E_SUCCESS;
}
std::uint32_t w2c_wasi__snapshot__preview1_fd_read(struct w2c_wasi__snapshot__preview1*, std::uint32_t fd, std::uint32_t iovs, std::uint32_t count, std::uint32_t read) {
    auto* h = handle(fd); if (!h || h->directory) { if (fd == 0) { store<std::uint32_t>(read, 0); return E_SUCCESS; } return E_BADF; }
    std::uint32_t total = 0;
    const std::uint64_t begin = SDL_GetPerformanceCounter();
    struct Account { std::uint64_t begin; std::uint32_t& total; ~Account() {
        perf::totals.io_ticks += SDL_GetPerformanceCounter() - begin; ++perf::totals.io_calls; perf::totals.io_bytes += total; } } account{begin, total};
    if (h->cached) {
        if (!h->buffer) h->buffer = std::make_unique<ReadAhead>(host_path(h->path), h->size);
        for (std::uint32_t i = 0; i < count; ++i) {
            std::uint8_t* to = base() + load<std::uint32_t>(iovs + i * 8);
            const std::uint32_t want = load<std::uint32_t>(iovs + i * 8 + 4);
            if (!want) continue;
            const std::int64_t n = h->buffer->read(h->fd, h->pos, to, want);
            if (n < 0) return errno_to_wasi(errno);
            h->pos += std::uint64_t(n); total += std::uint32_t(n);
            if (std::uint32_t(n) < want) break;   // end of file
        }
        store<std::uint32_t>(read, total);
        return E_SUCCESS;
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto buffer = load<std::uint32_t>(iovs + i * 8), length = load<std::uint32_t>(iovs + i * 8 + 4);
        if (!length) continue;
        const ssize_t n = ::read(h->fd, base() + buffer, length);
        if (n < 0) return errno_to_wasi(errno);
        total += std::uint32_t(n);
        if (std::uint32_t(n) < length) break;
    }
    store<std::uint32_t>(read, total);
    return E_SUCCESS;
}
std::uint32_t w2c_wasi__snapshot__preview1_fd_seek(struct w2c_wasi__snapshot__preview1*, std::uint32_t fd, std::uint64_t offset, std::uint32_t whence, std::uint32_t result) {
    auto* h = handle(fd); if (!h || h->directory) return E_BADF;
    if (h->cached) {
        const std::int64_t from = whence == 0 ? 0 : whence == 1 ? std::int64_t(h->pos) : std::int64_t(h->size);
        const std::int64_t to = from + std::int64_t(offset);
        if (to < 0) return E_INVAL;
        h->pos = std::uint64_t(to);
        store<std::uint64_t>(result, h->pos);
        return E_SUCCESS;
    }
    const int w = whence == 0 ? SEEK_SET : whence == 1 ? SEEK_CUR : SEEK_END;
    const std::uint64_t begin = SDL_GetPerformanceCounter();
    const off_t at = ::lseek(h->fd, off_t(std::int64_t(offset)), w);
    perf::totals.io_ticks += SDL_GetPerformanceCounter() - begin; ++perf::totals.io_calls;
    if (at < 0) return errno_to_wasi(errno);
    store<std::uint64_t>(result, std::uint64_t(at));
    return E_SUCCESS;
}
std::uint32_t w2c_wasi__snapshot__preview1_fd_close(struct w2c_wasi__snapshot__preview1*, std::uint32_t fd) {
    auto it = handles.find(fd); if (it == handles.end()) return fd <= 3 ? E_SUCCESS : E_BADF;
    if (it->second->fd >= 0) ::close(it->second->fd);
    handles.erase(it);
    return E_SUCCESS;
}
std::uint32_t w2c_wasi__snapshot__preview1_fd_fdstat_get(struct w2c_wasi__snapshot__preview1*, std::uint32_t fd, std::uint32_t out) {
    std::uint8_t type = T_UNKNOWN;
    if (fd <= 2) type = T_CHAR;
    else if (fd == ROOT_FD) type = T_DIRECTORY;
    else if (auto* h = handle(fd)) type = h->directory ? T_DIRECTORY : T_REGULAR;
    else return E_BADF;
    std::memset(base() + out, 0, 24);
    store<std::uint8_t>(out, type);
    store<std::uint64_t>(out + 8, ~0ull);    // all rights
    store<std::uint64_t>(out + 16, ~0ull);
    return E_SUCCESS;
}
std::uint32_t w2c_wasi__snapshot__preview1_fd_fdstat_set_flags(struct w2c_wasi__snapshot__preview1*, std::uint32_t, std::uint32_t) { return E_SUCCESS; }
std::uint32_t w2c_wasi__snapshot__preview1_fd_prestat_get(struct w2c_wasi__snapshot__preview1*, std::uint32_t fd, std::uint32_t out) {
    if (fd != ROOT_FD) return E_BADF;
    store<std::uint8_t>(out, 0);             // directory
    store<std::uint32_t>(out + 4, 1);        // "/"
    return E_SUCCESS;
}
std::uint32_t w2c_wasi__snapshot__preview1_fd_prestat_dir_name(struct w2c_wasi__snapshot__preview1*, std::uint32_t fd, std::uint32_t path, std::uint32_t length) {
    if (fd != ROOT_FD) return E_BADF;
    if (length >= 1) base()[path] = '/';
    return E_SUCCESS;
}
std::uint32_t w2c_wasi__snapshot__preview1_path_open(struct w2c_wasi__snapshot__preview1*, std::uint32_t dirfd, std::uint32_t dirflags,
        std::uint32_t path, std::uint32_t path_length, std::uint32_t oflags, std::uint64_t rights, std::uint64_t, std::uint32_t fdflags, std::uint32_t result) {
    (void)dirflags;
    std::string relative = guest_path(path, path_length);
    if (dirfd != std::uint32_t(ROOT_FD)) { auto* h = handle(dirfd); if (!h || !h->directory) return E_BADF; relative = h->path + "/" + relative; }
    const std::string host = host_path(relative);
    auto h = std::make_unique<Handle>(); h->path = relative;
    struct stat st {};
    const bool present = ::stat(host.c_str(), &st) == 0;
    if ((oflags & 2) || (present && S_ISDIR(st.st_mode) && !(oflags & 1))) {   // O_DIRECTORY, or opening a directory
        if (!present) return E_NOENT;
        if (!S_ISDIR(st.st_mode)) return E_NOTDIR;
        DIR* d = ::opendir(host.c_str()); if (!d) return errno_to_wasi(errno);
        h->directory = true;
        h->entries.push_back({".", T_DIRECTORY}); h->entries.push_back({"..", T_DIRECTORY});
        while (auto* e = ::readdir(d)) {
            const std::string name = e->d_name; if (name == "." || name == "..") continue;
            struct stat es {}; const bool known = ::stat((host + "/" + name).c_str(), &es) == 0;
            h->entries.push_back({name, known ? file_type(es.st_mode) : std::uint8_t(T_UNKNOWN)});
        }
        ::closedir(d);
    } else {
        const bool read = rights & (1ull << 1), write = rights & (1ull << 6);
        int flags = write ? (read ? O_RDWR : O_WRONLY) : O_RDONLY;
        if (oflags & 1) flags |= O_CREAT;
        if (oflags & 4) flags |= O_EXCL;
        if (oflags & 8) flags |= O_TRUNC;
        if (fdflags & 1) flags |= O_APPEND;
        h->fd = ::open(host.c_str(), flags, 0666);
        if (h->fd < 0) return errno_to_wasi(errno);
        if (!write && present && S_ISREG(st.st_mode)) { h->cached = true; h->size = std::uint64_t(st.st_size); }
    }
    const std::uint32_t fd = next_fd++;
    handles[fd] = std::move(h);
    store<std::uint32_t>(result, fd);
    return E_SUCCESS;
}
std::uint32_t w2c_wasi__snapshot__preview1_fd_readdir(struct w2c_wasi__snapshot__preview1*, std::uint32_t fd, std::uint32_t buffer, std::uint32_t length,
        std::uint64_t cookie, std::uint32_t used) {
    auto* h = handle(fd); if (!h || !h->directory) return E_BADF;
    std::uint32_t at = 0;
    for (std::uint64_t i = cookie; i < h->entries.size() && at < length; ++i) {
        const auto& e = h->entries[i];
        std::uint8_t record[24]{};
        const std::uint64_t next = i + 1, inode = i + 1; const std::uint32_t name_length = std::uint32_t(e.name.size());
        std::memcpy(record, &next, 8); std::memcpy(record + 8, &inode, 8); std::memcpy(record + 16, &name_length, 4); record[20] = e.type;
        // Records may be cut off at the end; wasi-libc then retries with a larger buffer.
        const std::uint32_t header = std::min<std::uint32_t>(24, length - at);
        std::memcpy(base() + buffer + at, record, header); at += header;
        const std::uint32_t name = std::min<std::uint32_t>(name_length, length - at);
        std::memcpy(base() + buffer + at, e.name.data(), name); at += name;
    }
    store<std::uint32_t>(used, at);
    return E_SUCCESS;
}
std::uint32_t w2c_wasi__snapshot__preview1_path_create_directory(struct w2c_wasi__snapshot__preview1*, std::uint32_t dirfd, std::uint32_t path, std::uint32_t length) {
    std::string relative = guest_path(path, length);
    if (dirfd != std::uint32_t(ROOT_FD)) { auto* h = handle(dirfd); if (!h || !h->directory) return E_BADF; relative = h->path + "/" + relative; }
    if (::mkdir(host_path(relative).c_str(), 0777) != 0) return errno_to_wasi(errno);
    return E_SUCCESS;
}
std::uint32_t w2c_wasi__snapshot__preview1_path_filestat_get(struct w2c_wasi__snapshot__preview1*, std::uint32_t dirfd, std::uint32_t, std::uint32_t path,
        std::uint32_t length, std::uint32_t out) {
    std::string relative = guest_path(path, length);
    if (dirfd != std::uint32_t(ROOT_FD)) { auto* h = handle(dirfd); if (!h || !h->directory) return E_BADF; relative = h->path + "/" + relative; }
    struct stat st {};
    if (::stat(host_path(relative).c_str(), &st) != 0) return errno_to_wasi(errno);
    std::memset(base() + out, 0, 64);
    store<std::uint64_t>(out + 8, std::uint64_t(st.st_ino));
    store<std::uint8_t>(out + 16, file_type(st.st_mode));
    store<std::uint64_t>(out + 24, 1);
    store<std::uint64_t>(out + 32, std::uint64_t(st.st_size));
    const std::uint64_t mtime = std::uint64_t(st.st_mtime) * 1000000000ull;
    store<std::uint64_t>(out + 40, mtime); store<std::uint64_t>(out + 48, mtime); store<std::uint64_t>(out + 56, mtime);
    return E_SUCCESS;
}
std::uint32_t w2c_wasi__snapshot__preview1_clock_time_get(struct w2c_wasi__snapshot__preview1*, std::uint32_t id, std::uint64_t, std::uint32_t out) {
    std::uint64_t ns;
#ifndef __SWITCH__
    static const bool fixed = std::getenv("TH20_FIXED_CLOCK") != nullptr;
#else
    constexpr bool fixed = false;
#endif
    if (id == 0 && !fixed) { struct timespec ts {}; clock_gettime(CLOCK_REALTIME, &ts); ns = std::uint64_t(ts.tv_sec) * 1000000000ull + std::uint64_t(ts.tv_nsec); }
    else ns = std::uint64_t(module_now_ms() * 1000000.0);
    store<std::uint64_t>(out, ns);
    return E_SUCCESS;
}
std::uint32_t w2c_wasi__snapshot__preview1_environ_sizes_get(struct w2c_wasi__snapshot__preview1*, std::uint32_t count, std::uint32_t size) {
    store<std::uint32_t>(count, 0); store<std::uint32_t>(size, 0); return E_SUCCESS;
}
std::uint32_t w2c_wasi__snapshot__preview1_environ_get(struct w2c_wasi__snapshot__preview1*, std::uint32_t, std::uint32_t) { return E_SUCCESS; }
std::uint32_t w2c_wasi__snapshot__preview1_random_get(struct w2c_wasi__snapshot__preview1*, std::uint32_t buffer, std::uint32_t length) {
#ifdef __SWITCH__
    // libnx's kernel entropy (std::random_device has no source on newlib).
    if (length) randomGet(base() + buffer, length);
#else
    static std::mt19937 rng(std::random_device{}());
    for (std::uint32_t i = 0; i < length; ++i) base()[buffer + i] = std::uint8_t(rng());
#endif
    return E_SUCCESS;
}
[[noreturn]] void th20n_module_exit(int code);
void w2c_wasi__snapshot__preview1_proc_exit(struct w2c_wasi__snapshot__preview1*, std::uint32_t code) { th20n_module_exit(int(code)); }
}
