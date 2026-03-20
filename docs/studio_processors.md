# Post-Amp Studio Processors

> Specification for studio-grade processing in the post-amp/cab signal chain.
> These replace pedal effects in the post-amp [+] menu — guitar pedals belong before the amp,
> studio rack gear belongs after.

## Rationale

In a real recording chain, the signal after the amp/cab goes through studio outboard gear:
compressors, EQs, tape machines, etc. The current post-amp [+] shows guitar pedals,
which doesn't reflect how a recording chain actually works. This spec defines studio
processors that belong in that position.

## Signal Flow Context

```
Input → [Gate] → [Pre Pedals] → AMP → CAB → [Studio Processors] → Output
                  (guitar pedals)              (rack gear)
```

---

## Studio Processor Types

### 1. COMPRESSORS

#### Iron Squeeze (inspired by: 1176-style FET compressor)
- **Character**: Fast, aggressive, punchy. Classic rock/metal tracking compressor.
- **Parameters**:
  - Input (drive into compressor) — 0.0–1.0
  - Output (makeup gain) — 0.0–1.0
  - Attack (0.02ms–10ms) — 0.0–1.0
  - Release (50ms–1200ms) — 0.0–1.0
  - Ratio (4:1, 8:1, 12:1, 20:1, All) — discrete selector
- **DSP**: Feed-forward FET-style compression with program-dependent release. All-buttons mode = parallel ratio summing.
- **Asset style**: Black faceplate, VU meter, silver knobs. 1U rack unit.

#### Velvet Press (inspired by: LA-2A optical compressor)
- **Character**: Smooth, musical, program-dependent. Vocals, clean guitar, bass.
- **Parameters**:
  - Peak Reduction — 0.0–1.0
  - Gain — 0.0–1.0
  - Mode (Compress / Limit) — toggle
- **DSP**: Optical-style with T4 cell emulation. Slow attack, program-dependent release. Two-stage: limiter → compressor.
- **Asset style**: Silver/gray faceplate, large VU meter, two big knobs. 2U rack unit.

#### Glue Bus (inspired by: SSL G-bus compressor)
- **Character**: Glue, punch, mix bus cohesion. Subtle to aggressive.
- **Parameters**:
  - Threshold (-20dB to +10dB) — 0.0–1.0
  - Ratio (2:1, 4:1, 10:1) — discrete
  - Attack (0.1ms, 0.3ms, 1ms, 3ms, 10ms, 30ms) — discrete
  - Release (0.1s, 0.3s, 0.6s, 1.2s, Auto) — discrete
  - Makeup — 0.0–1.0
- **DSP**: VCA-style with RMS detection. Auto-release uses program-dependent timing.
- **Asset style**: Blue/teal faceplate, center-detent knobs, compact 1U.

### 2. EQUALIZERS

#### Glass EQ (inspired by: Pultec EQP-1A passive EQ)
- **Character**: Musical, broad, sweet top end. Boost-and-cut low end trick.
- **Parameters**:
  - Low Freq (20, 30, 60, 100 Hz) — discrete
  - Low Boost — 0.0–1.0
  - Low Cut — 0.0–1.0 (attenuation)
  - High Freq (3k, 4k, 5k, 8k, 10k, 12k, 16k Hz) — discrete
  - High Boost — 0.0–1.0
  - High Atten (bandwidth) — 0.0–1.0
- **DSP**: Passive EQ topology with inductor/capacitor modeling. Simultaneous boost+cut on low end creates a characteristic dip-then-boost curve.
- **Asset style**: Cream/beige faceplate with chicken-head knobs, 2U.

#### Precision EQ (inspired by: Neve 1073/1084 channel EQ)
- **Character**: Warm, thick, musical. Transformer-colored.
- **Parameters**:
  - High Pass (off, 50, 80, 160, 300 Hz) — discrete
  - Low Shelf (35, 60, 110, 220 Hz) — freq selector + gain ±16dB
  - Mid Bell (0.36k–7.2k Hz) — freq + gain ±18dB + bandwidth
  - High Shelf (12k fixed) — gain ±16dB
- **DSP**: Proportional-Q EQ bands. Transformer saturation on signal path. Inductor-modeled resonance.
- **Asset style**: Dark blue/maroon faceplate, rotary switches, 1U.

### 3. TAPE / SATURATION

#### Reel Warmth (inspired by: Studer A800 / Ampex ATR tape machine)
- **Character**: Warmth, compression, harmonic richness, subtle wow/flutter.
- **Parameters**:
  - Input (drive into tape) — 0.0–1.0
  - Speed (7.5 ips, 15 ips, 30 ips) — discrete (affects frequency response)
  - Bias — 0.0–1.0 (under-bias = more harmonics, over-bias = duller)
  - Wow/Flutter — 0.0–1.0
  - Output — 0.0–1.0
- **DSP**: Hysteresis-based tape saturation (Jiles-Atherton simplified). Speed-dependent frequency response (HF rolloff at 7.5, flatter at 30). LFO-modulated pitch for wow/flutter.
- **Asset style**: Wood side panels, silver transport controls, VU meters. 3U.

#### Valve Color (inspired by: Thermionic Culture Vulture / tube saturation)
- **Character**: Rich harmonics, from subtle warmth to full distortion.
- **Parameters**:
  - Drive — 0.0–1.0
  - Bias (class A / class AB / class B) — discrete
  - Output — 0.0–1.0
  - Mix (dry/wet) — 0.0–1.0
- **DSP**: Triode/pentode waveshaping with bias-dependent even/odd harmonic balance. Class A = mostly even harmonics (warm), Class B = odd harmonics (edgy).
- **Asset style**: Chrome faceplate, glowing valve window, industrial knobs. 1U.

### 4. LIMITERS

#### Brick Wall (inspired by: modern mastering limiter)
- **Character**: Transparent loudness maximizer. Last in chain.
- **Parameters**:
  - Threshold (-12dB to 0dB) — 0.0–1.0
  - Ceiling (-1dB to 0dB) — 0.0–1.0
  - Release (fast / medium / slow / auto) — discrete
- **DSP**: Look-ahead brickwall limiter with inter-sample peak detection. Automatic gain reduction visualization.
- **Asset style**: Minimal black faceplate, LED gain reduction meter. 1U.

### 5. STEREO / SPATIAL

#### Room Engine (inspired by: convolution reverb / room simulation)
- **Character**: Studio room ambience, live room feel.
- **Parameters**:
  - Room (Tight Booth, Live Room, Large Hall) — discrete
  - Size — 0.0–1.0
  - Damping — 0.0–1.0
  - Mix — 0.0–1.0
- **DSP**: Short convolution or early-reflection network for room simulation. Not a reverb pedal — this emulates the physical recording space.
- **Asset style**: Digital display, LED meters, dark faceplate. 1U.

---

## Implementation Plan

### Phase 1: Core processors (minimum viable set)
1. **Iron Squeeze** (FET compressor) — most essential post-amp processor
2. **Glass EQ** (passive EQ) — musical tone shaping
3. **Reel Warmth** (tape saturation) — warmth and character
4. **Brick Wall** (limiter) — output protection

### Phase 2: Extended set
5. **Velvet Press** (optical compressor)
6. **Precision EQ** (channel EQ)
7. **Valve Color** (tube saturation)
8. **Glue Bus** (bus compressor)
9. **Room Engine** (room simulation)

### Engine API Extension

```c
/* Studio processor types — separate from pedal types */
typedef enum {
    FX_STUDIO_IRON_SQUEEZE,     /* FET compressor */
    FX_STUDIO_VELVET_PRESS,     /* Optical compressor */
    FX_STUDIO_GLUE_BUS,         /* Bus compressor */
    FX_STUDIO_GLASS_EQ,         /* Passive EQ */
    FX_STUDIO_PRECISION_EQ,     /* Channel EQ */
    FX_STUDIO_REEL_WARMTH,      /* Tape saturation */
    FX_STUDIO_VALVE_COLOR,      /* Tube saturation */
    FX_STUDIO_BRICK_WALL,       /* Limiter */
    FX_STUDIO_ROOM_ENGINE,      /* Room simulation */
    FX_STUDIO_COUNT
} fx_studio_type_t;

/* Studio processor API — mirrors pedal API pattern */
fx_studio_id  fx_studio_add(fx_engine_t *engine, fx_studio_type_t type);
void          fx_studio_remove(fx_engine_t *engine, fx_studio_id id);
void          fx_studio_set_param(fx_engine_t *engine, fx_studio_id id, int param, float value);
float         fx_studio_get_param(fx_engine_t *engine, fx_studio_id id, int param);
void          fx_studio_set_bypass(fx_engine_t *engine, fx_studio_id id, bool bypass);
const char   *fx_studio_get_type_name(fx_studio_type_t type);
int           fx_studio_get_param_count(fx_studio_type_t type);
const char   *fx_studio_get_param_name(fx_studio_type_t type, int param);
```

### GUI Changes

- Post-amp [+] button opens **Studio Rack** menu (not pedal gallery)
- Pre-amp [+] continues to show guitar pedal gallery
- Studio processors render as rack-mount units (wider, horizontal, silver/dark faceplates)
- Different node color in signal chain to distinguish from pedals

### Asset Generation Guide

Studio rack gear aesthetic:
- **Style**: Professional rack-mount equipment, 19" rack units
- **Palette**: Silver, black, dark blue, cream faceplates
- **Details**: VU meters, LED ladders, rotary switches, printed scales
- **Wear**: Subtle — light scratches on faceplate, slightly worn knob markings
- **Format**: `resources/studio/{name}_body_nobg.png` (transparent background)
- **Size**: 1024x256 for 1U, 1024x512 for 2U (wider than tall, rack-mount proportions)
