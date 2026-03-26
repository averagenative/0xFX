# CMake toolchain file for cross-compiling to Windows ARM64 via llvm-mingw
# Download llvm-mingw from: https://github.com/mstorsjo/llvm-mingw/releases
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# llvm-mingw prefix — set LLVM_MINGW_PREFIX env var or install to /opt/llvm-mingw
if(DEFINED ENV{LLVM_MINGW_PREFIX})
    set(LLVM_MINGW "$ENV{LLVM_MINGW_PREFIX}")
else()
    set(LLVM_MINGW "/opt/llvm-mingw")
endif()

set(CMAKE_C_COMPILER "${LLVM_MINGW}/bin/aarch64-w64-mingw32-clang")
set(CMAKE_CXX_COMPILER "${LLVM_MINGW}/bin/aarch64-w64-mingw32-clang++")
set(CMAKE_RC_COMPILER "${LLVM_MINGW}/bin/aarch64-w64-mingw32-windres")

set(CMAKE_FIND_ROOT_PATH "${LLVM_MINGW}/aarch64-w64-mingw32")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# ── Vendored SDL2 for ARM64 Windows ──────────────────────────────────
set(MINGW_SDL2_DIR "${CMAKE_SOURCE_DIR}/deps/SDL2-2.30.12/aarch64-w64-mingw32")
set(SDL2_FOUND TRUE)
set(SDL2_INCLUDE_DIRS "${MINGW_SDL2_DIR}/include;${MINGW_SDL2_DIR}/include/SDL2")

set(SDL2_STATIC_LIBRARIES
    "${MINGW_SDL2_DIR}/lib/libSDL2.a"
    imm32 version winmm setupapi gdi32 ole32 oleaut32 uuid
)
set(SDL2_DYNAMIC_LIBRARIES
    "${MINGW_SDL2_DIR}/lib/libSDL2.dll.a"
    imm32 version winmm setupapi gdi32 ole32 oleaut32 uuid
)
set(SDL2_DLL_PATH "${MINGW_SDL2_DIR}/bin/SDL2.dll")

set(SDL2_LIBRARIES ${SDL2_STATIC_LIBRARIES})

# ── OpenGL via mingw's opengl32 ────────────────────────────────────────────
set(OpenGL_FOUND TRUE)
set(OPENGL_FOUND TRUE)
set(OPENGL_gl_LIBRARY opengl32)
