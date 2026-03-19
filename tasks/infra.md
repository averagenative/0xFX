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
- **Status**: queued
- **Phase**: 4
- **Priority**: P1
- **Depends**: TASK-024 (engine - pedal chain wired)
- **Notes**: Serialize complete rig to .0xfx JSON. Input, pre-pedals, chains, output, metadata.

### TASK-043: Implement fx_preset_load()
- **Status**: queued
- **Phase**: 4
- **Priority**: P1
- **Depends**: TASK-042
- **Notes**: Parse .0xfx JSON, reconstruct engine state. Validate + clamp values.

### TASK-044: Create 5 default presets
- **Status**: queued
- **Phase**: 4
- **Priority**: P1
- **Depends**: TASK-042
- **Notes**: Clean Sparkle, Classic Crunch, Modern High Gain, Chimey British, Bluesy Tweed.

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
- **Status**: queued
- **Phase**: 7
- **Priority**: P2
- **Depends**: none
- **Notes**: tools/generate_assets.py. Replicate API + Flux Pro.

### TASK-075: Generate style reference images
- **Status**: queued
- **Phase**: 7
- **Priority**: P2
- **Depends**: TASK-074
- **Notes**: 3-5 "worn grime" reference images.

### TASK-076: Train LoRA on Replicate
- **Status**: queued
- **Phase**: 7
- **Priority**: P2
- **Depends**: TASK-075
- **Notes**: Fine-tune Flux Pro. ~$5. Document model ID + seeds.

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
