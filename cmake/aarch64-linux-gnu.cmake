# CMake toolchain file for cross-compiling to Linux ARM64 (aarch64)
# Install: sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

set(CMAKE_FIND_ROOT_PATH /usr/aarch64-linux-gnu)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# SDL2 for aarch64 — use system multiarch or cross-compiled
# Install: sudo apt install libsdl2-dev:arm64  (requires multiarch)
# Or: build SDL2 from source with this toolchain
# Fallback: build without GUI (standalone + plugins only)
