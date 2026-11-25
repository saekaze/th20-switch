// SDL3's file-stream API for the wasm2c build, implemented inside the module
// on the C library (WASI files on the native side). Keeping streams here means
// custom SDL_OpenIO interfaces (TH20's music stream) call back into module
// code directly instead of across the host boundary. Same semantics as SDL3
// for what TH20 uses: binary streams, SDL_IO_SEEK_*, status on EOF/error.
#include <SDL3/SDL.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define TH20N_IMPORT(name) __attribute__((import_module("th20n"), import_name(name)))
extern "C" TH20N_IMPORT("now_ms") double th20n_now_ms();

struct SDL_IOStream { SDL_IOStreamInterface iface; void* user; SDL_IOStatus status; };

namespace {
const char* last_error = "";
Sint64 file_size(void* u) { auto* f = static_cast<FILE*>(u); const long at = std::ftell(f); if (std::fseek(f, 0, SEEK_END)) return -1; const long n = std::ftell(f); std::fseek(f, at, SEEK_SET); return n; }
Sint64 file_seek(void* u, Sint64 offset, SDL_IOWhence whence) { auto* f = static_cast<FILE*>(u); const int w = whence == SDL_IO_SEEK_SET ? SEEK_SET : whence == SDL_IO_SEEK_CUR ? SEEK_CUR : SEEK_END; if (std::fseek(f, long(offset), w)) return -1; return std::ftell(f); }
size_t file_read(void* u, void* p, size_t n, SDL_IOStatus* status) { auto* f = static_cast<FILE*>(u); const size_t r = std::fread(p, 1, n, f); if (r < n) *status = std::ferror(f) ? SDL_IO_STATUS_ERROR : SDL_IO_STATUS_EOF; return r; }
size_t file_write(void* u, const void* p, size_t n, SDL_IOStatus* status) { auto* f = static_cast<FILE*>(u); const size_t r = std::fwrite(p, 1, n, f); if (r < n) *status = SDL_IO_STATUS_ERROR; return r; }
bool file_flush(void* u, SDL_IOStatus*) { return std::fflush(static_cast<FILE*>(u)) == 0; }
bool file_close(void* u) { return std::fclose(static_cast<FILE*>(u)) == 0; }
}

extern "C" {
SDL_IOStream* SDL_OpenIO(const SDL_IOStreamInterface* iface, void* userdata) {
    if (!iface || iface->version < sizeof(Uint32)) { last_error = "Invalid SDL_IOStreamInterface"; return nullptr; }
    auto* s = static_cast<SDL_IOStream*>(std::calloc(1, sizeof(SDL_IOStream)));
    if (!s) return nullptr;
    std::memcpy(&s->iface, iface, iface->version < sizeof(SDL_IOStreamInterface) ? iface->version : sizeof(SDL_IOStreamInterface));
    s->user = userdata;
    return s;
}
SDL_IOStream* SDL_IOFromFile(const char* file, const char* mode) {
    if (!file || !mode) { last_error = "Invalid file or mode"; return nullptr; }
    FILE* f = std::fopen(file, mode);
    if (!f) { last_error = "Couldn't open file"; return nullptr; }
    SDL_IOStreamInterface iface; SDL_INIT_INTERFACE(&iface);
    iface.size = file_size; iface.seek = file_seek; iface.read = file_read; iface.write = file_write; iface.flush = file_flush; iface.close = file_close;
    SDL_IOStream* s = SDL_OpenIO(&iface, f);
    if (!s) std::fclose(f);
    return s;
}
Sint64 SDL_GetIOSize(SDL_IOStream* s) { if (!s) return -1; if (!s->iface.size) { last_error = "Stream has no size"; return -1; } return s->iface.size(s->user); }
Sint64 SDL_SeekIO(SDL_IOStream* s, Sint64 offset, SDL_IOWhence whence) { if (!s || !s->iface.seek) return -1; return s->iface.seek(s->user, offset, whence); }
Sint64 SDL_TellIO(SDL_IOStream* s) { return SDL_SeekIO(s, 0, SDL_IO_SEEK_CUR); }
size_t SDL_ReadIO(SDL_IOStream* s, void* p, size_t n) {
    if (!s || !s->iface.read) return 0;
    s->status = SDL_IO_STATUS_READY;
    if (!n) return 0;
    return s->iface.read(s->user, p, n, &s->status);
}
size_t SDL_WriteIO(SDL_IOStream* s, const void* p, size_t n) {
    if (!s || !s->iface.write) return 0;
    s->status = SDL_IO_STATUS_READY;
    if (!n) return 0;
    return s->iface.write(s->user, p, n, &s->status);
}
SDL_IOStatus SDL_GetIOStatus(SDL_IOStream* s) { return s ? s->status : SDL_IO_STATUS_ERROR; }
bool SDL_FlushIO(SDL_IOStream* s) { return s && (!s->iface.flush || s->iface.flush(s->user, &s->status)); }
bool SDL_CloseIO(SDL_IOStream* s) {
    if (!s) return false;
    bool ok = true;
    if (s->iface.flush) ok = s->iface.flush(s->user, &s->status) && ok;
    if (s->iface.close) ok = s->iface.close(s->user) && ok;
    std::free(s);
    return ok;
}
void* SDL_LoadFile_IO(SDL_IOStream* s, size_t* datasize, bool closeio) {
    if (datasize) *datasize = 0;
    if (!s) return nullptr;
    size_t size = 0, cap = 0; char* data = nullptr;
    const Sint64 known = SDL_GetIOSize(s);
    cap = known > 0 ? size_t(known) : 4096;
    data = static_cast<char*>(std::malloc(cap + 1));
    while (data) {
        if (size == cap) { cap *= 2; char* grown = static_cast<char*>(std::realloc(data, cap + 1)); if (!grown) { std::free(data); data = nullptr; break; } data = grown; }
        const size_t got = SDL_ReadIO(s, data + size, cap - size);
        size += got;
        if (!got) break;
    }
    if (data) { data[size] = 0; if (datasize) *datasize = size; }
    if (closeio) SDL_CloseIO(s);
    return data;
}
void* SDL_LoadFile(const char* file, size_t* datasize) { return SDL_LoadFile_IO(SDL_IOFromFile(file, "rb"), datasize, true); }
void SDL_free(void* p) { std::free(p); }
const char* SDL_GetError(void) { return last_error; }
bool SDL_SetHint(const char*, const char*) { return true; }
int SDL_GetVersion(void) { return SDL_VERSION; }
Uint64 SDL_GetTicks(void) { return Uint64(th20n_now_ms()); }
Uint64 SDL_GetTicksNS(void) { return Uint64(th20n_now_ms() * 1000000.0); }
// No temporary files are needed by the native build (libc's tmpfile has no
// WASI backing).
FILE* tmpfile(void) { return nullptr; }
}
