#!/bin/bash
# Build a clean wasm32-wasi sysroot from Ubuntu's wasi-libc / libc++ packages.
#
# Ubuntu's clang also searches the host /usr/include for wasm32-wasi, which
# lets glibc headers (dlfcn.h, ...) leak into the wasm build; this sysroot
# holds only the wasi headers. Its libc++ configuration drops
# _LIBCPP_HAS_NO_FILESYSTEM so <fstream> is available (TH20 reads files with
# std::ifstream); the file streams are instantiated in user code.
#
# Usage: scripts/make_wasi_sysroot.sh <dest>
set -euo pipefail
DEST="$1"
INC=/usr/include/wasm32-wasi
LIB=/usr/lib/wasm32-wasi
rm -rf "$DEST"
mkdir -p "$DEST/include/wasm32-wasi/c++/v1" "$DEST/lib"
for e in "$INC"/*; do [ "$(basename "$e")" = c++ ] || ln -s "$e" "$DEST/include/wasm32-wasi/"; done
for e in "$INC"/c++/v1/*; do [ "$(basename "$e")" = __config_site ] || ln -s "$e" "$DEST/include/wasm32-wasi/c++/v1/"; done
grep -v '_LIBCPP_HAS_NO_FILESYSTEM' "$INC/c++/v1/__config_site" > "$DEST/include/wasm32-wasi/c++/v1/__config_site"
ln -s "$LIB" "$DEST/lib/wasm32-wasi"
echo "wasi sysroot ready in $DEST"
