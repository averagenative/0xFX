# 0xFX

**Guitar amp simulator and effects pedalboard — open source, cross-platform**

A real-time guitar effects processor and amp simulator available as a standalone application and as VST3/CLAP plugins for DAWs. Built on a pure C99 DSP engine with a layered, API-driven architecture.

> **Status: Pre-alpha — actively developed.** Core engine and GUI are functional. Not yet production-ready.

---

## Features

### Amp Models

Five amp models, each with distinct voicing, gain staging, and tone stack character:

| Name | Inspired by |
|---|---|
| Fullerton Clean | Classic American clean (Fender Twin/Deluxe) |
| Brit Crunch | Classic British crunch (Marshall JCM800) |
| Southwest Lead | American high-gain (Mesa Rectifier) |
| Essex Chime | British chime (Vox AC30) |
| Tweed Blues | American tweed-era (Fender Bassman) |

Each model runs: preamp gain stages (1–4 cascaded waveshapers) → 3-band tone stack EQ + presence → power amp compression + sag simulation.

### Effect Pedals (20+ types)

**Overdrive**
- Jade Drive — mid-humped soft-clip (Tube Screamer topology)
- Gold Drive — transparent OD with clean blend (Klon-style)
- Blues Grit — FET-based gritty OD

**Distortion**
- Rodent — op-amp hard clip with backwards filter (RAT-style)
- Orange Distortion — bright hard clip
- Metal Zone — dual gain with parametric mid
- Amp Box — tight high-gain amp-in-a-box

**Fuzz**
- Mammoth Fuzz — 4-stage scooped-mid fuzz (Big Muff-style)
- Round Fuzz — smooth germanium Fuzz Face-style
- Wraith Fuzz — aggressive germanium character
- Chaos Fuzz — gated sputtery fuzz with oscillation

**Delay**
- Echo Delay — clean digital delay
- Carbon Delay — warm analog BBD with repeat darkening and LFO mod
- Tape Machine — tape echo with wow/flutter and saturation
- Memory Echo — modulated delay

**Reverb**
- Drip Verb — spring reverb (allpass diffusion + comb)
- Hall Verb — algorithmic hall reverb (Freeverb topology)
- Plate Verb — plate reverb
- Shimmer Verb — octave-up shimmer reverb
- Cloud Verb — ambient/freeze reverb

**Modulation**
- Liquid Chorus — BBD-style chorus
- Phase Sweep — 6-stage allpass phaser
- Jet Flanger — flanger with through-zero
- Pulse Trem — tremolo (sine/square/triangle LFO)
- Drift Vibrato — true pitch vibrato

**Wah / Filter**
- Howl Wah — expression wah (swept peak filter)
- Quack Filter — auto-wah driven by envelope follower

**Compressor**
- Squeeze Box — OTA-style squashy compressor
- Glass Comp — transparent compressor with blend
- Punch Comp — fast-attack 1176-style compressor

**EQ**
- Tone Sculptor — 7-band graphic EQ (100Hz–6.4kHz, ±12dB per band)
- Precision EQ — parametric EQ

**Utility / Other**
- Noise Gate — threshold/hold/attack/release gate
- Grit Crush — bitcrusher + sample-and-hold
- Ring Tone — ring modulator
- Warm Tape — tape saturation
- Octave Engine — polyphonic octaver
- Pitch Warp — pitch bend / whammy
- Loop Station — looper
- Infinite Hold — freeze / drone
- Grain Cloud — granular delay

### Cabinet IR Convolution

- Load any `.wav` impulse response file (thousands available free online)
- 5 built-in synthetic IRs (no third-party samples required): 1x12 open back, 2x12 closed back, 4x12 straight, 4x12 slant, direct
- Overlap-add FFT convolution via KissFFT (real-time safe)
- Bypass for direct/re-amp signal flow

### Preset System

- `.0xfx` JSON preset format — saves complete rig state
- Includes all pedals, params, order, amp models, cab settings
- 5 factory presets included: Clean Sparkle, Classic Crunch, Modern High Gain, Chimey British, Bluesy Tweed

### Signal Chain

```
Input → [Noise Gate] → [Pre-Amp Pedals] → [Amp Model] → [Cab IR] → [Post-Amp Pedals] → Output
```

- Parallel amp chains (up to 4, each with independent amp + cab)
- Drag-and-drop pedalboard reordering
- Pre/post amp pedal placement
- Chromatic tuner (pitch detection via autocorrelation)
- Audio device selector with configurable buffer size and sample rate

---

## Building

**Linux (standalone)**

```bash
sudo apt install libsdl2-dev libgl-dev g++
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/fx_api_test    # run engine API tests
```

**Windows (cross-compile from Linux using MinGW)**

```bash
cmake -B build_win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake
cmake --build build_win -j$(nproc)
```

Build targets:
- `0xfx_standalone` — standalone application (SDL2 + ImGui)
- `0xfx_clap` — CLAP plugin shared library
- `0xfx_vst3` — VST3 plugin shared library
- `fx_api_test` — engine API test binary

---

## Architecture

Four-layer design with strict API boundaries:

```
+--------------------------------------------------+
|  Layer 4: Host Wrappers                          |
|  Standalone (SDL2) | CLAP | VST3                 |
+--------------------------------------------------+
|  Layer 3: GUI (C++ / Dear ImGui / OpenGL)        |
|  Pedalboard, amp panel, cab selector, tuner      |
+--------------------------------------------------+
|  Layer 2: Signal Chain Controller                |
|  Pedal ordering, pre/post routing, presets       |
+--------------------------------------------------+
|  Layer 1: DSP Engine (pure C99, zero deps)       |
|  Effects, amp models, IR convolution, tuner      |
+--------------------------------------------------+
```

The GUI and plugin layers communicate exclusively through the public API in `src/engine/fx_engine.h`. Internal DSP structures are never exposed. The engine is real-time safe: no heap allocation in the audio callback after initialization.

**Tech stack:**
- Engine: C99, no runtime dependencies
- GUI: C++ / Dear ImGui / SDL2 / OpenGL 3.3
- Convolution: KissFFT (overlap-add)
- Plugins: CLAP + VST3 via CPLUG
- Platforms: Linux, Windows (macOS planned)

---

## Trademark Disclaimer

All amp model and pedal names in 0xFX are original creative names. Any resemblance to commercial product names is coincidental. References to real-world equipment in comments and documentation are for technical description only. 0xFX is not affiliated with, endorsed by, or associated with any commercial amp or pedal manufacturer.

---

## License

MIT
