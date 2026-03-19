cmake_minimum_required(VERSION 3.16)
project(0xFX VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ── Compiler warnings ─────────────────────────────────────────
add_compile_options(-Wall -Wextra -Wpedantic)

# ── Options ───────────────────────────────────────────────────
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
if(ENABLE_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()

# ── Engine library (no platform deps, pure C99) ──────────────
add_library(fx_engine STATIC
    src/engine/internal/engine.c
    src/engine/internal/amp.c
    src/engine/internal/biquad.c
    src/engine/internal/noise_gate.c
    src/engine/internal/tuner.c
    src/engine/internal/pedals.c
    src/engine/internal/cab_ir.c
)
target_include_directories(fx_engine PUBLIC
    ${CMAKE_SOURCE_DIR}/src/engine
)
target_include_directories(fx_engine PRIVATE
    ${CMAKE_SOURCE_DIR}/deps
)
target_link_libraries(fx_engine PRIVATE m)

# ── Standalone application ────────────────────────────────────
add_executable(0xfx_standalone
    src/standalone/main.c
    src/audio/audio_device.c
)
target_include_directories(0xfx_standalone PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/deps
)
target_link_libraries(0xfx_standalone PRIVATE fx_engine m)

# Platform-specific audio deps
if(UNIX AND NOT APPLE)
    target_link_libraries(0xfx_standalone PRIVATE pthread dl)
elseif(WIN32)
    target_link_libraries(0xfx_standalone PRIVATE ole32 winmm)
endif()

# ── API test ──────────────────────────────────────────────────
add_executable(fx_api_test tests/api_test.c)
target_include_directories(fx_api_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src/engine
)
target_link_libraries(fx_api_test PRIVATE fx_engine m)

enable_testing()
add_test(NAME api_test COMMAND fx_api_test)

# ── Cross-compile support (MinGW) ────────────────────────────
if(MINGW)
    target_link_libraries(0xfx_standalone PRIVATE -static)
endif()
