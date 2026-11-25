// Native (wasm2c) stand-in for <emscripten.h> in the TH20 wasm build.
// Browser-side snippets are dropped; the few runtime services the sources use
// come from the native host as imports (th20n_now_ms).
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
__attribute__((import_module("th20n"),import_name("now_ms"))) double th20n_now_ms(void);
static inline double emscripten_get_now(void){return th20n_now_ms();}
static inline void emscripten_sleep(unsigned ms){(void)ms;}
#ifdef __cplusplus
}
#endif
#define EM_ASM(...) ((void)0)
#define EM_ASM_INT(...) 0
#define EM_ASM_DOUBLE(...) 0.0
// EM_JS hooks become ordinary external functions; src/BrowserHooks.cpp
// implements each one for the native host.
#ifdef __cplusplus
#define EM_JS(ret, name, params, ...) extern "C" ret name params
#else
#define EM_JS(ret, name, params, ...) ret name params
#endif
#define EMSCRIPTEN_KEEPALIVE __attribute__((used))
#define EM_BOOL int
#define EM_TRUE 1
#define EM_FALSE 0
