# 0xFX — Implementation Tasks

> Ordered by value delivered. Each phase produces working software you can demo.
> Priority mapping from proposal: P0 → Phases 1-3, P1 → Phases 4-5, P2 → Phases 6-8, P3 → Phases 9-11
> Naming: All pedal/amp/cab names use original creative names (Line 6 Helix pattern). No trademarks in code or UI.

---

## Phase 1: "Hello Tone" — Build System + Audio I/O + Passthrough
**Delivers**: A binary that compiles, detects your audio interface, and passes guitar signal through to output (clean, no processing).
**Why first**: Nothing else matters until audio flows. This proves the toolchain, audio device manager, and real-time audio callback work. Plug in your iRig or Scarlett, hear yourself.
**Specs covered**: `fx-engine-api` (partial), `audio-device-manager` (partial)

- [ ] TASK-001: Create project directory structure — `src/engine/`, `src/engine/internal/`, `src/audio/`, `src/gui/`, `src/standalone/`, `src/plugin/`, `deps/`, `resources/`, `tests/`, `tools/`, `presets/`, `themes/`
- [ ] TASK-002: Download and vendor single-header dependencies into `deps/` — miniaudio.h, dr_wav.h, KissFFT, cJSON.h
- [ ] TASK-003: Create `CMakeLists.txt` with `0xfx_standalone` target. Verify compiles on Linux + MinGW cross-compile.
- [ ] TASK-004: Define public API header `src/engine/fx_engine.h` — opaque `fx_engine_t*` handle, `fx_engine_create()`, `fx_engine_destroy()`, `fx_engine_process(engine, in, out, frames)`. No internal structs exposed.
- [ ] TASK-005: Implement `src/engine/internal/engine_impl.c` — private struct behind opaque handle. `fx_engine_create()` allocates, `fx_engine_process()` copies input to output (passthrough).
- [ ] TASK-006: Implement `src/audio/audio_device.c` — miniaudio device enumeration, selection, hot-plug detection. Public API: `fx_audio_get_device_count()`, `fx_audio_get_device_name()`, `fx_audio_set_device()`.
- [ ] TASK-007: Implement `src/standalone/main.c` — initialize miniaudio device with audio callback that calls `fx_engine_process()`. Print detected devices to console, select first input+output device.
- [ ] TASK-008: **Milestone test** — binary compiles, enumerates audio devices (iRig/Scarlett detected by name), guitar input passes through to output with no processing. Latency feels acceptable. Offline test: process a WAV file through the passthrough engine, verify output matches input.

## Phase 2: "It Has an Amp" — Amp Models + Noise Gate
**Delivers**: Guitar signal through a selectable amp model. Plug in, select an amp, hear your guitar through a crunch tone with noise gate.
**Why next**: Amp modeling is the core value proposition. Without it, this is just another effects processor.
**Specs covered**: `amp-models`, `noise-gate`

- [ ] TASK-009: Define amp model types in public API — `fx_amp_type_t` enum: `FX_AMP_FULLERTON_CLEAN`, `FX_AMP_BRIT_CRUNCH`, `FX_AMP_SOUTHWEST_LEAD`, `FX_AMP_ESSEX_CHIME`, `FX_AMP_TWEED_BLUES`. Per-chain API: `fx_amp_set_model()`, `fx_amp_set_param()`, `fx_amp_get_param()`.
- [ ] TASK-010: Implement preamp gain stages in `src/engine/internal/amp.c` — cascaded waveshaping (1-4 stages per model). Each stage: input gain → waveshaper → coupling capacitor highpass. Waveshapers: `tanh(x)` (clean/crunch), `x/(1+|x|)` (asymmetric), `atan(x)` (moderate), hard clip (high gain).
- [ ] TASK-011: Implement tone stack EQ per amp model — biquad filter chains. Fullerton: scooped mids. Brit: pronounced mids. Southwest: 5-band contour. Essex: treble/bass with cut. Tweed: bass/treble passive.
- [ ] TASK-012: Implement power amp stage — soft compression (output transformer saturation), presence/resonance controls, sag (supply voltage droop).
- [ ] TASK-013: Implement noise gate in `src/engine/internal/noise_gate.c` — threshold, attack, release, hold params. Envelope follower with hysteresis.
- [ ] TASK-014: Wire amp + noise gate into `fx_engine_process()` — signal flow: input → noise gate → amp model → output.
- [ ] TASK-015: Add `fx_amp_get_param_count()` and `fx_amp_get_param_name()` to API — each amp model exposes its specific knob set (gain, volume, bass, mid, treble, presence, sag, etc.). Required for GUI knob rendering.
- [ ] TASK-016: **Milestone test** — select Brit Crunch amp, process a guitar DI WAV through it, output has audible distortion character. Switch to Fullerton Clean, output is clean with headroom. Noise gate silences input below threshold. Sweep gain knob from 0→1, hear progressive breakup.

## Phase 3: "Pedalboard" — Effects + Signal Chain + Drag-and-Drop [MVP]
**Delivers**: A visual pedalboard with amp and pedals. Drag pedals to reorder, turn knobs, hear changes live. This is the MVP.
**Why next**: The pedalboard + amp + knobs are the core interaction. After this phase, you have a usable instrument.
**Specs covered**: `overdrive`, `distortion`, `signal-chain-routing` (single chain), `fx-engine-api` (pedal API)

### 3A: Core Effects (Port from 0x808)

- [ ] TASK-017: Implement pedal plugin interface — `fx_pedal_type_t` enum, `fx_chain_add_pedal()`, `fx_chain_remove_pedal()`, `fx_chain_move_pedal()`, `fx_pedal_set_param()`, `fx_pedal_get_param()`, `fx_pedal_set_bypass()`. Internal dispatch table maps type → process/init/destroy functions.
- [ ] TASK-018: Port overdrive from 0x808 → `jade_drive` — soft-clip `x/(1+|x|)`, mid-hump EQ, drive/tone/level params. Adapt `sq_` prefix to `fx_`, wrap in pedal interface.
- [ ] TASK-019: Implement `gold_drive` (transparent OD) — clean blend mixed with soft-clipped signal, treble/gain/output params.
- [ ] TASK-020: Implement `rodent` (distortion) — op-amp hard clip with backwards filter (high → low), distortion/filter/volume params.
- [ ] TASK-021: Port delay from 0x808 — circular buffer, time/feedback/mix, BPM sync. Rename to `echo_delay` (digital delay).
- [ ] TASK-022: Port reverb from 0x808 — Freeverb (8 comb + 4 allpass), room_size/damping/mix. Rename to `hall_verb`.
- [ ] TASK-023: Port compressor from 0x808 — peak-detecting, threshold/ratio/attack/release/makeup. Rename to `squeeze_box`.
- [ ] TASK-024: Wire pedal chain into `fx_engine_process()` — input → noise gate → pre-pedals (in order) → amp → post-pedals (in order) → output.

### 3B: GUI — ImGui Pedalboard + Amp Panel + Knobs

- [ ] TASK-025: Vendor ImGui + SDL2+OpenGL backend into `deps/`. Add to CMakeLists.txt.
- [ ] TASK-026: Implement `src/gui/gui_main.c` — SDL2 window (1400×800), OpenGL context, ImGui initialization, main render loop with frame timing.
- [ ] TASK-027: Implement `src/gui/knobs.c` — custom ImGui rotary knob widget: click-drag to rotate, visual arc indicator, value label, double-click reset, shift+drag fine adjust. Port from 0x808 knob code, adapt for ImGui.
- [ ] TASK-028: Implement `src/gui/amp_panel.c` — skeuomorphic amp front panel. Turnable knobs for gain, volume, bass, mid, treble, presence, sag. Amp model selector dropdown. Knob layout changes per amp type (Fullerton has reverb knob, Brit has pre/master gain, etc.). All knobs call `fx_amp_set_param()`.
- [ ] TASK-029: Implement `src/gui/pedalboard.c` — horizontal row of pedal slots (pre-amp and post-amp sections). Each pedal rendered as a stomp box with knobs and bypass LED. "+" button to add pedal (type selector popup). Click knobs to adjust. Bypass toggle per pedal.
- [ ] TASK-030: Implement drag-and-drop pedal reordering — click-hold a pedal, drag to new position, visual ghost shows insertion point, release calls `fx_chain_move_pedal()`. Signal chain updates in real-time (no audio glitch).
- [ ] TASK-031: Implement audio device selector in GUI — settings dropdown showing detected interfaces. Select device calls `fx_audio_set_device()`. Buffer size and sample rate selectors.
- [ ] TASK-032: Implement tuner display in toolbar — `fx_tuner_get_frequency()`, note name + cents bar (-50 to +50). Always-on, updates ~30Hz.
- [ ] TASK-033: **Milestone test** — launch app, see pedalboard + amp panel. Add Jade Drive to pre-amp chain, turn drive knob, hear overdrive increase. Drag pedal to post-amp position. Switch amp model, hear tone change. Turn amp gain knob, hear breakup. Tuner shows pitch. This is the MVP demo.

## Phase 4: "Full Rig" — Cabinet IR + Multi-Amp + Presets
**Delivers**: Complete guitar rig: pedals → amp → cab IR → output. Multi-amp splitting. Save/load rig presets in .0xfx JSON format.
**Why next**: Amp without cab sounds terrible. Multi-amp routing and presets complete the core experience.
**Specs covered**: `cabinet-ir`, `signal-chain-routing` (multi-chain), `preset-system`, `open-rig-format`

### 4A: Cabinet IR Convolution

- [ ] TASK-034: Implement overlap-add FFT convolution in `src/engine/internal/cab_ir.c` — load .wav IR, zero-pad to power of 2, pre-compute IR FFT, per-block: FFT input → complex multiply → IFFT → overlap-add. Use KissFFT.
- [ ] TASK-035: Implement `fx_cab_load_ir()` API — load user .wav IR files (44.1/48 kHz, 16/24/32-bit, mono, up to 4096 samples). Resample if needed.
- [ ] TASK-036: Implement synthetic IR generation — `fx_cab_generate_ir()` from `fx_cab_params_t`: speaker type (Thiele-Small Fs/Qts), cabinet type (open/closed), mic type + position. Frequency domain design → minimum-phase reconstruction → IFFT → 2048-sample WAV.
- [ ] TASK-037: Generate 5 bundled synthetic IRs — 1x12 open back, 2x12 closed, 4x12 straight, 4x12 slant, direct (flat). Save as .wav in `resources/cabs/`. Zero licensing concerns.
- [ ] TASK-038: Implement cab IR panel in GUI — cab selector dropdown (bundled + user-loaded), "Load IR..." file browser button, IR waveform display, bypass toggle.

### 4B: Multi-Amp Signal Routing

- [ ] TASK-039: Implement parallel chain routing — `fx_chain_create()` adds a parallel amp+cab+post-fx chain. `fx_chain_set_mix()` controls per-chain blend. Default: 1 chain. Max: 4 chains.
- [ ] TASK-040: Update `fx_engine_process()` for multi-chain — pre-pedals → split → per-chain (amp → cab → post-fx) → sum with mix levels → output.
- [ ] TASK-041: Implement multi-chain GUI — visual splitter node in signal flow. Each chain rendered as its own amp+cab column. Mix/blend slider per chain. "Add Chain" / "Remove Chain" buttons.

### 4C: Preset System (.0xfx JSON)

- [ ] TASK-042: Implement `fx_preset_save()` — serialize complete rig to .0xfx JSON: input settings, pre-pedal chain (type + params + order), all amp+cab chains (model + params + IR path + post-fx), output, metadata (name, author, tags).
- [ ] TASK-043: Implement `fx_preset_load()` — parse .0xfx JSON, reconstruct engine state. Validate all fields, clamp out-of-range values.
- [ ] TASK-044: Create 5 default presets — "Clean Sparkle" (Fullerton + spring verb), "Classic Crunch" (Brit + Jade Drive), "Modern High Gain" (Southwest + gate + tight delay), "Chimey British" (Essex + chorus), "Bluesy Tweed" (Tweed + Gold Drive).
- [ ] TASK-045: Implement preset browser in GUI — preset list panel, click to load, right-click to save-as. Preset name + tags shown.
- [ ] TASK-046: **Milestone test** — load "Classic Crunch" preset, hear guitar through Jade Drive → Brit Crunch amp → 4x12 cab. Load a user .wav IR, cab character changes. Save rig as new preset, reload it, verify identical sound. Split to dual amp (Brit + Fullerton), blend 50/50, hear combined tone.

## Phase 5: "Sounds Like a Record" — Expanded Effects Library
**Delivers**: Full effect palette — spring reverb, tape delay, wah, fuzz, modulation, EQ. Covers what guitarists expect.
**Why next**: The core rig works (Phase 4). Now fill out the pedalboard with the effects that make it musically complete.
**Specs covered**: `delay` (expanded), `reverb` (expanded), `wah`, `fuzz`, `chorus`, `phaser`, `flanger`, `tremolo`, `eq`, `compressor` (expanded)

### 5A: Reverb Variants

- [ ] TASK-047: Implement spring reverb (`drip_verb`) — allpass diffusion + comb filter network with "drip" transient response. Dwell/mix/tone params. Essential guitar reverb.
- [ ] TASK-048: Implement plate reverb (`plate_verb`) — dense diffusion network, smooth tail. Decay/mix/damping params.
- [ ] TASK-049: Port shimmer reverb from 0x808 — octave-up pitch shift in feedback loop. Decay/shimmer/mix params. Rename to `shimmer_verb`.

### 5B: Delay Variants

- [ ] TASK-050: Implement analog delay (`carbon_delay`) — BBD-style: repeats darken progressively (LP filter in feedback), optional modulation (chorus on repeats). Time/feedback/tone/mod/mix params.
- [ ] TASK-051: Implement tape echo (`tape_machine`) — wow/flutter (LFO on delay time), tape degradation (saturation + HP/LP roll-off per repeat), multi-head option. Time/feedback/wow/flutter/age/mix params.
- [ ] TASK-052: Implement modulated delay (`memory_echo`) — chorus/vibrato on delay repeats. Time/feedback/mod_rate/mod_depth/mix params.

### 5C: Wah / Filter / EQ (New to 0xFX)

- [ ] TASK-053: Implement expression wah (`howl_wah`) — bandpass sweep controlled by expression value (0-1). Frequency range, Q, volume params. GUI renders as a rocker pedal.
- [ ] TASK-054: Implement auto-wah / envelope filter (`quack_filter`) — envelope follower drives bandpass cutoff. Sensitivity/decay/Q/mode(LP/BP/HP) params.
- [ ] TASK-055: Implement graphic EQ (`tone_sculptor`) — 7-band (100Hz–6.4kHz) or 10-band (31Hz–16kHz), per-band gain slider, output level. GUI renders as a row of sliders.
- [ ] TASK-056: Implement parametric EQ (`precision_eq`) — 3-band fully parametric, per-band freq/gain/Q, plus boost.

### 5D: Fuzz Variants (New to 0xFX)

- [ ] TASK-057: Port fuzz from 0x808 → `mammoth_fuzz` — 4-stage clipping, scooped mids. Sustain/tone/volume params.
- [ ] TASK-058: Implement germanium fuzz (`round_fuzz`) — smooth, cleans up with guitar volume. Fuzz/volume params + ge/si toggle.
- [ ] TASK-059: Implement gated fuzz (`chaos_fuzz`) — sputtery gated decay, oscillation control. Volume/gate/drive/stab params.

### 5E: Modulation (Port from 0x808 + Expand)

- [ ] TASK-060: Port chorus from 0x808 → `liquid_chorus` — BBD-style modulated delay. Rate/depth/mix params.
- [ ] TASK-061: Port phaser from 0x808 → `phase_sweep` — 6-stage allpass + LFO. Rate/depth/feedback/mix params.
- [ ] TASK-062: Port flanger from 0x808 → `jet_flanger` — short modulated delay. Add through-zero mode. Rate/depth/feedback/mix params.
- [ ] TASK-063: Port tremolo from 0x808 → `pulse_trem` — amplitude LFO. Add harmonic tremolo mode (separate high/low frequency rates). Rate/depth/wave params.
- [ ] TASK-064: Implement true vibrato (`drift_vibrato`) — pitch modulation only (no amplitude). Rate/depth params. Distinct from chorus.

### 5F: Additional Utility

- [ ] TASK-065: Port bitcrusher from 0x808 → `grit_crush`. Port ring mod → `ring_tone`. Port tape saturation → `warm_tape`.
- [ ] TASK-066: **Milestone test** — build a complex rig: Howl Wah → Jade Drive → Brit Crunch → 4x12 cab → Carbon Delay → Drip Verb. Sweep wah, hear filter sweep. Bypass individual pedals, hear each drop out cleanly. Save as preset, reload, verify.

## Phase 6: "Works in Bitwig" — CLAP + VST3 Plugins
**Delivers**: CLAP plugin (first-class) and VST3 plugin loadable in Reaper, Bitwig, and other DAWs.
**Why next**: Plugin format extends reach to DAW users. CLAP first, then VST3.
**Specs covered**: `clap-plugin`, `vst3-plugin`

- [ ] TASK-067: Vendor CPLUG dependency into `deps/`. Study CPLUG CLAP and VST3 examples.
- [ ] TASK-068: Add `0xfx_clap` and `0xfx_vst3` CMake targets. Shared library builds with CPLUG linking.
- [ ] TASK-069: Implement `src/plugin/plugin_clap.c` — CLAP process callback wrapping `fx_engine_process()`. Buffer format adaptation (mono input → engine → stereo output).
- [ ] TASK-070: Implement CLAP parameter interface — expose all amp params + pedal params as automatable CLAP parameters. Thread-safe parameter updates.
- [ ] TASK-071: Implement GUI embedding for CLAP — ImGui+SDL2 rendering surface inside host-provided native window. Pedalboard + amp panel + cab selector render correctly in plugin context.
- [ ] TASK-072: Implement VST3 plugin via CPLUG — same engine, same GUI, VST3 wrapper. Secondary priority — just verify it loads and passes audio.
- [ ] TASK-073: **Milestone test** — load CLAP in Reaper, see pedalboard GUI, play guitar through it, automate gain knob, hear smooth automation. Load VST3 in same project, verify it also works.

## Phase 7: "Make It Pretty" — Visual Assets + Theme + Aesthetic
**Delivers**: The "worn grime" visual identity. AI-generated pedal/amp/cab graphics. Cohesive dark theme with texture.
**Why next**: Functional MVP exists. Now make it look and feel like a real instrument, not a developer prototype.
**Specs covered**: `asset-generation`

- [ ] TASK-074: Set up image generation pipeline — Python script in `tools/generate_assets.py`. Replicate API integration with Flux Pro. LoRA training workflow documented.
- [ ] TASK-075: Generate 3-5 style reference images — establish the "worn grime" 0xFX aesthetic: beat-up, scuffed, dirty pedals/amps under dark moody lighting.
- [ ] TASK-076: Train LoRA on Replicate — fine-tune Flux Pro on reference images for style consistency. Document the trained model ID and seed values.
- [ ] TASK-077: Generate pedal assets (20+) — one image per pedal type using templated prompts. Scuffed enclosures, worn knobs, scratched faceplates. Top-down view, transparent background where possible. Save as PNGs in `resources/pedals/`.
- [ ] TASK-078: Generate amp panel assets (5) — one per amp model. Aged tolex, dusty knobs, yellowed controls, road-worn look. Front panel view. Save in `resources/amps/`.
- [ ] TASK-079: Generate cabinet assets (5+) — one per bundled cab. Dusty grille cloth, scuffed corners, stage-used look. Save in `resources/cabs/`.
- [ ] TASK-080: Generate theme textures — dark pedalboard surface, grungy metal textures, worn leather backgrounds, industrial grime overlays. For GUI panel backgrounds and borders.
- [ ] TASK-081: Implement themed GUI — apply generated textures as ImGui window backgrounds. Dark color scheme with warm accent colors (amber LEDs, rust highlights). Consistent "worn grime" feel throughout.
- [ ] TASK-082: Integrate pedal/amp/cab graphics into GUI — pedalboard renders actual pedal images (not just colored rectangles). Amp panel shows the generated amp face. Cab selector shows cab images.
- [ ] TASK-083: **Milestone test** — launch app, visual impression is cohesive "gigging musician's pedalboard." All pedals have unique worn graphics. Amps look like aged vintage gear. Theme feels dark and textured, not clinical.

## Phase 8: "Pitch and Space" — Octave, Pitch, Looper, Experimental
**Delivers**: Pitch-shifting effects, looper, freeze, and experimental/ambient effects.
**Why next**: P2 specialty effects that round out the pedalboard for advanced users.
**Specs covered**: `octaver`, `looper`, `freeze`, `granular`

- [ ] TASK-084: Implement polyphonic octave (`octave_engine`) — sub-octave and octave-up tracking. Polyphonic (works on chords). Sub/dry/up level params.
- [ ] TASK-085: Implement pitch bend (`pitch_warp`) — expression-controlled pitch shifting (-2 to +2 octaves). Harmonizer intervals (3rd, 5th, octave). Shift/mix params.
- [ ] TASK-086: Implement looper (`loop_station`) — record/overdub/play/stop/undo. Reverse and half-speed playback. Up to 5 minutes. Pre-allocated buffer, no malloc in audio path.
- [ ] TASK-087: Implement freeze (`infinite_hold`) — capture current audio frame, loop indefinitely as a drone. Momentary and latch modes. Decay/rise params.
- [ ] TASK-088: Implement granular delay (`grain_cloud`) — chop audio into grains, manipulate pitch/density/size. Time/pitch/density/mix params.
- [ ] TASK-089: Implement ambient reverb (`cloud_verb`) — infinite decay mode, modulated tails, pad-like sustain. Decay/filter/mix/sustain params.
- [ ] TASK-090: **Milestone test** — octave pedal tracks chords cleanly. Looper records, overdubs, undoes. Freeze holds a chord as a drone. Granular produces textural effects.

## Phase 9: "Open Ecosystem" — Import/Export + Custom Pedals + NAM
**Delivers**: Import NAM amp profiles, user-designed custom pedals (JSON), IR metadata. Community sharing via .0xfx format.
**Why next**: Opens the platform to community contributions and interop with other tools.
**Specs covered**: `open-rig-format` (expanded)

- [ ] TASK-091: Implement NAM (.nam) profile importer — parse NAM JSON + weights, load neural network model for inference. Use RTNeural or custom inference engine. `fx_preset_import_nam()` API.
- [ ] TASK-092: Implement custom pedal loader — `fx_pedal_load_custom()` reads JSON pedal definition specifying DSP topology (waveshaper type, filter chain, modulation routing), parameter ranges, defaults, and display names. Community-shareable pedal designs.
- [ ] TASK-093: Implement IR metadata sidecar — JSON file alongside .wav IR containing mic type, cab type, speaker model, mic position, capture notes. Display metadata in cab selector.
- [ ] TASK-094: Implement preset export/import UI — "Share Rig" button exports .0xfx JSON. "Import Rig" loads from file. Drag-and-drop .0xfx files onto window.
- [ ] TASK-095: **Milestone test** — import a NAM amp capture, hear it in the signal chain alongside built-in amps. Load a community-designed custom pedal from JSON. Import a .wav IR with metadata sidecar, see mic/cab info in selector.

## Phase 10: "AI Tone Matcher" — BYOK Agentic Tone Matching (Later Phase)
**Delivers**: "Match this tone" feature. Upload audio/URL, AI configures a matching rig.
**Why now**: Major differentiator, but needs significant design work. BYOK model (user's own LLM API key).
**Specs covered**: `ai-tone-match`

- [ ] TASK-096: Design tone matching architecture — define spectral analysis pipeline (FFT → feature extraction → tone fingerprint). Define LLM prompt format for parameter mapping. Design IPC protocol between engine and out-of-process agent.
- [ ] TASK-097: Implement spectral analysis in C — extract frequency response envelope, harmonic content ratios, dynamic envelope (attack/sustain/release character), noise floor level. Output: JSON tone fingerprint.
- [ ] TASK-098: Implement BYOK API key configuration — settings panel for LLM provider (Anthropic/OpenAI/etc.) + API key. Store in user config (not in preset files). Never log or transmit keys except to configured provider.
- [ ] TASK-099: Implement tone matching agent — out-of-process (Python or similar). Receives tone fingerprint + available amps/effects/params. Calls LLM API with structured prompt. Returns rig configuration as .0xfx JSON.
- [ ] TASK-100: Implement YouTube audio extraction — download audio from URL (yt-dlp or similar), extract relevant snippet, feed to spectral analysis.
- [ ] TASK-101: Integrate into GUI — "Match Tone" button. File picker / URL input / mic capture toggle. Progress indicator (async). "Apply" button loads matched rig. "Save as Preset" to keep it.
- [ ] TASK-102: **Milestone test** — upload a blues guitar clip, AI configures a rig (amp model, gain, EQ, effects). A/B the original and matched tone — recognizably similar character.

## Phase 11: "Ship It" — Polish, Testing, Release
**Delivers**: Cross-platform builds, installer, documentation, competitive parity review.
**Specs covered**: All remaining polish

### 11A: Testing & Quality

- [ ] TASK-103: Create API test suite — headless tests that create engine, add pedals, process audio, verify output. Every public API function covered.
- [ ] TASK-104: Create effect DSP tests — render sine sweeps through each effect, verify spectral characteristics (e.g., overdrive adds harmonics, LP filter rolls off highs, delay produces echoes at correct timing).
- [ ] TASK-105: Create preset round-trip test — save preset, load preset, verify engine state is identical. Test with complex multi-chain rigs.
- [ ] TASK-106: AddressSanitizer build — `cmake -DENABLE_ASAN=ON`. Run all tests under ASAN. Fix any findings.
- [ ] TASK-107: Fuzz test preset loader — random/corrupt JSON, extreme values, missing fields. Verify no crashes.

### 11B: Competitive Analysis & Parity Review

- [ ] TASK-108: Neural DSP competitive analysis — deep dive into Neural DSP's product line (Archetype plugins, Quad Cortex). Document feature set: amp models, effects, cab sim approach, UI/UX patterns, preset sharing, parameter count, unique features (e.g., captures, dual-chain routing, expression pedal mapping). Identify parity gaps.
- [ ] TASK-109: Broader competitive review — survey AmpliTube 5, Helix Native, Guitar Rig 7, BIAS FX 2, TH-U, STL ToneHub. Feature matrix: what do they all have that we're missing? What do we have that they don't (open format, BYOK AI, open source)?
- [ ] TASK-110: Prioritize parity gaps — from TASK-108/109, create a ranked list of missing features that matter most to users. Feed back into proposal as new capabilities or priority adjustments.

### 11C: Release & Distribution

- [ ] TASK-111: GitHub Actions CI — matrix builds for Linux (gcc) + Windows (MinGW). Build standalone + CLAP + VST3. Run tests.
- [ ] TASK-112: Windows packaging — NSIS installer or portable zip with exe + resources + presets + README.
- [ ] TASK-113: Linux packaging — AppImage or tar.gz with standalone binary + resources.
- [ ] TASK-114: Version stamping — embed version from git tag into binary. Show in title bar and about dialog.
- [ ] TASK-115: README.md — build instructions, feature list, screenshots, preset sharing guide, trademark disclaimer.
- [ ] TASK-116: **Final milestone** — all targets build cleanly on Linux + Windows. CLAP loads in Reaper/Bitwig. VST3 loads in Reaper. Standalone detects iRig/Scarlett. Presets save/load correctly. Visual theme is cohesive "worn grime." README has screenshots.

---

## Task Summary

| Phase | Tasks | Delivers | Priority |
|-------|-------|----------|----------|
| 1. Hello Tone | TASK-001 → 008 | Audio passthrough, device detection | P0 |
| 2. It Has an Amp | TASK-009 → 016 | Amp models + noise gate | P0 |
| 3. Pedalboard (MVP) | TASK-017 → 033 | Visual pedalboard + amp + knobs + drag-and-drop | P0 |
| 4. Full Rig | TASK-034 → 046 | Cab IR + multi-amp + presets (.0xfx) | P1 |
| 5. Sounds Like a Record | TASK-047 → 066 | Full effects library (20+ effects) | P1 |
| 6. Works in Bitwig | TASK-067 → 073 | CLAP (first-class) + VST3 plugins | P2 |
| 7. Make It Pretty | TASK-074 → 083 | "Worn grime" visual assets + theme | P2 |
| 8. Pitch and Space | TASK-084 → 090 | Octave, looper, freeze, granular | P2 |
| 9. Open Ecosystem | TASK-091 → 095 | NAM import, custom pedals, community sharing | P3 |
| 10. AI Tone Matcher | TASK-096 → 102 | BYOK tone matching (later phase) | P3 |
| 11. Ship It | TASK-103 → 116 | Testing, competitive review, release | P3 |

**Total: 116 tasks across 11 phases**

Each phase ends with a milestone test. Phases 1-3 (P0) deliver the MVP — a working pedalboard with amp modeling. Phase 4-5 (P1) complete the rig with cab IR, presets, and full effects. Phase 6-8 (P2) add plugin support, visuals, and advanced effects. Phase 9-11 (P3) open the ecosystem, add AI, and ship.
