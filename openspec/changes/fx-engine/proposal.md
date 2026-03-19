## Why

Guitarists and producers need a free, lightweight, cross-platform amp simulator and effects pedalboard that works both standalone and as a DAW plugin — without the bloat of commercial amp sims ($100-400) or the limitations of single-effect open-source projects.

Existing options fall short:

- **Commercial amp sims** (Neural DSP, Amplitube, Helix Native) — expensive per-amp pricing, platform-locked, closed source
- **Open-source effects** (Guitarix) — Linux-only, monolithic architecture, no plugin format support, aging UI
- **Hardware modelers** (HX Stomp, Kemper) — $500-2000, not software-extensible
- **Free plugins** (LePou, Ignite Amps) — individual effects with no unified pedalboard or signal chain management
- **Audio interface friction** — most tools assume you already know how to configure ASIO/ALSA/CoreAudio. Guitarists with iRig, Scarlett, or Bluetooth interfaces just want to plug in and play.
- **Tone matching** — commercial tools (ToneX $100, Kemper profiling $2000 hardware) charge premium prices for AI-assisted tone capture. No open-source option offers "match this tone from a recording."
- **Closed formats everywhere** — amp profiles (Kemper .kipr, ToneX proprietary), cabinet IRs (no metadata standard), presets (per-vendor lock-in). No open, human-readable format ties a complete rig together.

There's a gap for a focused instrument that lets you: plug in a guitar through any audio interface, build a pedalboard, dial in an amp (or let AI match a tone from a recording), load a cabinet IR, and play — all in one lightweight C application that runs on Linux, Windows, and macOS, both standalone and inside DAWs.

This is the third project in the 0x family (alongside 0x808 and 0xSYNTH), sharing engine architecture and build tooling but purpose-built for guitar processing.

## Business Value Analysis

### Who Benefits and How

**1. The Home Guitarist on a Budget (primary persona)**
A guitar player who records at home and can't justify $150+ per amp sim plugin. They want a pedalboard they can customize — overdrive into a crunch amp with spring reverb — without subscription fees or iLok dongles. 0xFX gives them a complete rig for free, with the same signal chain flexibility as commercial tools.

**2. The Linux Musician (underserved market)**
A guitarist running Linux who is stuck with Guitarix (no plugin support, complex routing) or running Windows amp sims through Wine. 0xFX provides a native experience with CLAP/VST3 plugin support for Linux DAWs like Reaper, Ardour, and Bitwig.

**3. The DAW Producer Who Wants a Pedalboard Plugin**
A producer in Reaper or another DAW who wants a single plugin that combines amp modeling, cabinet IRs, and a reorderable effects chain — rather than stacking 5-6 separate effect plugins and managing routing between them.

**4. The "I Just Want to Plug In" Guitarist**
A player with an iRig, Scarlett, or USB interface who doesn't want to fight with audio driver configuration. They plug in, 0xFX detects the interface, and they're playing. Bluetooth audio support means they can even jam wirelessly through headphones (latency-permitting).

**5. The Tone Chaser**
A guitarist who hears a tone on a recording or YouTube video and wants to recreate it. Instead of spending hours tweaking knobs, they feed a reference clip to the AI tone matcher, which analyzes the spectral characteristics and sets up a rig that approximates it — amp model, gain staging, EQ, effects chain. Uses their own LLM API key (BYOK model, similar to opencode/claude-code) — no subscription to us required.

**6. The Tinkerer / Pedal Designer**
A programmer or tone enthusiast who wants to design custom pedals, share amp profiles, or create cabinet IRs — all in an open, human-readable JSON format. They can study DSP algorithms in clean C99, tweak parameters by hand, and share their creations with the community.

### What Problem This Solves

The core problem: **guitar amp simulation is either expensive, platform-locked, or architecturally inflexible.**

- Free tools are Linux-only or single-effects with no signal chain management
- Commercial tools charge per-amp, require authorization dongles, and ship as opaque binaries
- No open-source option provides a complete guitar rig (pedals + amp + cab + routing) as a DAW plugin
- Existing open-source projects couple their DSP directly to their UI, making it impossible to reuse the engine in different contexts
- Audio interface setup is still a pain point — most tools punt on device enumeration and expect users to configure externally
- Tone matching (Neural DSP ToneX: $100, Kemper profiling: $2000 hardware) is locked behind expensive commercial products with no open alternative
- No open, human-readable format exists for sharing complete rigs (amp + cab + effects + routing). Every vendor locks you in.

0xFX solves this with a clean API-driven architecture: a C engine library that any frontend or plugin host can call, with a complete signal chain from input to output, an open preset/profile format, and AI tone matching powered by the user's own LLM API key.

### What Happens If We Don't Build This

**For the 0x family ecosystem:**
- 0x808 and 0xSYNTH remain isolated instruments. The effects engine code (14+ effects) stays locked inside drum machine / synth contexts with no guitar-optimized application. The investment in DSP code doesn't compound across products.
- The architectural lessons learned from 0x808 (dual-frontend pain, direct struct access coupling) never get applied. The next project repeats the same mistakes instead of evolving to API-driven design.

**For the guitar community:**
- Linux guitarists remain stuck with Guitarix (monolithic, no plugin support) or Wine-wrapped Windows amp sims. No native CLAP/VST3 guitar plugin exists in the open-source world.
- Budget guitarists continue paying $100-400 per amp sim plugin or pirating commercial tools. The "focused guitar rig" gap between free single-effects and expensive suites stays unfilled.
- Tone matching remains locked behind $100+ commercial tools (ToneX) or $2000 hardware (Kemper). No open-source, BYOK alternative emerges.
- No open, human-readable format for sharing complete guitar rigs exists. Every preset stays locked in vendor silos.

**For the developer/tinkerer community:**
- There's no clean, well-documented open-source amp sim engine to study or extend. Guitarix's codebase is monolithic and hard to learn from. The opportunity to create the "reference implementation" for guitar DSP in C99 is missed.

**Nothing catastrophic happens.** People keep muddling through. But the compounding value of the 0x family — shared DSP code, shared build system, evolving architecture — only materializes if we build the third leg of the stool.

### Priority Ranking by Value Delivered

| Priority | Capability | Value Rationale |
|----------|-----------|-----------------|
| **P0** | `fx-engine-api` | The foundation. A clean C API that exposes signal chain, effects, and amp modeling without requiring knowledge of internal structs. Everything else builds on this. |
| **P0** | `audio-device-manager` | Enumerate and manage audio interfaces (USB, class-compliant, Bluetooth). Without this, no sound. Must handle iRig-style mobile interfaces, pro interfaces (Scarlett), and BT audio. |
| **P0** | `amp-models` (clean, crunch, high-gain) | The core value proposition. Without amp modeling, this is just another effects processor. Skeuomorphic GUI with turnable knobs for gain, volume, bass, mid, treble, presence, sag — just like a real amp. |
| **P0** | `overdrive` + `distortion` + `noise-gate` | The minimum useful guitar signal chain. Overdrive into an amp with a noise gate is the atomic unit of guitar tone. |
| **P1** | `cabinet-ir` | Amp without cab sounds terrible. IR convolution + synthetic IR generation + user IR import (.wav). |
| **P1** | `signal-chain-routing` | Drag-and-drop pedal reorder + multi-amp signal splitting. Pre/post routing and parallel chains (e.g., clean+dirty blend) are what make this a pro-grade pedalboard. |
| **P1** | `open-rig-format` | JSON-based open format for presets, amp profiles, cab IR metadata, and user-designed pedals. Human-readable, shareable, version-controlled. |
| **P1** | `clap-plugin` | CLAP plugin format — first-class citizen. Open format, better parameter handling than VST3, growing DAW support (Bitwig, Reaper, Studio One). |
| **P2** | `vst3-plugin` | VST3 plugin format — second-class but necessary for DAW compatibility (Ableton, Pro Tools, etc.). |
| **P2** | `wah` + `compressor` + `eq` | Essential utility pedals. Wah is the #1 missing effect for guitar vs what 0x808 has. |
| **P2** | `delay` + `reverb` (expanded) | Beyond 0x808's basic delay/Freeverb: tape echo (wow/flutter), spring reverb, analog delay (BBD darkening), shimmer. |
| **P2** | `chorus` + `phaser` + `flanger` + `tremolo` + `vibrato` | Modulation suite. Port from 0x808 + add harmonic tremolo, through-zero flanging, true vibrato. |
| **P2** | `tuner` | Expected in any guitar tool. Autocorrelation pitch detection. |
| **P3** | `fuzz` (expanded) + `bitcrusher` + `ring-mod` + `tape-sat` + `octaver` | Big Muff, Fuzz Face, Tone Bender variants. Pitch shifting (Whammy-style). |
| **P3** | `looper` | Live loop recording with overdub, undo, reverse, half-speed. |
| **P3** | `freeze` + `granular` | Infinite sustain, granular delay/pitch. Experimental/ambient effects. |
| **P3** | `ai-tone-match` | BYOK agentic tone matching (later phase — needs significant design work). |
| **P3** | `asset-generation` | Dev-time image generation pipeline for cohesive skeuomorphic pedal/amp/cab visuals. |

### Success Metrics

| Metric | Target | How to Measure |
|--------|--------|----------------|
| **Time to first tone** | < 15 seconds after launch | User opens app → hears guitar through amp model. No config dialogs, sensible defaults. |
| **Latency** | < 10ms round-trip at 128-sample buffer | Measure input-to-output latency. Guitar players are extremely sensitive to latency. |
| **Audio quality** | No audible aliasing or artifacts at 44.1kHz | Automated sweep tests: render sine sweeps through each effect, check for spectral artifacts. |
| **API usability** | Engine usable without reading implementation | A developer can create a signal chain, add effects, and process audio using only the public API header. |
| **Cross-platform build** | Linux + Windows (MinGW cross-compile) | CI passes on both platforms on every commit. |
| **CLAP plugin loads in Reaper/Bitwig** | CLAP scans and loads without errors | Automated test: host plugin scan returns success. CLAP is first-class. |
| **VST3 plugin loads** | VST3 scans and loads | Secondary format — tested but CLAP takes priority for new features. |
| **Audio interface detection** | Detect and enumerate USB/class-compliant interfaces on plug | User plugs in iRig/Scarlett → device appears in device selector within 2 seconds. |
| **Open format adoption** | Presets shareable between users | User A exports a rig as JSON, User B imports it and gets the same tone. |
| **Effect porting velocity** | Port 0x808 effect to 0xFX in < 1 hour each | Effects share DSP math but use the new API. Measure time to port + test each effect. |

## What Changes

This is a greenfield project — building a complete application from scratch. The key architectural decisions that distinguish 0xFX from 0x808 and 0xSYNTH:

### API-Driven Engine Architecture

**The engine exposes a public C API. Frontends and plugins call the API — never touch internal structs directly.**

This is a deliberate departure from 0x808's approach (where the GUI reads/writes engine structs directly). The motivation:

1. **Single frontend** — no GTK/ImGui parity burden. One ImGui frontend calls the API.
2. **Plugin isolation** — CLAP/VST3 hosts interact through the same API as standalone, reducing plugin-specific bugs.
3. **Testability** — engine can be tested entirely through its API without any GUI.
4. **Future-proofing** — headless mode, alternative frontends, agentic AI integration, or network control become possible without engine changes.
5. **AI integration** — the tone matching agent calls the same API as the GUI. It analyzes a reference signal and calls `fx_amp_set_param()`, `fx_chain_add_pedal()`, etc. to build a matching rig. No special internal access needed.

### System Architecture

```
┌──────────────────────────────────────────────────────────┐
│  Layer 4: Host Wrappers                                  │
│  Standalone (SDL2) | CLAP (first-class) | VST3           │
├──────────────────────────────────────────────────────────┤
│  Layer 3: GUI (ImGui C)                                  │
│  Skeuomorphic pedalboard, turnable amp knobs, cab loader │
│  Drag-and-drop pedal reorder, multi-chain routing view   │
├──────────────────────────────────────────────────────────┤
│  Layer 2: Audio Device Manager                           │
│  miniaudio — USB, class-compliant, Bluetooth, ASIO/ALSA │
├──────────────────────────────────────────────────────────┤
│  Layer 1: Engine API (C99, public header)                │
│  fx_engine_*() — create, configure, process              │
│  ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ │
│  Engine Internals (private)                              │
│  Effects DSP, amp models, IR convolution, IR synthesis   │
└──────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────┐
│  AI Agent Layer (out-of-process, optional, later phase)  │
│  Tone matching — BYOK (user's own LLM API key)          │
│  Input: audio file, YouTube URL, microphone capture      │
│  Output: .0xfx preset (JSON rig file)                    │
│  Calls Engine API to configure rig — no special access   │
└──────────────────────────────────────────────────────────┘
```

### Plugin Architecture — CLAP First

CLAP is the first-class plugin format. VST3 is supported but secondary.

**Why CLAP first:**
- Open format (MIT licensed) — no Steinberg SDK licensing concerns
- Better parameter model (thread-safe, supports per-note modulation)
- Growing DAW adoption (Bitwig, Reaper, Studio One, FL Studio)
- Simpler API surface than VST3 — less boilerplate
- CLAP extensions are opt-in, not mandatory

Both formats are built via CPLUG, but CLAP gets new features first and is the primary test target. VST3 is a compatibility layer.

### Audio Device Management

The standalone app handles audio I/O via miniaudio, which abstracts platform backends:
- **Linux**: ALSA, PulseAudio, JACK
- **Windows**: WASAPI, ASIO (via ASIO SDK if available)
- **macOS**: CoreAudio

Device types that must work out-of-the-box:
- **USB class-compliant interfaces** (iRig HD, iRig Pro, Scarlett series) — detected on plug, selectable in UI
- **Built-in audio** — laptop mic/speakers as fallback
- **Bluetooth audio** — supported with latency warning (BT adds 50-200ms, unusable for real-time monitoring but fine for playback/recording review)

The device manager provides enumeration, hot-plug detection, sample rate/buffer size configuration, and input/output channel mapping. In plugin mode, the host provides audio I/O — the device manager is standalone-only.

### Signal Routing — Multi-Amp Splitting

The signal chain supports splitting and parallel routing, not just a single linear chain:

```
Guitar Input
    │
    ▼
[Input Stage / Noise Gate]
    │
    ▼
[Pre-Amp Pedals] ← drag-and-drop reorder in GUI
    │
    ├──────────────────┐
    ▼                  ▼
[Amp A: Crunch]    [Amp B: Clean]
  (turnable knobs)   (turnable knobs)
    │                  │
    ▼                  ▼
[Cab A: 4x12]     [Cab B: 1x12]
    │                  │
    ▼                  ▼
[Post FX A]        [Post FX B]
    │                  │
    └──────┬───────────┘
           ▼
    [Mix / Blend]
           │
           ▼
        Output
```

The GUI provides drag-and-drop pedal reordering — grab a pedal, drag it to a new position, and the signal chain updates in real-time. Signal can be split to multiple amp+cab chains with per-chain post effects and a final mix stage. This enables dual-amp rigs (e.g., clean + dirty blend) that are common in professional guitar setups.

### Skeuomorphic GUI — Turnable Knobs

The GUI renders amps and pedals as photorealistic skeuomorphic panels. **Amp controls are turnable knobs** — click-and-drag to adjust gain, volume, bass, mid, treble, presence, sag — just like turning knobs on a real amplifier. Each amp model has its own visual panel with the correct knob layout for that amp type:

- **Clean (Fender Twin style)**: Volume, Treble, Mid, Bass, Reverb, Bright switch
- **Crunch (Marshall JCM800 style)**: Pre Gain, Master, Treble, Mid, Bass, Presence
- **High Gain (Mesa Rectifier style)**: Gain, Treble, Mid, Bass, Presence, Master, Channel switch
- **British (Vox AC30 style)**: Volume, Treble, Bass, Cut, Tone Cut, Tremolo Speed/Depth
- **Tweed (Fender Bassman style)**: Volume, Treble, Bass, Presence

Pedals similarly have turnable knobs matching their real-world counterparts. Every parameter is continuously adjustable via the knobs — no sliders, no text boxes for primary controls. The knob interaction is the core UX.

### Open Rig Format (.0xfx)

A JSON-based open format for presets, amp profiles, cabinet IR metadata, and user-designed pedals. Human-readable, shareable, version-controllable.

**Why:** There is no industry-standard open format for complete guitar rigs. Kemper uses proprietary .kipr, ToneX is closed, Line 6 .hlx is JSON but undocumented. NAM (Neural Amp Modeler) is the closest open format for amp captures specifically, but nothing ties amp + cab + effects + routing together in a portable, documented format.

```json
{
  "format": "0xfx",
  "version": "1.0",
  "name": "SRV Clean Blues",
  "author": "username",
  "license": "CC-BY-4.0",
  "tags": ["blues", "clean", "srv", "strat"],
  "signal_chain": {
    "input": { "gain_db": 0, "noise_gate": { "threshold_db": -50, "attack_ms": 1, "release_ms": 50 } },
    "pre_pedals": [
      {
        "type": "jade_drive",
        "name": "Jade Drive",
        "bypass": false,
        "params": { "drive": 0.3, "tone": 0.6, "level": 0.7 }
      }
    ],
    "chains": [
      {
        "amp": {
          "model": "fullerton_clean",
          "params": { "gain": 0.4, "volume": 0.7, "bass": 0.5, "mid": 0.6, "treble": 0.7, "presence": 0.5 }
        },
        "cab": {
          "ir_file": "1x12_open_back.wav",
          "bypass": false
        },
        "post_pedals": [
          { "type": "spring_reverb", "params": { "decay": 0.4, "mix": 0.3, "tone": 0.6 } }
        ],
        "mix": 1.0
      }
    ],
    "output": { "gain_db": 0 }
  }
}
```

**Importable formats:**
- **Cabinet IRs**: .wav files (standard). Import with optional JSON sidecar for metadata (mic type, cab type, speaker, position).
- **Amp profiles**: NAM (.nam) and AIDA-X model files for neural amp captures. 0xFX's own parametric amp profiles (.0xfx JSON).
- **User-designed pedals**: JSON definitions specifying DSP topology, parameter ranges, and default values. Allows community pedal sharing.
- **Presets from other tools**: Import from Line 6 .hlx (JSON-based) where feasible.

### Cabinet IR — Generation + Import

**Import:** Standard .wav IR files (44.1/48 kHz, 24/32-bit, mono, 512-4096 samples typical). Thousands available from free/commercial sources.

**Synthetic IR Generation:** 0xFX can also generate cabinet IRs programmatically from parametric descriptions — no need to record real cabinets:

1. **Speaker modeling**: Thiele-Small parameters (Fs, Qts, Cms) define low-frequency behavior. Empirical breakup curves model high-frequency character per speaker type (V30-style, Greenback-style, Jensen-style).
2. **Cabinet modeling**: Sealed vs open-back transfer functions. Open-back comb filtering from rear radiation path-length difference.
3. **Microphone modeling**: Apply frequency response curve + proximity effect for mic type (SM57-style, condenser-style, ribbon-style). Position (on-axis/off-axis/distance) modeled as variable LP filter.
4. **Synthesis**: Compute combined frequency response → minimum-phase reconstruction via Hilbert transform → IFFT to time-domain IR → window to 2048 samples.

Bundled IRs are synthesized (no licensing concerns). Users can import their own captured IRs.

Target specs for max compatibility with other tools: **48 kHz, 32-bit float .wav, 2048 samples**.

### Visual Assets — AI-Generated (Dev-Time Pipeline)

Pedal, amp, and cabinet graphics are AI-generated at dev time with a consistent visual style.

**Visual Aesthetic: "Worn Grime"**
The 0xFX visual identity is beat-up, dirty, lived-in gear — not pristine showroom renders. Pedals have scuffed enclosures, worn paint, grime around the knobs, scratched faceplates. Amps have aged tolex, dusty grille cloth, yellowed knobs. Cabinets show road wear. Think: a gigging musician's pedalboard after years of touring, not a Guitar Center display case. This extends to the overall GUI theme — dark, textured, with grit.

**Approach for cohesive visuals:**
1. Generate 3-5 reference images establishing the "worn grime" 0xFX aesthetic
2. Train a LoRA on Replicate (Flux Pro base) for style consistency (~$5, minutes)
3. Generate all 20-30+ assets using the LoRA with templated prompts and fixed seeds
4. Each asset type gets a structured prompt template:
   - Pedals: `"beat-up [color] guitar stomp pedal, scuffed metal enclosure, worn paint, grime around [N] knobs, scratched faceplate, LED indicator, top-down view, dark moody lighting, 0xfx grime style"`
   - Amps: `"vintage guitar amplifier front panel, [amp style] aesthetic, aged tolex, dusty chicken-head knobs, yellowed controls, road-worn, dark atmosphere, 0xfx grime style"`
   - Cabinets: `"road-worn guitar speaker cabinet, [config] speakers, dusty grille cloth, scuffed corners, stage-used look, 0xfx grime style"`
   - Theme elements: `"dark textured background, grungy metal texture, worn leather, industrial grime, pedalboard surface, 0xfx grime style"`

**Image generation API options (evaluated):**
- **Replicate + Flux Pro + LoRA** (recommended) — best API accessibility, LoRA training for style lock, excellent text rendering for knob labels. Pay-per-request, ~$3-10 for full asset set.
- **Midjourney API** — highest quality product shots, `--sref` for style references. API access expanding (was limited beta as of early 2025, check current status).
- **Ideogram** — best text rendering (knob labels), good API. Fallback if Flux text rendering isn't sufficient.
- **Stability AI (SD3)** — pay-per-generation, ControlNet for structural consistency. Good alternative.

The generation pipeline is a dev-time tool (likely a Python script in `tools/`), not user-facing. Assets are baked into the build as PNGs. GUI theme elements (backgrounds, textures, panel surfaces) are also generated for a cohesive "worn grime" feel throughout.

### AI Tone Matching (Later Phase — P3)

**BYOK Model (Bring Your Own Key):** The tone matcher uses the user's existing LLM API key — similar to how opencode/claude-code work. No 0xFX subscription required. Users configure their API provider (Anthropic, OpenAI, etc.) in settings.

**Architecture (high-level, needs significant design work):**
1. User provides reference: audio file (.wav/.mp3), YouTube URL, or live mic capture
2. Spectral analysis (in-process, C): Extract frequency response, harmonic content, dynamic envelope, noise floor, stereo field characteristics
3. Feature extraction produces a "tone fingerprint" — a structured description of the sound
4. LLM agent (out-of-process, user's API key): Receives the tone fingerprint + 0xFX's available amps/effects/parameters. Reasons about what rig configuration would produce a similar tone.
5. Agent calls the engine API (same as GUI) to configure the rig: `fx_amp_set_model()`, `fx_amp_set_param()`, `fx_chain_add_pedal()`, `fx_pedal_set_param()`, `fx_cab_load_ir()`
6. Output: a .0xfx preset file the user can save, tweak, and share

**This is a later phase.** It needs:
- Research into which LLM providers work best for audio-to-parameter mapping
- A well-designed spectral analysis → feature extraction pipeline
- Possibly fine-tuned prompts or a specialized agent framework
- Evaluation of whether spectral analysis alone is sufficient or if neural amp matching (NAM-style) should be combined with LLM reasoning

### Effects Library — Diverse + Inspired by Iconic Pedals

The effects library goes beyond 0x808's 14 effects to cover the full spectrum of guitar effects. Each effect is inspired by real-world pedals that guitarists know and love.

#### Ported from 0x808 (14 effects, wrapped in new API)
- Biquad filter (LP/HP/BP), delay (with BPM sync), Freeverb reverb, overdrive (soft-clip), fuzz (hard-clip), chorus, bitcrusher, compressor, phaser (6-stage), flanger, tremolo (3 waveforms), ring mod, tape saturation, shimmer reverb

#### Additional 0xSYNTH filters available for porting
- Notch filter, ladder (24dB/oct Moog-style), comb filter, formant filter

#### New Effects for 0xFX

**Overdrive variants** (beyond 0x808's single overdrive):
- Tube Screamer-style (mid-humped soft clip, tanh) — the reference
- Klon-style (transparent, clean-blend mixed with clipped signal)
- Blues Driver-style (FET-based, grittier, more open)

**Distortion:**
- RAT-style (op-amp hard clip, unique backwards filter control)
- DS-1-style (bright, aggressive hard clip)
- Metal Zone-style (dual gain stages, parametric mid EQ)
- Amp-in-a-box (Friedman BE-OD style, tight high-gain)

**Fuzz variants** (beyond 0x808's single fuzz):
- Big Muff-style (4-stage clipping, scooped mids, sustain-for-days)
- Fuzz Face-style (germanium smooth / silicon bright, cleans up with volume knob)
- Tone Bender-style (aggressive, raspy germanium)
- Fuzz Factory-style (gated sputter, controllable oscillation, chaos knob)

**Delay variants** (beyond 0x808's basic delay):
- Digital delay (clean repeats, Boss DD-style)
- Analog delay (BBD-style, repeats darken and degrade, optional modulation — MXR Carbon Copy-style)
- Tape echo (wow/flutter, tape degradation, multi-head — Echoplex/Space Echo-style)
- Modulated delay (chorus on repeats — Memory Man-style)

**Reverb variants** (beyond 0x808's Freeverb):
- Spring reverb (drip-and-splash character, essential for guitar — Fender tank style)
- Plate reverb (dense, smooth, studio-quality)
- Hall reverb (large space, long tails)
- Shimmer reverb (octave-up pitch shift in feedback — already in 0x808)
- Ambient/freeze reverb (infinite sustain, pad-like — Walrus Slo-style)

**Wah / Filter** (new to 0xFX):
- Expression wah (bandpass sweep — Cry Baby/Vox style)
- Auto-wah / envelope filter (attack-sensitive — Q-Tron style)
- Fixed wah (manual frequency, use as a tone shaper)

**Compressor variants:**
- Squashy OTA compressor (Dyna Comp-style, colored, obvious)
- Transparent compressor (Keeley-style, parallel blend knob)
- 1176-style (fast attack, punchy, sidechain HP filter)

**EQ:**
- Graphic EQ (7/10-band, slider per band — MXR/Boss style)
- Parametric EQ (3-band, per-band freq/gain/Q)

**Octave / Pitch** (new to 0xFX):
- Polyphonic octave (octave up + down, works on chords — POG-style)
- Monophonic octave (analog-style tracking)
- Pitch bend / Whammy-style (expression-controlled pitch shifting, harmonizer intervals)

**Modulation additions:**
- Harmonic tremolo (separate rate for high/low frequencies — Fender brownface style)
- Through-zero flanging (jet-plane sweep — MXR style)
- True vibrato (pitch modulation, not amplitude — distinct from chorus)
- Uni-Vibe-style (photocell phaser/vibrato hybrid)

**Experimental / Ambient** (P3):
- Freeze (infinite sustain, hold a chord as a drone — EHX Freeze-style)
- Granular delay (chop audio into grains, manipulate pitch/density — Red Panda Particle-style)
- Synth tracking (guitar-to-synth voice — DigiTech Synth Wah-style)

**Looper:**
- Record / overdub / play / stop / undo
- Reverse and half-speed playback
- Up to 5 minutes loop time

### Naming Convention — No Trademarks in Code or UI

All pedal, amp, and cabinet names use **original creative names** — never trademarked names. This follows the Line 6 Helix / LePou / Kemper pattern (the proven industry approach for unlicensed amp modeling).

**Amp model names:**

| Internal Codename | Ship Name | Inspired By (docs only) |
|-------------------|-----------|------------------------|
| `fullerton_clean` | Fullerton Clean | Classic American clean amps |
| `brit_crunch` | British Crunch | Classic British crunch amps |
| `southwest_lead` | Southwest Lead | American high-gain amps |
| `essex_chime` | Essex Chime | British chime amps |
| `tweed_blues` | Tweed Blues | American tweed-era amps |

**Pedal names (examples):**

| Codename | Ship Name | Inspired By (docs only) |
|----------|-----------|------------------------|
| `jade_drive` | Jade Drive | Mid-humped Japanese overdrive circuits |
| `gold_drive` | Gold Drive | Transparent overdrive with clean blend |
| `blues_grit` | Blues Grit | Gritty FET-based overdrive |
| `rodent` | Rodent | Op-amp hard-clipping distortion |
| `orange_dist` | Orange Distortion | Bright hard-clipping distortion |
| `mammoth_fuzz` | Mammoth Fuzz | 4-stage scooped-mid fuzz |
| `round_fuzz` | Round Fuzz | Smooth germanium/silicon fuzz |
| `tone_bender` | Wraith Fuzz | Aggressive germanium fuzz |
| `chaos_fuzz` | Chaos Fuzz | Gated sputter, self-oscillation |
| `carbon_delay` | Carbon Delay | Warm analog delay with darkening repeats |
| `tape_machine` | Tape Machine | Tape echo with wow/flutter/degradation |
| `drip_verb` | Drip Verb | Classic spring reverb |
| `crybaby_wah` | Howl Wah | Expression-controlled bandpass sweep |
| `squeezebox` | Squeeze Box | Squashy OTA compressor |

**Rules:**
- **Code identifiers, API enums, UI labels, preset files:** Original names only (`FX_PEDAL_JADE_DRIVE`, never `FX_PEDAL_TUBE_SCREAMER`)
- **Documentation / README:** May reference real names with "inspired by" framing
- **Code comments:** Real names acceptable for clarity (`/* Tube Screamer-style mid-hump curve */`)
- **README disclaimer:** "All product names, trademarks, and registered trademarks mentioned in documentation are property of their respective owners and are used solely for identification and description purposes. 0xFX is not affiliated with, endorsed by, or sponsored by any of the companies mentioned."

The API boundary is the critical design element:

```c
// Public API — this is what frontends and plugins call
fx_engine_t *fx_engine_create(float sample_rate);
void         fx_engine_destroy(fx_engine_t *engine);
void         fx_engine_process(fx_engine_t *engine, float *in, float *out, int frames);

// Signal chain management — pedals (drag-and-drop reorder in GUI)
fx_pedal_id  fx_chain_add_pedal(fx_engine_t *engine, fx_pedal_type_t type, fx_chain_pos_t pos);
void         fx_chain_remove_pedal(fx_engine_t *engine, fx_pedal_id id);
void         fx_chain_move_pedal(fx_engine_t *engine, fx_pedal_id id, fx_chain_pos_t pos, int index);
void         fx_pedal_set_param(fx_engine_t *engine, fx_pedal_id id, int param, float value);
float        fx_pedal_get_param(fx_engine_t *engine, fx_pedal_id id, int param);
void         fx_pedal_set_bypass(fx_engine_t *engine, fx_pedal_id id, bool bypass);

// Multi-amp routing — split signal to parallel amp+cab chains
fx_chain_id  fx_chain_create(fx_engine_t *engine);         // add a parallel chain (default: 1 chain)
void         fx_chain_destroy(fx_engine_t *engine, fx_chain_id id);
void         fx_chain_set_mix(fx_engine_t *engine, fx_chain_id id, float level);  // 0.0-1.0 blend

// Amp model — per chain, every param accessible as a turnable knob
void         fx_amp_set_model(fx_engine_t *engine, fx_chain_id chain, fx_amp_type_t type);
void         fx_amp_set_param(fx_engine_t *engine, fx_chain_id chain, fx_amp_param_t param, float value);
float        fx_amp_get_param(fx_engine_t *engine, fx_chain_id chain, fx_amp_param_t param);
int          fx_amp_get_param_count(fx_engine_t *engine, fx_chain_id chain);
const char  *fx_amp_get_param_name(fx_engine_t *engine, fx_chain_id chain, fx_amp_param_t param);

// Cabinet IR — per chain
bool         fx_cab_load_ir(fx_engine_t *engine, fx_chain_id chain, const char *wav_path);
bool         fx_cab_generate_ir(fx_engine_t *engine, fx_chain_id chain, const fx_cab_params_t *params);
void         fx_cab_set_bypass(fx_engine_t *engine, fx_chain_id chain, bool bypass);

// Presets — open .0xfx JSON format
bool         fx_preset_save(fx_engine_t *engine, const char *path);
bool         fx_preset_load(fx_engine_t *engine, const char *path);
bool         fx_preset_import_nam(fx_engine_t *engine, const char *nam_path);  // NAM amp profiles

// User-designed pedals — load custom pedal definitions from JSON
bool         fx_pedal_load_custom(fx_engine_t *engine, const char *json_path);

// Tuner
float        fx_tuner_get_frequency(fx_engine_t *engine);
int          fx_tuner_get_note(fx_engine_t *engine);     // MIDI note number
float        fx_tuner_get_cents(fx_engine_t *engine);    // -50 to +50

// Audio device management (standalone only — plugins get I/O from host)
int          fx_audio_get_device_count(void);
const char  *fx_audio_get_device_name(int index);
bool         fx_audio_set_device(fx_engine_t *engine, int index);
bool         fx_audio_set_buffer_size(fx_engine_t *engine, int frames);
bool         fx_audio_set_sample_rate(fx_engine_t *engine, float rate);

// AI tone matching (async — out-of-process, BYOK, later phase)
fx_match_id  fx_tone_match_from_file(fx_engine_t *engine, const char *audio_path);
fx_match_id  fx_tone_match_from_url(fx_engine_t *engine, const char *url);
int          fx_tone_match_status(fx_engine_t *engine, fx_match_id id);  // 0=pending, 1=done, -1=error
bool         fx_tone_match_apply(fx_engine_t *engine, fx_match_id id);   // applies matched preset
```

Internal structs (`signal_chain_t`, `fx_pedal_t`, `amp_model_t`, etc.) are defined in private headers — the GUI never includes them.

## Capabilities

### New Capabilities

- `fx-engine-api`: Public C99 API for engine lifecycle, signal chain management, parameter control. Opaque handle design — callers never see internal structs.
- `amp-models`: Clean (Fender Twin), Crunch (Marshall JCM800), High Gain (Mesa Rectifier), British (Vox AC30), Tweed (Bassman). Each with preamp gain stages → tone stack EQ → power amp compression. GUI renders each as a skeuomorphic amp panel with turnable knobs matching real amp controls.
- `cabinet-ir`: Overlap-add FFT convolution. Import .wav IRs (standard format). Generate synthetic IRs from parametric descriptions (speaker Thiele-Small params, cabinet type, mic model/position). Bundled IRs are synthesized. Target: 48kHz, 32-bit float, 2048 samples.
- `signal-chain-routing`: Drag-and-drop pedalboard with pre/post amp placement. Supports signal splitting to multiple parallel amp+cab chains with per-chain post effects and mix/blend control. Enables dual/triple amp rigs.
- `open-rig-format`: JSON-based .0xfx format for presets (complete rig), amp profiles, cab IR metadata, and user-designed custom pedals. Importers for NAM (.nam) amp profiles and Line 6 .hlx presets where feasible.
- `audio-device-manager`: Enumerate, select, and hot-plug audio interfaces via miniaudio. Supports USB class-compliant (iRig, Scarlett), built-in audio, Bluetooth (with latency warning). Sample rate, buffer size, and channel mapping configuration.
- `clap-plugin`: CLAP plugin format — first-class citizen. Open format, thread-safe parameters, per-note modulation support. Primary plugin build target.
- `vst3-plugin`: VST3 plugin format — secondary, for DAW compatibility. Built via CPLUG alongside CLAP.
- `overdrive` (3 variants): Tube Screamer-style, Klon-style transparent, Blues Driver-style gritty.
- `distortion` (4 variants): RAT-style, DS-1-style, Metal Zone-style (parametric mid), amp-in-a-box.
- `fuzz` (4 variants): Big Muff-style, Fuzz Face (germanium/silicon), Tone Bender-style, Fuzz Factory-style (gated/chaos).
- `delay` (4 variants): Digital, analog/BBD, tape echo (wow/flutter), modulated (Memory Man-style).
- `reverb` (5 variants): Spring, plate, hall, shimmer, ambient/freeze.
- `chorus`: BBD-style analog (CE-2/Small Clone-style). Tri-chorus option.
- `phaser`: 4/6/8-stage allpass sweep. Phase 90-style simple + Small Stone-style deep.
- `flanger`: Standard + through-zero flanging.
- `tremolo`: Optical/bias + harmonic tremolo (separate high/low frequency rates).
- `vibrato`: True pitch vibrato (distinct from chorus).
- `wah`: Expression wah (Cry Baby-style), auto-wah/envelope filter (Q-Tron-style), fixed wah.
- `compressor` (3 variants): Squashy OTA (Dyna Comp-style), transparent (Keeley-style + blend), 1176-style (fast, sidechain HP).
- `eq`: 7/10-band graphic + 3-band parametric.
- `noise-gate`: Intelligent noise reduction. Threshold, attack, release, hold params.
- `octaver`: Polyphonic (POG-style) + monophonic. Pitch bend (Whammy-style expression control).
- `bitcrusher`: Sample rate and bit depth reduction.
- `ring-mod`: Carrier frequency modulation.
- `tape-saturation`: Warm analog compression via tanh + warmth LP.
- `looper`: Record/overdub/play/stop/undo, reverse, half-speed, up to 5 min.
- `freeze`: Infinite sustain / drone pedal.
- `granular`: Granular delay/pitch (Particle-style).
- `tuner`: Autocorrelation-based chromatic tuner with note name + cents display.
- `ai-tone-match` (later phase): BYOK agentic tone matching. User provides audio/URL, spectral analysis extracts tone fingerprint, LLM agent (user's API key) configures matching rig via engine API. Outputs .0xfx preset.
- `asset-generation` (dev-time): LoRA-based image generation pipeline (Replicate + Flux Pro) for cohesive skeuomorphic pedal/amp/cab visuals. Templated prompts with fixed seeds for style consistency.

### Ported from 0x808/0xSYNTH

DSP algorithms ported and wrapped in new API:
- Overdrive (soft-clip), fuzz (hard-clip), chorus, delay (with BPM sync), Freeverb reverb, phaser (6-stage), flanger, tremolo (3 waveforms), compressor, tape saturation, bitcrusher, ring mod, shimmer reverb, biquad filter (LP/HP/BP)
- From 0xSYNTH: notch filter, ladder (24dB/oct), comb filter, formant filter

New to 0xFX (not in 0x808/0xSYNTH):
- Amp modeling, cabinet IR convolution + synthesis, noise gate, wah/envelope filter, EQ (graphic + parametric), octaver/pitch shifting, looper, tuner, spring/plate/hall reverb, tape echo, analog delay, through-zero flanging, harmonic tremolo, true vibrato, freeze, granular, multi-amp signal routing, open rig format, audio device management, AI tone matching

## Impact

- **New codebase**: ~50+ source files across engine API, engine internals, GUI, and host wrappers
- **Dependencies** (vendored, single-header where possible): miniaudio (audio I/O + device enumeration), dr_wav, ImGui, SDL2, CPLUG (CLAP + VST3), KissFFT (IR convolution), cJSON (preset format)
- **AI dependencies** (later phase): Out-of-process agent using user's LLM API key. No AI deps in core build.
- **Build system**: CMake with targets for standalone, CLAP plugin (primary), VST3 plugin (secondary), and API tests
- **Dev tools**: Python script in `tools/` for AI asset generation (Replicate/Flux API), IR synthesis tool
- **Platform dependencies**: SDL2 (windowing/input), OpenGL (rendering)
- **Bundled assets**: Synthesized cabinet IR wavs (no licensing concerns), AI-generated pedal/amp/cab graphics, default preset files
- **Target platforms**: Linux (gcc), Windows (MinGW cross-compile from Linux)
- **Key architectural constraints**:
  - All engine interaction goes through `fx_engine_*()` API — no direct struct access from GUI or plugin layers
  - CLAP is the primary plugin format; VST3 is secondary
  - .0xfx JSON is the open rig/preset format
  - AI tone matching is BYOK (user's own API key) and later phase
