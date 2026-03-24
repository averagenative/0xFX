# CMake toolchain file for cross-compiling to Windows via mingw-w64
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)

set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# ── Vendored SDL2 for mingw cross-compile ──────────────────────────────────
# Downloaded from https://github.com/libsdl-org/SDL/releases (mingw dev package)
set(MINGW_SDL2_DIR "${CMAKE_SOURCE_DIR}/deps/SDL2-2.30.12/x86_64-w64-mingw32")
set(SDL2_FOUND TRUE)
set(SDL2_INCLUDE_DIRS "${MINGW_SDL2_DIR}/include;${MINGW_SDL2_DIR}/include/SDL2")

# Two SDL2 link modes:
# - Static (libSDL2.a) for standalone exe — no DLL needed
# - Dynamic (libSDL2.dll.a) for plugins — shares SDL2.dll with other plugins
set(SDL2_STATIC_LIBRARIES
    "${MINGW_SDL2_DIR}/lib/libSDL2.a"
    imm32 version winmm setupapi gdi32 ole32 oleaut32 uuid
)
set(SDL2_DYNAMIC_LIBRARIES
    "${MINGW_SDL2_DIR}/lib/libSDL2.dll.a"
    imm32 version winmm setupapi gdi32 ole32 oleaut32 uuid
)
set(SDL2_DLL_PATH "${MINGW_SDL2_DIR}/bin/SDL2.dll")

# Default: static (standalone builds use this)
set(SDL2_LIBRARIES ${SDL2_STATIC_LIBRARIES})

# ── OpenGL via mingw's opengl32 ────────────────────────────────────────────
set(OpenGL_FOUND TRUE)
set(OPENGL_FOUND TRUE)
set(OPENGL_gl_LIBRARY opengl32)
