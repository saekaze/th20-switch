#!/bin/bash
# Fetch the sources the TH20 Switch build is made from, at pinned versions:
#   upstream/              eagler-touhou (ports/th20 runtime and shared headers)
#                          at UPSTREAM_COMMIT, plus patches/native.patch
#   third_party/llvm/      LLVM 18.1.3 libc++abi + libunwind sources (the wasm
#                          C++ exception runtime), plus patches/llvm-wasm-eh.patch
#   third_party/zlib/      zlib 1.3.1 (snapshot PNG writer, built to wasm)
#   third_party/sdl3/      SDL 3.4 headers (API of the wasm imports)
#   third_party/libdeflate/ libdeflate 1.22 (the native PNG fast path)
# None of the repositories' game content is downloaded.
#
# Usage: scripts/fetch_upstream.sh
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

sparse() { # dest url ref paths...
    local dest="$1" url="$2" ref="$3"; shift 3
    if [ ! -d "${dest}/.git" ]; then git init -q "${dest}"; git -C "${dest}" remote add origin "${url}"; fi
    git -C "${dest}" config core.sparseCheckout true
    printf '%s\n' "$@" > "${dest}/.git/info/sparse-checkout"
    git -C "${dest}" fetch -q --depth 1 --filter=blob:none origin "${ref}"
    git -C "${dest}" checkout -q --force FETCH_HEAD
}

sparse "${ROOT}/upstream" https://github.com/Ritosa2333/eagler-touhou.git "$(tr -d ' \n' < "${ROOT}/UPSTREAM_COMMIT")" \
    /ports/th20/src/ /ports/th20/include/ /ports/th20/web/cpp/ \
    /ports/th20/portable/sdl/ /ports/th20/portable/numeric/ /ports/th20/portable/input/ \
    /common/include/eagler/audio/ /common/include/eagler/input/ /common/third_party/imgui/
[ ! -s "${ROOT}/patches/native.patch" ] || git -C "${ROOT}/upstream" apply --whitespace=nowarn "${ROOT}/patches/native.patch"

sparse "${ROOT}/third_party/llvm" https://github.com/llvm/llvm-project.git llvmorg-18.1.3 \
    /libcxxabi/src/ /libcxxabi/include/ /libunwind/src/ /libunwind/include/ /libcxx/src/include/ /libcxxabi/LICENSE.TXT
git -C "${ROOT}/third_party/llvm" apply "${ROOT}/patches/llvm-wasm-eh.patch"

# SDL3 headers only (the wasm module is compiled against the SDL3 API; the
# native host implements it on SDL2). Same revision the upstream build used.
sparse "${ROOT}/third_party/sdl3" https://github.com/libsdl-org/SDL.git f87239e71e42da91ca317a12eefb82cfbf3393eb /include/ /LICENSE.txt

sparse "${ROOT}/third_party/zlib" https://github.com/madler/zlib.git v1.3.1 '/*.c' '/*.h' /LICENSE

# libdeflate (MIT): the host's PNG fast path (native/PngFast.cpp).
sparse "${ROOT}/third_party/libdeflate" https://github.com/ebiggers/libdeflate.git v1.22 /lib/ /libdeflate.h /common_defs.h /COPYING
echo "sources ready"
