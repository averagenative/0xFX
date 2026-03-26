# CMake toolchain file for cross-compiling to Linux ARM64 (aarch64)
#
# Prerequisites:
#   sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
#   sudo dpkg --add-architecture arm64
#   sudo apt install libgl-dev:arm64 libx11-dev:arm64 libasound2-dev:arm64
#
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

set(CMAKE_FIND_ROOT_PATH /usr/aarch64-linux-gnu /usr)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Multiarch: tell linker and find_library about aarch64 lib dir
set(CMAKE_LIBRARY_PATH /usr/lib/aarch64-linux-gnu)
link_directories(/usr/lib/aarch64-linux-gnu)

# ── Cross-compiled SDL2 for aarch64-linux (static) ───────────────────
# Built from SDL2 source with this toolchain (static, no X11/Wayland backends)
# For full standalone GUI display, build natively on ARM64 Linux with X11 dev libs.
# Plugins (CLAP/VST3) use X11+GLX directly — they don't need SDL2.
set(AARCH64_SDL2_DIR "${CMAKE_SOURCE_DIR}/deps/SDL2-aarch64-linux")
if(EXISTS "${AARCH64_SDL2_DIR}/lib/libSDL2.a")
    set(SDL2_FOUND TRUE)
    set(SDL2_INCLUDE_DIRS "${AARCH64_SDL2_DIR}/include;${AARCH64_SDL2_DIR}/include/SDL2")
    set(SDL2_LIBRARIES "${AARCH64_SDL2_DIR}/lib/libSDL2.a" pthread dl m)
    message(STATUS "SDL2 aarch64: ${AARCH64_SDL2_DIR}")
endif()

