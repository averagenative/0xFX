# Engine Tasks (Layers 1-2)

> Engine API, DSP processing, amp models, effects, cabinet IR, audio device manager.
> All code under `src/engine/`, `src/engine/internal/`, `src/audio/`.

---

## Phase 1: Hello Tone — Audio I/O + Passthrough

### TASK-004: Define public API header `fx_engine.h`
- **Status**: done
- **Phase**: 1
- **Priority**: P0
- **Depends**: TASK-001 (infra)
- **Notes**: Opaque `fx_engine_t*` handle, `create/destroy/process`. No internal structs exposed.

### TASK-005: Implement engine internals (passthrough)
- **Status**: done
- **Phase**: 1
- **Priority**: P0
- **Depends**: TASK-004
- **Notes**: Private struct behind opaque handle. `fx_engine_process()` copies input→output.

### TASK-006: Audio device manager
- **Status**: done
- **Phase**: 1
- **Priority**: P0
- **Depends**: TASK-002 (infra)
- **Notes**: miniaudio device enumeration, selection, hot-plug. `fx_audio_get_device_count()`, `fx_audio_get_device_name()`, `fx_audio_set_device()`.

### TASK-007: Standalone main with audio callback
- **Status**: done
- **Phase**: 1
- **Priority**: P0
- **Depends**: TASK-005, TASK-006
- **Notes**: Initialize miniaudio device, audio callback calls `fx_engine_process()`. Print detected devices.

---

## Phase 2: It Has an Amp

### TASK-009: Define amp model types in API
- **Status**: done
- **Phase**: 2
- **Priority**: P0
- **Depends**: TASK-004
- **Notes**: `fx_amp_type_t` enum: FULLERTON_CLEAN, BRIT_CRUNCH, SOUTHWEST_LEAD, ESSEX_CHIME, TWEED_BLUES. Per-chain API.

### TASK-010: Implement preamp gain stages
- **Status**: done
- **Phase**: 2
- **Priority**: P0
- **Depends**: TASK-009
- **Notes**: Cascaded waveshaping (1-4 stages). tanh, x/(1+|x|), atan, hard clip per model. Per-model configs in amp_configs[].

### TASK-011: Implement tone stack EQ
- **Status**: done
- **Phase**: 2
- **Priority**: P0
- **Depends**: TASK-010
- **Notes**: 3-band biquad EQ (lowshelf/peak/highshelf) + presence. Per-model frequencies and range. Cached recalc on param change.

### TASK-012: Implement power amp stage
- **Status**: done
- **Phase**: 2
- **Priority**: P0
- **Depends**: TASK-010
- **Notes**: Envelope follower compression (2:1 soft ratio), sag simulation with attack/release, per-model threshold.

### TASK-013: Implement noise gate
- **Status**: done
- **Phase**: 2
- **Priority**: P0
- **Depends**: TASK-005
- **Notes**: Envelope follower, threshold/attack/release/hold, smooth gain transitions. Wired at input stage.

### TASK-014: Wire amp + noise gate into process chain
- **Status**: done
- **Phase**: 2
- **Priority**: P0
- **Depends**: TASK-010, TASK-011, TASK-012, TASK-013
- **Notes**: Signal flow: input → gate → pre-pedals → amp(preamp→tone→power) → cab → post-pedals → output. Multi-chain mixing works.

### TASK-015: Amp param introspection API
- **Status**: done
- **Phase**: 2
- **Priority**: P0
- **Depends**: TASK-009
- **Notes**: `fx_amp_get_param_count()`, `fx_amp_get_param_name()`. Required for GUI knob rendering.

---

## Phase 3A: Core Effects

### TASK-017: Pedal plugin interface
- **Status**: done
- **Phase**: 3
- **Priority**: P0
- **Depends**: TASK-005
- **Notes**: `fx_pedal_type_t` enum, add/remove/move/set_param/bypass API. Internal dispatch table.

### TASK-018: Port overdrive → jade_drive
- **Status**: done
- **Phase**: 3
- **Priority**: P0
- **Depends**: TASK-017
- **Notes**: Mid-hump soft-clip x/(1+|x|), pre-HP at 720Hz, mid peak at 1kHz, variable tone LP. 3 params: drive/tone/level.

### TASK-019: Implement gold_drive (transparent OD)
- **Status**: done
- **Phase**: 3
- **Priority**: P0
- **Depends**: TASK-017
- **Notes**: Clean blend mixed with tanh-clipped signal. More gain = more wet. Treble shelf. 3 params: gain/treble/output.

### TASK-020: Implement rodent (distortion)
- **Status**: done
- **Phase**: 3
- **Priority**: P0
- **Depends**: TASK-017
- **Notes**: Op-amp hard clip with slight asymmetry, backwards filter (high=dark). 3 params: distortion/filter/volume.

### TASK-021: Port delay → echo_delay
- **Status**: done
- **Phase**: 3
- **Priority**: P0
- **Depends**: TASK-017
- **Notes**: Circular buffer, 20-1000ms time, feedback capped at 0.9, dry/wet mix. 4 params.

### TASK-022: Port reverb → hall_verb
- **Status**: done
- **Phase**: 3
- **Priority**: P0
- **Depends**: TASK-017
- **Notes**: Simplified Freeverb: 4 comb (LP damping) + 2 allpass (diffusion). 3 params: decay/damping/mix.

### TASK-023: Port compressor → squeeze_box
- **Status**: done
- **Phase**: 3
- **Priority**: P0
- **Depends**: TASK-017
- **Notes**: OTA-style, envelope follower, 4:1 ratio, 1ms attack / 100ms release. 2 params: output/sensitivity.

### TASK-024: Wire pedal chain into process
- **Status**: done
- **Phase**: 3
- **Priority**: P0
- **Depends**: TASK-017, TASK-014
- **Notes**: Done in other session. Pre-pedals → amp → cab → post-pedals. Multi-chain with per-chain mixing.

---

## Phase 4A: Cabinet IR

### TASK-034: Overlap-add FFT convolution
- **Status**: queued
- **Phase**: 4
- **Priority**: P1
- **Depends**: TASK-002 (KissFFT dep)
- **Notes**: Load .wav IR, zero-pad, pre-compute IR FFT, per-block convolve.

### TASK-035: fx_cab_load_ir() API
- **Status**: queued
- **Phase**: 4
- **Priority**: P1
- **Depends**: TASK-034
- **Notes**: Load .wav IR files (44.1/48 kHz, 16/24/32-bit, mono, up to 4096 samples).

### TASK-036: Synthetic IR generation
- **Status**: queued
- **Phase**: 4
- **Priority**: P1
- **Depends**: TASK-034
- **Notes**: fx_cab_generate_ir() from parametric description. Speaker Thiele-Small, cabinet type, mic model.

### TASK-037: Generate bundled synthetic IRs
- **Status**: queued
- **Phase**: 4
- **Priority**: P1
- **Depends**: TASK-036
- **Notes**: 5 IRs: 1x12 open, 2x12 closed, 4x12 straight, 4x12 slant, direct.

---

## Phase 4B: Multi-Amp Routing

### TASK-039: Parallel chain routing
- **Status**: queued
- **Phase**: 4
- **Priority**: P1
- **Depends**: TASK-024
- **Notes**: fx_chain_create() adds parallel amp+cab+post-fx chain. Mix per chain. Max 4.

### TASK-040: Update process for multi-chain
- **Status**: queued
- **Phase**: 4
- **Priority**: P1
- **Depends**: TASK-039
- **Notes**: pre-pedals → split → per-chain (amp→cab→post-fx) → sum with mix → output.

---

## Phase 5: Expanded Effects

> Tasks TASK-047 through TASK-065. See openspec/changes/fx-engine/tasks.md for full details.
> Will be expanded here as Phase 4 nears completion.
