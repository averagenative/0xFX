/*
 * 0xFX — Studio processor DSP dispatch + implementations
 *
 * Post-amp rack gear: compressors, EQ, tape saturation, limiter.
 * These run after amp+cab in the signal chain, before output.
 *
 * Each processor type has init/process/free functions.
 * State is heap-allocated per instance (stored in p->state).
 * Processing is in-place on the buffer.
 *
 * Param conventions: all params 0.0-1.0, mapped to useful ranges inside process.
 */
#include "engine_internal.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ══════════════════════════════════════════════════════════════════
 * Studio processor state structs
 * ══════════════════════════════════════════════════════════════════ */

/* Iron Squeeze — FET compressor (1176-style) */
typedef struct {
    float envelope;      /* envelope follower (peak detection) */
    float gain_reduction; /* current GR in linear */
} iron_squeeze_state_t;

/* Glass EQ — Pultec-style passive EQ */
typedef struct {
    fx_biquad_t low_boost_filt;   /* low shelf boost */
    fx_biquad_t low_cut_filt;     /* low shelf cut (slightly higher freq = dip-then-boost) */
    fx_biquad_t high_boost_filt;  /* high shelf boost */
    fx_biquad_t high_atten_filt;  /* high bell attenuation */
    float       cached_params[6]; /* detect param changes */
    float       cached_sr;
} glass_eq_state_t;

/* Reel Warmth — tape saturation (Studer/Ampex-style) */
typedef struct {
    float       hysteresis_z1;   /* hysteresis feedback state */
    fx_biquad_t hf_rolloff;      /* speed-dependent HF rolloff */
    float       wow_phase;       /* slow wow LFO */
    float       flutter_phase;   /* fast flutter LFO */
    float       cached_speed;    /* detect speed param changes */
    float       cached_sr;
} reel_warmth_state_t;

/* Brick Wall — look-ahead brickwall limiter */
#define BRICK_LOOKAHEAD_MS  1.0f  /* 1ms look-ahead */
#define BRICK_MAX_LOOKAHEAD 64    /* max look-ahead samples (1ms @ 48kHz + margin) */

typedef struct {
    float delay_buf[BRICK_MAX_LOOKAHEAD]; /* look-ahead delay line */
    int   delay_len;                       /* actual delay length in samples */
    int   delay_pos;                       /* write position */
    float envelope;                        /* gain reduction envelope */
} brick_wall_state_t;

/* ══════════════════════════════════════════════════════════════════
 * IRON SQUEEZE — FET compressor
 * Inspired by 1176-style fast attack FET compression
 * Params: [0] input, [1] output, [2] attack, [3] release, [4] ratio
 * ══════════════════════════════════════════════════════════════════ */

static void iron_squeeze_init(fx_studio_instance_t *p, float sr) {
    (void)sr;
    iron_squeeze_state_t *s = (iron_squeeze_state_t *)calloc(1, sizeof(*s));
    if (!s) return;

    s->envelope = 0.0f;
    s->gain_reduction = 1.0f;

    p->state = s;
    p->params[0] = 0.5f;  /* input */
    p->params[1] = 0.5f;  /* output */
    p->params[2] = 0.3f;  /* attack (fast-ish) */
    p->params[3] = 0.5f;  /* release */
    p->params[4] = 0.5f;  /* ratio */
}

static void iron_squeeze_process(fx_studio_instance_t *p, float *buf, int n, float sr) {
    iron_squeeze_state_t *s = (iron_squeeze_state_t *)p->state;
    if (!s) return;

    /* Map params to physical ranges */
    float input_gain = 1.0f + p->params[0] * 3.0f;  /* 1x to 4x input drive */
    float output_gain = p->params[1] * 2.0f;         /* 0 to 2x makeup gain */

    /* Attack: 0.02ms to 10ms */
    float attack_ms = 0.02f + p->params[2] * 9.98f;
    float attack_coeff = expf(-1.0f / (attack_ms * 0.001f * sr));

    /* Release: 50ms to 1200ms */
    float release_ms = 50.0f + p->params[3] * 1150.0f;
    float release_coeff = expf(-1.0f / (release_ms * 0.001f * sr));

    /* Ratio: map 0-1 to discrete ratios: 4:1, 8:1, 12:1, 20:1, inf:1 */
    float ratio;
    float r = p->params[4];
    if (r < 0.2f)      ratio = 4.0f;
    else if (r < 0.4f) ratio = 8.0f;
    else if (r < 0.6f) ratio = 12.0f;
    else if (r < 0.8f) ratio = 20.0f;
    else                ratio = 100.0f;  /* "all buttons" ~ limiting */

    float threshold_db = -20.0f;  /* fixed threshold, input drive controls how much hits it */
    float threshold_lin = powf(10.0f, threshold_db / 20.0f);

    for (int i = 0; i < n; i++) {
        float x = buf[i] * input_gain;

        /* Peak detection envelope follower */
        float abs_x = fabsf(x);
        if (abs_x > s->envelope) {
            s->envelope = attack_coeff * s->envelope + (1.0f - attack_coeff) * abs_x;
        } else {
            s->envelope = release_coeff * s->envelope + (1.0f - release_coeff) * abs_x;
        }

        /* Compute gain reduction */
        float gr = 1.0f;
        if (s->envelope > threshold_lin) {
            /* Convert to dB, apply ratio, convert back */
            float env_db = 20.0f * log10f(s->envelope + 1e-30f);
            float over_db = env_db - threshold_db;
            float target_db = threshold_db + over_db / ratio;
            float target_lin = powf(10.0f, target_db / 20.0f);
            gr = target_lin / (s->envelope + 1e-30f);
        }

        /* Smooth gain reduction */
        if (gr < s->gain_reduction) {
            s->gain_reduction = attack_coeff * s->gain_reduction +
                                (1.0f - attack_coeff) * gr;
        } else {
            s->gain_reduction = release_coeff * s->gain_reduction +
                                (1.0f - release_coeff) * gr;
        }

        /* Apply gain reduction + makeup */
        buf[i] = x * s->gain_reduction * output_gain;
    }
}

/* ══════════════════════════════════════════════════════════════════
 * GLASS EQ — Pultec-style passive EQ
 * Inspired by EQP-1A: simultaneous boost+cut on lows creates
 * a characteristic dip-then-boost curve
 * Params: [0] low_freq, [1] low_boost, [2] low_cut,
 *         [3] high_freq, [4] high_boost, [5] high_atten
 * ══════════════════════════════════════════════════════════════════ */

/* Discrete frequency selections */
static const float glass_low_freqs[]  = { 20.0f, 30.0f, 60.0f, 100.0f };
static const float glass_high_freqs[] = { 3000.0f, 4000.0f, 5000.0f, 8000.0f,
                                           10000.0f, 12000.0f, 16000.0f };

static void glass_eq_init(fx_studio_instance_t *p, float sr) {
    glass_eq_state_t *s = (glass_eq_state_t *)calloc(1, sizeof(*s));
    if (!s) return;

    /* Initialize all filters flat (0dB gain) */
    fx_biquad_lowshelf(&s->low_boost_filt, 60.0f, 0.0f, sr);
    fx_biquad_lowshelf(&s->low_cut_filt, 80.0f, 0.0f, sr);
    fx_biquad_highshelf(&s->high_boost_filt, 8000.0f, 0.0f, sr);
    fx_biquad_peak(&s->high_atten_filt, 8000.0f, 0.0f, 0.7f, sr);
    memset(s->cached_params, 0, sizeof(s->cached_params));
    s->cached_sr = sr;

    p->state = s;
    p->params[0] = 0.5f;  /* low freq (60Hz) */
    p->params[1] = 0.0f;  /* low boost */
    p->params[2] = 0.0f;  /* low cut */
    p->params[3] = 0.5f;  /* high freq (~8kHz) */
    p->params[4] = 0.0f;  /* high boost */
    p->params[5] = 0.0f;  /* high atten */
}

static void glass_eq_process(fx_studio_instance_t *p, float *buf, int n, float sr) {
    glass_eq_state_t *s = (glass_eq_state_t *)p->state;
    if (!s) return;

    /* Check if params changed — recalculate coefficients */
    bool changed = (s->cached_sr != sr);
    for (int i = 0; i < 6; i++) {
        if (s->cached_params[i] != p->params[i]) {
            changed = true;
            s->cached_params[i] = p->params[i];
        }
    }

    if (changed) {
        s->cached_sr = sr;

        /* Select low frequency */
        int low_idx = (int)(p->params[0] * 3.99f);
        if (low_idx > 3) low_idx = 3;
        float low_freq = glass_low_freqs[low_idx];

        /* Low boost: 0-1 maps to 0dB to +10dB shelf */
        float low_boost_db = p->params[1] * 10.0f;
        fx_biquad_lowshelf(&s->low_boost_filt, low_freq, low_boost_db, sr);

        /* Low cut: 0-1 maps to 0dB to -10dB shelf at slightly higher freq
         * (this creates the Pultec dip-then-boost characteristic) */
        float low_cut_db = -p->params[2] * 10.0f;
        float cut_freq = low_freq * 1.5f;  /* cut centered ~1.5x above boost freq */
        fx_biquad_lowshelf(&s->low_cut_filt, cut_freq, low_cut_db, sr);

        /* Select high frequency */
        int high_idx = (int)(p->params[3] * 6.99f);
        if (high_idx > 6) high_idx = 6;
        float high_freq = glass_high_freqs[high_idx];

        /* High boost: 0-1 maps to 0dB to +10dB shelf */
        float high_boost_db = p->params[4] * 10.0f;
        fx_biquad_highshelf(&s->high_boost_filt, high_freq, high_boost_db, sr);

        /* High atten: broad bell attenuation centered at high freq */
        float high_atten_db = -p->params[5] * 10.0f;
        fx_biquad_peak(&s->high_atten_filt, high_freq, high_atten_db, 0.5f, sr);
    }

    /* Process through all 4 filters in series */
    for (int i = 0; i < n; i++) {
        float x = buf[i];
        x = fx_biquad_process(&s->low_boost_filt, x);
        x = fx_biquad_process(&s->low_cut_filt, x);
        x = fx_biquad_process(&s->high_boost_filt, x);
        x = fx_biquad_process(&s->high_atten_filt, x);
        buf[i] = x;
    }
}

/* ══════════════════════════════════════════════════════════════════
 * REEL WARMTH — tape saturation
 * Inspired by Studer A800 / Ampex ATR tape machines
 * Params: [0] input, [1] speed, [2] bias, [3] output
 * ══════════════════════════════════════════════════════════════════ */

static void reel_warmth_init(fx_studio_instance_t *p, float sr) {
    reel_warmth_state_t *s = (reel_warmth_state_t *)calloc(1, sizeof(*s));
    if (!s) return;

    s->hysteresis_z1 = 0.0f;
    s->wow_phase = 0.0f;
    s->flutter_phase = 0.0f;
    s->cached_speed = -1.0f;
    s->cached_sr = sr;

    /* Default HF rolloff for 15 ips */
    fx_biquad_lowpass(&s->hf_rolloff, 14000.0f, 0.707f, sr);

    p->state = s;
    p->params[0] = 0.5f;  /* input (drive) */
    p->params[1] = 0.5f;  /* speed (15 ips) */
    p->params[2] = 0.5f;  /* bias */
    p->params[3] = 0.7f;  /* output */
}

static void reel_warmth_process(fx_studio_instance_t *p, float *buf, int n, float sr) {
    reel_warmth_state_t *s = (reel_warmth_state_t *)p->state;
    if (!s) return;

    float input_gain = 0.5f + p->params[0] * 2.5f;  /* 0.5x to 3x drive */
    float output_gain = p->params[3];

    /* Speed: 0-1 maps to 7.5/15/30 ips with corresponding HF rolloff */
    float speed = p->params[1];
    if (speed != s->cached_speed || sr != s->cached_sr) {
        s->cached_speed = speed;
        s->cached_sr = sr;
        /* 7.5 ips = lots of HF rolloff, 30 ips = flatter response */
        float rolloff_freq;
        if (speed < 0.33f) {
            rolloff_freq = 8000.0f;       /* 7.5 ips — dark, warm */
        } else if (speed < 0.66f) {
            rolloff_freq = 14000.0f;      /* 15 ips — balanced */
        } else {
            rolloff_freq = 20000.0f;      /* 30 ips — bright, open */
        }
        fx_biquad_lowpass(&s->hf_rolloff, rolloff_freq, 0.707f, sr);
    }

    /* Bias: under-bias = more harmonics (aggressive), over-bias = smoother/duller */
    float bias = p->params[2];
    float hysteresis_fb = 0.3f + (1.0f - bias) * 0.4f;  /* 0.3-0.7 feedback */
    float saturation_drive = 1.0f + (1.0f - bias) * 1.0f;  /* more harmonics when under-biased */

    /* LFO rates for wow and flutter */
    float wow_rate = 0.5f;       /* ~0.5 Hz slow wow */
    float flutter_rate = 8.0f;   /* ~8 Hz faster flutter */
    float wow_inc = wow_rate / sr;
    float flutter_inc = flutter_rate / sr;

    for (int i = 0; i < n; i++) {
        float x = buf[i] * input_gain;

        /* Simplified hysteresis tape saturation:
         * x_sat = tanh(x + feedback * z1)
         * z1 = x_sat
         * This creates the asymmetric, history-dependent saturation of tape */
        float x_hyst = x + hysteresis_fb * s->hysteresis_z1;
        x_hyst *= saturation_drive;

        /* Soft saturation (tape-like) */
        float x_sat = tanhf(x_hyst);
        s->hysteresis_z1 = x_sat;

        /* Speed-dependent HF rolloff */
        x_sat = fx_biquad_process(&s->hf_rolloff, x_sat);

        /* Subtle wow + flutter pitch modulation (applied as amplitude modulation
         * since we don't have a delay line — this approximates the effect) */
        float wow = sinf(s->wow_phase * 2.0f * (float)M_PI) * 0.003f;
        float flutter = sinf(s->flutter_phase * 2.0f * (float)M_PI) * 0.001f;
        x_sat *= (1.0f + wow + flutter);

        s->wow_phase += wow_inc;
        if (s->wow_phase >= 1.0f) s->wow_phase -= 1.0f;
        s->flutter_phase += flutter_inc;
        if (s->flutter_phase >= 1.0f) s->flutter_phase -= 1.0f;

        buf[i] = x_sat * output_gain;
    }
}

/* ══════════════════════════════════════════════════════════════════
 * BRICK WALL — look-ahead brickwall limiter
 * Inspired by modern mastering limiters
 * Params: [0] threshold, [1] ceiling, [2] release
 * ══════════════════════════════════════════════════════════════════ */

static void brick_wall_init(fx_studio_instance_t *p, float sr) {
    brick_wall_state_t *s = (brick_wall_state_t *)calloc(1, sizeof(*s));
    if (!s) return;

    /* Calculate look-ahead delay in samples */
    s->delay_len = (int)(BRICK_LOOKAHEAD_MS * 0.001f * sr);
    if (s->delay_len > BRICK_MAX_LOOKAHEAD) s->delay_len = BRICK_MAX_LOOKAHEAD;
    if (s->delay_len < 1) s->delay_len = 1;
    s->delay_pos = 0;
    s->envelope = 0.0f;
    memset(s->delay_buf, 0, sizeof(s->delay_buf));

    p->state = s;
    p->params[0] = 0.5f;  /* threshold (-6dB) */
    p->params[1] = 0.9f;  /* ceiling (-0.1dB) */
    p->params[2] = 0.5f;  /* release (medium) */
}

static void brick_wall_process(fx_studio_instance_t *p, float *buf, int n, float sr) {
    brick_wall_state_t *s = (brick_wall_state_t *)p->state;
    if (!s) return;

    /* Threshold: 0-1 maps to -12dB to 0dB */
    float threshold_db = -12.0f + p->params[0] * 12.0f;
    float threshold_lin = powf(10.0f, threshold_db / 20.0f);

    /* Ceiling: 0-1 maps to -1dB to 0dB */
    float ceiling_db = -1.0f + p->params[1] * 1.0f;
    float ceiling_lin = powf(10.0f, ceiling_db / 20.0f);

    /* Release: discrete — fast/medium/slow/auto
     * 0-0.25 = fast(50ms), 0.25-0.5 = medium(100ms), 0.5-0.75 = slow(300ms), 0.75-1 = auto */
    float release_ms;
    float r = p->params[2];
    if (r < 0.25f)      release_ms = 50.0f;
    else if (r < 0.5f)  release_ms = 100.0f;
    else if (r < 0.75f) release_ms = 300.0f;
    else                 release_ms = 150.0f; /* auto: moderate default */

    float release_coeff = expf(-1.0f / (release_ms * 0.001f * sr));
    /* Fast attack for brickwall: essentially instant */
    float attack_coeff = expf(-1.0f / (0.05f * 0.001f * sr));

    for (int i = 0; i < n; i++) {
        float x = buf[i];

        /* Read delayed sample (look-ahead) */
        float delayed = s->delay_buf[s->delay_pos];

        /* Write current sample to delay */
        s->delay_buf[s->delay_pos] = x;
        s->delay_pos++;
        if (s->delay_pos >= s->delay_len) s->delay_pos = 0;

        /* Peak detection on current (non-delayed) sample */
        float abs_x = fabsf(x);

        /* Envelope: fast attack, variable release */
        if (abs_x > s->envelope) {
            s->envelope = attack_coeff * s->envelope + (1.0f - attack_coeff) * abs_x;
        } else {
            s->envelope = release_coeff * s->envelope;
        }

        /* Compute gain reduction */
        float gain = 1.0f;
        if (s->envelope > threshold_lin) {
            gain = threshold_lin / (s->envelope + 1e-30f);
        }

        /* Apply ceiling */
        gain *= ceiling_lin / threshold_lin;
        if (gain > 1.0f) gain = 1.0f;

        /* Apply to delayed signal */
        buf[i] = delayed * gain;
    }
}

/* ══════════════════════════════════════════════════════════════════
 * Type name + param name tables
 * ══════════════════════════════════════════════════════════════════ */

static const char *studio_type_names[FX_STUDIO_COUNT] = {
    "Iron Squeeze",
    "Glass EQ",
    "Reel Warmth",
    "Brick Wall",
};

static const char *iron_squeeze_params[] = { "Input", "Output", "Attack", "Release", "Ratio" };
static const char *glass_eq_params[]     = { "Low Freq", "Low Boost", "Low Cut",
                                              "High Freq", "High Boost", "High Atten" };
static const char *reel_warmth_params[]  = { "Input", "Speed", "Bias", "Output" };
static const char *brick_wall_params[]   = { "Threshold", "Ceiling", "Release" };

const char *fx_studio_get_type_name(fx_studio_type_t type) {
    if (type < 0 || type >= FX_STUDIO_COUNT) return "?";
    return studio_type_names[type];
}

int fx_studio_get_param_count(fx_studio_type_t type) {
    switch (type) {
        case FX_STUDIO_IRON_SQUEEZE: return 5;
        case FX_STUDIO_GLASS_EQ:     return 6;
        case FX_STUDIO_REEL_WARMTH:  return 4;
        case FX_STUDIO_BRICK_WALL:   return 3;
        default: return 0;
    }
}

const char *fx_studio_get_param_name(fx_studio_type_t type, int param) {
    if (param < 0 || param >= FX_STUDIO_MAX_PARAMS) return "?";

    switch (type) {
        case FX_STUDIO_IRON_SQUEEZE:
            if (param < 5) return iron_squeeze_params[param];
            break;
        case FX_STUDIO_GLASS_EQ:
            if (param < 6) return glass_eq_params[param];
            break;
        case FX_STUDIO_REEL_WARMTH:
            if (param < 4) return reel_warmth_params[param];
            break;
        case FX_STUDIO_BRICK_WALL:
            if (param < 3) return brick_wall_params[param];
            break;
        default:
            break;
    }

    static const char *generic[] = {
        "Param 1", "Param 2", "Param 3", "Param 4",
        "Param 5", "Param 6", "Param 7", "Param 8",
    };
    return generic[param];
}

/* ══════════════════════════════════════════════════════════════════
 * DSP dispatch
 * ══════════════════════════════════════════════════════════════════ */

void fx_studio_init_state(fx_studio_instance_t *p, float sr) {
    if (!p) return;

    /* Set default param values */
    for (int i = 0; i < FX_STUDIO_MAX_PARAMS; i++) p->params[i] = 0.5f;
    p->state = NULL;

    switch (p->type) {
        case FX_STUDIO_IRON_SQUEEZE: iron_squeeze_init(p, sr); break;
        case FX_STUDIO_GLASS_EQ:     glass_eq_init(p, sr);     break;
        case FX_STUDIO_REEL_WARMTH:  reel_warmth_init(p, sr);  break;
        case FX_STUDIO_BRICK_WALL:   brick_wall_init(p, sr);   break;
        default:
            break;
    }
}

void fx_studio_free_state(fx_studio_instance_t *p) {
    if (!p) return;

    /* No sub-allocations for Phase 1 processors — just free the state struct */
    if (p->state) {
        free(p->state);
        p->state = NULL;
    }
}

void fx_studio_process_dsp(fx_studio_instance_t *p, float *buf, int n, float sr) {
    if (!p || p->bypass || !p->state) return;

    switch (p->type) {
        case FX_STUDIO_IRON_SQUEEZE: iron_squeeze_process(p, buf, n, sr); break;
        case FX_STUDIO_GLASS_EQ:     glass_eq_process(p, buf, n, sr);     break;
        case FX_STUDIO_REEL_WARMTH:  reel_warmth_process(p, buf, n, sr);  break;
        case FX_STUDIO_BRICK_WALL:   brick_wall_process(p, buf, n, sr);   break;
        default:
            break;
    }
}
