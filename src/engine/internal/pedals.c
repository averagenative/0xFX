/*
 * 0xFX — Pedal DSP dispatch + implementations
 *
 * Each pedal type has init/process/free functions.
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
 * Pedal state structs
 * ══════════════════════════════════════════════════════════════════ */

/* Jade Drive — mid-humped soft-clip overdrive */
typedef struct {
    fx_biquad_t pre_hp;    /* input coupling cap (highpass ~720Hz hump) */
    fx_biquad_t mid_peak;  /* mid-hump EQ */
    fx_biquad_t post_lp;   /* tone control (lowpass) */
    float dc_z1;           /* DC blocker state */
} jade_drive_state_t;

/* Gold Drive — transparent overdrive with clean blend */
typedef struct {
    fx_biquad_t post_lp;
    float dc_z1;
} gold_drive_state_t;

/* Rodent — op-amp hard clipping with backwards filter */
typedef struct {
    fx_biquad_t filter;    /* LP filter (backwards: high = dark, low = bright) */
    float dc_z1;
} rodent_state_t;

/* Echo Delay — clean digital delay */
typedef struct {
    float *buffer;
    int    buf_len;
    int    write_pos;
} echo_delay_state_t;

/* Hall Verb — simple Freeverb (4 comb + 2 allpass) */
#define VERB_NUM_COMBS    4
#define VERB_NUM_ALLPASS  2

typedef struct {
    float *comb_buf[VERB_NUM_COMBS];
    int    comb_len[VERB_NUM_COMBS];
    int    comb_pos[VERB_NUM_COMBS];
    float  comb_filt[VERB_NUM_COMBS]; /* LP filter state in comb feedback */
    float *ap_buf[VERB_NUM_ALLPASS];
    int    ap_len[VERB_NUM_ALLPASS];
    int    ap_pos[VERB_NUM_ALLPASS];
} hall_verb_state_t;

/* Squeeze Box — simple OTA compressor */
typedef struct {
    float envelope;
} squeeze_box_state_t;

/* ══════════════════════════════════════════════════════════════════
 * DC blocker helper (shared by drive pedals)
 * ══════════════════════════════════════════════════════════════════ */

static inline float dc_block(float in, float *z1, float R) {
    float out = in - *z1;
    *z1 = in - R * out;
    return out;
}

/* ══════════════════════════════════════════════════════════════════
 * JADE DRIVE — mid-humped soft-clip overdrive
 * Inspired by classic Japanese OD circuits (Tube Screamer topology)
 * Params: [0] drive, [1] tone, [2] level
 * ══════════════════════════════════════════════════════════════════ */

static void jade_drive_init(fx_pedal_instance_t *p, float sr) {
    jade_drive_state_t *s = (jade_drive_state_t *)calloc(1, sizeof(*s));
    if (!s) return;

    /* Input coupling: highpass removes bass before clipping (the TS "mid hump") */
    fx_biquad_highpass(&s->pre_hp, 720.0f, 0.707f, sr);
    /* Mid-range emphasis peak */
    fx_biquad_peak(&s->mid_peak, 1000.0f, 3.0f, 1.2f, sr);
    /* Tone control: lowpass, will be updated per-block */
    fx_biquad_lowpass(&s->post_lp, 4000.0f, 0.707f, sr);

    p->state = s;
    p->params[0] = 0.5f;  /* drive */
    p->params[1] = 0.5f;  /* tone */
    p->params[2] = 0.7f;  /* level */
}

static void jade_drive_process(fx_pedal_instance_t *p, float *buf, int n, float sr) {
    jade_drive_state_t *s = (jade_drive_state_t *)p->state;
    if (!s) return;

    float drive = 1.0f + p->params[0] * p->params[0] * 30.0f; /* 1x-31x */
    float level = p->params[2];

    /* Update tone control: map 0-1 to 800Hz-8000Hz lowpass */
    float tone_freq = 800.0f + p->params[1] * 7200.0f;
    fx_biquad_lowpass(&s->post_lp, tone_freq, 0.707f, sr);

    float dc_R = 1.0f - (2.0f * (float)M_PI * 10.0f / sr);

    for (int i = 0; i < n; i++) {
        float x = buf[i];

        /* Pre-clipping: highpass (removes bass = mid hump) + mid peak */
        x = fx_biquad_process(&s->pre_hp, x);
        x = fx_biquad_process(&s->mid_peak, x);

        /* Drive + soft clip: x/(1+|x|) — asymmetric, warm */
        x *= drive;
        x = x / (1.0f + fabsf(x));

        /* DC block */
        x = dc_block(x, &s->dc_z1, dc_R);

        /* Tone control */
        x = fx_biquad_process(&s->post_lp, x);

        /* Output level */
        x *= level;

        buf[i] = x;
    }
}

/* ══════════════════════════════════════════════════════════════════
 * GOLD DRIVE — transparent overdrive with clean blend
 * Inspired by Klon-style circuits
 * Params: [0] gain, [1] treble, [2] output
 * ══════════════════════════════════════════════════════════════════ */

static void gold_drive_init(fx_pedal_instance_t *p, float sr) {
    gold_drive_state_t *s = (gold_drive_state_t *)calloc(1, sizeof(*s));
    if (!s) return;

    fx_biquad_highshelf(&s->post_lp, 3000.0f, 3.0f, sr);

    p->state = s;
    p->params[0] = 0.5f;  /* gain */
    p->params[1] = 0.5f;  /* treble */
    p->params[2] = 0.7f;  /* output */
}

static void gold_drive_process(fx_pedal_instance_t *p, float *buf, int n, float sr) {
    gold_drive_state_t *s = (gold_drive_state_t *)p->state;
    if (!s) return;

    float gain = 1.0f + p->params[0] * p->params[0] * 15.0f;
    float treble_db = (p->params[1] - 0.5f) * 12.0f; /* -6dB to +6dB */
    float output = p->params[2];

    fx_biquad_highshelf(&s->post_lp, 3000.0f, treble_db, sr);

    float dc_R = 1.0f - (2.0f * (float)M_PI * 10.0f / sr);

    for (int i = 0; i < n; i++) {
        float dry = buf[i];

        /* Clip path: soft clip */
        float wet = dry * gain;
        wet = tanhf(wet);
        wet = dc_block(wet, &s->dc_z1, dc_R);

        /* Blend: more gain knob = more wet, less dry
         * At gain=0: 100% dry. At gain=1: ~30% dry + 70% wet (always some clean) */
        float blend = p->params[0] * 0.7f;
        float x = dry * (1.0f - blend) + wet * blend;

        /* Treble + output */
        x = fx_biquad_process(&s->post_lp, x);
        x *= output;

        buf[i] = x;
    }
}

/* ══════════════════════════════════════════════════════════════════
 * RODENT — op-amp hard clipping distortion
 * Inspired by ProCo RAT-style circuits
 * Params: [0] distortion, [1] filter (backwards: 1=dark, 0=bright), [2] volume
 * ══════════════════════════════════════════════════════════════════ */

static void rodent_init(fx_pedal_instance_t *p, float sr) {
    rodent_state_t *s = (rodent_state_t *)calloc(1, sizeof(*s));
    if (!s) return;

    fx_biquad_lowpass(&s->filter, 5000.0f, 0.707f, sr);

    p->state = s;
    p->params[0] = 0.5f;  /* distortion */
    p->params[1] = 0.5f;  /* filter (backwards) */
    p->params[2] = 0.5f;  /* volume */
}

static void rodent_process(fx_pedal_instance_t *p, float *buf, int n, float sr) {
    rodent_state_t *s = (rodent_state_t *)p->state;
    if (!s) return;

    float dist = 1.0f + p->params[0] * p->params[0] * 50.0f; /* heavy gain range */
    /* Filter is backwards: low value = bright (high cutoff), high value = dark (low cutoff) */
    float filter_freq = 800.0f + (1.0f - p->params[1]) * 9200.0f; /* 800-10000Hz */
    float volume = p->params[2];

    fx_biquad_lowpass(&s->filter, filter_freq, 0.707f, sr);

    float dc_R = 1.0f - (2.0f * (float)M_PI * 10.0f / sr);

    for (int i = 0; i < n; i++) {
        float x = buf[i];

        /* Op-amp gain stage */
        x *= dist;

        /* Hard clip (diode clipping) with slight asymmetry */
        if (x > 1.0f) x = 1.0f;
        else if (x < -0.95f) x = -0.95f;

        /* DC block */
        x = dc_block(x, &s->dc_z1, dc_R);

        /* Filter */
        x = fx_biquad_process(&s->filter, x);

        /* Volume */
        x *= volume;

        buf[i] = x;
    }
}

/* ══════════════════════════════════════════════════════════════════
 * ECHO DELAY — clean digital delay
 * Params: [0] time (0-1 → 20ms-1000ms), [1] feedback, [2] mix, [3] reserved
 * ══════════════════════════════════════════════════════════════════ */

static void echo_delay_init(fx_pedal_instance_t *p, float sr) {
    echo_delay_state_t *s = (echo_delay_state_t *)calloc(1, sizeof(*s));
    if (!s) return;

    /* Max 1 second at any sample rate */
    s->buf_len = (int)(sr * 1.0f) + 1;
    s->buffer = (float *)calloc((size_t)s->buf_len, sizeof(float));
    s->write_pos = 0;

    p->state = s;
    p->params[0] = 0.4f;  /* time (~400ms) */
    p->params[1] = 0.3f;  /* feedback */
    p->params[2] = 0.3f;  /* mix */
}

static void echo_delay_process(fx_pedal_instance_t *p, float *buf, int n, float sr) {
    echo_delay_state_t *s = (echo_delay_state_t *)p->state;
    if (!s || !s->buffer) return;

    /* Map time param: 20ms-1000ms */
    float delay_ms = 20.0f + p->params[0] * 980.0f;
    int delay_samples = (int)(delay_ms * sr / 1000.0f);
    if (delay_samples >= s->buf_len) delay_samples = s->buf_len - 1;
    if (delay_samples < 1) delay_samples = 1;

    float feedback = p->params[1] * 0.9f; /* cap at 0.9 to prevent runaway */
    float mix = p->params[2];

    for (int i = 0; i < n; i++) {
        float dry = buf[i];

        /* Read from delay line */
        int read_pos = s->write_pos - delay_samples;
        if (read_pos < 0) read_pos += s->buf_len;
        float delayed = s->buffer[read_pos];

        /* Write to delay line: input + feedback */
        s->buffer[s->write_pos] = dry + delayed * feedback;
        s->write_pos = (s->write_pos + 1) % s->buf_len;

        /* Output: dry + wet mix */
        buf[i] = dry * (1.0f - mix) + delayed * mix;
    }
}

static void echo_delay_free(fx_pedal_instance_t *p) {
    echo_delay_state_t *s = (echo_delay_state_t *)p->state;
    if (s) {
        free(s->buffer);
    }
}

/* ══════════════════════════════════════════════════════════════════
 * HALL VERB — Freeverb-style algorithmic reverb
 * Simplified: 4 comb filters + 2 allpass filters
 * Params: [0] decay, [1] damping, [2] mix
 * ══════════════════════════════════════════════════════════════════ */

/* Comb/allpass delay lengths (in samples at 44100Hz, will be scaled) */
static const int comb_lengths_44k[VERB_NUM_COMBS]     = { 1116, 1188, 1277, 1356 };
static const int allpass_lengths_44k[VERB_NUM_ALLPASS] = { 556, 441 };

static void hall_verb_init(fx_pedal_instance_t *p, float sr) {
    hall_verb_state_t *s = (hall_verb_state_t *)calloc(1, sizeof(*s));
    if (!s) return;

    float scale = sr / 44100.0f;

    for (int i = 0; i < VERB_NUM_COMBS; i++) {
        s->comb_len[i] = (int)((float)comb_lengths_44k[i] * scale) + 1;
        s->comb_buf[i] = (float *)calloc((size_t)s->comb_len[i], sizeof(float));
        s->comb_pos[i] = 0;
        s->comb_filt[i] = 0.0f;
    }
    for (int i = 0; i < VERB_NUM_ALLPASS; i++) {
        s->ap_len[i] = (int)((float)allpass_lengths_44k[i] * scale) + 1;
        s->ap_buf[i] = (float *)calloc((size_t)s->ap_len[i], sizeof(float));
        s->ap_pos[i] = 0;
    }

    p->state = s;
    p->params[0] = 0.5f;  /* decay */
    p->params[1] = 0.5f;  /* damping */
    p->params[2] = 0.3f;  /* mix */
}

static void hall_verb_process(fx_pedal_instance_t *p, float *buf, int n, float sr) {
    hall_verb_state_t *s = (hall_verb_state_t *)p->state;
    if (!s) return;
    (void)sr;

    float feedback = 0.7f + p->params[0] * 0.28f; /* 0.7-0.98 */
    float damp = p->params[1] * 0.4f;              /* 0-0.4 LP coefficient */
    float mix = p->params[2];

    for (int i = 0; i < n; i++) {
        float dry = buf[i];
        float wet = 0.0f;

        /* Parallel comb filters with LP damping in feedback */
        for (int c = 0; c < VERB_NUM_COMBS; c++) {
            float *cb = s->comb_buf[c];
            int pos = s->comb_pos[c];

            float out = cb[pos];
            /* LP filter in feedback (damping) */
            s->comb_filt[c] = out * (1.0f - damp) + s->comb_filt[c] * damp;
            cb[pos] = dry + s->comb_filt[c] * feedback;
            s->comb_pos[c] = (pos + 1) % s->comb_len[c];

            wet += out;
        }
        wet *= (1.0f / VERB_NUM_COMBS);

        /* Series allpass filters (diffusion) */
        for (int a = 0; a < VERB_NUM_ALLPASS; a++) {
            float *ab = s->ap_buf[a];
            int pos = s->ap_pos[a];
            float ap_out = ab[pos];
            float ap_in = wet + ap_out * 0.5f;
            ab[pos] = ap_in;
            wet = ap_out - ap_in * 0.5f;
            s->ap_pos[a] = (pos + 1) % s->ap_len[a];
        }

        buf[i] = dry * (1.0f - mix) + wet * mix;
    }
}

static void hall_verb_free(fx_pedal_instance_t *p) {
    hall_verb_state_t *s = (hall_verb_state_t *)p->state;
    if (s) {
        for (int i = 0; i < VERB_NUM_COMBS; i++) free(s->comb_buf[i]);
        for (int i = 0; i < VERB_NUM_ALLPASS; i++) free(s->ap_buf[i]);
    }
}

/* ══════════════════════════════════════════════════════════════════
 * SQUEEZE BOX — simple OTA compressor
 * Params: [0] output, [1] sensitivity
 * ══════════════════════════════════════════════════════════════════ */

static void squeeze_box_init(fx_pedal_instance_t *p, float sr) {
    squeeze_box_state_t *s = (squeeze_box_state_t *)calloc(1, sizeof(*s));
    if (!s) return;
    (void)sr;

    s->envelope = 0.0f;

    p->state = s;
    p->params[0] = 0.5f;  /* output */
    p->params[1] = 0.5f;  /* sensitivity */
}

static void squeeze_box_process(fx_pedal_instance_t *p, float *buf, int n, float sr) {
    squeeze_box_state_t *s = (squeeze_box_state_t *)p->state;
    if (!s) return;

    float output_gain = 0.5f + p->params[0] * 2.0f; /* 0.5x - 2.5x */
    /* Sensitivity maps to threshold: high sensitivity = low threshold */
    float threshold = 0.05f + (1.0f - p->params[1]) * 0.45f; /* 0.05-0.5 */
    float ratio = 4.0f; /* fixed 4:1 ratio (squashy OTA style) */

    float attack  = expf(-1.0f / (0.001f * sr)); /* 1ms attack */
    float release = expf(-1.0f / (0.100f * sr)); /* 100ms release */

    for (int i = 0; i < n; i++) {
        float x = buf[i];
        float abs_x = fabsf(x);

        /* Envelope follower */
        if (abs_x > s->envelope)
            s->envelope = attack * s->envelope + (1.0f - attack) * abs_x;
        else
            s->envelope = release * s->envelope + (1.0f - release) * abs_x;

        /* Gain computation: compress above threshold at fixed ratio */
        float gain = 1.0f;
        if (s->envelope > threshold && threshold > 0.0f) {
            gain = (threshold + (s->envelope - threshold) / ratio) / s->envelope;
        }

        buf[i] = x * gain * output_gain;
    }
}

/* ══════════════════════════════════════════════════════════════════
 * Pedal metadata
 * ══════════════════════════════════════════════════════════════════ */

static const char *pedal_type_names[FX_PEDAL_TYPE_COUNT] = {
    /* Overdrive */
    "Jade Drive", "Gold Drive", "Blues Grit",
    /* Distortion */
    "Rodent", "Orange Distortion", "Metal Zone", "Amp Box",
    /* Fuzz */
    "Mammoth Fuzz", "Round Fuzz", "Wraith Fuzz", "Chaos Fuzz",
    /* Delay */
    "Echo Delay", "Carbon Delay", "Tape Machine", "Memory Echo",
    /* Reverb */
    "Drip Verb", "Plate Verb", "Hall Verb", "Shimmer Verb", "Cloud Verb",
    /* Modulation */
    "Liquid Chorus", "Phase Sweep", "Jet Flanger", "Pulse Trem", "Drift Vibrato",
    /* Wah / Filter */
    "Howl Wah", "Quack Filter",
    /* Compressor */
    "Squeeze Box", "Glass Comp", "Punch Comp",
    /* EQ */
    "Tone Sculptor", "Precision EQ",
    /* Noise */
    "Noise Gate",
    /* Utility */
    "Grit Crush", "Ring Tone", "Warm Tape",
    /* Pitch */
    "Octave Engine", "Pitch Warp",
    /* Looper */
    "Loop Station",
    /* Experimental */
    "Infinite Hold", "Grain Cloud",
};

/* Per-type param names */
static const char *jade_drive_params[]  = { "Drive", "Tone", "Level" };
static const char *gold_drive_params[]  = { "Gain", "Treble", "Output" };
static const char *rodent_params[]      = { "Distortion", "Filter", "Volume" };
static const char *echo_delay_params[]  = { "Time", "Feedback", "Mix", "Sync" };
static const char *hall_verb_params[]   = { "Decay", "Damping", "Mix" };
static const char *squeeze_box_params[] = { "Output", "Sensitivity" };

const char *fx_pedal_get_type_name(fx_pedal_type_t type) {
    if (type < 0 || type >= FX_PEDAL_TYPE_COUNT) return "?";
    return pedal_type_names[type];
}

int fx_pedal_get_param_count(fx_pedal_type_t type) {
    switch (type) {
        case FX_PEDAL_JADE_DRIVE:    return 3;
        case FX_PEDAL_GOLD_DRIVE:    return 3;
        case FX_PEDAL_RODENT:        return 3;
        case FX_PEDAL_ECHO_DELAY:    return 4;
        case FX_PEDAL_HALL_VERB:     return 3;
        case FX_PEDAL_DRIP_VERB:     return 3;
        case FX_PEDAL_SQUEEZE_BOX:   return 2;
        case FX_PEDAL_NOISE_GATE:    return 4;
        case FX_PEDAL_LIQUID_CHORUS: return 3;
        case FX_PEDAL_PHASE_SWEEP:   return 4;
        case FX_PEDAL_HOWL_WAH:      return 3;
        default: return 3;
    }
}

const char *fx_pedal_get_param_name(fx_pedal_type_t type, int param) {
    if (param < 0 || param >= FX_MAX_PARAMS) return "?";

    switch (type) {
        case FX_PEDAL_JADE_DRIVE:
            if (param < 3) return jade_drive_params[param];
            break;
        case FX_PEDAL_GOLD_DRIVE:
            if (param < 3) return gold_drive_params[param];
            break;
        case FX_PEDAL_RODENT:
            if (param < 3) return rodent_params[param];
            break;
        case FX_PEDAL_ECHO_DELAY:
            if (param < 4) return echo_delay_params[param];
            break;
        case FX_PEDAL_HALL_VERB:
            if (param < 3) return hall_verb_params[param];
            break;
        case FX_PEDAL_SQUEEZE_BOX:
            if (param < 2) return squeeze_box_params[param];
            break;
        default:
            break;
    }

    /* Fallback generic names */
    static const char *generic[] = {
        "Param 1", "Param 2", "Param 3", "Param 4",
        "Param 5", "Param 6", "Param 7", "Param 8",
        "Param 9", "Param 10", "Param 11", "Param 12",
    };
    return generic[param];
}

/* ══════════════════════════════════════════════════════════════════
 * DSP dispatch
 * ══════════════════════════════════════════════════════════════════ */

void fx_pedal_init_state(fx_pedal_instance_t *p, float sr) {
    if (!p) return;

    /* Set default param values */
    for (int i = 0; i < FX_MAX_PARAMS; i++) p->params[i] = 0.5f;
    p->state = NULL;

    switch (p->type) {
        case FX_PEDAL_JADE_DRIVE:  jade_drive_init(p, sr); break;
        case FX_PEDAL_GOLD_DRIVE:  gold_drive_init(p, sr); break;
        case FX_PEDAL_RODENT:      rodent_init(p, sr);     break;
        case FX_PEDAL_ECHO_DELAY:  echo_delay_init(p, sr); break;
        case FX_PEDAL_HALL_VERB:   hall_verb_init(p, sr);   break;
        case FX_PEDAL_SQUEEZE_BOX: squeeze_box_init(p, sr); break;
        default:
            /* Unimplemented pedals: passthrough */
            break;
    }
}

void fx_pedal_free_state(fx_pedal_instance_t *p) {
    if (!p) return;

    /* Type-specific cleanup (for types with sub-allocations) */
    if (p->state) {
        switch (p->type) {
            case FX_PEDAL_ECHO_DELAY: echo_delay_free(p); break;
            case FX_PEDAL_HALL_VERB:  hall_verb_free(p);   break;
            default: break;
        }
        free(p->state);
        p->state = NULL;
    }
}

void fx_pedal_process(fx_pedal_instance_t *p, float *buf, int n, float sr) {
    if (!p || p->bypass || !p->state) return;

    switch (p->type) {
        case FX_PEDAL_JADE_DRIVE:  jade_drive_process(p, buf, n, sr);  break;
        case FX_PEDAL_GOLD_DRIVE:  gold_drive_process(p, buf, n, sr);  break;
        case FX_PEDAL_RODENT:      rodent_process(p, buf, n, sr);      break;
        case FX_PEDAL_ECHO_DELAY:  echo_delay_process(p, buf, n, sr);  break;
        case FX_PEDAL_HALL_VERB:   hall_verb_process(p, buf, n, sr);   break;
        case FX_PEDAL_SQUEEZE_BOX: squeeze_box_process(p, buf, n, sr); break;
        default:
            /* Unimplemented pedals: passthrough (no state, caught above) */
            break;
    }
}
