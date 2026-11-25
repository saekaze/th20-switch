#!/bin/bash
# Build th20.wasm and convert it to C with wasm2c (into generated/).
#
# Needs: clang/lld 18 with the wasm32 target, wasi-libc, libc++-18-dev-wasm32,
# libclang-rt-18-dev-wasm32, wabt (wasm2c), cmake, ninja (Ubuntu 24.04).
#
# Usage: scripts/build_wasm.sh [build-dir]
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${1:-${ROOT}/build-wasm}"
[ -f "${ROOT}/upstream/ports/th20/web/cpp/sdl/GameHost.cpp" ] || "${ROOT}/scripts/fetch_upstream.sh"
[ -d "${BUILD}/sysroot" ] || "${ROOT}/scripts/make_wasi_sysroot.sh" "${BUILD}/sysroot"
cmake -S "${ROOT}/wasm" -B "${BUILD}/th20" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="${ROOT}/wasm/toolchain-wasi.cmake" -DWASI_SYSROOT="${BUILD}/sysroot" \
    -DCMAKE_BUILD_TYPE=Release > "${BUILD}/cmake.log"
ninja -C "${BUILD}/th20"
# wasm2c: the module as C, split so the native compiler can work in parallel.
OUT="${ROOT}/generated"
rm -rf "${OUT}" && mkdir -p "${OUT}"
wasm2c --enable-exceptions --num-outputs=16 -n th20 "${BUILD}/th20/th20.wasm" -o "${OUT}/th20.c"
# The wasm2c runtime goes with it (the Switch build has no wabt installed).
WASM2C_RT="$(dirname "$(dpkg -L wabt 2>/dev/null | grep -m1 'wasm2c/wasm-rt-impl.c$' || echo /usr/share/wabt/wasm2c/x)")"
cp /usr/include/wasm-rt.h /usr/include/wasm-rt-exceptions.h "${WASM2C_RT}/wasm-rt-impl.h" \
   "${WASM2C_RT}/wasm-rt-impl.c" "${WASM2C_RT}/wasm-rt-exceptions-impl.c" "${OUT}/"
# WASM_RT_MEMCHECK_UNCHECKED (the Switch build): no per-access bounds checks
# on a malloc'ed memory. wasm-rt.h only allows that with mmap guard pages,
# which the Switch does not have.
python3 - "${OUT}/wasm-rt.h" <<'PY'
import sys
path = sys.argv[1]
s = open(path).read()
old = '#if WASM_RT_MEMCHECK_GUARD_PAGES && !WASM_RT_GUARD_PAGES_SUPPORTED\n'
assert s.count(old) == 1, old
s = s.replace(old, '#if WASM_RT_MEMCHECK_GUARD_PAGES && !WASM_RT_GUARD_PAGES_SUPPORTED && !WASM_RT_MEMCHECK_UNCHECKED\n')
open(path, 'w').write(s)
PY
# The linear memory comes from the host (native/Memory.cpp): one reserved
# block the memory grows inside, instead of a realloc that copies all of it.
python3 - "${OUT}/wasm-rt-impl.c" <<'PY'
import sys
path = sys.argv[1]
s = open(path).read()
for old, new in (
    ('#include <string.h>\n', '#include <string.h>\n'
        'void* th20n_memory_reserve(uint64_t bytes);\n'
        'void* th20n_memory_resize(void* data, uint64_t old_size, uint64_t new_size);\n'
        'void th20n_memory_release(void* data);\n'),
    ('memory->data = calloc(byte_length, 1);', 'memory->data = th20n_memory_reserve(byte_length);'),
    ('uint8_t* new_data = realloc(memory->data, new_size);', 'uint8_t* new_data = th20n_memory_resize(memory->data, old_size, new_size);'),
    ('  free(memory->data);\n#endif', '  th20n_memory_release(memory->data);\n#endif'),
):
    assert s.count(old) == 1, old
    s = s.replace(old, new)
open(path, 'w').write(s)
PY
ls -la "${BUILD}/th20/th20.wasm"
echo "generated C in ${OUT}"
