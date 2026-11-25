/* Switch build: newlib has no <sys/mman.h>. The wasm2c runtime includes it
 * for its mmap-based memory, which this build does not use
 * (WASM_RT_USE_MMAP=0: the memory comes from native/Memory.cpp), so these
 * only let the unused helpers compile; they always fail. */
#ifndef TH20_SWITCH_SYS_MMAN_H
#define TH20_SWITCH_SYS_MMAN_H
#include <stddef.h>
#include <errno.h>
#define PROT_NONE 0
#define PROT_READ 1
#define PROT_WRITE 2
#define MAP_PRIVATE 2
#define MAP_ANONYMOUS 0x20
#define MAP_FAILED ((void*)-1)
static inline void* mmap(void* a, size_t n, int p, int f, int fd, long o) { (void)a; (void)n; (void)p; (void)f; (void)fd; (void)o; errno = ENOMEM; return MAP_FAILED; }
static inline int munmap(void* a, size_t n) { (void)a; (void)n; return -1; }
static inline int mprotect(void* a, size_t n, int p) { (void)a; (void)n; (void)p; return -1; }
#endif
