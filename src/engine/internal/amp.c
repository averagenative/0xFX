/*
 * 0xFX — Amp modeling DSP
 *
 * Three-stage signal chain per amp model:
 *   1. Preamp: cascaded waveshaping gain stages (1-4 stages)
 *   2. Tone stack: 3-band biquad EQ (bass/mid/treble) + presence
 *   3. Power amp: soft compression + sag
 *
 * Each model has different gain structure, waveshaper, and tone stack voicing.
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
 * Each stage: input_gain → waveshaper → DC blocking highpass
 * DC blocker is a simple first-order highpass at ~10Hz to remove
 * the DC offset introduced by asymmetric clipping.
 */
static inline float preamp_stage(float in, float gain,
                                 float (*shaper)(float),
                                 float *dc_z1, float dc_coeff) {
    float x = in * gain;
    float shaped = shaper(x);

    /* DC blocking: highpass at ~10Hz */
    /* y = x - z1; z1 = x - coeff * y  (simplified) */
    float dc_out = shaped - *dc_z1;
    *dc_z1 = shaped - dc_coeff * dc_out;

    return dc_out;
}

/* ── Per-model configuration ──────────────────────────────────── */

typedef struct {
    int   num_stages;
    float stage_gains[AMP_MAX_PREAMP_STAGES]; /* base gain per stage (multiplied by param) */
    float (*shaper)(float);
    /* Tone stack center frequencies */
    float bass_freq;
    float mid_freq;
    float treble_freq;
    float presence_freq;
    /* Tone stack character: how much the knobs affect (dB range) */
    float tone_range_db;
    /* Power amp compression threshold */
    float power_threshold;
} amp_model_config_t;

static const amp_model_config_t amp_configs[FX_AMP_COUNT] = {
    /* Fullerton Clean — inspired by classic American clean amps
     * 2 stages, gentle tanh clipping, scooped mid voicing
     * Lots of headroom, breaks up gracefully at high gain */
    [FX_AMP_FULLERTON_CLEAN] = {
        .num_stages = 2,
        .stage_gains = { 3.0f, 2.0f, 0, 0 },
        .shaper = waveshape_tanh,
        .bass_freq = 100.0f, .mid_freq = 800.0f,
        .treble_freq = 3200.0f, .presence_freq = 5000.0f,
        .tone_range_db = 12.0f,
        .power_threshold = 0.85f,
    },
    /* British Crunch — inspired by classic British crunch amps
     * 3 stages, asymmetric clipping, pronounced mids
     * Classic rock breakup, responds to pick dynamics */
    [FX_AMP_BRIT_CRUNCH] = {
        .num_stages = 3,
        .stage_gains = { 4.0f, 3.0f, 2.5f, 0 },
        .shaper = waveshape_asym,
        .bass_freq = 120.0f, .mid_freq = 1000.0f,
        .treble_freq = 3500.0f, .presence_freq = 5500.0f,
        .tone_range_db = 15.0f,
        .power_threshold = 0.75f,
    },
    /* Southwest Lead — inspired by American high-gain amps
     * 4 stages, hard clipping, tight low end, aggressive
     * Mesa-style: scooped mids, massive gain on tap */
    [FX_AMP_SOUTHWEST_LEAD] = {
        .num_stages = 4,
        .stage_gains = { 5.0f, 4.0f, 3.5f, 3.0f },
        .shaper = waveshape_hard,
        .bass_freq = 80.0f, .mid_freq = 700.0f,
        .treble_freq = 4000.0f, .presence_freq = 6000.0f,
        .tone_range_db = 18.0f,
        .power_threshold = 0.70f,
    },
    /* Essex Chime — inspired by British chime amps
     * 2 stages, moderate atan saturation, chimey top end
     * Vox-style: edge-of-breakup, jangly, responds to volume knob */
    [FX_AMP_ESSEX_CHIME] = {
        .num_stages = 2,
        .stage_gains = { 3.5f, 2.5f, 0, 0 },
        .shaper = waveshape_atan,
        .bass_freq = 150.0f, .mid_freq = 1200.0f,
        .treble_freq = 4500.0f, .presence_freq = 7000.0f,
        .tone_range_db = 12.0f,
        .power_threshold = 0.80f,
    },
    /* Tweed Blues — inspired by American tweed-era amps
     * 2 stages, asymmetric clipping, spongy feel
     * Bassman-style: warm, bluesy, sags under load */
    [FX_AMP_TWEED_BLUES] = {
        .num_stages = 2,
        .stage_gains = { 4.0f, 3.0f, 0, 0 },
        .shaper = waveshape_asym,
        .bass_freq = 90.0f, .mid_freq = 900.0f,
        .treble_freq = 3000.0f, .presence_freq = 4500.0f,
        .tone_range_db = 14.0f,
        .power_threshold = 0.72f,
    },
    /* Meridian High Gain — inspired by American high-gain metal amps
     * 4 stages, aggressive hard clipping, scooped mids, tight low end
     * Pre-gain HP at 100Hz for tightness, deep scoop at 400Hz, presence peak at 5kHz */
    [FX_AMP_MERIDIAN_HIGH_GAIN] = {
        .num_stages = 4,
        .stage_gains = { 6.0f, 5.0f, 4.5f, 4.0f },
        .shaper = waveshape_hard,
        .bass_freq = 100.0f, .mid_freq = 400.0f,
        .treble_freq = 4000.0f, .presence_freq = 5000.0f,
        .tone_range_db = 20.0f,
        .power_threshold = 0.65f,
    },
    /* Citrus Roar — inspired by British thick/fuzzy crunch amps
     * 3 stages, soft clipping (tanh) for EL34 warmth
     * Warm low-mids, less fizzy top end */
    [FX_AMP_CITRUS_ROAR] = {
        .num_stages = 3,
        .stage_gains = { 4.5f, 3.5f, 3.0f, 0 },
        .shaper = waveshape_tanh,
        .bass_freq = 110.0f, .mid_freq = 600.0f,
        .treble_freq = 3000.0f, .presence_freq = 4500.0f,
        .tone_range_db = 14.0f,
        .power_threshold = 0.72f,
    },
    /* Citrus Terror — inspired by British low-wattage Class A amps
     * 2 stages, asymmetric clipping for Class A character
     * Simple 3-knob design: Gain, Tone, Volume */
    [FX_AMP_CITRUS_TERROR] = {
        .num_stages = 2,
        .stage_gains = { 4.0f, 3.5f, 0, 0 },
        .shaper = waveshape_asym,
        .bass_freq = 120.0f, .mid_freq = 800.0f,
        .treble_freq = 3500.0f, .presence_freq = 5000.0f,
        .tone_range_db = 14.0f,
        .power_threshold = 0.78f,
    },
    /* Regent 800 — inspired by classic British rock/metal amps
     * 2 stages, moderate hard clipping, bright channel character
     * Mid-forward voicing, classic British aggression */
    [FX_AMP_REGENT_800] = {
        .num_stages = 2,
        .stage_gains = { 5.0f, 4.0f, 0, 0 },
        .shaper = waveshape_hard,
        .bass_freq = 100.0f, .mid_freq = 1000.0f,
        .treble_freq = 3800.0f, .presence_freq = 5500.0f,
        .tone_range_db = 16.0f,
        .power_threshold = 0.73f,
    },
    /* Solar Monolith — inspired by massive clean-to-doom amps
     * 2 stages but with HUGE headroom before clipping
     * Deep low end (bass shelf at 40Hz), thunderous */
    [FX_AMP_SOLAR_MONOLITH] = {
        .num_stages = 2,
        .stage_gains = { 3.0f, 2.5f, 0, 0 },
        .shaper = waveshape_atan,
        .bass_freq = 40.0f, .mid_freq = 500.0f,
        .treble_freq = 2500.0f, .presence_freq = 4000.0f,
        .tone_range_db = 18.0f,
        .power_threshold = 0.90f,
    },
    /* Eclipse Drone — inspired by extreme low-end drone amps
     * 2 stages, aggressive saturation, subsonic emphasis
     * Feedback parameter adds harmonic feedback sustain */
    [FX_AMP_ECLIPSE_DRONE] = {
        .num_stages = 2,
        .stage_gains = { 4.5f, 4.0f, 0, 0 },
        .shaper = waveshape_tanh,
        .bass_freq = 30.0f, .mid_freq = 400.0f,
        .treble_freq = 2000.0f, .presence_freq = 3500.0f,
        .tone_range_db = 20.0f,
        .power_threshold = 0.88f,
    },
};

/* ── Tone stack update ────────────────────────────────────────── */

static void amp_update_tone_stack(fx_amp_state_t *amp, float sr) {
    const amp_model_config_t *cfg = &amp_configs[amp->type];
    float range = cfg->tone_range_db;

    float bass_db, mid_db, treble_db, pres_db;

    if (amp->type == FX_AMP_CITRUS_TERROR) {
        /* Single Tone knob: 0.0 = dark, 1.0 = bright
         * Controls a tilt-style EQ: bass goes down as treble goes up */
        float tone = amp->params[FX_AMP_PARAM_TONE];
        bass_db   = (0.5f - tone) * 2.0f * range;
        mid_db    = 0.0f;  /* mids stay flat */
        treble_db = (tone - 0.5f) * 2.0f * range;
        pres_db   = (tone - 0.5f) * 8.0f;
    } else {
        /* Map 0-1 knob to -range..+range dB */
        bass_db   = (amp->params[FX_AMP_PARAM_BASS]   - 0.5f) * 2.0f * range;
        mid_db    = (amp->params[FX_AMP_PARAM_MID]    - 0.5f) * 2.0f * range;
        treble_db = (amp->params[FX_AMP_PARAM_TREBLE] - 0.5f) * 2.0f * range;
        pres_db   = (amp->params[FX_AMP_PARAM_PRESENCE] - 0.5f) * 2.0f * 8.0f;
    }

    fx_biquad_lowshelf(&amp->tone_bass, cfg->bass_freq, bass_db, sr);
    fx_biquad_peak(&amp->tone_mid, cfg->mid_freq, mid_db, 0.7f, sr);
    fx_biquad_highshelf(&amp->tone_treble, cfg->treble_freq, treble_db, sr);
    fx_biquad_highshelf(&amp->presence_filter, cfg->presence_freq, pres_db, sr);

    /* Cache current values so we know when to recalculate */
    if (amp->type == FX_AMP_CITRUS_TERROR) {
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
    if (amp->type == FX_AMP_CITRUS_TERROR) {
        /* Citrus Terror uses single Tone knob — store in cache[0] */
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

    for (int i = 0; i < n; i++) {
        float x = buf[i];

        /* ── Stage 1: Preamp gain stages ─────────────────────── */
        for (int s = 0; s < cfg->num_stages; s++) {
            float stage_gain = cfg->stage_gains[s] * (0.2f + gain_knob * 0.8f);
            x = preamp_stage(x, stage_gain, cfg->shaper,
                            &amp->dc_block_z1[s], dc_coeff);
        }

        /* ── Stage 2: Tone stack EQ ──────────────────────────── */
        x = fx_biquad_process(&amp->tone_bass, x);
        x = fx_biquad_process(&amp->tone_mid, x);
        x = fx_biquad_process(&amp->tone_treble, x);
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

        /* Sag simulation: supply voltage droops under sustained loud signal
         * Makes the amp feel "spongy" — attack has full voltage,
         * sustained notes compress as supply sags */
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
        float makeup = 4.0f;  /* +12dB makeup — waveshapers compress heavily */
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
        case FX_AMP_MERIDIAN_HIGH_GAIN: return 7;  /* Gain, Bass, Mid, Treble, Presence, Volume, Master */
        case FX_AMP_CITRUS_ROAR:        return 5;  /* Gain, Bass, Mid, Treble, Volume */
        case FX_AMP_CITRUS_TERROR:      return 3;  /* Gain, Tone, Volume */
        case FX_AMP_REGENT_800:         return 7;  /* Gain, Bass, Mid, Treble, Presence, Volume, Master */
        case FX_AMP_SOLAR_MONOLITH:     return 6;  /* Gain, Bass, Mid, Treble, Volume, Master */
        case FX_AMP_ECLIPSE_DRONE:      return 6;  /* Gain, Bass, Mid, Treble, Feedback, Volume */
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
