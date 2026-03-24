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

# ── Core library (logging + crash handler) ───────────────────
add_library(fx_core STATIC
    src/core/log.c
    src/core/crash.c
)
target_include_directories(fx_core PUBLIC
    ${CMAKE_SOURCE_DIR}/src/core
)
if(UNIX AND NOT APPLE)
    target_link_libraries(fx_core PRIVATE pthread)
endif()
set_target_properties(fx_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ── Shine MP3 encoder library ────────────────────────────────
add_library(shine STATIC
    deps/shine/bitstream.c
    deps/shine/huffman.c
    deps/shine/l3bitstream.c
    deps/shine/l3loop.c
    deps/shine/l3mdct.c
    deps/shine/l3subband.c
    deps/shine/layer3.c
    deps/shine/reservoir.c
    deps/shine/tables.c
)
target_include_directories(shine PUBLIC ${CMAKE_SOURCE_DIR}/deps/shine)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(shine PRIVATE -w)
endif()
target_link_libraries(shine PRIVATE m)
set_target_properties(shine PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ── Engine library (no platform deps, pure C99) ──────────────
add_library(fx_engine STATIC
    src/engine/internal/engine.c
    src/engine/internal/amp.c
    src/engine/internal/biquad.c
    src/engine/internal/noise_gate.c
    src/engine/internal/tuner.c
    src/engine/internal/pedals.c
    src/engine/internal/studio.c
    src/engine/internal/cab_ir.c
    src/engine/internal/mic_sim.c
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
# SDL2 detection: toolchain may set SDL2_FOUND+SDL2_INCLUDE_DIRS, else try system
if(NOT SDL2_FOUND)
    find_package(SDL2 QUIET)
endif()
if(NOT SDL2_FOUND AND EXISTS "/usr/include/SDL2/SDL.h")
    set(SDL2_FOUND TRUE)
    set(SDL2_INCLUDE_DIRS "/usr/include/SDL2")
    set(SDL2_LIBRARIES SDL2)
endif()

if(SDL2_FOUND)
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
        ${SDL2_INCLUDE_DIRS}
    )
    set_target_properties(imgui PROPERTIES POSITION_INDEPENDENT_CODE ON)
    set(HAS_GUI TRUE)
    message(STATUS "GUI enabled (SDL2 from: ${SDL2_INCLUDE_DIRS})")
else()
    set(HAS_GUI FALSE)
    message(STATUS "GUI disabled (SDL2 not found)")
endif()

# ── ALSA detection (optional, for MIDI on Linux) ──────────────
if(UNIX AND NOT APPLE)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(ALSA QUIET alsa)
    endif()
    if(ALSA_FOUND)
        message(STATUS "ALSA found — MIDI input enabled")
    else()
        message(STATUS "ALSA not found — MIDI input disabled (no-op fallback)")
    endif()
endif()

# ── Standalone application (console, no GUI) ──────────────────
add_executable(0xfx_standalone
    src/standalone/main.c
    src/audio/audio_device.c
    src/audio/midi_input.c
    src/audio/recorder.c
)
target_include_directories(0xfx_standalone PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/deps
)
target_link_libraries(0xfx_standalone PRIVATE fx_engine fx_core shine m)

# Platform-specific audio deps
if(UNIX AND NOT APPLE)
    target_link_libraries(0xfx_standalone PRIVATE pthread dl)
    if(ALSA_FOUND)
        target_include_directories(0xfx_standalone PRIVATE ${ALSA_INCLUDE_DIRS})
        target_link_libraries(0xfx_standalone PRIVATE ${ALSA_LIBRARIES})
    endif()
elseif(WIN32)
    target_link_libraries(0xfx_standalone PRIVATE ole32 winmm)
endif()

# ── GUI application (SDL2 + ImGui + OpenGL) ───────────────────
if(HAS_GUI)
    # Windows icon resource
    if(WIN32 OR MINGW)
        set(GUI_RC resources/icon/0xfx.rc)
    else()
        set(GUI_RC "")
    endif()

    add_executable(0xfx_gui WIN32
        src/gui/gui_main.cpp
        src/gui/gui_render.cpp
        src/gui/knobs.cpp
        src/gui/texture.c
        src/gui/embedded_assets.c
        src/audio/audio_device.c
        src/audio/midi_input.c
        src/audio/recorder.c
        ${GUI_RC}
    )
    target_include_directories(0xfx_gui PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/deps
        ${CMAKE_SOURCE_DIR}/deps/imgui
        ${CMAKE_SOURCE_DIR}/deps/imgui/backends
    )
    target_link_libraries(0xfx_gui PRIVATE fx_engine fx_core imgui shine m)
    if(SDL2_FOUND)
        target_link_libraries(0xfx_gui PRIVATE ${SDL2_LIBRARIES})
    else()
        target_link_libraries(0xfx_gui PRIVATE SDL2)
    endif()
    if(WIN32)
        # SDL2main provides WinMain → SDL_main wrapper
        set(MINGW_SDL2_DIR "${CMAKE_SOURCE_DIR}/deps/SDL2-2.30.12/x86_64-w64-mingw32")
        target_link_libraries(0xfx_gui PRIVATE
            -static mingw32
            "${MINGW_SDL2_DIR}/lib/libSDL2main.a"
            ${SDL2_LIBRARIES}
            opengl32 ole32 winmm)
    elseif(UNIX AND NOT APPLE)
        target_link_libraries(0xfx_gui PRIVATE GL pthread dl)
        if(ALSA_FOUND)
            target_include_directories(0xfx_gui PRIVATE ${ALSA_INCLUDE_DIRS})
            target_link_libraries(0xfx_gui PRIVATE ${ALSA_LIBRARIES})
        endif()
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

# ── Shared plugin GUI sources ─────────────────────────────────
# GUI render + widget sources shared between plugin targets
if(HAS_GUI)
    set(PLUGIN_GUI_SOURCES
        src/gui/gui_render.cpp
        src/gui/knobs.cpp
        src/gui/texture.c
        src/gui/embedded_assets.c
    )
    set(PLUGIN_GUI_INCLUDES
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/deps
        ${CMAKE_SOURCE_DIR}/deps/imgui
        ${CMAKE_SOURCE_DIR}/deps/imgui/backends
    )
endif()

# ── CLAP plugin shared library ────────────────────────────────
    set(CLAP_SOURCES
        src/plugin/plugin.c
        deps/cplug/src/cplug_clap.c
        src/core/log.c
    )
    if(HAS_GUI)
        list(APPEND CLAP_SOURCES ${PLUGIN_GUI_SOURCES}
            src/plugin/gui_plugin_bridge.cpp)
    endif()

    add_library(0xfx_clap MODULE ${CLAP_SOURCES})
    target_include_directories(0xfx_clap PRIVATE
        ${CMAKE_SOURCE_DIR}/deps/cplug/src
        ${CMAKE_SOURCE_DIR}/src/engine
    )
    if(HAS_GUI)
        target_include_directories(0xfx_clap PRIVATE ${PLUGIN_GUI_INCLUDES})
        target_compile_definitions(0xfx_clap PRIVATE OXFX_PLUGIN_HAS_GUI=1)
        target_link_libraries(0xfx_clap PRIVATE imgui)
        if(SDL2_FOUND)
            target_link_libraries(0xfx_clap PRIVATE ${SDL2_LIBRARIES})
        else()
            target_link_libraries(0xfx_clap PRIVATE SDL2)
        endif()
        if(UNIX AND NOT APPLE)
            target_link_libraries(0xfx_clap PRIVATE GL)
        endif()
    endif()
    target_compile_definitions(0xfx_clap PRIVATE CPLUG_SHARED)
    target_compile_options(0xfx_clap PRIVATE
        -include ${CMAKE_SOURCE_DIR}/src/plugin/config.h
        # Suppress multichar literal warnings from CPLUG_VST3_TUID_* macros
        -Wno-multichar
        -Wno-pedantic
    )
    set_target_properties(0xfx_clap PROPERTIES
        OUTPUT_NAME "0xFX"
        PREFIX ""
        SUFFIX ".clap"
        C_VISIBILITY_PRESET hidden
    )
    if(MINGW)
        target_link_options(0xfx_clap PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    target_link_libraries(0xfx_clap PRIVATE fx_engine m)
    if(UNIX AND NOT APPLE)
        target_link_libraries(0xfx_clap PRIVATE pthread dl)
    elseif(WIN32 OR MINGW)
        target_link_libraries(0xfx_clap PRIVATE ole32 winmm opengl32)
    endif()
    message(STATUS "CLAP plugin target enabled")

# ── VST3 plugin shared library ────────────────────────────────
    set(VST3_SOURCES
        src/plugin/plugin.c
        deps/cplug/src/cplug_vst3.c
        src/core/log.c
    )
    if(HAS_GUI)
        list(APPEND VST3_SOURCES ${PLUGIN_GUI_SOURCES}
            src/plugin/gui_plugin_bridge.cpp)
    endif()

    add_library(0xfx_vst3 MODULE ${VST3_SOURCES})
    target_include_directories(0xfx_vst3 PRIVATE
        ${CMAKE_SOURCE_DIR}/deps/cplug/src
        ${CMAKE_SOURCE_DIR}/src/engine
    )
    if(HAS_GUI)
        target_include_directories(0xfx_vst3 PRIVATE ${PLUGIN_GUI_INCLUDES})
        target_compile_definitions(0xfx_vst3 PRIVATE OXFX_PLUGIN_HAS_GUI=1)
        target_link_libraries(0xfx_vst3 PRIVATE imgui)
        if(SDL2_FOUND)
            target_link_libraries(0xfx_vst3 PRIVATE ${SDL2_LIBRARIES})
        else()
            target_link_libraries(0xfx_vst3 PRIVATE SDL2)
        endif()
        if(UNIX AND NOT APPLE)
            target_link_libraries(0xfx_vst3 PRIVATE GL)
        endif()
    endif()
    target_compile_definitions(0xfx_vst3 PRIVATE CPLUG_SHARED)
    target_compile_options(0xfx_vst3 PRIVATE
        -include ${CMAKE_SOURCE_DIR}/src/plugin/config.h
        -Wno-multichar
        -Wno-pedantic
    )
    set_target_properties(0xfx_vst3 PROPERTIES
        OUTPUT_NAME "0xFX"
        PREFIX ""
        SUFFIX ".vst3"
        C_VISIBILITY_PRESET hidden
    )
    if(MINGW)
        target_link_options(0xfx_vst3 PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
    target_link_libraries(0xfx_vst3 PRIVATE fx_engine m)
    if(UNIX AND NOT APPLE)
        target_link_libraries(0xfx_vst3 PRIVATE pthread dl)
    elseif(WIN32 OR MINGW)
        target_link_libraries(0xfx_vst3 PRIVATE ole32 winmm opengl32)
    endif()

    # VST3 bundle structure (required by Reaper and most hosts)
    if(CMAKE_SYSTEM_NAME STREQUAL "Windows" OR MINGW)
        set(VST3_ARCH "x86_64-win")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set(VST3_ARCH "MacOS")
    else()
        set(VST3_ARCH "x86_64-linux")
    endif()
    set(VST3_BUNDLE_DIR "${CMAKE_BINARY_DIR}/0xFX.vst3.bundle/Contents/${VST3_ARCH}")
    add_custom_command(TARGET 0xfx_vst3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${VST3_BUNDLE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:0xfx_vst3>" "${VST3_BUNDLE_DIR}/0xFX.vst3"
        COMMENT "Creating VST3 bundle: 0xFX.vst3.bundle"
    )
    # Copy SDL2.dll alongside plugins (needed for dynamic linking)
    if(MINGW AND SDL2_DLL_PATH)
        add_custom_command(TARGET 0xfx_clap POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy "${SDL2_DLL_PATH}" "${CMAKE_BINARY_DIR}/SDL2.dll"
            COMMENT "Copying SDL2.dll for plugin use"
        )
        add_custom_command(TARGET 0xfx_vst3 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy "${SDL2_DLL_PATH}" "${VST3_BUNDLE_DIR}/SDL2.dll"
        )
    endif()
    message(STATUS "VST3 plugin target enabled")
