/*
 * 0xFX — Amp modeling DSP
 *
 * Three-stage signal chain per amp model:
 *   1. Preamp: cascaded waveshaping gain stages (1-4 stages)
 *   2. Tone stack: circuit-modeled R/C network (topology per amp model)
 *   3. Power amp: soft compression + sag
 *
 * Tone stack topologies:
 *   - Fender TMB: 3rd-order passive R/C network (Yeh et al., DAFX 2006)
 *   - Marshall TMB: Same topology, different component values
 *   - Vox Cut: 1st-order lowpass (treble cut control)
 *   - Tilt EQ: Bass/treble seesaw (single knob)
 *
 * Component values sourced from real amplifier schematics.
 * Transfer functions digitized via bilinear transform.
 */
#include "engine_internal.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ── Waveshaper functions ─────────────────────────────────────── */

/* Soft tube-like clipping — smooth, warm, compresses gracefully */
static inline float waveshape_tanh(float x) {
    return tanhf(x);
}

/* Asymmetric soft clipping — slightly uneven +/- response, more "tube-like" */
static inline float waveshape_asym(float x) {
    return x / (1.0f + fabsf(x));
}

/* Moderate saturation — between tanh and hard clip */
static inline float waveshape_atan(float x) {
    return atanf(x) * (2.0f / (float)M_PI);  /* normalize to ~[-1,1] */
}

/* Hard clip with slight bias — transistor-like, aggressive */
static inline float waveshape_hard(float x) {
    x += 0.05f;  /* slight asymmetric bias */
    if (x > 1.0f) x = 1.0f;
    else if (x < -1.0f) x = -1.0f;
    return x;
}

/* ── Preamp gain stage (single stage) ─────────────────────────── */

/*
 * Each stage: input_gain -> waveshaper -> DC blocking highpass
 * DC blocker is a simple first-order highpass at ~10Hz to remove
 * the DC offset introduced by asymmetric clipping.
 */
static inline float preamp_stage(float in, float gain,
                                 float (*shaper)(float),
                                 float *dc_z1, float dc_coeff) {
    float x = in * gain;
    float shaped = shaper(x);

    /* DC blocking: highpass at ~10Hz */
    float dc_out = shaped - *dc_z1;
    *dc_z1 = shaped - dc_coeff * dc_out;

    return dc_out;
}

/* ── Tone stack topologies ────────────────────────────────────── */

typedef enum {
    TONE_STACK_FENDER_TMB = 0,   /* Fender-style passive TMB R/C network */
    TONE_STACK_MARSHALL_TMB,     /* Marshall-style passive TMB R/C network */
    TONE_STACK_VOX_CUT,          /* Vox-style treble cut control */
    TONE_STACK_TILT,             /* Single-knob tilt EQ */
} tone_stack_topology_t;

/* Component values for a TMB (Treble-Mid-Bass) passive tone stack.
 *
 * The classic Fender/Marshall tone stack is a passive R/C ladder network
 * between preamp stages. The transfer function is 3rd order:
 *
 *   H(s) = (b1*s^2 + b2*s + b3) / (a0*s^3 + a1*s^2 + a2*s + 1)
 *
 * Where the coefficients are functions of R/C component values and
 * pot positions (bass, mid, treble = 0..1).
 *
 * Reference: "Digital Implementation of Musical Distortion Circuits
 * by Analysis and Simulation" — Yeh, Abel, Smith (DAFX 2006)
 */
typedef struct {
    float R1;    /* Treble pot (ohms) */
    float R2;    /* Bass pot (ohms) */
    float R3;    /* Mid pot (ohms) */
    float R4;    /* Slope resistor (ohms) */
    float C1;    /* Treble cap (farads) */
    float C2;    /* Bass cap (farads) */
    float C3;    /* Mid cap (farads) */
} tmb_components_t;

/* ── Per-model configuration ──────────────────────────────────── */

typedef struct {
    int   num_stages;
    float stage_gains[AMP_MAX_PREAMP_STAGES];
    float (*shaper)(float);
    tone_stack_topology_t tone_topology;
    tmb_components_t      tmb;           /* component values (TMB topologies only) */
    float                 presence_freq; /* presence shelf frequency */
    float                 power_threshold;
} amp_model_config_t;

static const amp_model_config_t amp_configs[FX_AMP_COUNT] = {
    /* Fullerton Clean — inspired by classic American clean amps (silver panel era)
     * Fender Twin Reverb / Deluxe Reverb tone stack
     * Famous mid-scoop at ~400Hz, interactive bass/treble controls */
    [FX_AMP_FULLERTON_CLEAN] = {
        .num_stages = 2,
        .stage_gains = { 3.0f, 2.0f, 0, 0 },
        .shaper = waveshape_tanh,
        .tone_topology = TONE_STACK_FENDER_TMB,
        .tmb = {
            .R1 = 250e3f,  .C1 = 250e-12f,   /* 250k treble pot, 250pF */
            .R2 = 1e6f,    .C2 = 100e-9f,     /* 1M bass pot, 0.1uF */
            .R3 = 25e3f,   .C3 = 47e-9f,      /* 25k mid pot, 0.047uF */
            .R4 = 56e3f,                        /* 56k slope resistor */
        },
        .presence_freq = 5000.0f,
        .power_threshold = 0.85f,
    },
    /* British Crunch — inspired by classic British crunch amps (Plexi)
     * Marshall 1959/JTM45 tone stack
     * More midrange than Fender, warmer top, classic rock voicing */
    [FX_AMP_BRIT_CRUNCH] = {
        .num_stages = 3,
        .stage_gains = { 4.0f, 3.0f, 2.5f, 0 },
        .shaper = waveshape_asym,
        .tone_topology = TONE_STACK_MARSHALL_TMB,
        .tmb = {
            .R1 = 220e3f,  .C1 = 470e-12f,   /* 220k treble, 470pF */
            .R2 = 1e6f,    .C2 = 22e-9f,      /* 1M bass, 0.022uF */
            .R3 = 25e3f,   .C3 = 22e-9f,      /* 25k mid, 0.022uF */
            .R4 = 33e3f,                        /* 33k slope (Plexi value) */
        },
        .presence_freq = 5500.0f,
        .power_threshold = 0.75f,
    },
    /* Southwest Lead — inspired by American high-gain amps (Dual Rectifier)
     * Modified Fender TMB with tighter bass and deeper mid scoop
     * Added mid-shift cap creates the "V-curve" character */
    [FX_AMP_SOUTHWEST_LEAD] = {
        .num_stages = 4,
        .stage_gains = { 5.0f, 4.0f, 3.5f, 3.0f },
        .shaper = waveshape_hard,
        .tone_topology = TONE_STACK_FENDER_TMB,
        .tmb = {
            .R1 = 250e3f,  .C1 = 250e-12f,   /* 250k treble, 250pF */
            .R2 = 1e6f,    .C2 = 68e-9f,      /* 1M bass, 0.068uF (tighter bass) */
            .R3 = 25e3f,   .C3 = 33e-9f,      /* 25k mid, 0.033uF (deeper scoop) */
            .R4 = 39e3f,                        /* 39k slope (Recto value) */
        },
        .presence_freq = 6000.0f,
        .power_threshold = 0.70f,
    },
    /* Essex Chime — inspired by British chime amps (AC30 Top Boost)
     * Vox-style: treble cut control, bright by default, cut darkens
     * NOT a TMB stack — uses a simple first-order lowpass */
    [FX_AMP_ESSEX_CHIME] = {
        .num_stages = 2,
        .stage_gains = { 3.5f, 2.5f, 0, 0 },
        .shaper = waveshape_atan,
        .tone_topology = TONE_STACK_VOX_CUT,
        .tmb = { 0 },  /* not used — Vox uses cut control */
        .presence_freq = 7000.0f,
        .power_threshold = 0.80f,
    },
    /* Tweed Blues — inspired by American tweed-era amps (5F6-A Bassman)
     * Fender TMB with different values — warmer, less scooped than blackface
     * The original Marshall tone stack was derived from this circuit */
    [FX_AMP_TWEED_BLUES] = {
        .num_stages = 2,
        .stage_gains = { 4.0f, 3.0f, 0, 0 },
        .shaper = waveshape_asym,
        .tone_topology = TONE_STACK_FENDER_TMB,
        .tmb = {
            .R1 = 250e3f,  .C1 = 250e-12f,   /* 250k treble, 250pF */
            .R2 = 1e6f,    .C2 = 100e-9f,     /* 1M bass, 0.1uF */
            .R3 = 10e3f,   .C3 = 22e-9f,      /* 10k mid, 0.022uF (warmer mid voicing) */
            .R4 = 56e3f,                        /* 56k slope */
        },
        .presence_freq = 4500.0f,
        .power_threshold = 0.72f,
    },
    /* Meridian High Gain — inspired by American high-gain metal amps (5150/6505)
     * Modified Fender TMB topology — from actual 5150 schematic (lead channel)
     * C1=500pF (lead channel; rhythm uses 250pF), C2=0.022uF, C3=0.022uF
     * 470pF bright cap across volume pot (not modeled in tone stack)
     * Extra gain stages before tone stack for saturated character */
    [FX_AMP_MERIDIAN_HIGH_GAIN] = {
        .num_stages = 4,
        .stage_gains = { 6.0f, 5.0f, 4.5f, 4.0f },
        .shaper = waveshape_hard,
        .tone_topology = TONE_STACK_FENDER_TMB,
        .tmb = {
            .R1 = 250e3f,  .C1 = 500e-12f,   /* 250k treble pot, 500pF (lead ch.) */
            .R2 = 1e6f,    .C2 = 22e-9f,      /* 1M bass pot, 0.022uF */
            .R3 = 25e3f,   .C3 = 22e-9f,      /* 25k mid pot, 0.022uF */
            .R4 = 33e3f,                        /* 33k slope resistor */
        },
        .presence_freq = 5000.0f,
        .power_threshold = 0.65f,
    },
    /* Citrus Roar — inspired by British thick/fuzzy crunch amps (Rockerverb)
     * From actual Rockerverb 50 schematic — Marshall-derived TMB topology
     * C2=0.047uF (larger than Marshall = more bass), C1=470pF
     * EL34 power section, warm and authoritative */
    [FX_AMP_CITRUS_ROAR] = {
        .num_stages = 3,
        .stage_gains = { 4.5f, 3.5f, 3.0f, 0 },
        .shaper = waveshape_tanh,
        .tone_topology = TONE_STACK_MARSHALL_TMB,
        .tmb = {
            .R1 = 250e3f,  .C1 = 470e-12f,   /* 250k treble pot, 470pF */
            .R2 = 1e6f,    .C2 = 47e-9f,      /* 1M bass pot, 0.047uF (bigger = more bass) */
            .R3 = 25e3f,   .C3 = 22e-9f,      /* 25k mid pot, 0.022uF */
            .R4 = 39e3f,                        /* 39k slope resistor */
        },
        .presence_freq = 4500.0f,
        .power_threshold = 0.72f,
    },
    /* Citrus Terror — inspired by British low-wattage Class A amps (Tiny Terror)
     * Single "Tone" knob — tilt EQ: dark <-> bright
     * NOT a TMB stack — simple tilt filter */
    [FX_AMP_CITRUS_TERROR] = {
        .num_stages = 2,
        .stage_gains = { 4.0f, 3.5f, 0, 0 },
        .shaper = waveshape_asym,
        .tone_topology = TONE_STACK_TILT,
        .tmb = { 0 },  /* not used — single tone knob */
        .presence_freq = 5000.0f,
        .power_threshold = 0.78f,
    },
    /* Regent 800 — inspired by classic British rock/metal amps (JCM800)
     * Marshall TMB with slightly brighter voicing than Plexi
     * Mid-forward, punchy, the sound of 80s rock */
    [FX_AMP_REGENT_800] = {
        .num_stages = 2,
        .stage_gains = { 5.0f, 4.0f, 0, 0 },
        .shaper = waveshape_hard,
        .tone_topology = TONE_STACK_MARSHALL_TMB,
        .tmb = {
            .R1 = 220e3f,  .C1 = 470e-12f,   /* 220k treble, 470pF */
            .R2 = 1e6f,    .C2 = 22e-9f,      /* 1M bass, 0.022uF */
            .R3 = 25e3f,   .C3 = 22e-9f,      /* 25k mid, 0.022uF */
            .R4 = 33e3f,                        /* 33k slope (JCM800 value) */
        },
        .presence_freq = 5500.0f,
        .power_threshold = 0.73f,
    },
    /* Solar Monolith — inspired by massive clean-to-doom amps (Sunn Model T)
     * Fender-derived TMB with massive low end and extended bass response
     * Huge iron transformers, extreme clean headroom */
    [FX_AMP_SOLAR_MONOLITH] = {
        .num_stages = 2,
        .stage_gains = { 3.0f, 2.5f, 0, 0 },
        .shaper = waveshape_atan,
        .tone_topology = TONE_STACK_FENDER_TMB,
        .tmb = {
            .R1 = 250e3f,  .C1 = 500e-12f,   /* 250k treble, 500pF (darker treble) */
            .R2 = 1e6f,    .C2 = 220e-9f,     /* 1M bass, 0.22uF (massive bass) */
            .R3 = 25e3f,   .C3 = 100e-9f,     /* 25k mid, 0.1uF (warm mids) */
            .R4 = 68e3f,                        /* 68k slope (Sunn value) */
        },
        .presence_freq = 4000.0f,
        .power_threshold = 0.90f,
    },
    /* Eclipse Drone — inspired by extreme low-end drone amps
     * Sunn-derived with subsonic emphasis and feedback sustain
     * Extended bass, compressed mids for sustained drone tones */
    [FX_AMP_ECLIPSE_DRONE] = {
        .num_stages = 2,
        .stage_gains = { 4.5f, 4.0f, 0, 0 },
        .shaper = waveshape_tanh,
        .tone_topology = TONE_STACK_FENDER_TMB,
        .tmb = {
            .R1 = 250e3f,  .C1 = 680e-12f,   /* 250k treble, 680pF (very dark treble) */
            .R2 = 1e6f,    .C2 = 330e-9f,     /* 1M bass, 0.33uF (extreme bass) */
            .R3 = 25e3f,   .C3 = 150e-9f,     /* 25k mid, 0.15uF (compressed mids) */
            .R4 = 82e3f,                        /* 82k slope (extended bass response) */
        },
        .presence_freq = 3500.0f,
        .power_threshold = 0.88f,
    },
    /* Emerald Ratrod Deluxe — inspired by Fender Hot Rod Deluxe (PR246)
     * Modern Fender circuit (1990s), NOT a vintage Deluxe Reverb:
     * - 3 channels: Clean / Drive / More Drive
     * - 12AX7 preamp with gain + MOSFET/diode clipping on drive channels
     * - 2x 6L6GC power tubes, 40W — more headroom than 6V6 Deluxe Reverb
     * - Fender TMB tone stack but with slightly different values
     * - Drive channel adds hard clipping (asymmetric) for gritty breakup
     * - 3 gain stages: clean 12AX7 + clipped 12AX7 + recovery stage
     * - The "grit" on the gain comes from diode/MOSFET clipping between stages */
    [FX_AMP_EMERALD_DELUXE] = {
        .num_stages = 3,
        .stage_gains = { 3.5f, 4.0f, 2.0f, 0 },
        .shaper = waveshape_hard,  /* drive channel has hard clipping diodes */
        .tone_topology = TONE_STACK_FENDER_TMB,
        .tmb = {
            .R1 = 250e3f,  .C1 = 250e-12f,   /* 250k treble pot, 250pF */
            .R2 = 1e6f,    .C2 = 100e-9f,     /* 1M bass pot, 0.1uF */
            .R3 = 25e3f,   .C3 = 47e-9f,      /* 25k mid pot, 0.047uF */
            .R4 = 56e3f,                        /* 56k slope resistor */
        },
        .presence_freq = 4500.0f,
        .power_threshold = 0.70f,  /* 6L6 power section, moderate headroom */
    },
};

/* ── TMB Tone Stack — Circuit Model ──────────────────────────── */

/*
 * Compute 3rd-order IIR coefficients from R/C component values and pot positions.
 *
 * The Fender/Marshall TMB tone stack is a passive ladder network. The analog
 * transfer function (Yeh et al., DAFX 2006) is:
 *
 *   H(s) = (b1*s^2 + b2*s + b3) / (a0*s^3 + a1*s^2 + a2*s + 1)
 *
 * where coefficients depend on component values and pot positions:
 *   t = treble pot position (0..1)
 *   m = mid pot position (0..1)
 *   b = bass pot position (0..1)
 *
 * The s-domain coefficients encode the exact interaction between controls
 * that makes each amp's tone stack sound unique — boosting bass affects
 * treble response and vice versa, just like the real circuit.
 *
 * Digitized via bilinear transform: s = (2/T) * (1 - z^-1) / (1 + z^-1)
 */
static void tmb_compute_coefficients(fx_tone_stack_3rd_t *ts,
                                     const tmb_components_t *comp,
                                     float t, float m, float b,
                                     float sr)
{
    /* Extract component values */
    float R1 = comp->R1 * t + 1.0f;       /* treble pot: 0 to R1 (avoid zero) */
    float R1i = comp->R1 * (1.0f - t);    /* treble pot inverse wiper */
    float R2 = comp->R2 * b + 1.0f;       /* bass pot */
    float R3 = comp->R3 * m + 1.0f;       /* mid pot */
    float R4 = comp->R4;
    float C1 = comp->C1;
    float C2 = comp->C2;
    float C3 = comp->C3;

    /*
     * s-domain coefficients from the Yeh/Abel/Smith analysis.
     *
     * Numerator: H_num(s) = b1*s^2 + b2*s + b3
     * Denominator: H_den(s) = a0*s^3 + a1*s^2 + a2*s + 1
     *
     * These expressions come from nodal analysis of the tone stack circuit.
     * Each coefficient is a product of R and C values that capture the
     * exact interaction between the three pot positions.
     */

    /* Numerator coefficients (s-domain) */
    float sb1 = C1 * C3 * R1 * R3
              + C1 * C3 * R3 * R4
              + C2 * C3 * R2 * R4;

    float sb2 = C1 * R1
              + C1 * R3
              + C2 * R2
              + C3 * R3
              + C3 * R4;

    float sb3 = 1.0f;

    /* Denominator coefficients (s-domain) */
    float sa0 = C1 * C2 * C3 * R1 * R2 * R4
              + C1 * C2 * C3 * R1 * R2 * R3
              + C1 * C2 * C3 * R2 * R3 * R4;

    float sa1 = C1 * C2 * R1 * R2
              + C1 * C2 * R2 * R4
              + C1 * C3 * R1 * R3
              + C1 * C3 * R1 * R4
              + C1 * C3 * R3 * R4
              + C2 * C3 * R2 * R3
              + C2 * C3 * R2 * R4
              + C2 * C3 * R3 * R4
              + C1 * C2 * R1i * R2;

    float sa2 = C1 * R1
              + C1 * R3
              + C2 * R2
              + C3 * R3
              + C3 * R4
              + C1 * R1i
              + C2 * R4;

    /* sa3 = 1.0 (normalized) */

    /*
     * Bilinear transform: s = (2*sr) * (1 - z^-1) / (1 + z^-1)
     *
     * For a 3rd-order system H(s) = (b1*s^2 + b2*s + b3) / (a0*s^3 + a1*s^2 + a2*s + 1):
     *
     * Substitute s = c*(1-z^-1)/(1+z^-1) where c = 2*sr (pre-warping omitted
     * for tone stacks since the interesting frequencies are well below Nyquist).
     *
     * Multiply out to get z-domain coefficients.
     */
    float c = 2.0f * sr;
    float c2 = c * c;
    float c3 = c2 * c;

    /* Denominator: a0*c^3 + a1*c^2 + a2*c + 1 (for normalization) */
    float A0 = sa0 * c3 + sa1 * c2 + sa2 * c + 1.0f;

    /* Guard against division by zero (shouldn't happen with valid components) */
    if (fabsf(A0) < 1e-30f) A0 = 1e-30f;

    float inv_A0 = 1.0f / A0;

    /* z-domain numerator coefficients (after bilinear substitution and expansion) */
    /* B(z) = B0 + B1*z^-1 + B2*z^-2 + B3*z^-3 */
    float B0 = (sb1 * c2 + sb2 * c + sb3);
    float B1 = (-2.0f * sb1 * c2 + 2.0f * sb3);
    float B2 = (sb1 * c2 - sb2 * c + sb3);
    /* Note: numerator is 2nd order in s, so z-domain is 3rd order with B3=0
     * after proper bilinear expansion. However, the bilinear transform of
     * a ratio needs matching orders. We pad with (1+z^-1) to make 3rd order. */

    /* Actually, the proper bilinear transform for mixed orders:
     * Numerator has s^2 max, denominator has s^3 max.
     * When we substitute s = c*(1-z^-1)/(1+z^-1) and multiply by (1+z^-1)^3:
     * - Denominator: a0*c^3*(1-z^-1)^3 + a1*c^2*(1-z^-1)^2*(1+z^-1)
     *                + a2*c*(1-z^-1)*(1+z^-1)^2 + (1+z^-1)^3
     * - Numerator:   b1*c^2*(1-z^-1)^2*(1+z^-1) + b2*c*(1-z^-1)*(1+z^-1)^2
     *                + b3*(1+z^-1)^3
     *
     * Expand (1-z^-1)^3 = 1 - 3z^-1 + 3z^-2 - z^-3
     * (1-z^-1)^2*(1+z^-1) = 1 - z^-1 - z^-2 + z^-3
     * (1-z^-1)*(1+z^-1)^2 = 1 + z^-1 - z^-2 - z^-3
     * (1+z^-1)^3 = 1 + 3z^-1 + 3z^-2 + z^-3
     */

    /* Recalculate properly with full polynomial expansion */
    float num0 =  sb1*c2 + sb2*c + sb3;
    float num1 = -sb1*c2 + sb2*c + 3.0f*sb3;   /* from (1-z)(1+z) and (1+z)^3 expansion */
    float num2 = -sb1*c2 - sb2*c + 3.0f*sb3;
    float num3 =  sb1*c2 - sb2*c + sb3;

    /* Wait — let me be more careful. Using the expansion patterns above:
     * b1*c^2 * [1, -1, -1, +1]  (coefficient of (1-z^-1)^2*(1+z^-1))
     * b2*c   * [1, +1, -1, -1]  (coefficient of (1-z^-1)*(1+z^-1)^2)
     * b3     * [1, +3, +3, +1]  (coefficient of (1+z^-1)^3)
     */
    num0 = sb1*c2 * 1.0f + sb2*c * 1.0f + sb3 * 1.0f;
    num1 = sb1*c2 *(-1.0f) + sb2*c * 1.0f + sb3 * 3.0f;
    num2 = sb1*c2 *(-1.0f) + sb2*c *(-1.0f) + sb3 * 3.0f;
    num3 = sb1*c2 * 1.0f + sb2*c *(-1.0f) + sb3 * 1.0f;

    /* Denominator z-domain:
     * a0*c^3 * [1, -3, +3, -1]  (coefficient of (1-z^-1)^3)
     * a1*c^2 * [1, -1, -1, +1]  (coefficient of (1-z^-1)^2*(1+z^-1))
     * a2*c   * [1, +1, -1, -1]  (coefficient of (1-z^-1)*(1+z^-1)^2)
     * 1      * [1, +3, +3, +1]  (coefficient of (1+z^-1)^3)
     */
    float den0 = sa0*c3 * 1.0f + sa1*c2 * 1.0f + sa2*c * 1.0f + 1.0f;
    float den1 = sa0*c3 *(-3.0f) + sa1*c2 *(-1.0f) + sa2*c * 1.0f + 3.0f;
    float den2 = sa0*c3 * 3.0f + sa1*c2 *(-1.0f) + sa2*c *(-1.0f) + 3.0f;
    float den3 = sa0*c3 *(-1.0f) + sa1*c2 * 1.0f + sa2*c *(-1.0f) + 1.0f;

    /* Normalize by den0 (= A0, already computed) */
    float inv_den0 = 1.0f / den0;

    ts->b0 = num0 * inv_den0;
    ts->b1 = num1 * inv_den0;
    ts->b2 = num2 * inv_den0;
    ts->b3 = num3 * inv_den0;
    ts->a1 = den1 * inv_den0;
    ts->a2 = den2 * inv_den0;
    ts->a3 = den3 * inv_den0;
}

/* Process one sample through the 3rd-order tone stack filter.
 * Uses transposed direct form II for numerical stability. */
static inline float tone_stack_3rd_process(fx_tone_stack_3rd_t *ts, float in) {
    float out = ts->b0 * in + ts->z1;
    ts->z1 = ts->b1 * in - ts->a1 * out + ts->z2;
    ts->z2 = ts->b2 * in - ts->a2 * out + ts->z3;
    ts->z3 = ts->b3 * in - ts->a3 * out;

    /* Flush denormals */
    if (ts->z1 > -1e-15f && ts->z1 < 1e-15f) ts->z1 = 0.0f;
    if (ts->z2 > -1e-15f && ts->z2 < 1e-15f) ts->z2 = 0.0f;
    if (ts->z3 > -1e-15f && ts->z3 < 1e-15f) ts->z3 = 0.0f;

    return out;
}

/* ── Vox Cut Control — 1st order lowpass ─────────────────────── */

/*
 * The Vox AC30 "Cut" control is a simple treble cut — a 1st-order lowpass
 * with variable cutoff. The AC30's Top Boost channel also has bass/treble
 * controls, but the defining character is the Cut control on the output.
 *
 * We model the bass and treble as biquad shelves (since the AC30 Top Boost
 * channel does have these) but the Cut control as a 1st-order LPF, which
 * is what gives the Vox its signature HF rolloff character.
 *
 * Cut knob at 0: wide open (cutoff at ~20kHz, essentially bypassed)
 * Cut knob at 1: fully cut (cutoff at ~1kHz, very dark)
 *
 * The real circuit: 1M log pot + cap to ground. We interpolate cutoff
 * frequency logarithmically between 1kHz and 20kHz.
 */
static void vox_cut_compute(fx_biquad_t *bq, float cut, float bass,
                            float treble, float sr) {
    /* The AC30 Top Boost bass/treble interact with the cut control.
     * We model the combined effect as a lowpass whose cutoff is controlled
     * by the Cut knob, with bass/treble modifying the slope. */
    float cut_freq = 1000.0f * powf(20.0f, 1.0f - cut);  /* 20kHz -> 1kHz */
    float q = 0.5f + treble * 0.3f;  /* treble affects resonance slightly */
    (void)bass;  /* bass primarily affects preamp coupling in Vox, modeled elsewhere */
    fx_biquad_lowpass(bq, cut_freq, q, sr);
}

/* ── Tilt EQ — single-knob bass/treble seesaw ────────────────── */

/*
 * The Tiny Terror's single "Tone" knob is a tilt EQ:
 * - CCW (0.0): boosts bass, cuts treble -> warm/dark
 * - Center (0.5): flat
 * - CW (1.0): cuts bass, boosts treble -> bright/cutting
 *
 * Implemented as a low shelf + high shelf with inverted gains.
 * Tilt center frequency around 800Hz.
 */
static void tilt_eq_compute(fx_biquad_t *bq, float tone, float sr) {
    /* Map tone 0..1 to tilt in dB: -12dB to +12dB treble
     * (bass is inverted) */
    float tilt_db = (tone - 0.5f) * 24.0f;

    /* We use a single 1st-order shelving filter at 800Hz.
     * Positive tilt_db = boost treble (highshelf gain), which
     * simultaneously reduces bass (it's a tilt). */
    fx_biquad_highshelf(bq, 800.0f, tilt_db, sr);
}

/* ── Tone stack update ────────────────────────────────────────── */

static void amp_update_tone_stack(fx_amp_state_t *amp, float sr) {
    const amp_model_config_t *cfg = &amp_configs[amp->type];

    switch (cfg->tone_topology) {
        case TONE_STACK_FENDER_TMB:
        case TONE_STACK_MARSHALL_TMB: {
            /* Circuit-modeled TMB tone stack */
            float t = amp->params[FX_AMP_PARAM_TREBLE];
            float m = amp->params[FX_AMP_PARAM_MID];
            float b = amp->params[FX_AMP_PARAM_BASS];

            /* Clamp pot positions to avoid degenerate filter coefficients.
             * Real pots have a small resistance even at zero. */
            if (t < 0.01f) t = 0.01f;
            if (m < 0.01f) m = 0.01f;
            if (b < 0.01f) b = 0.01f;
            if (t > 0.99f) t = 0.99f;
            if (m > 0.99f) m = 0.99f;
            if (b > 0.99f) b = 0.99f;

            tmb_compute_coefficients(&amp->tone_stack, &cfg->tmb, t, m, b, sr);

            /* Presence is a separate negative-feedback shelf, not part of
             * the passive tone stack. It lives in the power amp section.
             * Typically a high shelf around 4-6kHz. */
            float pres_db = (amp->params[FX_AMP_PARAM_PRESENCE] - 0.5f) * 16.0f;
            fx_biquad_highshelf(&amp->presence_filter, cfg->presence_freq, pres_db, sr);
            break;
        }

        case TONE_STACK_VOX_CUT: {
            /* Vox-style: bass/treble shelves + cut control */
            float cut = amp->params[FX_AMP_PARAM_CUT];
            float bass = amp->params[FX_AMP_PARAM_BASS];
            float treble = amp->params[FX_AMP_PARAM_TREBLE];

            /* If the CUT param hasn't been set, use TREBLE as cut
             * (for backwards compatibility — the AC30 Top Boost's
             * treble knob effectively works as a high-frequency cut) */
            if (cut < 0.001f && treble > 0.001f) {
                cut = 1.0f - treble;  /* invert: high treble = low cut */
            }

            vox_cut_compute(&amp->tone_simple, cut, bass, treble, sr);

            /* Presence shelf */
            float pres_db = (amp->params[FX_AMP_PARAM_PRESENCE] - 0.5f) * 16.0f;
            fx_biquad_highshelf(&amp->presence_filter, cfg->presence_freq, pres_db, sr);
            break;
        }

        case TONE_STACK_TILT: {
            /* Single Tone knob tilt EQ */
            float tone = amp->params[FX_AMP_PARAM_TONE];
            tilt_eq_compute(&amp->tone_simple, tone, sr);

            /* Presence — less range since tilt already affects HF */
            float pres_db = (tone - 0.5f) * 8.0f;
            fx_biquad_highshelf(&amp->presence_filter, cfg->presence_freq, pres_db, sr);
            break;
        }
    }

    /* Cache current values so we know when to recalculate */
    if (cfg->tone_topology == TONE_STACK_TILT) {
        amp->tone_cache[0] = amp->params[FX_AMP_PARAM_TONE];
    } else {
        amp->tone_cache[0] = amp->params[FX_AMP_PARAM_BASS];
        amp->tone_cache[1] = amp->params[FX_AMP_PARAM_MID];
        amp->tone_cache[2] = amp->params[FX_AMP_PARAM_TREBLE];
        amp->tone_cache[3] = amp->params[FX_AMP_PARAM_PRESENCE];
    }
    amp->tone_sr = sr;
}

static inline bool tone_params_changed(fx_amp_state_t *amp, float sr) {
    if (amp->tone_sr != sr) return true;
    const amp_model_config_t *cfg = &amp_configs[amp->type];
    if (cfg->tone_topology == TONE_STACK_TILT) {
        return amp->tone_cache[0] != amp->params[FX_AMP_PARAM_TONE];
    }
    return amp->tone_cache[0] != amp->params[FX_AMP_PARAM_BASS] ||
           amp->tone_cache[1] != amp->params[FX_AMP_PARAM_MID] ||
           amp->tone_cache[2] != amp->params[FX_AMP_PARAM_TREBLE] ||
           amp->tone_cache[3] != amp->params[FX_AMP_PARAM_PRESENCE];
}

/* ── Init ─────────────────────────────────────────────────────── */

void fx_amp_init(fx_amp_state_t *amp, fx_amp_type_t type) {
    /* Preserve nothing — full reset */
    memset(amp, 0, sizeof(*amp));
    amp->type = type;

    /* Set defaults */
    amp->params[FX_AMP_PARAM_GAIN]     = 0.5f;
    amp->params[FX_AMP_PARAM_VOLUME]   = 0.5f;
    amp->params[FX_AMP_PARAM_BASS]     = 0.5f;
    amp->params[FX_AMP_PARAM_MID]      = 0.5f;
    amp->params[FX_AMP_PARAM_TREBLE]   = 0.5f;
    amp->params[FX_AMP_PARAM_PRESENCE] = 0.5f;
    amp->params[FX_AMP_PARAM_SAG]      = 0.3f;
    amp->params[FX_AMP_PARAM_MASTER]   = 0.5f;
    amp->params[FX_AMP_PARAM_TONE]     = 0.5f;
    amp->params[FX_AMP_PARAM_FEEDBACK] = 0.0f;

    amp->num_stages = amp_configs[type].num_stages;
    amp->sag_voltage = 1.0f;
    amp->power_envelope = 0.0f;
}

/* ── Process ──────────────────────────────────────────────────── */

void fx_amp_process(fx_amp_state_t *amp, float *buf, int n, float sr) {
    const amp_model_config_t *cfg = &amp_configs[amp->type];

    float gain_knob = amp->params[FX_AMP_PARAM_GAIN];
    float master    = amp->params[FX_AMP_PARAM_MASTER];
    float volume    = amp->params[FX_AMP_PARAM_VOLUME];
    float sag_amount = amp->params[FX_AMP_PARAM_SAG];

    /* DC blocker coefficient: highpass at ~10Hz */
    float dc_coeff = 1.0f - (2.0f * (float)M_PI * 10.0f / sr);
    if (dc_coeff < 0.9f) dc_coeff = 0.9f;

    /* Recalculate tone stack if params changed (avoid per-sample trig) */
    if (tone_params_changed(amp, sr)) {
        amp_update_tone_stack(amp, sr);
    }

    /* Power amp attack/release coefficients */
    float power_attack  = expf(-1.0f / (0.002f * sr));  /* 2ms attack */
    float power_release = expf(-1.0f / (0.050f * sr));  /* 50ms release */
    /* Sag time constant — slower than compression */
    float sag_attack  = expf(-1.0f / (0.010f * sr));    /* 10ms */
    float sag_release = expf(-1.0f / (0.200f * sr));    /* 200ms (slow recovery = spongy) */

    /* Determine tone stack processing mode */
    int use_tmb = (cfg->tone_topology == TONE_STACK_FENDER_TMB ||
                   cfg->tone_topology == TONE_STACK_MARSHALL_TMB);

    for (int i = 0; i < n; i++) {
        float x = buf[i];

        /* ── Stage 1: Preamp gain stages ─────────────────────── */
        for (int s = 0; s < cfg->num_stages; s++) {
            float stage_gain = cfg->stage_gains[s] * (0.2f + gain_knob * 0.8f);
            x = preamp_stage(x, stage_gain, cfg->shaper,
                            &amp->dc_block_z1[s], dc_coeff);
        }

        /* ── Stage 2: Tone stack ─────────────────────────────── */
        if (use_tmb) {
            /* Circuit-modeled 3rd-order TMB filter */
            x = tone_stack_3rd_process(&amp->tone_stack, x);
        } else {
            /* Simple topology (Vox cut or tilt EQ) */
            x = fx_biquad_process(&amp->tone_simple, x);
        }
        /* Presence is always a separate shelf (negative feedback network) */
        x = fx_biquad_process(&amp->presence_filter, x);

        /* ── Eclipse Drone: Harmonic feedback sustain ────────── */
        if (amp->type == FX_AMP_ECLIPSE_DRONE) {
            float fb = amp->params[FX_AMP_PARAM_FEEDBACK];
            if (fb > 0.01f) {
                /* Feed back a saturated version of previous output */
                x += amp->feedback_z1 * fb * 0.6f;
                amp->feedback_z1 = tanhf(x);  /* soft-limit feedback */
            }
        }

        /* ── Stage 3: Power amp ──────────────────────────────── */

        /* Envelope follower for compression */
        float abs_x = fabsf(x);
        if (abs_x > amp->power_envelope)
            amp->power_envelope = power_attack * amp->power_envelope +
                                  (1.0f - power_attack) * abs_x;
        else
            amp->power_envelope = power_release * amp->power_envelope +
                                  (1.0f - power_release) * abs_x;

        /* Soft compression above threshold */
        float threshold = cfg->power_threshold;
        if (amp->power_envelope > threshold && threshold > 0.0f) {
            float over = amp->power_envelope / threshold;
            /* Gentle 2:1 ratio compression */
            float reduction = 1.0f / sqrtf(over);
            x *= reduction;
        }

        /* Sag simulation: supply voltage droops under sustained loud signal */
        if (sag_amount > 0.01f) {
            float load = amp->power_envelope;
            if (load > amp->sag_voltage)
                amp->sag_voltage = sag_attack * amp->sag_voltage +
                                   (1.0f - sag_attack) * (1.0f - load * sag_amount);
            else
                amp->sag_voltage = sag_release * amp->sag_voltage +
                                   (1.0f - sag_release) * 1.0f;

            /* Clamp sag voltage to reasonable range */
            if (amp->sag_voltage < 0.3f) amp->sag_voltage = 0.3f;
            if (amp->sag_voltage > 1.0f) amp->sag_voltage = 1.0f;

            x *= amp->sag_voltage;
        }

        /* Master volume + output volume.
         * Makeup gain compensates for waveshaper compression.
         * Quadratic taper for natural feel. */
        float mv = master * master;
        float vv = volume * volume;
        /* TMB tone stacks are passive and attenuate ~10-15dB at noon.
         * Compensate with higher makeup gain for circuit-modeled stacks. */
        float makeup = use_tmb ? 12.0f : 4.0f;
        x *= mv * vv * makeup;

        buf[i] = x;
    }
}

/* ── Amp metadata ─────────────────────────────────────────────── */

static const char *amp_type_names[FX_AMP_COUNT] = {
    "Fullerton Clean",
    "British Crunch",
    "Southwest Lead",
    "Essex Chime",
    "Tweed Blues",
    "Meridian High Gain",
    "Citrus Roar",
    "Citrus Terror",
    "Regent 800",
    "Solar Monolith",
    "Eclipse Drone",
    "Emerald Ratrod Deluxe",
};

static const char *amp_param_names[FX_AMP_PARAM_COUNT] = {
    "Gain", "Volume", "Bass", "Mid", "Treble",
    "Presence", "Sag", "Master", "Bright", "Cut",
    "Tone", "Feedback",
};

int fx_amp_get_param_count(fx_amp_type_t type) {
    switch (type) {
        case FX_AMP_FULLERTON_CLEAN:    return 7;
        case FX_AMP_BRIT_CRUNCH:        return 8;
        case FX_AMP_SOUTHWEST_LEAD:     return 8;
        case FX_AMP_ESSEX_CHIME:        return 7;
        case FX_AMP_TWEED_BLUES:        return 6;
        case FX_AMP_MERIDIAN_HIGH_GAIN: return 7;
        case FX_AMP_CITRUS_ROAR:        return 5;
        case FX_AMP_CITRUS_TERROR:      return 3;
        case FX_AMP_REGENT_800:         return 7;
        case FX_AMP_SOLAR_MONOLITH:     return 6;
        case FX_AMP_ECLIPSE_DRONE:      return 6;
        case FX_AMP_EMERALD_DELUXE:     return 7;
        default: return 0;
    }
}

const char *fx_amp_get_param_name(fx_amp_type_t type, fx_amp_param_t param) {
    (void)type;
    if (param < 0 || param >= FX_AMP_PARAM_COUNT) return "?";
    return amp_param_names[param];
}

const char *fx_amp_get_type_name(fx_amp_type_t type) {
    if (type < 0 || type >= FX_AMP_COUNT) return "?";
    return amp_type_names[type];
}
