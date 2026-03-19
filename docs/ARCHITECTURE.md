# 0xFX Architecture

## Signal Flow

```
Guitar Input
    │
    ▼
┌──────────┐
│ Input    │  Level, noise gate threshold
│ Stage    │
└────┬─────┘
     │
     ▼
┌──────────────────────────────┐
│ Pre-Amp Pedal Chain          │  User-ordered effects
│ [OD] → [Comp] → [Wah] → .. │  (drag to reorder)
└────────────┬─────────────────┘
             │
             ▼
┌──────────────────────────────┐
│ Amp Model                    │
│ ┌─────────┐ ┌──────┐ ┌────┐│
│ │Preamp   │→│Tone  │→│Pwr ││
│ │Gain     │ │Stack │ │Amp ││
│ │Stages   │ │(EQ)  │ │    ││
│ └─────────┘ └──────┘ └────┘│
└────────────┬─────────────────┘
             │
             ▼
┌──────────────────────────────┐
│ Cabinet IR                   │  Convolution with
│ (Impulse Response)           │  .wav IR files
└────────────┬─────────────────┘
             │
             ▼
┌──────────────────────────────┐
│ Post-Amp Pedal Chain         │  Delay, reverb, etc.
│ [Delay] → [Reverb] → ..     │
└────────────┬─────────────────┘
             │
             ▼
         Output

```

## Amp Modeling Approach

Each amp model consists of three stages:

### 1. Preamp (Gain Stages)
- Cascaded waveshaping stages (1-4 stages depending on gain level)
- Each stage: input gain → waveshaper → coupling capacitor (highpass)
- Waveshaper options:
  - `tanh(x)` — soft tube-like clipping
  - `x / (1 + |x|)` — asymmetric soft clipping
  - `atan(x)` — moderate saturation
  - Hard clip with bias — transistor-like

### 2. Tone Stack (EQ)
- Fender: Bass/Mid/Treble passive network (scooped mids default)
- Marshall: Bass/Mid/Treble (pronounced mids)
- Mesa: 5-band graphic + contour
- Implementation: cascaded biquad filters matching real component values

### 3. Power Amp
- Soft compression (simulates output transformer saturation)
- Presence/resonance controls
- Sag (supply voltage droop under load — makes it feel "spongy")

## Cabinet IR Convolution

### Algorithm: Overlap-Add FFT
1. Load IR wav file (typically 200-2000 samples at 44.1kHz)
2. Zero-pad IR to next power of 2
3. Pre-compute IR FFT once on load
4. For each audio block:
   - FFT the input block
   - Multiply with IR FFT (complex multiplication)
   - IFFT to get convolved output
   - Overlap-add with previous block's tail

### IR Sources (CC0/Free)
- Celestion (official free packs)
- Ownhammer free packs
- GuitarHack impulses
- Users can load any .wav IR file

## Tuner

- **Algorithm**: Autocorrelation-based pitch detection
  1. Apply window function to input buffer
  2. Compute autocorrelation
  3. Find first peak after initial dip = fundamental period
  4. Convert period to frequency
  5. Map to nearest note name + cents offset
- **Display**: Note name + pitch bar (-50 to +50 cents)
- **Update rate**: ~30Hz (every ~1500 samples at 44.1kHz)

## Pedal System

### Pedal Interface
```c
typedef struct {
    const char *name;          // "Tube Screamer"
    pedal_type_t type;         // PEDAL_OVERDRIVE
    bool bypass;               // true = pass through
    float params[MAX_PARAMS];  // up to 8 parameters
    void *state;               // heap-allocated state
} fx_pedal_t;
```

### Signal Chain
```c
typedef struct {
    fx_pedal_t pre_pedals[MAX_PEDALS];   // before amp
    int num_pre;
    amp_model_t amp;                      // amp model
    cab_ir_t cab;                         // cabinet IR
    fx_pedal_t post_pedals[MAX_PEDALS];  // after amp
    int num_post;
    tuner_t tuner;                        // always-on tuner
    noise_gate_t gate;                    // input gate
} signal_chain_t;
```

## GUI Layout

```
┌─────────────────────────────────────────────────────────┐
│ [Tuner: A4 440Hz ═══════●═══════]  [Preset ▼] [?] [X] │
├─────────────────────────────────────────────────────────┤
│                                                         │
│  PRE-AMP PEDALS                                        │
│  ┌─────┐ ┌─────┐ ┌─────┐ ┌─────┐           ┌───┐     │
│  │ OD  │ │Comp │ │ Wah │ │     │    ...    │ + │     │
│  │ (●) │ │ (●) │ │ (●) │ │     │           │   │     │
│  │○ ○ ○│ │○ ○  │ │○ ○  │ │     │           └───┘     │
│  └─────┘ └─────┘ └─────┘ └─────┘                      │
│                                                         │
├─────────────────────────────────────────────────────────┤
│  AMP                              CAB                   │
│  ┌─────────────────────┐  ┌──────────────────┐         │
│  │ [Clean ▼]           │  │ [4x12 Marshall ▼]│         │
│  │  Gain  Volume       │  │                  │         │
│  │  (●)    (●)         │  │  ████████████    │         │
│  │ Bass Mid Treble     │  │  (IR waveform)   │         │
│  │ (●)  (●)  (●)      │  │                  │         │
│  │ Presence  Sag       │  │  [Load IR...]    │         │
│  │  (●)      (●)      │  │                  │         │
│  └─────────────────────┘  └──────────────────┘         │
│                                                         │
├─────────────────────────────────────────────────────────┤
│  POST-AMP PEDALS                                       │
│  ┌─────┐ ┌─────┐ ┌─────┐                    ┌───┐     │
│  │Delay│ │ Rev │ │     │        ...         │ + │     │
│  │ (●) │ │ (●) │ │     │                    │   │     │
│  │○ ○ ○│ │○ ○  │ │     │                    └───┘     │
│  └─────┘ └─────┘ └─────┘                              │
└─────────────────────────────────────────────────────────┘
```
