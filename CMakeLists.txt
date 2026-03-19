cmake_minimum_required(VERSION 3.16)
project(0xFX VERSION 0.1.0 LANGUAGES C CXX)

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
    src/engine/internal/preset.c
    deps/cJSON.c
    deps/kissfft/kiss_fft.c
    deps/kissfft/kiss_fftr.c
)
target_include_directories(fx_engine PUBLIC
    ${CMAKE_SOURCE_DIR}/src/engine
)
target_include_directories(fx_engine PRIVATE
    ${CMAKE_SOURCE_DIR}/deps
)
target_link_libraries(fx_engine PRIVATE m)
set_target_properties(fx_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ── ImGui library (C++) ───────────────────────────────────────
find_package(SDL2 QUIET)

if(SDL2_FOUND OR EXISTS "/usr/include/SDL2/SDL.h")
    add_library(imgui STATIC
        deps/imgui/imgui.cpp
        deps/imgui/imgui_draw.cpp
        deps/imgui/imgui_tables.cpp
        deps/imgui/imgui_widgets.cpp
        deps/imgui/backends/imgui_impl_sdl2.cpp
        deps/imgui/backends/imgui_impl_opengl3.cpp
    )
    target_include_directories(imgui PUBLIC
        ${CMAKE_SOURCE_DIR}/deps/imgui
        ${CMAKE_SOURCE_DIR}/deps/imgui/backends
    )
    # ImGui's built-in GL3 loader (imgui_impl_opengl3_loader.h) provides all GL declarations
    if(SDL2_FOUND)
        target_include_directories(imgui PUBLIC ${SDL2_INCLUDE_DIRS})
    else()
        target_include_directories(imgui PUBLIC /usr/include/SDL2)
    endif()
    set(HAS_GUI TRUE)
    message(STATUS "GUI enabled (SDL2 + ImGui)")
else()
    set(HAS_GUI FALSE)
    message(STATUS "GUI disabled (SDL2 not found)")
endif()

# ── Standalone application (console, no GUI) ──────────────────
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

# ── GUI application (SDL2 + ImGui + OpenGL) ───────────────────
if(HAS_GUI)
    add_executable(0xfx_gui
        src/gui/gui_main.cpp
        src/gui/knobs.cpp
        src/audio/audio_device.c
    )
    target_include_directories(0xfx_gui PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/deps
        ${CMAKE_SOURCE_DIR}/deps/imgui
        ${CMAKE_SOURCE_DIR}/deps/imgui/backends
    )
    target_link_libraries(0xfx_gui PRIVATE fx_engine imgui m)
    if(SDL2_FOUND)
        target_link_libraries(0xfx_gui PRIVATE ${SDL2_LIBRARIES})
    else()
        target_link_libraries(0xfx_gui PRIVATE SDL2)
    endif()
    target_link_libraries(0xfx_gui PRIVATE GL)
    if(UNIX AND NOT APPLE)
        target_link_libraries(0xfx_gui PRIVATE pthread dl)
    elseif(WIN32)
        target_link_libraries(0xfx_gui PRIVATE opengl32 ole32 winmm)
    endif()
endif()

# ── API test ──────────────────────────────────────────────────
add_executable(fx_api_test tests/api_test.c)
target_include_directories(fx_api_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src/engine
    ${CMAKE_SOURCE_DIR}/deps
)
target_link_libraries(fx_api_test PRIVATE fx_engine m)

enable_testing()
add_test(NAME api_test COMMAND fx_api_test)

# ── Cross-compile support (MinGW) ────────────────────────────
if(MINGW)
    target_link_libraries(0xfx_standalone PRIVATE -static)
endif()

# ── CLAP plugin shared library ────────────────────────────────
# Build only on Linux (and Windows via MinGW) — not macOS (needs bundle)
if(UNIX AND NOT APPLE)
    add_library(0xfx_clap MODULE
        src/plugin/plugin.c
        deps/cplug/src/cplug_clap.c
    )
    target_include_directories(0xfx_clap PRIVATE
        ${CMAKE_SOURCE_DIR}/deps/cplug/src
        ${CMAKE_SOURCE_DIR}/src/engine
    )
    target_compile_definitions(0xfx_clap PRIVATE CPLUG_SHARED)
    target_compile_options(0xfx_clap PRIVATE
        -include ${CMAKE_SOURCE_DIR}/src/plugin/config.h
        # Suppress multichar literal warnings from CPLUG_VST3_TUID_* macros
        -Wno-multichar
        -Wno-pedantic
    )
    set_target_properties(0xfx_clap PROPERTIES
        PREFIX ""
        SUFFIX ".clap"
        C_VISIBILITY_PRESET hidden
    )
    target_link_libraries(0xfx_clap PRIVATE fx_engine m)
    if(UNIX AND NOT APPLE)
        target_link_libraries(0xfx_clap PRIVATE pthread dl)
    endif()
    message(STATUS "CLAP plugin target enabled")
endif()

# ── VST3 plugin shared library ────────────────────────────────
if(UNIX AND NOT APPLE)
    add_library(0xfx_vst3 MODULE
        src/plugin/plugin.c
        deps/cplug/src/cplug_vst3.c
    )
    target_include_directories(0xfx_vst3 PRIVATE
        ${CMAKE_SOURCE_DIR}/deps/cplug/src
        ${CMAKE_SOURCE_DIR}/src/engine
    )
    target_compile_definitions(0xfx_vst3 PRIVATE CPLUG_SHARED)
    target_compile_options(0xfx_vst3 PRIVATE
        -include ${CMAKE_SOURCE_DIR}/src/plugin/config.h
        -Wno-multichar
        -Wno-pedantic
    )
    set_target_properties(0xfx_vst3 PROPERTIES
        PREFIX ""
        SUFFIX ".so"
        C_VISIBILITY_PRESET hidden
    )
    target_link_libraries(0xfx_vst3 PRIVATE fx_engine m)
    if(UNIX AND NOT APPLE)
        target_link_libraries(0xfx_vst3 PRIVATE pthread dl)
    endif()
    message(STATUS "VST3 plugin target enabled")
endif()
