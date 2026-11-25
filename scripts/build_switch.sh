#!/bin/bash
# Build touhou20.nro for Nintendo Switch from generated/ (scripts/build_wasm.sh
# makes it on a Linux machine with clang 18 and wabt; the devkitPro container
# only compiles the generated C and the native host).
#
# switch.specs (libnx) expands %:getenv(DEVKITPRO ...) on every compiler and
# linker call, so DEVKITPRO must be set at configure and build time.
#
# Usage: scripts/build_switch.sh [build-dir] [extra ninja args...]
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-${ROOT}/build-switch}"
shift 2>/dev/null || true

export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
TOOLCHAIN="${DEVKITPRO}/cmake/Switch.cmake"
if [ ! -f "${TOOLCHAIN}" ]; then
    echo "devkitPro not found in ${DEVKITPRO}." >&2
    exit 1
fi
if [ ! -f "${ROOT}/generated/th20.h" ]; then
    echo "generated/ is missing: run scripts/build_wasm.sh first." >&2
    exit 1
fi

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"
if [ ! -f build.ninja ]; then
    cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN}" -DCMAKE_BUILD_TYPE=Release "${ROOT}"
fi
exec ninja "$@"
