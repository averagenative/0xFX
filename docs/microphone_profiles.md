# Microphone Profiles for Cabinet Simulation

> OPTIONAL feature — DI (direct inject) from cab is the default.
> Microphone model + placement is opt-in per chain for tonal variety.

## Concept

After the cabinet IR, an optional microphone simulation applies:
1. **Mic frequency response** — each mic model colors the sound differently
2. **Mic placement** — distance, angle, and position relative to the speaker cone
3. These combine to produce the tone differences that recording engineers achieve by moving mics around in front of a cab

```
CAB IR → [Mic Sim (optional)] → Post-Amp Processing → Output
          ↑                ↑
          model            placement
```

Default: **DI Mode** (no mic coloration — raw cab output, like a direct box from the cab)

---

## Microphone Models

### Dynamic Microphones

#### Stage Workhorse (inspired by: Shure SM57)
- **Original name**: `stage_workhorse`
- **Character**: Bright presence peak, mid-focused, handles high SPL. The #1 guitar cab mic.
- **Frequency response profile**:
  - Gentle rolloff below 200Hz
  - Broad presence peak +5dB at 5–6kHz
  - Rolloff above 12kHz
  - Proximity effect: +6dB shelf below 200Hz when < 2 inches
- **Polar pattern**: Cardioid
- **Placement sensitivity**: HIGH — small movements = big tonal changes

#### Roadie Vocal (inspired by: Shure SM58)
- **Original name**: `roadie_vocal`
- **Character**: Similar to Stage Workhorse but with smoother presence peak, warmer overall.
- **Frequency response profile**:
  - Rolloff below 150Hz (built-in bass rolloff)
  - Gentler presence peak +3dB at 5kHz (integrated windscreen dampens HF slightly)
  - Rolloff above 14kHz
  - Less proximity effect than Stage Workhorse
- **Polar pattern**: Cardioid
- **Placement sensitivity**: MEDIUM

#### Berlin Dynamic (inspired by: Sennheiser e609/e906)
- **Original name**: `berlin_dynamic`
- **Character**: Flat response, slightly scooped mids, extended lows. Great for "hang it in front of the cab" technique.
- **Frequency response profile**:
  - Flat to 100Hz, then gentle rolloff
  - Slight scoop at 400–800Hz (-2dB)
  - Broad presence rise 3–8kHz (+3dB)
  - Rolloff above 14kHz
  - Supercardioid rejection helps with bleed
- **Polar pattern**: Supercardioid
- **Placement sensitivity**: LOW — forgiving, sounds good almost anywhere

#### Silver Bullet (inspired by: Electro-Voice RE20)
- **Original name**: `silver_bullet`
- **Character**: Flat, neutral, minimal proximity effect. Broadcast quality.
- **Frequency response profile**:
  - Variable-D design: minimal proximity effect at any distance
  - Flat 45Hz–15kHz
  - Gentle presence rise at 4kHz (+2dB)
  - Clean rolloff above 15kHz
- **Polar pattern**: Cardioid
- **Placement sensitivity**: LOW — consistent at any distance

### Ribbon Microphones

#### Velvet Ribbon (inspired by: Royer R-121)
- **Original name**: `velvet_ribbon`
- **Character**: Dark, smooth, warm. Natural HF rolloff that tames harsh amps. Favorite for high-gain.
- **Frequency response profile**:
  - Extended low end, flat to 50Hz
  - Flat midrange
  - Gentle HF rolloff starting at 8kHz, -6dB at 12kHz
  - Figure-8 rear lobe captures room (blendable)
  - Strong proximity effect
- **Polar pattern**: Figure-8
- **Placement sensitivity**: MEDIUM — distance matters more than angle
- **Note**: Often blended 50/50 with Stage Workhorse for best-of-both

#### Heritage Ribbon (inspired by: Coles 4038)
- **Original name**: `heritage_ribbon`
- **Character**: Rich, vintage, BBC broadcast warmth. Pronounced low-mid body.
- **Frequency response profile**:
  - Strong low-mid emphasis 150–500Hz (+3dB)
  - Smooth midrange
  - Steep HF rolloff above 6kHz
  - Very warm, almost "woolly" character
- **Polar pattern**: Figure-8
- **Placement sensitivity**: MEDIUM

### Condenser Microphones

#### Studio Large (inspired by: Neumann U87)
- **Original name**: `studio_large`
- **Character**: Detailed, open, airy. Captures everything — best for clean/ambient tones.
- **Frequency response profile**:
  - Extended flat response 20Hz–16kHz
  - Subtle presence lift at 8–10kHz (+2dB)
  - Very extended top end
  - Pronounced proximity effect in cardioid
  - Can sound harsh on very high-gain amps
- **Polar pattern**: Switchable (Cardioid / Figure-8 / Omni)
- **Placement sensitivity**: HIGH — captures room reflections, distance critical

#### Austrian Pencil (inspired by: AKG C414)
- **Original name**: `austrian_pencil`
- **Character**: Versatile, slightly bright, detailed. Studio workhorse condenser.
- **Frequency response profile**:
  - Flat 30Hz–10kHz
  - Presence peak at 10–12kHz (+3dB)
  - Extended to 20kHz
  - Switchable pad and HPF
- **Polar pattern**: Switchable (Cardioid / Hyper / Figure-8 / Omni)
- **Placement sensitivity**: MEDIUM-HIGH

#### Room Pencil (inspired by: AKG C451 / Neumann KM84)
- **Original name**: `room_pencil`
- **Character**: Crisp, fast transients, bright. Best as a room/distance mic.
- **Frequency response profile**:
  - Tight low end, rolloff below 100Hz
  - Flat midrange
  - Crisp presence peak 8–12kHz (+4dB)
  - Very extended top end
  - Small diaphragm = accurate transients
- **Polar pattern**: Cardioid
- **Placement sensitivity**: HIGH — primarily a distance mic

---

## Placement Parameters

### Distance (0.0 – 1.0)
- **0.0** = touching the grille cloth (~0.5 inches) — maximum proximity effect
- **0.3** = close mic standard (~2 inches) — balanced, punchy
- **0.5** = moderate distance (~6 inches) — more room, less proximity
- **0.7** = distant (~12 inches) — open, ambient
- **1.0** = far room mic (~3+ feet) — room character dominates

**DSP effect**: Controls proximity effect (bass boost curve), direct-to-room ratio, and HF air.

### Angle (0.0 – 1.0)
- **0.0** = on-axis (pointing straight at speaker cone center) — brightest, most attack
- **0.5** = 45° off-axis — balanced, common studio placement
- **1.0** = 90° off-axis (perpendicular) — darkest, most muffled

**DSP effect**: Progressive HF rolloff as angle increases. Frequency-dependent: higher frequencies attenuate faster off-axis.

### Position (0.0 – 1.0)
- **0.0** = dead center of speaker cone (dust cap) — brightest, most aggressive
- **0.5** = between center and edge — classic balanced position
- **1.0** = edge of cone (surround) — warmest, darkest, least harsh

**DSP effect**: Shifts the frequency response — center has more HF content, edge has more low-mid warmth.

### Proximity Effect Model

Bass boost = `proximity_gain * (1.0 - distance) * (frequency < cutoff ? 1.0 : rolloff)`

- Dynamic mics: moderate proximity (cutoff ~300Hz, max +6dB)
- Ribbon mics: strong proximity (cutoff ~400Hz, max +10dB)
- Condenser mics: variable (cardioid = strong, omni = none)
- Silver Bullet (Variable-D): minimal regardless of distance

---

## Engine API Design

```c
/* Mic types */
typedef enum {
    FX_MIC_DI,               /* Direct inject — no mic coloration (default) */
    FX_MIC_STAGE_WORKHORSE,  /* SM57-style dynamic */
    FX_MIC_ROADIE_VOCAL,     /* SM58-style dynamic */
    FX_MIC_BERLIN_DYNAMIC,   /* e609-style dynamic */
    FX_MIC_SILVER_BULLET,    /* RE20-style dynamic */
    FX_MIC_VELVET_RIBBON,    /* R-121-style ribbon */
    FX_MIC_HERITAGE_RIBBON,  /* Coles 4038-style ribbon */
    FX_MIC_STUDIO_LARGE,     /* U87-style condenser */
    FX_MIC_AUSTRIAN_PENCIL,  /* C414-style condenser */
    FX_MIC_ROOM_PENCIL,      /* C451/KM84-style condenser */
    FX_MIC_COUNT
} fx_mic_type_t;

/* Mic placement parameters */
typedef enum {
    FX_MIC_PARAM_DISTANCE,   /* 0.0 (touching) to 1.0 (room) */
    FX_MIC_PARAM_ANGLE,      /* 0.0 (on-axis) to 1.0 (off-axis) */
    FX_MIC_PARAM_POSITION,   /* 0.0 (cone center) to 1.0 (cone edge) */
    FX_MIC_PARAM_COUNT
} fx_mic_param_t;

/* API functions */
void         fx_mic_set_type(fx_engine_t *engine, fx_chain_id chain, fx_mic_type_t type);
fx_mic_type_t fx_mic_get_type(fx_engine_t *engine, fx_chain_id chain);
void         fx_mic_set_param(fx_engine_t *engine, fx_chain_id chain, fx_mic_param_t param, float value);
float        fx_mic_get_param(fx_engine_t *engine, fx_chain_id chain, fx_mic_param_t param);
const char  *fx_mic_get_type_name(fx_mic_type_t type);
```

---

## DSP Implementation Notes

The mic simulation is implemented as a post-cab EQ filter chain:

1. **Base frequency response**: Per-mic-model biquad filter chain (2–4 biquads)
   - Defines the inherent frequency response character of each mic
2. **Position modifier**: Adjusts HF content based on cone position
   - Center → edge = progressive LP filter cutoff reduction
3. **Angle modifier**: Off-axis HF rolloff
   - On-axis → off-axis = steeper LP filter
4. **Distance / proximity effect**: Low-frequency boost
   - Near → far = variable low shelf gain
   - Model-dependent: ribbons have more, Variable-D has none

All filters use biquad coefficients recalculated only when parameters change (not per-sample). Processing is 2–4 biquad sections = very lightweight, real-time safe.

---

## Asset Generation Guide

Microphone PNG assets:
- **Style**: Studio microphones, photorealistic but with worn grime aesthetic
- **Details**: Scuffed bodies, slightly worn finish, realistic grille mesh
- **Format**: `resources/mics/{name}_nobg.png` (transparent background)
- **Size**: 512x512 or 256x512 (portrait orientation for most mics)
- **View**: 3/4 angle showing body + grille, as if positioned in front of a cab
- **Variations**: Could show mic on a stand clip for the signal chain view

## Blending / Multi-Mic

Future enhancement: allow two mics per cab (e.g., Stage Workhorse + Velvet Ribbon blend) with individual placement and a blend knob. This is how many professional recordings are done. Not in initial implementation — single mic per chain first.
