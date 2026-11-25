# TH20 game runtime -> th20.wasm (wasm32-wasi), the input of wasm2c.
#
# Configure with the toolchain file and the sysroot from
# scripts/make_wasi_sysroot.sh (see scripts/build_wasm.sh). The game, its
# software audio mixer and the upstream SDL3 host are compiled unchanged; SDL3,
# OpenGL ES and WASI calls become wasm imports that the native host provides.
cmake_minimum_required(VERSION 3.16)
project(th20_wasm C CXX)
set(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(UP "${ROOT}/upstream/ports/th20")
set(COMMON "${ROOT}/upstream/common/include")
set(TP "${ROOT}/third_party")

# ---- C++ exception runtime (LLVM 18 libc++abi + libunwind, wasm EH) -------
set(EH_FLAGS -O2 -fwasm-exceptions -D__USING_WASM_EXCEPTIONS__ -D_LIBUNWIND_HIDE_SYMBOLS
    -D_LIBCXXABI_BUILDING_LIBRARY -D_LIBCPP_BUILDING_LIBRARY -D_LIBCXXABI_HAS_NO_THREADS
    -D_LIBCPP_HAS_NO_THREADS -DNDEBUG)
set(EH_SOURCES)
foreach(f abort_message cxa_aux_runtime cxa_default_handlers cxa_demangle cxa_exception
          cxa_exception_storage cxa_guard cxa_handlers cxa_personality cxa_vector cxa_virtual
          fallback_malloc private_typeinfo stdlib_exception stdlib_stdexcept stdlib_typeinfo
          stdlib_new_delete)
    list(APPEND EH_SOURCES "${TP}/llvm/libcxxabi/src/${f}.cpp")
endforeach()
list(APPEND EH_SOURCES "${TP}/llvm/libunwind/src/Unwind-wasm.c")
add_library(ehabi STATIC ${EH_SOURCES})
target_compile_options(ehabi PRIVATE ${EH_FLAGS} $<$<COMPILE_LANGUAGE:CXX>:-std=c++20>)
target_include_directories(ehabi PRIVATE "${TP}/llvm/libcxxabi/include" "${TP}/llvm/libcxx/src"
    "${TP}/llvm/libcxx/src/include" "${TP}/llvm/libunwind/include")

# libc++ from the sysroot without the (exception-less) libc++abi it bundles.
set(CXX_NOABI "${CMAKE_BINARY_DIR}/libcxx_noabi.a")
add_custom_command(OUTPUT "${CXX_NOABI}"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SYSROOT}/lib/wasm32-wasi/libc++.a" "${CXX_NOABI}"
    COMMAND sh -c "${CMAKE_AR} t '${CXX_NOABI}' | grep -E '^(cxa_|stdlib_|private_typeinfo|fallback_malloc|abort_message)' | xargs ${CMAKE_AR} d '${CXX_NOABI}'"
    VERBATIM)
add_custom_target(cxx_noabi DEPENDS "${CXX_NOABI}")

# ---- zlib (snapshot PNG writer) ---------------------------------------------
file(GLOB ZLIB_SOURCES "${TP}/zlib/*.c")
list(FILTER ZLIB_SOURCES EXCLUDE REGEX "(gz[a-z]*|minigzip|example)\\.c$")
add_library(zlib STATIC ${ZLIB_SOURCES})
target_compile_options(zlib PRIVATE -O2 -w)
target_include_directories(zlib PUBLIC "${TP}/zlib")

# ---- TH20 -------------------------------------------------------------------
file(GLOB_RECURSE TH20_GAME CONFIGURE_DEPENDS "${UP}/src/*.cpp")
# Same exclusions as upstream's portable/build.mjs: oracle/test/diagnostic
# translation units and host-side CLI tools are not part of the runtime.
list(FILTER TH20_GAME EXCLUDE REGEX "/src/.*(oracle|test|diagnostics|audit|link_probe|cpu_compare)[^/]*$")
list(FILTER TH20_GAME EXCLUDE REGEX "/src/.*/(oracle|test|diagnostics|audit|link_probe|cpu_compare)[^/]*/")
list(FILTER TH20_GAME EXCLUDE REGEX "/src/archive/verify\\.cpp$|backend_smoke\\.cpp$|replay_format_probe\\.cpp$")
file(GLOB TH20_HOST CONFIGURE_DEPENDS "${UP}/web/cpp/platform/*.cpp" "${UP}/web/cpp/sdl/*.cpp")
# The browser frame loop is replaced by src/NativeLoop.cpp.
list(REMOVE_ITEM TH20_HOST "${UP}/web/cpp/sdl/ApplicationHost.cpp")
# FreeType needs setjmp, which wasi-libc lacks: text is rasterized natively
# (src/FontImports.cpp forwards to native/FontHost.cpp).
list(REMOVE_ITEM TH20_HOST "${UP}/web/cpp/sdl/FontHost.cpp")
file(GLOB TH20_NATIVE CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_library(softfloat STATIC "${UP}/web/cpp/third_party/softfloat.c")
target_compile_options(softfloat PRIVATE -O2 -w -std=c11 -fno-strict-aliasing -ffp-contract=off)
target_compile_definitions(softfloat PRIVATE SOFTFLOAT_FAST_INT64 INLINE_LEVEL=5)

add_executable(th20 ${TH20_GAME} ${TH20_HOST} "${UP}/portable/sdl/Renderer.cpp" ${TH20_NATIVE})
add_dependencies(th20 cxx_noabi)
set_target_properties(th20 PROPERTIES SUFFIX ".wasm")
file(GLOB MODULE_DIRS LIST_DIRECTORIES true "${UP}/src/*")
list(FILTER MODULE_DIRS EXCLUDE REGEX "\\.[a-z]+$")
target_include_directories(th20 PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/compat"
    "${UP}/portable/sdl" "${UP}/web/cpp/shim" "${UP}/src" "${UP}/include" "${UP}/web/cpp"
    ${MODULE_DIRS} "${COMMON}" "${TP}/sdl3/include" "${ROOT}/upstream/common/third_party/imgui")
target_compile_definitions(th20 PRIVATE TH_SDL3=1 TH_NATIVE_PLATFORM=1 MA_NO_RUNTIME_LINKING)
# ThinLTO: TH20 is ~500 files of small C++ functions; inlining across them
# makes the game code ~12% faster and stage loads ~18% faster once in C.
target_compile_options(th20 PRIVATE -O2 -flto=thin -std=c++20 -fwasm-exceptions -frtti
    -fno-strict-aliasing -ffp-contract=off -Wno-everything
    "SHELL:-include ${UP}/web/cpp/shim/msvc_compat.h")
target_link_libraries(th20 PRIVATE zlib softfloat ehabi "${CXX_NOABI}")
# Reactor module: the native host calls _initialize, then the exported entry
# points. Unresolved SDL3/GL symbols become imports from "env".
target_link_options(th20 PRIVATE -flto=thin -O2 -nostdlib++ -mexec-model=reactor -fwasm-exceptions
    -Wl,--allow-undefined -Wl,-z,stack-size=1048576
    -Wl,--initial-memory=67108864 -Wl,--max-memory=2147483648
    -Wl,--export=malloc -Wl,--export=free -Wl,--export=__stack_pointer)
