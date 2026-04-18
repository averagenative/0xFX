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
- **Status**: done
- **Phase**: 4
- **Priority**: P1
- **Depends**: TASK-002 (KissFFT dep)
- **Notes**: Full overlap-add in cab_ir.c. KissFFT real-to-complex. Pre-computed IR FFT, per-block convolve.

### TASK-035: fx_cab_load_ir() API
- **Status**: done
- **Phase**: 4
- **Priority**: P1
- **Depends**: TASK-034
- **Notes**: Loads .wav via dr_wav. Handles 16/24/32-bit, mono/stereo downmix, 44.1/48kHz. Caps at 4096 samples.

### TASK-036: Synthetic IR generation
- **Status**: done
- **Phase**: 4
- **Priority**: P1
- **Depends**: TASK-034
- **Notes**: fx_cab_generate_ir() + fx_cab_load_buffer() + fx_cab_synth_ir_generate(). Parametric speaker/cabinet/mic. Minimum-phase reconstruction via cepstral method. 2048 samples at 48kHz.

### TASK-037: Generate bundled synthetic IRs
- **Status**: done
- **Phase**: 4
- **Priority**: P1
- **Depends**: TASK-036
- **Notes**: fx_cab_load_bundled() with 5 presets: 1x12 open, 2x12 closed, 4x12 straight, 4x12 slant, direct/flat. All tested.

---

## Phase 4B: Multi-Amp Routing

### TASK-039: Parallel chain routing
- **Status**: done
- **Phase**: 4
- **Priority**: P1
- **Depends**: TASK-024
- **Notes**: Already wired in engine.c. fx_chain_create/destroy, per-chain amp+cab, mix levels. test_parallel_chain_routing passes.

### TASK-040: Update process for multi-chain
- **Status**: done
- **Phase**: 4
- **Priority**: P1
- **Depends**: TASK-039
- **Notes**: Implemented in engine.c. Single-chain fast path + multi-chain split/mix. Global post-pedals after mix.

---

## Phase 4C: Post-Amp Studio Processing

### TASK-329: Research — post-amp studio processing
- **Status**: queued
- **Phase**: 4
- **Priority**: P1
- **Notes**: Research studio processors (compressors, EQ, limiters, tape saturation). Original names. Replace pedal menu in post-amp [+] with studio gear.

### TASK-330: Implement studio processors for post-amp chain
- **Status**: queued
- **Phase**: 4
- **Priority**: P1
- **Depends**: TASK-329
- **Notes**: Studio processor DSP. Post-amp [+] shows studio menu, pre-amp [+] stays pedals. Generate rack-mount PNG assets.

---

## Phase 5: Expanded Effects + Microphone Simulation

### TASK-326: Research — microphone profiles for cab simulation
- **Status**: queued
- **Phase**: 5
- **Priority**: P1
- **Notes**: Research SM57, e609, R-121, U87, etc. Original names. Frequency profiles. Placement params. OPTIONAL — DI default, mic opt-in.

### TASK-327: Implement microphone simulation DSP + engine API
- **Status**: queued
- **Phase**: 5
- **Priority**: P2
- **Depends**: TASK-326
- **Notes**: fx_mic_* API. Post-cab EQ filtering for mic response. Placement affects tone. Proximity effect. RT-safe.

### TASK-328: Generate microphone PNG assets
- **Status**: queued
- **Phase**: 5
- **Priority**: P2
- **Depends**: TASK-326
- **Notes**: PNG assets per mic model. Worn grime aesthetic. Show mic in front of cab.

> Additional tasks TASK-047 through TASK-065. See openspec/changes/fx-engine/tasks.md for full details.
> Will be expanded here as Phase 4 nears completion.

## Phase 12: 1.2.0 Audio Fixes

### TASK-365: Mic / input device not detected on native Linux (Fedora 43, PipeWire)
- **Status**: queued
- **Phase**: 12
- **Priority**: HIGH
- **Release**: 1.2.0
- **Depends**: none
- **Files**: `src/audio/audio_device.c`, `src/audio/audio_device.h`
- **Notes**: Reported 2026-04-18 running the 1.1.0 AppImage on native Fedora 43 (Dan is off WSL now). The CLAUDE.md note about WSLg audio being broken no longer applies — we have a real Linux audio stack, but mic input is not being detected. Likely culprits: miniaudio backend selection (PipeWire vs PulseAudio vs ALSA), or the device-enumeration path returning an empty input list.

  **Acceptance criteria**:
  - Running the 1.1.0 AppImage + native build on Fedora 43 enumerates input devices (PipeWire/PulseAudio).
  - USB interfaces (iRig, Scarlett) appear in the input device dropdown.
  - Selecting an input and arming a chain passes signal end-to-end (verified via meter / recording).
  - Investigate which miniaudio backend gets selected on Fedora 43; confirm it includes capture.
  - Add logging so a missing/empty capture list surfaces clearly in `fx_log` instead of silently failing in the UI.

  **First steps**: run the standalone with `FX_LOG_LEVEL=DEBUG` or add a one-shot `ma_context_get_devices` dump to see what miniaudio reports on this box. Compare against `pw-cli list-objects Node` to confirm PipeWire does see the mic.
