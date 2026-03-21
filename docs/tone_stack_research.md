# Tone Stack Research — Circuit-Accurate Models

## Overview

The tone stacks in 0xFX are modeled from real amplifier circuit schematics. Instead of
generic biquad EQ filters with arbitrary frequency/gain values, each amp model uses a
digitized version of its actual analog R/C tone stack network.

## Tone Stack Topologies

### 1. Fender TMB (Treble-Mid-Bass)

**Used by:** Fullerton Clean, Emerald Deluxe, Tweed Blues, Southwest Lead, Meridian High Gain, Solar Monolith, Eclipse Drone

The classic Fender tone stack is a passive R/C ladder network placed between preamp stages.
It was first used in the 5F6-A Bassman and became the basis for nearly all American amp
tone stacks (and was adapted by Marshall for their amps).

**Circuit topology:**
```
            C1
In ──┤├──┬──────┬── Out
     R1  │      │
    (T)  C3    R4
         │      │
         R3    C2
        (M)    │
         │    R2
         ┴   (B)
              │
              ┴
```

**Transfer function** (s-domain, from Yeh/Abel/Smith, DAFX 2006):

```
H(s) = (b1*s^2 + b2*s + 1) / (a0*s^3 + a1*s^2 + a2*s + 1)
```

Where coefficients are products of R and C values multiplied by pot positions (t, m, b = 0..1).
The key characteristic is that controls are interactive: changing bass affects treble response
and vice versa, which is what creates the distinctive Fender "feel."

**Component values per model:**

| Model | R1 | C1 | R2 | C2 | R3 | C3 | R4 | Character |
|-------|----|----|----|----|----|----|-----|-----------|
| Fullerton Clean | 250k | 250pF | 1M | 0.1uF | 25k | 47nF | 56k | Classic silver panel mid-scoop |
| Emerald Deluxe | 250k | 250pF | 1M | 0.1uF | 10k | 47nF | 56k | AB763 Deluxe Reverb — 10k mid pot, warmer mids, earlier breakup |
| Tweed Blues | 250k | 250pF | 1M | 0.1uF | 10k | 22nF | 56k | Warmer, less scooped than blackface |
| Southwest Lead | 250k | 250pF | 1M | 68nF | 25k | 33nF | 39k | Tight bass, deep V-curve scoop |
| Meridian High Gain | 250k | 500pF | 1M | 22nF | 25k | 22nF | 33k | 5150 lead ch. schematic values |
| Solar Monolith | 250k | 500pF | 1M | 220nF | 25k | 100nF | 68k | Massive bass, warm mids |
| Eclipse Drone | 250k | 680pF | 1M | 330nF | 25k | 150nF | 82k | Extreme bass, subsonic emphasis |

**Sources:** Fender Twin Reverb AB763 schematic, Fender Deluxe Reverb AB763 schematic, 5F6-A Bassman schematic, Mesa Dual Rectifier service manual, Peavey 5150 schematic (1992).

### 2. Marshall TMB

**Used by:** British Crunch (Plexi), Regent 800 (JCM800), Citrus Roar (Rockerverb)

Same topology as Fender TMB but with different component values. Jim Marshall's original
amps were based on the Bassman circuit, so the topology is identical. The differences
in component values create the Marshall's characteristic mid-forward voicing with tighter
bass response compared to Fender.

**Component values per model:**

| Model | R1 | C1 | R2 | C2 | R3 | C3 | R4 | Character |
|-------|----|----|----|----|----|----|-----|-----------|
| British Crunch | 220k | 470pF | 1M | 22nF | 25k | 22nF | 33k | Classic Plexi warmth |
| Regent 800 | 220k | 470pF | 1M | 22nF | 25k | 22nF | 33k | JCM800 bright punch |
| Citrus Roar | 250k | 470pF | 1M | 47nF | 25k | 22nF | 39k | Rockerverb 50 schematic values |

**Key difference from Fender:** Smaller C2 (22nF vs 100nF) = tighter bass.
Larger C1 (470pF vs 250pF) = slightly brighter treble response.

**Sources:** Marshall JTM45/1959 schematic, JCM800 2203 schematic, Orange Rockerverb 50 schematic.

### 3. Vox Cut Control

**Used by:** Essex Chime (AC30)

The Vox AC30 does NOT use a TMB tone stack. The Top Boost channel has bass/treble controls,
but the defining EQ characteristic comes from the "Cut" control on the output — a simple
first-order lowpass (treble cut) that rolls off high frequencies.

**Circuit:** 1M log pot with capacitor to ground. The Cut control varies the cutoff
frequency of a 1st-order lowpass filter.

- Cut at 0 (fully open): cutoff ~20kHz (effectively bypassed, maximum brightness)
- Cut at 1 (fully closed): cutoff ~1kHz (very dark)

The jangly, chimey character of the AC30 comes from its inherently bright preamp
design — the Cut control tames the top end to taste.

**Implementation:** Logarithmic frequency interpolation between 1kHz and 20kHz,
mapped to a biquad lowpass filter.

**Source:** Vox AC30/6 Top Boost schematic.

### 4. Tilt EQ

**Used by:** Citrus Terror (Tiny Terror)

The Tiny Terror uses a single "Tone" knob that acts as a tilt EQ — a seesaw between
bass and treble. This is simpler than a TMB stack.

- Tone at 0.0: maximum bass boost, maximum treble cut (warm/dark)
- Tone at 0.5: flat response
- Tone at 1.0: maximum bass cut, maximum treble boost (bright/cutting)

**Implementation:** High shelf filter at 800Hz with gain range of +/-12dB,
controlled by the single Tone parameter.

**Source:** Orange Tiny Terror schematic.

## Digitization Method

### Bilinear Transform

The analog transfer functions are digitized using the bilinear transform:

```
s = (2 * sr) * (1 - z^-1) / (1 + z^-1)
```

For the 3rd-order TMB system, we substitute this into:

```
H(s) = (b1*s^2 + b2*s + 1) / (a0*s^3 + a1*s^2 + a2*s + 1)
```

and multiply through by (1 + z^-1)^3 to get the z-domain transfer function.

The polynomial expansion patterns used:
- (1-z^-1)^3 = [1, -3, +3, -1]
- (1-z^-1)^2(1+z^-1) = [1, -1, -1, +1]
- (1-z^-1)(1+z^-1)^2 = [1, +1, -1, -1]
- (1+z^-1)^3 = [1, +3, +3, +1]

No frequency pre-warping is applied since the tone stack's interesting frequencies
(100Hz-5kHz) are well below Nyquist at typical sample rates (44.1kHz+).

### Filter Implementation

The 3rd-order IIR filter uses **transposed direct form II** for numerical stability:

```
y[n] = b0*x[n] + z1
z1   = b1*x[n] - a1*y[n] + z2
z2   = b2*x[n] - a2*y[n] + z3
z3   = b3*x[n] - a3*y[n]
```

Denormals are flushed to zero to prevent self-oscillation artifacts.

## Presence Control

The presence control is modeled separately from the tone stack. In real amps,
presence is typically a negative feedback network in the power amp section,
not part of the passive tone stack. It's implemented as a high shelf filter
with a center frequency of 3.5-7kHz depending on the amp model.

## Passive Attenuation Compensation

Real TMB tone stacks are passive networks that attenuate the signal by approximately
10-15dB even with all controls at noon. The makeup gain in the power amp section
compensates for this: circuit-modeled TMB stacks use 12x makeup gain vs 4x for
simpler topologies (Vox cut, tilt EQ).

## References

1. Yeh, Abel, Smith. "Digital Implementation of Musical Distortion Circuits
   by Analysis and Simulation." Proc. of the 9th Int. Conference on Digital
   Audio Effects (DAFX-06), Montreal, Canada, 2006.

2. Fender Twin Reverb AB763 schematic (1963)
3. Fender Deluxe Reverb AB763 schematic (1963)
4. Fender 5F6-A Bassman schematic (1958)
5. Marshall JTM45 schematic (1962)
6. Marshall JCM800 2203 schematic (1981)
7. Vox AC30/6 Top Boost schematic (1964)
8. Mesa/Boogie Dual Rectifier schematic
9. Peavey 5150 schematic (1992)
10. Orange Tiny Terror schematic (2006)
11. Orange Rockerverb schematic
12. Sunn Model T schematic (1969)
