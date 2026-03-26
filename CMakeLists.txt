cmake_minimum_required(VERSION 3.16)
project(0xFX VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Compiler warnings ─────────────────────────────────────────
add_compile_options(-Wall -Wextra -Wpedantic)

# ── Options ───────────────────────────────────────────────────
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)

# macOS codesigning — set to a Developer ID or use "-" for ad-hoc signing
set(CODESIGN_IDENTITY "" CACHE STRING "macOS codesign identity (empty=skip, '-'=ad-hoc)")

# Enable Objective-C for macOS AU plugin builds
if(APPLE)
    enable_language(OBJC)
endif()
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
elseif(APPLE)
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

# ── SDL2 detection ───────────────────────────────────────────
# SDL2 is needed for the standalone GUI app only — plugins use native APIs
if(NOT SDL2_FOUND)
    find_package(SDL2 QUIET)
endif()
if(NOT SDL2_FOUND AND EXISTS "/usr/include/SDL2/SDL.h")
    set(SDL2_FOUND TRUE)
    set(SDL2_INCLUDE_DIRS "/usr/include/SDL2")
    set(SDL2_LIBRARIES SDL2)
endif()
# macOS: Homebrew SDL2
if(NOT SDL2_FOUND AND APPLE)
    find_path(SDL2_HOMEBREW_INC SDL.h
        PATHS /usr/local/include/SDL2 /opt/homebrew/include/SDL2)
    if(SDL2_HOMEBREW_INC)
        set(SDL2_FOUND TRUE)
        set(SDL2_INCLUDE_DIRS "${SDL2_HOMEBREW_INC}")
        find_library(SDL2_LIB SDL2 PATHS /usr/local/lib /opt/homebrew/lib)
        set(SDL2_LIBRARIES "${SDL2_LIB}")
        message(STATUS "SDL2 found via Homebrew: ${SDL2_LIB}")
    endif()
endif()

# ── ImGui library for standalone (SDL2 backend) ──────────────
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
    message(STATUS "Standalone GUI enabled (SDL2 from: ${SDL2_INCLUDE_DIRS})")
else()
    set(HAS_GUI FALSE)
    message(STATUS "Standalone GUI disabled (SDL2 not found)")
endif()

# ── ImGui library for plugins (native platform backend, no SDL2) ──
# Windows: ImGui_ImplWin32 + ImGui_ImplOpenGL3
# Linux:   ImGui_ImplOpenGL3 only (X11 events fed manually)
set(IMGUI_PLUGIN_SOURCES
    deps/imgui/imgui.cpp
    deps/imgui/imgui_draw.cpp
    deps/imgui/imgui_tables.cpp
    deps/imgui/imgui_widgets.cpp
    deps/imgui/backends/imgui_impl_opengl3.cpp
)
if(WIN32 OR MINGW)
    list(APPEND IMGUI_PLUGIN_SOURCES
        deps/imgui/backends/imgui_impl_win32.cpp
    )
endif()

add_library(imgui_plugin STATIC ${IMGUI_PLUGIN_SOURCES})
target_include_directories(imgui_plugin PUBLIC
    ${CMAKE_SOURCE_DIR}/deps/imgui
    ${CMAKE_SOURCE_DIR}/deps/imgui/backends
)
# Disable gamepad support in Win32 backend — plugins don't need XInput
target_compile_definitions(imgui_plugin PRIVATE IMGUI_IMPL_WIN32_DISABLE_GAMEPAD)
set_target_properties(imgui_plugin PROPERTIES POSITION_INDEPENDENT_CODE ON)
set(HAS_PLUGIN_GUI TRUE)
message(STATUS "Plugin GUI enabled (native platform APIs, no SDL2)")

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
elseif(APPLE)
    target_link_libraries(0xfx_standalone PRIVATE pthread
        "-framework CoreAudio" "-framework CoreMIDI" "-framework CoreFoundation"
        "-framework AudioToolbox" "-framework AudioUnit")
elseif(WIN32)
    target_link_libraries(0xfx_standalone PRIVATE ole32 winmm)
endif()

# ── Generate embedded assets from resource PNGs ───────────────
file(GLOB_RECURSE RESOURCE_PNGS
    ${CMAKE_SOURCE_DIR}/resources/amps/*.png
    ${CMAKE_SOURCE_DIR}/resources/buttons/*.png
    ${CMAKE_SOURCE_DIR}/resources/cables/*.png
    ${CMAKE_SOURCE_DIR}/resources/cabs/*.png
    ${CMAKE_SOURCE_DIR}/resources/knobs/*.png
    ${CMAKE_SOURCE_DIR}/resources/leds/*.png
    ${CMAKE_SOURCE_DIR}/resources/mics/*.png
    ${CMAKE_SOURCE_DIR}/resources/pedals/*.png
    ${CMAKE_SOURCE_DIR}/resources/studio/*.png
    ${CMAKE_SOURCE_DIR}/resources/theme/*.png
    ${CMAKE_SOURCE_DIR}/resources/logo/*.png
    ${CMAKE_SOURCE_DIR}/resources/icon/*.ico
    ${CMAKE_SOURCE_DIR}/resources/icon/*.png
)
find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_FOUND AND RESOURCE_PNGS)
    add_custom_command(
        OUTPUT ${CMAKE_SOURCE_DIR}/src/gui/embedded_assets.c
               ${CMAKE_SOURCE_DIR}/src/gui/embedded_assets.h
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/generate_embedded_assets.py
        DEPENDS ${RESOURCE_PNGS}
                ${CMAKE_SOURCE_DIR}/tools/generate_embedded_assets.py
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Generating embedded_assets.c from resource PNGs..."
    )
    add_custom_target(generate_assets DEPENDS
        ${CMAKE_SOURCE_DIR}/src/gui/embedded_assets.c
        ${CMAKE_SOURCE_DIR}/src/gui/embedded_assets.h
    )
    message(STATUS "Asset generation enabled (Python3 found, ${CMAKE_SOURCE_DIR}/tools/generate_embedded_assets.py)")
elseif(EXISTS ${CMAKE_SOURCE_DIR}/src/gui/embedded_assets.c)
    message(STATUS "Using pre-existing embedded_assets.c (Python3 not found or no resource PNGs)")
else()
    message(FATAL_ERROR "No embedded_assets.c and cannot generate (need Python3 + resource PNGs)")
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
        # Use toolchain-defined MINGW_SDL2_DIR, or fall back to x86_64
        if(NOT DEFINED MINGW_SDL2_DIR)
            set(MINGW_SDL2_DIR "${CMAKE_SOURCE_DIR}/deps/SDL2-2.30.12/x86_64-w64-mingw32")
        endif()
        if(CMAKE_C_COMPILER_ID MATCHES "Clang")
            target_link_libraries(0xfx_gui PRIVATE
                -static
                "${MINGW_SDL2_DIR}/lib/libSDL2main.a"
                ${SDL2_LIBRARIES}
                opengl32 ole32 winmm)
        else()
            target_link_libraries(0xfx_gui PRIVATE
                -static mingw32
                "${MINGW_SDL2_DIR}/lib/libSDL2main.a"
                ${SDL2_LIBRARIES}
                opengl32 ole32 winmm)
        endif()
    elseif(APPLE)
        target_link_libraries(0xfx_gui PRIVATE
            "-framework OpenGL" "-framework Cocoa"
            "-framework CoreAudio" "-framework CoreMIDI" "-framework CoreFoundation"
            "-framework AudioToolbox" "-framework AudioUnit" "-framework IOKit"
            pthread)
    elseif(UNIX AND NOT APPLE)
        target_link_libraries(0xfx_gui PRIVATE GL pthread dl)
        if(ALSA_FOUND)
            target_include_directories(0xfx_gui PRIVATE ${ALSA_INCLUDE_DIRS})
            target_link_libraries(0xfx_gui PRIVATE ${ALSA_LIBRARIES})
        endif()
    endif()
    if(TARGET generate_assets)
        add_dependencies(0xfx_gui generate_assets)
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
# No longer gated on HAS_GUI (SDL2) — plugins use native platform APIs
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

# ── macOS plugin bundle helper ────────────────────────────────
# Generates an Info.plist and configures macOS bundle properties for a plugin target.
# Usage: oxfx_configure_macos_bundle(<target> <bundle_id> <bundle_type> <bundle_ext> <executable_name>)
# bundle_type: "BNDL" for plugins, "APPL" for apps
# For AU plugins, pass AU_COMPONENTS content to embed AudioComponents in the plist.
if(APPLE)
    set(OXFX_PLUGIN_BUNDLE_ID_BASE "com.averagenative.0xfx")

    function(oxfx_configure_macos_bundle target_name bundle_id bundle_type bundle_ext exec_name)
        set(OXFX_BUNDLE_EXECUTABLE "${exec_name}")
        set(OXFX_BUNDLE_IDENTIFIER "${bundle_id}")
        set(OXFX_BUNDLE_PACKAGE_TYPE "${bundle_type}")
        # AU components plist fragment — empty for non-AU bundles
        if(ARGC GREATER 5)
            set(OXFX_AU_COMPONENTS "${ARGV5}")
        else()
            set(OXFX_AU_COMPONENTS "")
        endif()
        configure_file(
            ${CMAKE_SOURCE_DIR}/src/plugin/Info.plist.in
            ${CMAKE_BINARY_DIR}/${target_name}.plist
            @ONLY
        )
        set_target_properties(${target_name} PROPERTIES
            BUNDLE TRUE
            MACOSX_BUNDLE TRUE
            BUNDLE_EXTENSION "${bundle_ext}"
            MACOSX_BUNDLE_INFO_PLIST ${CMAKE_BINARY_DIR}/${target_name}.plist
        )
    endfunction()

    # Codesign a macOS bundle after build
    function(oxfx_codesign target_name)
        if(NOT CODESIGN_IDENTITY STREQUAL "")
            add_custom_command(TARGET ${target_name} POST_BUILD
                COMMAND codesign --force --sign "${CODESIGN_IDENTITY}" --deep
                    "$<TARGET_BUNDLE_DIR:${target_name}>"
                COMMENT "Codesigning ${target_name} with identity: ${CODESIGN_IDENTITY}"
            )
        endif()
    endfunction()
endif()

# ── CLAP plugin shared library ────────────────────────────────
    set(CLAP_SOURCES
        src/plugin/plugin.c
        deps/cplug/src/cplug_clap.c
        src/core/log.c
    )
    if(HAS_PLUGIN_GUI)
        list(APPEND CLAP_SOURCES ${PLUGIN_GUI_SOURCES}
            src/plugin/gui_plugin_bridge.cpp)
    endif()

    add_library(0xfx_clap MODULE ${CLAP_SOURCES})
    target_include_directories(0xfx_clap PRIVATE
        ${CMAKE_SOURCE_DIR}/deps/cplug/src
        ${CMAKE_SOURCE_DIR}/src/engine
    )
    if(HAS_PLUGIN_GUI)
        target_include_directories(0xfx_clap PRIVATE ${PLUGIN_GUI_INCLUDES})
        target_compile_definitions(0xfx_clap PRIVATE OXFX_PLUGIN_HAS_GUI=1)
        target_link_libraries(0xfx_clap PRIVATE imgui_plugin)
        if(UNIX AND NOT APPLE)
            target_link_libraries(0xfx_clap PRIVATE GL X11)
        elseif(APPLE)
            target_link_libraries(0xfx_clap PRIVATE "-framework OpenGL" "-framework Cocoa")
        endif()
    endif()
    target_compile_definitions(0xfx_clap PRIVATE CPLUG_SHARED)
    target_compile_options(0xfx_clap PRIVATE
        -include ${CMAKE_SOURCE_DIR}/src/plugin/config.h
        # Suppress multichar literal warnings from CPLUG_VST3_TUID_* macros
        -Wno-multichar
        -Wno-pedantic
    )
    if(APPLE)
        # macOS CLAP: proper bundle structure (.clap/Contents/MacOS/0xFX)
        set_target_properties(0xfx_clap PROPERTIES
            OUTPUT_NAME "0xFX"
            PREFIX ""
            C_VISIBILITY_PRESET hidden
        )
        oxfx_configure_macos_bundle(0xfx_clap
            "${OXFX_PLUGIN_BUNDLE_ID_BASE}.clap" "BNDL" "clap" "0xFX")
        file(WRITE "${CMAKE_BINARY_DIR}/clap_PkgInfo" "BNDL????")
        add_custom_command(TARGET 0xfx_clap POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
                "${CMAKE_BINARY_DIR}/clap_PkgInfo"
                "$<TARGET_BUNDLE_DIR:0xfx_clap>/Contents/PkgInfo"
            COMMENT "Writing PkgInfo for CLAP bundle"
        )
        oxfx_codesign(0xfx_clap)
    else()
        set_target_properties(0xfx_clap PROPERTIES
            OUTPUT_NAME "0xFX"
            PREFIX ""
            SUFFIX ".clap"
            C_VISIBILITY_PRESET hidden
        )
    endif()
    if(MINGW)
        if(CMAKE_C_COMPILER_ID MATCHES "Clang")
            target_link_options(0xfx_clap PRIVATE -static)
        else()
            target_link_options(0xfx_clap PRIVATE -static -static-libgcc -static-libstdc++)
        endif()
    endif()
    target_link_libraries(0xfx_clap PRIVATE fx_engine m)
    if(UNIX AND NOT APPLE)
        target_link_libraries(0xfx_clap PRIVATE pthread dl)
    elseif(APPLE)
        target_link_libraries(0xfx_clap PRIVATE pthread
            "-framework OpenGL" "-framework Cocoa"
            "-framework CoreAudio" "-framework CoreMIDI" "-framework CoreFoundation"
            "-framework AudioToolbox" "-framework AudioUnit")
    elseif(WIN32 OR MINGW)
        target_link_libraries(0xfx_clap PRIVATE ole32 winmm opengl32 dwmapi gdi32 imm32)
    endif()
    if(TARGET generate_assets)
        add_dependencies(0xfx_clap generate_assets)
    endif()
    message(STATUS "CLAP plugin target enabled")

# ── VST3 plugin shared library ────────────────────────────────
    set(VST3_SOURCES
        src/plugin/plugin.c
        deps/cplug/src/cplug_vst3.c
        src/core/log.c
    )
    if(HAS_PLUGIN_GUI)
        list(APPEND VST3_SOURCES ${PLUGIN_GUI_SOURCES}
            src/plugin/gui_plugin_bridge.cpp)
    endif()

    add_library(0xfx_vst3 MODULE ${VST3_SOURCES})
    target_include_directories(0xfx_vst3 PRIVATE
        ${CMAKE_SOURCE_DIR}/deps/cplug/src
        ${CMAKE_SOURCE_DIR}/src/engine
    )
    if(HAS_PLUGIN_GUI)
        target_include_directories(0xfx_vst3 PRIVATE ${PLUGIN_GUI_INCLUDES})
        target_compile_definitions(0xfx_vst3 PRIVATE OXFX_PLUGIN_HAS_GUI=1)
        target_link_libraries(0xfx_vst3 PRIVATE imgui_plugin)
        if(UNIX AND NOT APPLE)
            target_link_libraries(0xfx_vst3 PRIVATE GL X11)
        elseif(APPLE)
            target_link_libraries(0xfx_vst3 PRIVATE "-framework OpenGL" "-framework Cocoa")
        endif()
    endif()
    target_compile_definitions(0xfx_vst3 PRIVATE CPLUG_SHARED)
    target_compile_options(0xfx_vst3 PRIVATE
        -include ${CMAKE_SOURCE_DIR}/src/plugin/config.h
        -Wno-multichar
        -Wno-pedantic
    )
    if(MINGW)
        if(CMAKE_C_COMPILER_ID MATCHES "Clang")
            target_link_options(0xfx_vst3 PRIVATE -static)
        else()
            target_link_options(0xfx_vst3 PRIVATE -static -static-libgcc -static-libstdc++)
        endif()
    endif()
    target_link_libraries(0xfx_vst3 PRIVATE fx_engine m)

    if(APPLE)
        # macOS VST3: proper bundle structure (.vst3/Contents/MacOS/0xFX)
        set_target_properties(0xfx_vst3 PROPERTIES
            OUTPUT_NAME "0xFX"
            PREFIX ""
            C_VISIBILITY_PRESET hidden
        )
        oxfx_configure_macos_bundle(0xfx_vst3
            "${OXFX_PLUGIN_BUNDLE_ID_BASE}.vst3" "BNDL" "vst3" "0xFX")
        file(WRITE "${CMAKE_BINARY_DIR}/vst3_PkgInfo" "BNDL????")
        add_custom_command(TARGET 0xfx_vst3 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
                "${CMAKE_BINARY_DIR}/vst3_PkgInfo"
                "$<TARGET_BUNDLE_DIR:0xfx_vst3>/Contents/PkgInfo"
            COMMENT "Writing PkgInfo for VST3 bundle"
        )
        target_link_libraries(0xfx_vst3 PRIVATE pthread
            "-framework OpenGL" "-framework Cocoa"
            "-framework CoreAudio" "-framework CoreMIDI" "-framework CoreFoundation"
            "-framework AudioToolbox" "-framework AudioUnit")
        oxfx_codesign(0xfx_vst3)
    elseif(UNIX)
        # Linux VST3: flat bundle structure (.vst3/Contents/x86_64-linux/0xFX.so)
        set_target_properties(0xfx_vst3 PROPERTIES
            OUTPUT_NAME "0xFX"
            PREFIX ""
            SUFFIX ".vst3"
            C_VISIBILITY_PRESET hidden
        )
        target_link_libraries(0xfx_vst3 PRIVATE pthread dl)
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|ARM64")
            set(VST3_LINUX_ARCH "aarch64-linux")
        else()
            set(VST3_LINUX_ARCH "x86_64-linux")
        endif()
        set(VST3_BUNDLE_DIR "${CMAKE_BINARY_DIR}/0xFX.vst3/Contents/${VST3_LINUX_ARCH}")
        add_custom_command(TARGET 0xfx_vst3 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${VST3_BUNDLE_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:0xfx_vst3>" "${VST3_BUNDLE_DIR}/0xFX.so"
            COMMENT "Creating VST3 bundle: 0xFX.vst3"
        )
    elseif(WIN32 OR MINGW)
        set_target_properties(0xfx_vst3 PROPERTIES
            OUTPUT_NAME "0xFX"
            PREFIX ""
            SUFFIX ".vst3"
            C_VISIBILITY_PRESET hidden
        )
        target_link_libraries(0xfx_vst3 PRIVATE ole32 winmm opengl32 dwmapi gdi32 imm32)
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|ARM64")
            set(VST3_WIN_ARCH "arm64-win")
        else()
            set(VST3_WIN_ARCH "x86_64-win")
        endif()
        set(VST3_BUNDLE_DIR "${CMAKE_BINARY_DIR}/0xFX.vst3/Contents/${VST3_WIN_ARCH}")
        add_custom_command(TARGET 0xfx_vst3 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${VST3_BUNDLE_DIR}"
            COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:0xfx_vst3>" "${VST3_BUNDLE_DIR}/0xFX.vst3"
            COMMENT "Creating VST3 bundle: 0xFX.vst3"
        )
    endif()
    if(TARGET generate_assets)
        add_dependencies(0xfx_vst3 generate_assets)
    endif()
    message(STATUS "VST3 plugin target enabled")

# ── AUv2 plugin (macOS only) ─────────────────────────────────
# CPLUG supports AUv2 via cplug_auv2.c. The AU plugin requires Objective-C
# compilation (.m) and a macOS .component bundle with AudioComponents in the
# Info.plist. The AU host loads the factory function GetAUv2PluginFactory.
if(APPLE)
    set(AUV2_SOURCES
        src/plugin/plugin.c
        deps/cplug/src/cplug_auv2.c
        src/core/log.c
    )
    if(HAS_PLUGIN_GUI)
        list(APPEND AUV2_SOURCES ${PLUGIN_GUI_SOURCES}
            src/plugin/gui_plugin_bridge.cpp)
    endif()

    add_library(0xfx_au MODULE ${AUV2_SOURCES})
    target_include_directories(0xfx_au PRIVATE
        ${CMAKE_SOURCE_DIR}/deps/cplug/src
        ${CMAKE_SOURCE_DIR}/src/engine
    )
    if(HAS_PLUGIN_GUI)
        target_include_directories(0xfx_au PRIVATE ${PLUGIN_GUI_INCLUDES})
        target_compile_definitions(0xfx_au PRIVATE OXFX_PLUGIN_HAS_GUI=1)
        target_link_libraries(0xfx_au PRIVATE imgui_plugin)
        target_link_libraries(0xfx_au PRIVATE "-framework OpenGL" "-framework Cocoa")
    endif()
    target_compile_definitions(0xfx_au PRIVATE CPLUG_SHARED)

    # AU version as integer: major << 16 | minor << 8 | patch
    math(EXPR OXFX_AUV2_VERSION_INT
        "${PROJECT_VERSION_MAJOR} << 16 | ${PROJECT_VERSION_MINOR} << 8 | ${PROJECT_VERSION_PATCH}"
        OUTPUT_FORMAT DECIMAL)

    target_compile_definitions(0xfx_au PRIVATE
        CPLUG_AUV2_VERSION_INT=${OXFX_AUV2_VERSION_INT}
        CPLUG_AUV2_BUNDLE_ID="${OXFX_PLUGIN_BUNDLE_ID_BASE}.audiounit"
        CPLUG_BUILD_AUV2=1
    )
    target_compile_options(0xfx_au PRIVATE
        -include ${CMAKE_SOURCE_DIR}/src/plugin/config.h
        -Wno-multichar
        -Wno-pedantic
        -Wno-nullability-completeness
    )
    # Suppress warnings from CPLUG's AUv2 wrapper (third-party code)
    set_source_files_properties(deps/cplug/src/cplug_auv2.c PROPERTIES
        COMPILE_FLAGS "-Wno-sign-compare -Wno-unused-parameter -Wno-unused-function"
    )
    set_target_properties(0xfx_au PROPERTIES
        OUTPUT_NAME "0xFX"
        PREFIX ""
        C_VISIBILITY_PRESET hidden
    )
    target_link_libraries(0xfx_au PRIVATE fx_engine m pthread
        "-framework OpenGL" "-framework Cocoa"
        "-framework CoreAudio" "-framework CoreMIDI" "-framework CoreFoundation"
        "-framework AudioToolbox" "-framework AudioUnit")

    # AU .component bundle with AudioComponents in Info.plist
    # AU type: "aufx" = Effect, subtype: "0xFx" (4-char), manufacturer: "Avgn"
    set(OXFX_AU_PLIST_FRAGMENT [=[
	<key>AudioComponents</key>
	<array>
		<dict>
			<key>name</key>
			<string>averagenative: 0xFX</string>
			<key>description</key>
			<string>Guitar amp simulator and effects pedalboard</string>
			<key>factoryFunction</key>
			<string>GetAUv2PluginFactory</string>
			<key>manufacturer</key>
			<string>Avgn</string>
			<key>sandboxSafe</key>
			<true/>
			<key>subtype</key>
			<string>0xFx</string>
			<key>tags</key>
			<array>
				<string>Effects</string>
				<string>Guitar</string>
			</array>
			<key>type</key>
			<string>aufx</string>
			<key>version</key>
			<integer>${OXFX_AUV2_VERSION_INT}</integer>
		</dict>
	</array>
]=])
    oxfx_configure_macos_bundle(0xfx_au
        "${OXFX_PLUGIN_BUNDLE_ID_BASE}.audiounit" "BNDL" "component" "0xFX"
        "${OXFX_AU_PLIST_FRAGMENT}")
    file(WRITE "${CMAKE_BINARY_DIR}/au_PkgInfo" "BNDL????")
    add_custom_command(TARGET 0xfx_au POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_BINARY_DIR}/au_PkgInfo"
            "$<TARGET_BUNDLE_DIR:0xfx_au>/Contents/PkgInfo"
        COMMENT "Writing PkgInfo for AU bundle"
    )
    oxfx_codesign(0xfx_au)

    if(TARGET generate_assets)
        add_dependencies(0xfx_au generate_assets)
    endif()
    message(STATUS "AUv2 plugin target enabled")
endif()
