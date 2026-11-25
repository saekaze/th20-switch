cmake_minimum_required(VERSION 3.16)
project(th20_switch C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_C_FLAGS_RELEASE "")
set(CMAKE_CXX_FLAGS_RELEASE "")
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# generated/: the TH20 module as C (scripts/build_wasm.sh: clang -> wasm32 ->
# wasm2c) together with the wasm2c runtime.
set(GEN "${CMAKE_SOURCE_DIR}/generated")
if(NOT EXISTS "${GEN}/th20.h")
    message(FATAL_ERROR "generated/ is missing: run scripts/build_wasm.sh first.")
endif()
file(GLOB TH20_C CONFIGURE_DEPENDS "${GEN}/th20_*.c")
add_library(th20_module OBJECT ${TH20_C} "${GEN}/wasm-rt-impl.c" "${GEN}/wasm-rt-exceptions-impl.c")
target_include_directories(th20_module PUBLIC "${GEN}")
# The linear memory is heap allocated (native/Memory.cpp). Desktop test builds
# bounds-check every module access, so a stray access in testing stops the
# game with a trap message. The Switch build does not: the checks cost a
# third of the code and of the load time, and the module's accesses were
# already proven in the browser and in the checked test runs. The game runs on
# a 32 MB stack instead of counting call depth. -ffp-contract=off keeps wasm
# float semantics: GCC would otherwise fuse multiply-adds on AArch64 and
# change the game's arithmetic (and so replays).
target_compile_definitions(th20_module PUBLIC WASM_RT_USE_MMAP=0)
if(NINTENDO_SWITCH)
    set(TH20_UNCHECKED_DEFAULT ON)
else()
    set(TH20_UNCHECKED_DEFAULT OFF)
endif()
option(TH20_UNCHECKED_MEMORY "No bounds checks on module memory accesses (Switch default)" ${TH20_UNCHECKED_DEFAULT})
if(TH20_UNCHECKED_MEMORY)
    target_compile_definitions(th20_module PUBLIC WASM_RT_MEMCHECK_GUARD_PAGES=1 WASM_RT_MEMCHECK_UNCHECKED=1
        WASM_RT_SKIP_SIGNAL_RECOVERY=1 WASM_RT_USE_STACK_DEPTH_COUNT=0)
else()
    target_compile_definitions(th20_module PUBLIC WASM_RT_MEMCHECK_BOUNDS_CHECK=1 WASM_RT_MAX_CALL_STACK_DEPTH=8000)
endif()
# gnu99: wasm-rt.h then declares its runtime state without _Thread_local, the
# same as the C++ host sees it (the module runs on one thread).
target_compile_options(th20_module PRIVATE -std=gnu99 -O2 -ffp-contract=off -fno-strict-aliasing -w)

# libdeflate (MIT; third_party/libdeflate, scripts/fetch_upstream.sh): the
# decompressor of the PNG fast path (native/PngFast.cpp).
set(LIBDEFLATE "${CMAKE_SOURCE_DIR}/third_party/libdeflate")
if(NOT EXISTS "${LIBDEFLATE}/libdeflate.h")
    message(FATAL_ERROR "third_party/libdeflate is missing: run scripts/fetch_upstream.sh first.")
endif()
set(LIBDEFLATE_SOURCES "${LIBDEFLATE}/lib/deflate_decompress.c" "${LIBDEFLATE}/lib/zlib_decompress.c"
    "${LIBDEFLATE}/lib/adler32.c" "${LIBDEFLATE}/lib/utils.c")
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|i[3-6]86")
    list(APPEND LIBDEFLATE_SOURCES "${LIBDEFLATE}/lib/x86/cpu_features.c")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64|arm")
    list(APPEND LIBDEFLATE_SOURCES "${LIBDEFLATE}/lib/arm/cpu_features.c")
endif()
add_library(deflate STATIC ${LIBDEFLATE_SOURCES})
target_include_directories(deflate PUBLIC "${LIBDEFLATE}")
target_compile_options(deflate PRIVATE -O2)

file(GLOB HOST_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/native/*.cpp")
add_executable(touhou20 ${HOST_SOURCES})
target_link_libraries(touhou20 PRIVATE th20_module deflate)
# wasm-rt.h is C: its const null funcref is a tentative definition, which
# C++ only accepts with -fpermissive (each unit gets an internal copy).
target_compile_options(touhou20 PRIVATE -O2 -ffp-contract=off -Wno-attributes -fpermissive)
# stb_image (native/Images.cpp decodes the non-PNG images with the module's copy).
target_include_directories(touhou20 PRIVATE "${CMAKE_SOURCE_DIR}/upstream/ports/th20/portable/sdl/third_party")

if(NINTENDO_SWITCH)
    if(NOT DEFINED ENV{DEVKITPRO})
        set(ENV{DEVKITPRO} "/opt/devkitpro")
    endif()
    set(TH_DEVKITPRO "$ENV{DEVKITPRO}")
    target_compile_definitions(th20_module PUBLIC __SWITCH__)
    target_include_directories(th20_module PRIVATE "${CMAKE_SOURCE_DIR}/native/switch_compat")
    target_include_directories(th20_module PUBLIC
        ${TH_DEVKITPRO}/portlibs/switch/include ${TH_DEVKITPRO}/portlibs/switch/include/freetype2
        ${TH_DEVKITPRO}/libnx/include)
    target_link_directories(touhou20 PRIVATE ${TH_DEVKITPRO}/portlibs/switch/lib ${TH_DEVKITPRO}/libnx/lib)
    target_link_libraries(touhou20 PRIVATE SDL2 EGL glapi drm_nouveau GLESv2 freetype harfbuzz png bz2 z nx m)
    nx_generate_nacp(OUTPUT "${CMAKE_BINARY_DIR}/touhou20-base.nacp"
        NAME "Touhou 20: Fossilized Wonders"
        AUTHOR "ZUN / Team Shanghai Alice"
        VERSION "1.00a-r1")
    add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/touhou20.nacp"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/nacp_lang.py"
                "${CMAKE_BINARY_DIR}/touhou20-base.nacp" "${CMAKE_BINARY_DIR}/touhou20.nacp"
                --lang Japanese
                --name "東方錦上京　～ Fossilized Wonders"
                --author "上海アリス幻樂団"
                --fill-empty-name "Touhou 20: Fossilized Wonders"
                --fill-empty-author "ZUN / Team Shanghai Alice"
        DEPENDS "${CMAKE_BINARY_DIR}/touhou20-base.nacp" "${CMAKE_SOURCE_DIR}/scripts/nacp_lang.py"
        COMMENT "Patching Japanese title into NACP"
        VERBATIM)
    nx_create_nro(touhou20
        ICON "${CMAKE_SOURCE_DIR}/platform/switch/icon.jpg"
        NACP "${CMAKE_BINARY_DIR}/touhou20.nacp")
else()
    # Linux test build (same module, SDL2 + FreeType + GLES3 desktop).
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED sdl2)
    pkg_check_modules(FT REQUIRED freetype2)
    pkg_check_modules(PNG REQUIRED libpng)
    find_package(Threads REQUIRED)
    target_include_directories(touhou20 PRIVATE ${SDL2_INCLUDE_DIRS} ${FT_INCLUDE_DIRS} ${PNG_INCLUDE_DIRS})
    target_link_libraries(touhou20 PRIVATE ${SDL2_LIBRARIES} ${FT_LIBRARIES} ${PNG_LIBRARIES} GLESv2 m Threads::Threads)
endif()
