# 0xFX

**Guitar amp simulator & effects pedalboard — open source, cross-platform**

Real-time guitar effects processor and amp simulator. Standalone app + CLAP/VST3 plugins. Pure C99 DSP engine with skeuomorphic GUI.

> **Status: Pre-alpha** — engine and GUI functional, not yet production-ready.

---

## Signal Flow

```
Input → [Gate] → [Pre Pedals] → AMP → CAB → [Mic Sim] → [Studio Rack] → Output
                                  ↕ (parallel chains with mix)
```

- Dual amp chains with independent amp/cab/mic per chain
- Pre/post amp pedal placement
- Studio rack processors after cab (compressors, EQ, tape, limiter)
- Optional microphone simulation with placement controls

---

## Amp Models (11)

| Model | Inspired By | Character |
|---|---|---|
| Fullerton Clean | Fender Twin/Deluxe | Clean, chimey American — silver panel era |
| British Crunch | Marshall Plexi | Classic British crunch |
| Southwest Lead | Mesa Rectifier | High-gain American lead |
| Essex Chime | Vox AC30 | British chime, Class A |
| Tweed Blues | Fender Bassman | Warm vintage breakup |
| Meridian High Gain | Peavey 6505 | Scooped, brutal modern metal |
| Citrus Roar | Orange Rockerverb | Thick British EL34 warmth |
| Citrus Terror | Orange Tiny Terror | Class A lunchbox grit |
| Regent 800 | Marshall JCM800 | Single-channel rock aggression |
| Solar Monolith | Sunn Model T | Massive doom, crushing fuzz |
| Eclipse Drone | Sunn O))) | Extreme low-end with feedback sustain |

Each model: cascaded waveshaper preamp (1–4 stages) → 3-band tone stack + presence → power amp compression + sag.

## Effects Pedals (39)

| Category | Pedals |
|---|---|
| **Overdrive** | Jade Drive, Gold Drive, Blues Grit |
| **Distortion** | Rodent, Orange Dist, Metal Zone, Amp Box |
| **Fuzz** | Mammoth Fuzz, Round Fuzz, Wraith Fuzz, Chaos Fuzz |
| **Delay** | Echo Delay, Carbon Delay, Tape Machine, Memory Echo |
| **Reverb** | Drip Verb, Hall Verb, Plate Verb, Shimmer Verb, Cloud Verb |
| **Modulation** | Liquid Chorus, Phase Sweep, Jet Flanger, Pulse Trem, Drift Vibrato |
| **Wah/Filter** | Howl Wah, Quack Filter, Tone Sculptor, Precision EQ |
| **Dynamics** | Squeeze Box, Glass Comp, Punch Comp, Noise Gate |
| **Pitch** | Octave Engine, Pitch Warp |
| **Utility** | Grit Crush, Ring Tone, Warm Tape |
| **Experimental** | Loop Station, Infinite Hold, Grain Cloud |

## Studio Processors (post-amp rack gear)

| Processor | Type | Inspired By |
|---|---|---|
| Iron Squeeze | FET compressor | 1176 |
| Glass EQ | Passive EQ | Pultec EQP-1A |
| Reel Warmth | Tape saturation | Studer/Ampex |
| Brick Wall | Brickwall limiter | Modern mastering |

## Microphone Simulation (optional, per-chain)

9 models with distance, angle, and cone position controls:

| Mic | Type | Inspired By |
|---|---|---|
| Stage Workhorse | Dynamic | SM57 |
| Roadie Vocal | Dynamic | SM58 |
| Berlin Dynamic | Dynamic | Sennheiser e609 |
| Silver Bullet | Dynamic | EV RE20 |
| Velvet Ribbon | Ribbon | Royer R-121 |
| Heritage Ribbon | Ribbon | Coles 4038 |
| Studio Large | Condenser | Neumann U87 |
| Austrian Pencil | Condenser | AKG C414 |
| Room Pencil | Condenser | AKG C451 |

Default: DI (direct inject, no mic coloration).

## Cabinet IR

- Load any `.wav` impulse response
- 5 built-in synthetic IRs: 1x12 open, 2x12 closed, 4x12 straight, 4x12 slant, direct
- FFT overlap-add convolution (KissFFT)

## Additional Features

- **MIDI CC control** with MIDI learn mode
- **Chromatic tuner** (autocorrelation pitch detection)
- **Preset system** — `.0xfx` JSON format, session auto-save/restore
- **CLAP + VST3 plugins** (59 automatable parameters)
- **Debug audio recorder** — captures input+output to WAV

---

## Building

```bash
# Linux
sudo apt install libsdl2-dev libgl-dev g++
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Windows (cross-compile via MinGW)
cmake -B build_win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake
cmake --build build_win -j$(nproc)
```

**Targets:** `0xfx_gui` (standalone) · `0xfx_clap` (CLAP plugin) · `0xfx_vst3` (VST3) · `fx_api_test` (334 tests)

---

## Architecture

```
Layer 4: Host Wrappers — Standalone (SDL2) | CLAP | VST3
Layer 3: GUI — C++ / Dear ImGui / OpenGL 3.3
Layer 2: Audio I/O — miniaudio (duplex) + MIDI input
Layer 1: DSP Engine — pure C99, zero deps, RT-safe
```

GUI and plugins interact exclusively through the public API (`fx_engine.h`). No internal struct access. Engine is real-time safe: no allocation in the audio callback.

---

## Trademark Disclaimer

All names are original. References to commercial products in documentation are for technical description only. 0xFX is not affiliated with any amp or pedal manufacturer.

## License

MIT
