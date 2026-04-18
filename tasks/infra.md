# Infrastructure Tasks

> Build system, dependencies, CI/CD, packaging, plugin wrappers, presets, asset pipeline.
> Cross-cutting concerns that support engine and frontend work.

---

## Phase 1: Project Setup

### TASK-001: Create project directory structure
- **Status**: done
- **Phase**: 1
- **Priority**: P0
- **Depends**: none
- **Notes**: src/engine/, src/engine/internal/, src/audio/, src/gui/, src/standalone/, src/plugin/, deps/, resources/, tests/, tools/, presets/, themes/

### TASK-002: Vendor single-header dependencies
- **Status**: done
- **Phase**: 1
- **Priority**: P0
- **Depends**: TASK-001
- **Notes**: miniaudio.h, dr_wav.h, KissFFT, cJSON.h into deps/

### TASK-003: CMakeLists.txt with standalone target
- **Status**: done
- **Phase**: 1
- **Priority**: P0
- **Depends**: TASK-001
- **Notes**: `0xfx_standalone` target. Verify Linux + MinGW cross-compile.

---

## Phase 4C: Preset System

### TASK-042: Implement fx_preset_save()
- **Status**: done
- **Phase**: 4
- **Priority**: P1
- **Depends**: TASK-024 (engine - pedal chain wired)
- **Notes**: Full implementation in preset.c. Serializes gate, pre-pedals, chains (amp+cab+mix), post-pedals.

### TASK-043: Implement fx_preset_load()
- **Status**: done
- **Phase**: 4
- **Priority**: P1
- **Depends**: TASK-042
- **Notes**: Full implementation in preset.c. Validates format, clamps values, handles unknown pedal types gracefully.

### TASK-044: Create 5 default presets
- **Status**: done
- **Phase**: 4
- **Priority**: P1
- **Depends**: TASK-042
- **Notes**: 5 presets in presets/: clean_sparkle, classic_crunch, modern_high_gain, chimey_british, bluesy_tweed. All tested — load and produce audio.

---

## Phase 6: Plugin Wrappers (CLAP + VST3)

### TASK-067: Vendor CPLUG dependency
- **Status**: queued
- **Phase**: 6
- **Priority**: P2
- **Depends**: TASK-003
- **Notes**: Into deps/. Study CPLUG CLAP and VST3 examples.

### TASK-068: CLAP + VST3 CMake targets
- **Status**: queued
- **Phase**: 6
- **Priority**: P2
- **Depends**: TASK-067
- **Notes**: `0xfx_clap` and `0xfx_vst3` shared library targets.

### TASK-069: CLAP process callback
- **Status**: queued
- **Phase**: 6
- **Priority**: P2
- **Depends**: TASK-068
- **Notes**: Wrap fx_engine_process(). Buffer format: mono in → engine → stereo out.

### TASK-070: CLAP parameter interface
- **Status**: queued
- **Phase**: 6
- **Priority**: P2
- **Depends**: TASK-069
- **Notes**: Expose all params as automatable CLAP params. Thread-safe updates.

### TASK-072: VST3 plugin via CPLUG
- **Status**: queued
- **Phase**: 6
- **Priority**: P2
- **Depends**: TASK-068
- **Notes**: Same engine, same GUI, VST3 wrapper. Secondary — just verify it loads.

---

## Phase 7: Asset Generation Pipeline

### TASK-074: Image generation pipeline script
- **Status**: done
- **Phase**: 7
- **Priority**: P2
- **Depends**: none
- **Notes**: tools/generate_assets.py using OpenAI gpt-image-1 API. Style guide in tools/ASSET_STYLE_GUIDE.md. Commands: test, reference, pedal, amp, cab, texture.

### TASK-075: Generate style reference images
- **Status**: done
- **Phase**: 7
- **Priority**: P2
- **Depends**: TASK-074
- **Notes**: 3 reference images in resources/reference/. Pedal board, amp+cab, grime closeup. All consistent with style lock.

### TASK-076: LoRA / style consistency
- **Status**: done
- **Phase**: 7
- **Priority**: P2
- **Depends**: TASK-075
- **Notes**: Using prompt-based style lock instead of LoRA (OpenAI API doesn't support LoRA). Consistent results via shared style suffix, lighting, wear vocabulary. See ASSET_STYLE_GUIDE.md.

### TASK-077: Generate pedal assets (20+)
- **Status**: queued
- **Phase**: 7
- **Priority**: P2
- **Depends**: TASK-076
- **Notes**: Top-down, scuffed, transparent bg. PNGs in resources/pedals/.

### TASK-078: Generate amp panel assets (5)
- **Status**: queued
- **Phase**: 7
- **Priority**: P2
- **Depends**: TASK-076
- **Notes**: Front panel views. PNGs in resources/amps/.

### TASK-079: Generate cabinet assets (5+)
- **Status**: queued
- **Phase**: 7
- **Priority**: P2
- **Depends**: TASK-076
- **Notes**: Dusty, scuffed. PNGs in resources/cabs/.

### TASK-080: Generate theme textures
- **Status**: queued
- **Phase**: 7
- **Priority**: P2
- **Depends**: TASK-076
- **Notes**: Pedalboard surface, metal, leather, grime overlays.

### TASK-200: Generate knobless pedal body images
- **Status**: queued
- **Phase**: 7
- **Priority**: P2
- **Depends**: TASK-074
- **Notes**: Each pedal type gets a _body.png with empty knob mounting holes (no knobs rendered). Transparent bg via rembg. For composite rendering — knobs are placed as separate rotatable sprites by the GUI.

### TASK-201: Generate knobless amp panel images
- **Status**: queued
- **Phase**: 7
- **Priority**: P2
- **Depends**: TASK-074
- **Notes**: Each amp model gets a _body.png panel with bare knob posts (no knobs). Pilot light visible. For composite rendering.

---

## Phase 11: CI/CD + Packaging

### TASK-111: GitHub Actions CI
- **Status**: queued
- **Phase**: 11
- **Priority**: P3
- **Depends**: TASK-103 (testing)
- **Notes**: Matrix: Linux gcc + Windows MinGW. Build standalone + CLAP + VST3. Run tests.

### TASK-112: Windows packaging
- **Status**: queued
- **Phase**: 11
- **Priority**: P3
- **Depends**: TASK-111
- **Notes**: NSIS installer or portable zip.

### TASK-113: Linux packaging
- **Status**: queued
- **Phase**: 11
- **Priority**: P3
- **Depends**: TASK-111
- **Notes**: AppImage or tar.gz.

### TASK-114: Version stamping
- **Status**: queued
- **Phase**: 11
- **Priority**: P3
- **Depends**: TASK-003
- **Notes**: Git tag → binary version. Title bar + about dialog.

---

## Release 1.1.0 — Multi-Architecture Support

> ARM64 support across all platforms. Universal macOS, ARM64 Windows (Surface/Snapdragon laptops), ARM64 Linux (RPi/Pine64).

### TASK-349: macOS universal binary (arm64 + x86_64)
- **Status**: queued
- **Phase**: 9
- **Priority**: P1
- **Release**: 1.1.0
- **Depends**: TASK-344 (macOS plugin builds)
- **Notes**: Set `CMAKE_OSX_ARCHITECTURES="arm64;x86_64"` in CMakeLists.txt. Update package_macos.sh naming to `macos-universal`. Can be cross-compiled from Intel Mac — Xcode 12+ required. Verify with `lipo -info`.

### TASK-350: Windows ARM64 cross-compilation via llvm-mingw
- **Status**: queued
- **Phase**: 9
- **Priority**: P2
- **Release**: 1.1.0
- **Depends**: none
- **Notes**: llvm-mingw has native aarch64-w64-mingw32 support (better than GNU mingw for ARM). Need SDL2 ARM64 Windows dev libs. New toolchain file `cmake/llvm-mingw-arm64.cmake`. x86 emulation on ARM Windows works as fallback but native is preferred for audio latency. Install: `apt install llvm-mingw` or download from [llvm-mingw releases](https://github.com/mstorsjo/llvm-mingw).

### TASK-351: Linux ARM64 (aarch64) cross-compilation
- **Status**: queued
- **Phase**: 9
- **Priority**: P2
- **Release**: 1.1.0
- **Depends**: none
- **Notes**: Cross-compile toolchain `aarch64-linux-gnu-gcc`. Install: `apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu`. SDL2 cross-compile deps needed (build from source or use multiarch). Targets: RPi 4/5, Pine64, ARM Chromebooks, Linux ARM servers.

### TASK-353: macOS universal build + installer (run from MacBook)
- **Status**: queued
- **Phase**: 9
- **Priority**: P1
- **Release**: 1.1.0
- **Depends**: TASK-349 (CMake universal binary support)
- **Notes**: **Must be run on MacBook.** After TASK-349 lands the CMake changes, pull to MacBook and build: `cmake -B build -DCMAKE_OSX_ARCHITECTURES='arm64;x86_64' -DCMAKE_BUILD_TYPE=Release && cmake --build build -j$(sysctl -n hw.ncpu)`. Verify with `lipo -info build/0xfx_gui`. Package with `./scripts/packaging/package_macos.sh 1.1.0`. Test standalone + plugins on M-series. DMG should have drag-to-Applications layout.

### TASK-352: Multi-arch release packaging and naming
- **Status**: queued
- **Phase**: 9
- **Priority**: P2
- **Release**: 1.1.0
- **Depends**: TASK-349, TASK-350, TASK-351
- **Notes**: Unified release script that builds all arch variants. Naming: `0xFX-{ver}-{platform}-{arch}`. Platforms: `linux-x64`, `linux-arm64`, `windows-x64`, `windows-arm64`, `macos-universal`. Update CLAUDE.md with ARM build commands.

### TASK-364: Linux AppImage packaging for 1.1.0 release
- **Status**: queued
- **Phase**: 11
- **Priority**: HIGH
- **Release**: 1.1.0
- **Depends**: none
- **Files**: `scripts/packaging/package_release.sh`, `scripts/packaging/package_appimage.sh`
- **Notes**: AppImage was missing from 1.1.0. Sibling project 0x808 AppImage hits `dlopen(): error loading libfuse.so.2` on modern distros — FUSE 2 is not installed by default on Fedora 38+, Ubuntu 22.04+, etc.

  **FUSE issue fix**: Use the fuse3-compatible runtime instead of the default fuse2 runtime:
  ```bash
  # Download fuse3 runtime (replaces default fuse2 runtime)
  wget https://github.com/AppImage/type2-runtime/releases/latest/download/runtime-fuse3-x86_64
  appimagetool --runtime-file runtime-fuse3-x86_64 AppDir 0xFX-1.1.0-x86_64.AppImage
  ```

  **End-user workaround** (document in release notes for systems without any FUSE):
  ```bash
  APPIMAGE_EXTRACT_AND_RUN=1 ./0xFX-1.1.0-x86_64.AppImage
  ```

  **Acceptance criteria**:
  - AppImage for x86_64 built via `linuxdeploy` + `appimagetool` with fuse3 runtime
  - AppImage for aarch64 built similarly (cross or native)
  - Both added to `release/` and uploaded via `gh release upload`
  - `package_release.sh` updated to include AppImage step
  - Release notes include FUSE note + `APPIMAGE_EXTRACT_AND_RUN=1` workaround
