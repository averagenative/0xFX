# 0xFX

**Guitar amp simulator & effects pedalboard — open source, cross-platform**

A standalone guitar effects processor and amp simulator, also available as VST3/CLAP plugins for DAWs. Built on the same engine architecture as [0x808](https://github.com/averagenative/0x808).

## Features (Planned)

### Effects Pedals
- **Overdrive** — Tube Screamer-style soft clipping
- **Distortion** — High-gain hard clipping
- **Fuzz** — Germanium/silicon fuzz circuits
- **Compressor** — Optical/VCA-style compression
- **Chorus** — Analog bucket-brigade emulation
- **Phaser** — 4/6/8-stage allpass sweep
- **Flanger** — Through-zero flanging
- **Delay** — Analog/digital/tape delay with tap tempo
- **Reverb** — Spring, plate, hall, shimmer
- **Tremolo** — Optical/bias tremolo
- **Wah** — Auto-wah and expression pedal
- **EQ** — Parametric and graphic EQ
- **Noise Gate** — Intelligent noise reduction
- **Bitcrusher** — Lo-fi digital destruction
- **Ring Mod** — Metallic frequency modulation
- **Tape Saturation** — Warm analog compression
- **Octaver** — Sub-octave and octave-up tracking
- **Looper** — Live loop recording and playback

### Amp Modeling
- **Clean** — Fender Twin / Deluxe style (warm, headroom)
- **Crunch** — Marshall JCM800 style (classic rock breakup)
- **High Gain** — Mesa Rectifier style (tight, aggressive)
- **British** — Vox AC30 style (chimey, edge of breakup)
- **Tweed** — Fender Bassman style (spongy, bluesy)

Each amp model: preamp gain stages → tone stack EQ → power amp compression → output level

### Cabinet Simulation
- **IR Loader** — Load .wav impulse response files (thousands available free online)
- **Built-in Cabs** — 8-10 common cabinet IRs bundled
  - 1x12 open back (Fender combo)
  - 2x12 closed back (Vox style)
  - 4x12 straight (Marshall stack)
  - 4x12 slant (Mesa Recto cab)
  - Direct (no cab, for re-amping)

### Signal Chain
- **Drag-and-drop pedalboard** — Reorder effects freely
- **Pre/post amp routing** — Place pedals before or after the amp
- **Preset system** — Save/load complete rigs
- **Tuner** — Chromatic tuner with pitch detection

## Architecture

```
Input → [Noise Gate] → [Pre-Amp Pedals] → [Amp Model] → [Cab IR] → [Post-Amp Pedals] → Output
```

```
+--------------------------------------------------+
|  Layer 4: Host Wrappers                          |
|  Standalone (SDL2) | VST3 | CLAP                |
+--------------------------------------------------+
|  Layer 3: GUI (ImGui)                            |
|  Pedalboard, amp panel, cab selector, tuner      |
+--------------------------------------------------+
|  Layer 2: Signal Chain Controller                |
|  Pedal ordering, pre/post routing, presets       |
+--------------------------------------------------+
|  Layer 1: DSP Engine (pure C99)                  |
|  Effects, amp models, IR convolution, tuner      |
+--------------------------------------------------+
```

## Shared Code from 0x808

The following will be reused/adapted from the 0x808 project:
- Effects engine (overdrive, fuzz, chorus, delay, reverb, phaser, flanger, tremolo, compressor, tape, bitcrusher, ring mod, shimmer)
- CPLUG plugin framework (VST3/CLAP)
- ImGui GUI framework (knobs, sliders, themes)
- Build system (CMake, cross-compile, NSIS installer)
- Logging, crash handler

## Tech Stack

- **Engine**: C99 (zero deps, real-time safe)
- **GUI**: C++ / Dear ImGui / SDL2 / OpenGL
- **Convolution**: Overlap-add FFT (for IR cab loading)
- **Plugins**: VST3 / CLAP via CPLUG
- **Platforms**: Windows, Linux, macOS

## Building

```bash
# Linux
sudo apt install libsdl2-dev libgl-dev g++
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Windows (cross-compile from Linux)
cmake -B build_win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake
cmake --build build_win -j$(nproc)
```

## License

MIT

## Status

**Pre-alpha** — Project scaffolding. See [0x808](https://github.com/averagenative/0x808) for the mature sibling project.
