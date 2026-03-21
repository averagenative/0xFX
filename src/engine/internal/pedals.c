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

/* Drip Verb — spring reverb (allpass diffusion + short comb filters) */
#define DRIP_NUM_ALLPASS  4
#define DRIP_NUM_COMBS    3

typedef struct {
    float *ap_buf[DRIP_NUM_ALLPASS];
    int    ap_len[DRIP_NUM_ALLPASS];
    int    ap_pos[DRIP_NUM_ALLPASS];
    float *comb_buf[DRIP_NUM_COMBS];
    int    comb_len[DRIP_NUM_COMBS];
    int    comb_pos[DRIP_NUM_COMBS];
    float  comb_filt[DRIP_NUM_COMBS]; /* LP state in comb feedback */
    fx_biquad_t tone_lp;              /* tone control on wet signal */
} drip_verb_state_t;

/* Carbon Delay — analog BBD-style delay */
#define CARBON_BUF_SECONDS 1.1f

typedef struct {
    float      *buffer;
    int         buf_len;
    int         write_pos;
    fx_biquad_t feedback_lp;  /* LP in feedback path (darkens repeats) */
    float       lfo_phase;    /* LFO for mod effect */
} carbon_delay_state_t;

/* Tape Machine — tape echo with wow/flutter */
#define TAPE_BUF_SECONDS 1.5f

typedef struct {
    float      *buffer;
    int         buf_len;
    int         write_pos;
    float       wow_phase;     /* slow LFO (wow) */
    float       flutter_phase; /* fast LFO (flutter) */
} tape_machine_state_t;

/* Howl Wah — expression wah (swept bandpass/peak) */
typedef struct {
    fx_biquad_t peak_filter;
} howl_wah_state_t;

/* Quack Filter — auto-wah / envelope filter */
typedef struct {
    float       envelope;
    fx_biquad_t bp_filter;
} quack_filter_state_t;

/* Liquid Chorus — BBD-style chorus */
#define CHORUS_BUF_LEN 2048

typedef struct {
    float buffer[CHORUS_BUF_LEN];
    int   write_pos;
    float lfo_phase;
} liquid_chorus_state_t;

/* Phase Sweep — 6-stage allpass phaser */
#define PHASER_STAGES 6

typedef struct {
    fx_biquad_t ap[PHASER_STAGES];
    float       lfo_phase;
    float       feedback_z1;  /* feedback sample */
} phase_sweep_state_t;

/* Pulse Trem — tremolo (sine/square/triangle LFO) */
typedef struct {
    float lfo_phase;
} pulse_trem_state_t;

/* Mammoth Fuzz — Big Muff-style 4-stage fuzz */
typedef struct {
    float dc_z1[4];        /* DC blocker state per stage */
    fx_biquad_t tone_lp;   /* scooped-mid: lowpass path */
    fx_biquad_t tone_hp;   /* scooped-mid: highpass path */
} mammoth_fuzz_state_t;

/* Round Fuzz — germanium Fuzz Face-style */
typedef struct {
    float dc_z1;
} round_fuzz_state_t;

/* Chaos Fuzz — gated/sputtery fuzz */
typedef struct {
    float feedback_z1;     /* feedback from output to input */
} chaos_fuzz_state_t;

/* Grit Crush — bitcrusher + sample-and-hold */
typedef struct {
    float hold_sample;
    int   hold_counter;
} grit_crush_state_t;

/* Ring Tone — ring modulator */
typedef struct {
    float phase;           /* sine oscillator phase */
} ring_tone_state_t;

/* Warm Tape — tape saturation + warmth LP */
typedef struct {
    float warmth_z1;       /* one-pole LP filter state */
} warm_tape_state_t;

/* Tone Sculptor — 7-band graphic EQ */
#define TONE_SCULPTOR_BANDS 7

typedef struct {
    fx_biquad_t bands[TONE_SCULPTOR_BANDS];
    float       cached_gains[TONE_SCULPTOR_BANDS];  /* detect param changes */
    float       cached_output;
    float       cached_sr;
} tone_sculptor_state_t;

/* Fixed band center frequencies (Hz) */
static const float tone_sculptor_freqs[TONE_SCULPTOR_BANDS] = {
    100.0f, 200.0f, 400.0f, 800.0f, 1600.0f, 3200.0f, 6400.0f
};

/* Drift Vibrato — true pitch vibrato via modulated delay line */
#define DRIFT_BUF_LEN 512  /* ~11ms at 44.1kHz, plenty for 5ms vibrato */

typedef struct {
    float buffer[DRIFT_BUF_LEN];
    int   write_pos;
    float lfo_phase;
} drift_vibrato_state_t;

/* Jet Flanger — through-zero flanging with negative feedback */
#define FLANGER_BUF_LEN 320  /* ~7.3ms at 44.1kHz */

typedef struct {
    float buffer[FLANGER_BUF_LEN];
    int   write_pos;
    float lfo_phase;
} jet_flanger_state_t;

/* Plate Verb — dense plate reverb (4 allpass + 2 parallel feedback delay with LP) */
#define PLATE_NUM_ALLPASS 4
#define PLATE_NUM_DELAYS  2

typedef struct {
    float *ap_buf[PLATE_NUM_ALLPASS];
    int    ap_len[PLATE_NUM_ALLPASS];
    int    ap_pos[PLATE_NUM_ALLPASS];
    float *dl_buf[PLATE_NUM_DELAYS];
    int    dl_len[PLATE_NUM_DELAYS];
    int    dl_pos[PLATE_NUM_DELAYS];
    float  dl_filt[PLATE_NUM_DELAYS];  /* LP state in feedback */
    float  dl_fb[PLATE_NUM_DELAYS];    /* feedback sample */
} plate_verb_state_t;

/* Shimmer Verb — hall-style reverb with octave-up pitch shift in feedback */
#define SHIMMER_NUM_COMBS   4
#define SHIMMER_NUM_ALLPASS 2

typedef struct {
    float *comb_buf[SHIMMER_NUM_COMBS];
    int    comb_len[SHIMMER_NUM_COMBS];
    int    comb_pos[SHIMMER_NUM_COMBS];
    float  comb_filt[SHIMMER_NUM_COMBS];
    float *ap_buf[SHIMMER_NUM_ALLPASS];
    int    ap_len[SHIMMER_NUM_ALLPASS];
    int    ap_pos[SHIMMER_NUM_ALLPASS];
    /* Pitch shift state: read pointer advances at 2x for octave up */
    float  pitch_read_pos;   /* fractional read position in comb_buf[0] */
} shimmer_verb_state_t;

/* Octave Engine — polyphonic octave shifter (sub + octave-up via delay buffer) */
#define OCTAVE_BUF_LEN 2048

typedef struct {
    float buffer[OCTAVE_BUF_LEN];
    int   write_pos;
    float sub_read_pos;  /* advances at 0.5x speed for sub-octave */
    float up_read_pos;   /* advances at 2.0x speed for octave-up */
} octave_engine_state_t;

/* Loop Station — looper with record/play/overdub */
#define LOOP_MAX_SECONDS 30
/* max_length allocated at init based on sample rate; 30s * 44100 = 1323000 */
#define LOOP_MAX_SAMPLES 1400000   /* headroom for up to ~31.7s @ 44.1kHz */

typedef struct {
    float *buffer;      /* heap-allocated loop buffer */
    int    max_length;  /* size of buffer in samples */
    int    length;      /* current recorded loop length (0 = not set) */
    int    position;    /* current playback/record position */
    int    mode;        /* 0=idle, 1=recording, 2=playing, 3=overdub */
    int    prev_mode;   /* detect mode changes */
} loop_station_state_t;

/* Infinite Hold — freeze / drone pedal */
#define HOLD_BUF_LEN 2048

typedef struct {
    float buffer[HOLD_BUF_LEN];
    int   position;      /* looping playback position */
    int   capture_pos;   /* samples captured so far during fill */
    int   frozen;        /* 0 = passthrough / capturing, 1 = looping */
    float amplitude;     /* current amplitude of frozen signal (decay) */
} infinite_hold_state_t;

/* Grain Cloud — granular delay */
#define GRAIN_REC_LEN  44100  /* 1 second record buffer */
#define GRAIN_VOICES   4

typedef struct {
    float read_pos;   /* fractional playback position in record buffer */
    int   remaining;  /* samples left in this grain */
    float pitch_rate; /* playback rate (0.5-2.0) */
} grain_voice_t;

typedef struct {
    float       rec_buf[GRAIN_REC_LEN];
    int         write_pos;          /* circular write head */
    grain_voice_t voices[GRAIN_VOICES];
    float       trigger_accum;      /* fractional trigger accumulator */
} grain_cloud_state_t;

/* Cloud Verb — long ambient reverb with freeze/near-infinite feedback */
#define CLOUD_NUM_COMBS   4
#define CLOUD_NUM_ALLPASS 2

typedef struct {
    float *comb_buf[CLOUD_NUM_COMBS];
    int    comb_len[CLOUD_NUM_COMBS];
    int    comb_pos[CLOUD_NUM_COMBS];
    float  comb_filt[CLOUD_NUM_COMBS];
    float *ap_buf[CLOUD_NUM_ALLPASS];
    int    ap_len[CLOUD_NUM_ALLPASS];
    int    ap_pos[CLOUD_NUM_ALLPASS];
    float  lp_state;  /* one-pole LP on wet output */
} cloud_verb_state_t;

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
 * DRIP VERB — spring reverb
 * Allpass diffusion + short comb filters with spring-like resonance
 * Params: [0] dwell, [1] tone, [2] mix
 * ══════════════════════════════════════════════════════════════════ */

/* Allpass and comb delay lengths (samples at 44.1kHz) */
static const int drip_ap_lens_44k[DRIP_NUM_ALLPASS]   = { 113, 162, 241, 399 };
static const int drip_comb_lens_44k[DRIP_NUM_COMBS]    = { 631, 797, 1009 };

static void drip_verb_init(fx_pedal_instance_t *p, float sr) {
    drip_verb_state_t *s = (drip_verb_state_t *)calloc(1, sizeof(*s));
    if (!s) return;

    float scale = sr / 44100.0f;
    for (int i = 0; i < DRIP_NUM_ALLPASS; i++) {
        s->ap_len[i] = (int)((float)drip_ap_lens_44k[i] * scale) + 1;
        s->ap_buf[i] = (float *)calloc((size_t)s->ap_len[i], sizeof(float));
        s->ap_pos[i] = 0;
    }
    for (int i = 0; i < DRIP_NUM_COMBS; i++) {
        s->comb_len[i] = (int)((float)drip_comb_lens_44k[i] * scale) + 1;
        s->comb_buf[i] = (float *)calloc((size_t)s->comb_len[i], sizeof(float));
        s->comb_pos[i] = 0;
        s->comb_filt[i] = 0.0f;
    }
    fx_biquad_lowpass(&s->tone_lp, 4000.0f, 0.707f, sr);

    p->state = s;
    p->params[0] = 0.5f; /* dwell */
    p->params[1] = 0.6f; /* tone */
    p->params[2] = 0.3f; /* mix */
}

static void drip_verb_process(fx_pedal_instance_t *p, float *buf, int n, float sr) {
    drip_verb_state_t *s = (drip_verb_state_t *)p->state;
    if (!s) return;

    float dwell   = 0.5f + p->params[0] * 0.45f; /* 0.5-0.95 feedback */
    float tone_hz = 1000.0f + p->params[1] * 7000.0f; /* 1kHz-8kHz */
    float mix     = p->params[2];

    fx_biquad_lowpass(&s->tone_lp, tone_hz, 0.707f, sr);

    for (int i = 0; i < n; i++) {
        float dry = buf[i];

        /* Series allpass diffusion */
        float x = dry;
        for (int a = 0; a < DRIP_NUM_ALLPASS; a++) {
            float *ab = s->ap_buf[a];
            int pos   = s->ap_pos[a];
            float out = ab[pos];
            float in  = x + out * 0.6f;
            ab[pos]   = in;
            x         = out - in * 0.6f;
            s->ap_pos[a] = (pos + 1) % s->ap_len[a];
        }

        /* Parallel comb filters (spring resonance) */
        float wet = 0.0f;
        for (int c = 0; c < DRIP_NUM_COMBS; c++) {
            float *cb = s->comb_buf[c];
            int pos   = s->comb_pos[c];
            float out = cb[pos];
            /* LP damping in feedback */
            s->comb_filt[c] = out * 0.55f + s->comb_filt[c] * 0.45f;
            cb[pos] = x + s->comb_filt[c] * dwell;
            s->comb_pos[c] = (pos + 1) % s->comb_len[c];
            wet += out;
        }
        wet *= (1.0f / DRIP_NUM_COMBS);

        /* Tone control on wet */
        wet = fx_biquad_process(&s->tone_lp, wet);

        buf[i] = dry * (1.0f - mix) + wet * mix;
    }
}

static void drip_verb_free(fx_pedal_instance_t *p) {
    drip_verb_state_t *s = (drip_verb_state_t *)p->state;
    if (s) {
        for (int i = 0; i < DRIP_NUM_ALLPASS; i++) free(s->ap_buf[i]);
        for (int i = 0; i < DRIP_NUM_COMBS;   i++) free(s->comb_buf[i]);
    }
}

/* ══════════════════════════════════════════════════════════════════
 * CARBON DELAY — analog BBD-style delay
 * LP filter in feedback path (each repeat gets darker), optional LFO
 * Params: [0] time, [1] feedback, [2] tone, [3] mod, [4] mix
 * ══════════════════════════════════════════════════════════════════ */

static void carbon_delay_init(fx_pedal_instance_t *p, float sr) {
    carbon_delay_state_t *s = (carbon_delay_state_t *)calloc(1, sizeof(*s));
    if (!s) return;

    s->buf_len  = (int)(sr * CARBON_BUF_SECONDS) + 1;
    s->buffer   = (float *)calloc((size_t)s->buf_len, sizeof(float));
    s->write_pos = 0;
    s->lfo_phase = 0.0f;

    fx_biquad_lowpass(&s->feedback_lp, 3000.0f, 0.707f, sr);

    p->state = s;
    p->params[0] = 0.4f; /* time */
    p->params[1] = 0.4f; /* feedback */
    p->params[2] = 0.6f; /* tone */
    p->params[3] = 0.1f; /* mod */
    p->params[4] = 0.35f; /* mix */
}

static void carbon_delay_process(fx_pedal_instance_t *p, float *buf, int n, float sr) {
    carbon_delay_state_t *s = (carbon_delay_state_t *)p->state;
    if (!s || !s->buffer) return;

    /* Time: 30ms-1000ms */
    float delay_ms = 30.0f + p->params[0] * 970.0f;
    int   base_delay = (int)(delay_ms * sr / 1000.0f);
    if (base_delay >= s->buf_len) base_delay = s->buf_len - 1;
    if (base_delay < 1) base_delay = 1;

    float feedback = p->params[1] * 0.85f;
    /* Tone: map 0-1 to 500Hz-8kHz LP cutoff */
    float tone_hz  = 500.0f + p->params[2] * 7500.0f;
    float mod_depth = p->params[3]; /* 0-1 → 0 to ~3ms of mod */
    float mix       = p->params[4];

    fx_biquad_lowpass(&s->feedback_lp, tone_hz, 0.707f, sr);

    /* LFO rate: ~0.5Hz fixed for classic wobble */
    float lfo_inc = (float)(2.0 * M_PI * 0.5 / sr);
    float mod_samples = mod_depth * (sr * 0.003f); /* max 3ms */

    for (int i = 0; i < n; i++) {
        float dry = buf[i];

        /* LFO modulates read position */
        s->lfo_phase += lfo_inc;
        if (s->lfo_phase > (float)(2.0 * M_PI)) s->lfo_phase -= (float)(2.0 * M_PI);
        float lfo = sinf(s->lfo_phase);

        int delay_samples = base_delay + (int)(lfo * mod_samples);
        if (delay_samples < 1) delay_samples = 1;
        if (delay_samples >= s->buf_len) delay_samples = s->buf_len - 1;

        int read_pos = s->write_pos - delay_samples;
        if (read_pos < 0) read_pos += s->buf_len;
        float delayed = s->buffer[read_pos];

        /* LP filter in feedback (BBD-style repeat darkening) */
        float fb_signal = fx_biquad_process(&s->feedback_lp, delayed * feedback);

        s->buffer[s->write_pos] = dry + fb_signal;
        s->write_pos = (s->write_pos + 1) % s->buf_len;

        buf[i] = dry * (1.0f - mix) + delayed * mix;
    }
}

static void carbon_delay_free(fx_pedal_instance_t *p) {
    carbon_delay_state_t *s = (carbon_delay_state_t *)p->state;
    if (s) free(s->buffer);
}

/* ══════════════════════════════════════════════════════════════════
 * TAPE MACHINE — tape echo with wow/flutter
 * Wow/flutter LFO modulates delay time, tanh saturation in feedback
 * Params: [0] time, [1] feedback, [2] wow, [3] flutter, [4] mix
 * ══════════════════════════════════════════════════════════════════ */

static void tape_machine_init(fx_pedal_instance_t *p, float sr) {
    tape_machine_state_t *s = (tape_machine_state_t *)calloc(1, sizeof(*s));
    if (!s) return;

    s->buf_len    = (int)(sr * TAPE_BUF_SECONDS) + 1;
    s->buffer     = (float *)calloc((size_t)s->buf_len, sizeof(float));
    s->write_pos  = 0;
    s->wow_phase  = 0.0f;
    s->flutter_phase = 0.0f;

    p->state = s;
    p->params[0] = 0.4f;  /* time */
    p->params[1] = 0.35f; /* feedback */
    p->params[2] = 0.3f;  /* wow */
    p->params[3] = 0.2f;  /* flutter */
    p->params[4] = 0.35f; /* mix */
}

static void tape_machine_process(fx_pedal_instance_t *p, float *buf, int n, float sr) {
    tape_machine_state_t *s = (tape_machine_state_t *)p->state;
    if (!s || !s->buffer) return;

    /* Time: 50ms-1200ms */
    float delay_ms = 50.0f + p->params[0] * 1150.0f;
    int   base_delay = (int)(delay_ms * sr / 1000.0f);
    if (base_delay >= s->buf_len) base_delay = s->buf_len - 1;
    if (base_delay < 1) base_delay = 1;

    float feedback      = p->params[1] * 0.85f;
    float wow_depth     = p->params[2]; /* slow wobble */
    float flutter_depth = p->params[3]; /* fast wobble */
    float mix           = p->params[4];

    /* Wow: ~0.5Hz, Flutter: ~7Hz */
    float wow_inc     = (float)(2.0 * M_PI * 0.5  / sr);
    float flutter_inc = (float)(2.0 * M_PI * 7.0  / sr);

    /* Max modulation depths in samples */
    float wow_samps     = wow_depth     * (sr * 0.008f); /* up to 8ms */
    float flutter_samps = flutter_depth * (sr * 0.0015f); /* up to 1.5ms */

    for (int i = 0; i < n; i++) {
        float dry = buf[i];

        s->wow_phase     += wow_inc;
        s->flutter_phase += flutter_inc;
        if (s->wow_phase     > (float)(2.0 * M_PI)) s->wow_phase     -= (float)(2.0 * M_PI);
        if (s->flutter_phase > (float)(2.0 * M_PI)) s->flutter_phase -= (float)(2.0 * M_PI);

        float mod = sinf(s->wow_phase) * wow_samps
                  + sinf(s->flutter_phase) * flutter_samps;

        int delay_samples = base_delay + (int)mod;
        if (delay_samples < 1) delay_samples = 1;
        if (delay_samples >= s->buf_len) delay_samples = s->buf_len - 1;

        int read_pos = s->write_pos - delay_samples;
        if (read_pos < 0) read_pos += s->buf_len;
        float delayed = s->buffer[read_pos];

        /* Tape saturation in feedback path */
        float fb = tanhf(delayed * feedback * 1.5f) * 0.67f;

        s->buffer[s->write_pos] = dry + fb;
        s->write_pos = (s->write_pos + 1) % s->buf_len;

        buf[i] = dry * (1.0f - mix) + delayed * mix;
    }
}

static void tape_machine_free(fx_pedal_instance_t *p) {
    tape_machine_state_t *s = (tape_machine_state_t *)p->state;
    if (s) free(s->buffer);
}

/* ══════════════════════════════════════════════════════════════════
 * HOWL WAH — expression wah
 * Peak/bandpass filter swept by frequency param
 * Params: [0] frequency (0-1 → 400Hz-2500Hz), [1] q (1-10), [2] volume
 * ══════════════════════════════════════════════════════════════════ */

static void howl_wah_init(fx_pedal_instance_t *p, float sr) {
    howl_wah_state_t *s = (howl_wah_state_t *)calloc(1, sizeof(*s));
    if (!s) return;

    fx_biquad_peak(&s->peak_filter, 1000.0f, 18.0f, 4.0f, sr);

    p->state = s;
    p->params[0] = 0.5f; /* frequency */
    p->params[1] = 0.5f; /* q */
    p->params[2] = 0.8f; /* volume */
}

static void howl_wah_process(fx_pedal_instance_t *p, float *buf, int n, float sr) {
    howl_wah_state_t *s = (howl_wah_state_t *)p->state;
    if (!s) return;

    /* Map 0-1 to 400Hz-2500Hz */
    float freq   = 400.0f + p->params[0] * 2100.0f;
    float q      = 1.0f   + p->params[1] * 9.0f;    /* 1-10 */
    float volume = p->params[2];

    fx_biquad_peak(&s->peak_filter, freq, 18.0f, q, sr);

    for (int i = 0; i < n; i++) {
        float x = fx_biquad_process(&s->peak_filter, buf[i]);
        buf[i] = x * volume;
    }
}

/* ══════════════════════════════════════════════════════════════════
 * QUACK FILTER — auto-wah / envelope filter
 * Envelope follower drives bandpass cutoff
 * Params: [0] sensitivity, [1] decay, [2] q, [3] mix
 * ══════════════════════════════════════════════════════════════════ */

static void quack_filter_init(fx_pedal_instance_t *p, float sr) {
    quack_filter_state_t *s = (quack_filter_state_t *)calloc(1, sizeof(*s));
    if (!s) return;

    s->envelope = 0.0f;
    fx_biquad_lowpass(&s->bp_filter, 500.0f, 2.0f, sr);

    p->state = s;
    p->params[0] = 0.6f; /* sensitivity */
    p->params[1] = 0.4f; /* decay */
    p->params[2] = 0.5f; /* q */
    p->params[3] = 0.7f; /* mix */
}

static void quack_filter_process(fx_pedal_instance_t *p, float *buf, int n, float sr) {
    quack_filter_state_t *s = (quack_filter_state_t *)p->state;
    if (!s) return;

    float sensitivity = p->params[0] * 8.0f + 0.5f;  /* signal gain into env */
    /* Decay: 0-1 → 20ms-500ms */
    float decay_ms    = 20.0f + p->params[1] * 480.0f;
    float rel_coeff   = expf(-1.0f / (decay_ms * 0.001f * sr));
    float att_coeff   = expf(-1.0f / (0.001f * sr)); /* 1ms attack */
    float q           = 1.0f + p->params[2] * 9.0f;  /* 1-10 */
    float mix         = p->params[3];

    for (int i = 0; i < n; i++) {
        float dry    = buf[i];
        float abs_in = fabsf(dry) * sensitivity;

        /* Envelope follower */
        if (abs_in > s->envelope)
            s->envelope = att_coeff * s->envelope + (1.0f - att_coeff) * abs_in;
        else
            s->envelope = rel_coeff * s->envelope;

        /* Map envelope (0-1) to cutoff frequency: 200Hz-3000Hz */
        float cutoff = 200.0f + s->envelope * 2800.0f;
        if (cutoff > 3000.0f) cutoff = 3000.0f;

        fx_biquad_lowpass(&s->bp_filter, cutoff, q, sr);
        float wet = fx_biquad_process(&s->bp_filter, dry);

        buf[i] = dry * (1.0f - mix) + wet * mix;
    }
}

/* ══════════════════════════════════════════════════════════════════
 * LIQUID CHORUS — BBD-style chorus
 * Modulated delay line (~7ms base) with sine LFO
 * Params: [0] rate (0.1-5Hz), [1] depth, [2] mix
 * ══════════════════════════════════════════════════════════════════ */

static void liquid_chorus_init(fx_pedal_instance_t *p, float sr) {
    (void)sr;
    liquid_chorus_state_t *s = (liquid_chorus_state_t *)calloc(1, sizeof(*s));
    if (!s) return;

    memset(s->buffer, 0, sizeof(s->buffer));
    s->write_pos = 0;
    s->lfo_phase = 0.0f;

    p->state = s;
    p->params[0] = 0.2f; /* rate */
    p->params[1] = 0.5f; /* depth */
    p->params[2] = 0.5f; /* mix */
}

static void liquid_chorus_process(fx_pedal_instance_t *p, float *buf, int n, float sr) {
    liquid_chorus_state_t *s = (liquid_chorus_state_t *)p->state;
    if (!s) return;

    /* Rate: 0.1-5Hz */
    float rate  = 0.1f + p->params[0] * 4.9f;
    float depth = p->params[1];
    float mix   = p->params[2];

    float lfo_inc = (float)(2.0 * M_PI * rate / sr);

    /* Base delay ~7ms, depth extends up to ~7ms */
    float base_delay  = sr * 0.007f;
    float depth_samps = depth * sr * 0.007f;

    for (int i = 0; i < n; i++) {
        float dry = buf[i];

        s->lfo_phase += lfo_inc;
        if (s->lfo_phase > (float)(2.0 * M_PI)) s->lfo_phase -= (float)(2.0 * M_PI);
        float lfo = sinf(s->lfo_phase);

        float delay_f = base_delay + lfo * depth_samps;
        int   delay_i = (int)delay_f;
        float frac    = delay_f - (float)delay_i;

        /* Linear interpolation */
        int r0 = s->write_pos - delay_i;
        if (r0 < 0) r0 += CHORUS_BUF_LEN;
        int r1 = r0 - 1;
        if (r1 < 0) r1 += CHORUS_BUF_LEN;

        float wet = s->buffer[r0] * (1.0f - frac) + s->buffer[r1] * frac;

        s->buffer[s->write_pos] = dry;
        s->write_pos = (s->write_pos + 1) % CHORUS_BUF_LEN;

        buf[i] = dry * (1.0f - mix) + wet * mix;
    }
}

/* ══════════════════════════════════════════════════════════════════
 * PHASE SWEEP — allpass phaser
 * 6 cascaded allpass filters with LFO-swept frequency
 * Params: [0] rate, [1] depth, [2] feedback, [3] mix
 * ══════════════════════════════════════════════════════════════════ */

static void phase_sweep_init(fx_pedal_instance_t *p, float sr) {
    phase_sweep_state_t *s = (phase_sweep_state_t *)calloc(1, sizeof(*s));
    if (!s) return;

    for (int i = 0; i < PHASER_STAGES; i++) {
        /* Initialize as allpass (using peak with 0dB gain as approximation) */
        fx_biquad_peak(&s->ap[i], 1000.0f, 0.0f, 0.707f, sr);
    }
    s->lfo_phase  = 0.0f;
    s->feedback_z1 = 0.0f;

    p->state = s;
    p->params[0] = 0.2f;  /* rate */
    p->params[1] = 0.7f;  /* depth */
    p->params[2] = 0.5f;  /* feedback (maps to -0.9 to 0.9) */
    p->params[3] = 0.5f;  /* mix */
}

/* First-order allpass: y[n] = -g*x[n] + x[n-1] + g*y[n-1] */
static inline float allpass1_process(float in, float g, float *z1) {
    float out = -g * in + *z1;
    *z1 = in + g * out;
    return out;
}

static void phase_sweep_process(fx_pedal_instance_t *p, float *buf, int n, float sr) {
    phase_sweep_state_t *s = (phase_sweep_state_t *)p->state;
    if (!s) return;

    float rate     = 0.1f + p->params[0] * 4.9f; /* 0.1-5Hz */
    float depth    = p->params[1];
    float feedback = (p->params[2] - 0.5f) * 1.8f; /* -0.9 to +0.9 */
    float mix      = p->params[3];

    float lfo_inc = (float)(2.0 * M_PI * rate / sr);

    /* Sweep center frequency: 200Hz to 2000Hz */
    float freq_min = 200.0f;
    float freq_max = 2000.0f;

    /* Reuse biquad z1/z2 as first-order allpass states */
    for (int i = 0; i < n; i++) {
        float dry = buf[i];

        s->lfo_phase += lfo_inc;
        if (s->lfo_phase > (float)(2.0 * M_PI)) s->lfo_phase -= (float)(2.0 * M_PI);
        float lfo = 0.5f + 0.5f * sinf(s->lfo_phase); /* 0-1 */

        float freq = freq_min + lfo * depth * (freq_max - freq_min);
        /* First-order allpass coefficient from frequency */
        float w = (float)(2.0 * M_PI * freq / sr);
        float tan_w = tanf(w * 0.5f);
        float g = (tan_w - 1.0f) / (tan_w + 1.0f);

        /* Input includes feedback from previous output */
        float x = dry + s->feedback_z1 * feedback;

        /* 6 cascaded first-order allpass stages (using biquad z1 only) */
        float out = x;
        for (int stage = 0; stage < PHASER_STAGES; stage++) {
            float *z = &s->ap[stage].z1;
            out = allpass1_process(out, g, z);
        }

        s->feedback_z1 = out;

        buf[i] = dry * (1.0f - mix) + out * mix;
    }
}

/* ══════════════════════════════════════════════════════════════════
 * PULSE TREM — tremolo
 * Amplitude modulation via LFO (sine/square/triangle)
 * Params: [0] rate (1-15Hz), [1] depth, [2] wave (0-0.33=sine, 0.33-0.66=square, 0.66-1=tri)
 * ══════════════════════════════════════════════════════════════════ */

static void pulse_trem_init(fx_pedal_instance_t *p, float sr) {
    (void)sr;
    pulse_trem_state_t *s = (pulse_trem_state_t *)calloc(1, sizeof(*s));
    if (!s) return;

    s->lfo_phase = 0.0f;

    p->state = s;
    p->params[0] = 0.2f;  /* rate */
    p->params[1] = 0.7f;  /* depth */
    p->params[2] = 0.0f;  /* wave (sine) */
}

static void pulse_trem_process(fx_pedal_instance_t *p, float *buf, int n, float sr) {
    pulse_trem_state_t *s = (pulse_trem_state_t *)p->state;
    if (!s) return;

    float rate  = 1.0f + p->params[0] * 14.0f; /* 1-15Hz */
    float depth = p->params[1];
    float wave  = p->params[2];

    float lfo_inc = (float)(2.0 * M_PI * rate / sr);

    for (int i = 0; i < n; i++) {
        s->lfo_phase += lfo_inc;
        if (s->lfo_phase > (float)(2.0 * M_PI)) s->lfo_phase -= (float)(2.0 * M_PI);

        float lfo;
        if (wave < 0.33f) {
            /* Sine */
            lfo = sinf(s->lfo_phase);
        } else if (wave < 0.66f) {
            /* Square */
            lfo = s->lfo_phase < (float)M_PI ? 1.0f : -1.0f;
        } else {
            /* Triangle */
            float t = s->lfo_phase / (float)(2.0 * M_PI);
            lfo = (t < 0.5f) ? (4.0f * t - 1.0f) : (3.0f - 4.0f * t);
        }

        /* LFO ranges -1 to 1; convert to gain: 1.0 down to (1-depth) */
        float gain = 1.0f - depth * (0.5f + 0.5f * lfo);
        buf[i] *= gain;
    }
}

/* ══════════════════════════════════════════════════════════════════
 * TONE SCULPTOR — 7-band graphic EQ
 * Biquad peak filters at fixed frequencies with variable gain
 * Q = 1.4 (standard graphic EQ bandwidth)
 * Params: [0-6] band gains (100Hz-6.4kHz), [7] output level
 *   0.0 = -12dB, 0.5 = 0dB (flat), 1.0 = +12dB
 * ══════════════════════════════════════════════════════════════════ */

static void tone_sculptor_init(fx_pedal_instance_t *p, float sr) {
    tone_sculptor_state_t *s = (tone_sculptor_state_t *)calloc(1, sizeof(*s));
    if (!s) return;

    s->cached_sr = sr;
    for (int b = 0; b < TONE_SCULPTOR_BANDS; b++) {
        /* Init all bands flat (0dB) */
        fx_biquad_peak(&s->bands[b], tone_sculptor_freqs[b], 0.0f, 1.4f, sr);
        s->cached_gains[b] = 0.5f;
    }
    s->cached_output = 0.5f;

    p->state = s;
    /* Params [0-6]: band gains (default 0.5 = flat) */
    for (int b = 0; b < TONE_SCULPTOR_BANDS; b++) p->params[b] = 0.5f;
    /* Param [7]: output level (default 0.5 = unity) */
    p->params[7] = 0.5f;
}

static void tone_sculptor_process(fx_pedal_instance_t *p, float *buf, int n, float sr) {
    tone_sculptor_state_t *s = (tone_sculptor_state_t *)p->state;
    if (!s) return;

    /* Recalculate band coefficients only when params change */
    int sr_changed = (s->cached_sr != sr);
    for (int b = 0; b < TONE_SCULPTOR_BANDS; b++) {
        float gain_param = p->params[b];
        if (sr_changed || gain_param != s->cached_gains[b]) {
            /* Map 0-1 to -12dB to +12dB */
            float gain_db = (gain_param - 0.5f) * 24.0f;
            fx_biquad_peak(&s->bands[b], tone_sculptor_freqs[b], gain_db, 1.4f, sr);
            s->cached_gains[b] = gain_param;
        }
    }
    s->cached_sr = sr;
    s->cached_output = p->params[7];

    /* Output level: 0 = silence, 0.5 = unity gain, 1.0 = +6dB */
    float output = s->cached_output * 2.0f;

    for (int i = 0; i < n; i++) {
        float x = buf[i];
        for (int b = 0; b < TONE_SCULPTOR_BANDS; b++) {
            x = fx_biquad_process(&s->bands[b], x);
        }
        buf[i] = x * output;
    }
}


/* ══════════════════════════════════════════════════════════════════
 * MAMMOTH FUZZ — Big Muff-style 4-stage fuzz
 * 4 cascaded tanh soft-clip stages, scooped-mid tone control
 * Params: [0] sustain (gain 1-50x), [1] tone (scoop 0-1), [2] volume
 * ══════════════════════════════════════════════════════════════════ */

static void mammoth_fuzz_init(fx_pedal_instance_t *p, float sr) {
    mammoth_fuzz_state_t *s = (mammoth_fuzz_state_t *)calloc(1, sizeof(*s));
    if (!s) return;

    /* Scooped-mid tone control: parallel LP + HP */
    fx_biquad_lowpass(&s->tone_lp, 500.0f, 0.707f, sr);
    fx_biquad_highpass(&s->tone_hp, 1500.0f, 0.707f, sr);

    p->state = s;
    p->params[0] = 0.5f;  /* sustain */
    p->params[1] = 0.5f;  /* tone */
    p->params[2] = 0.7f;  /* volume */
}

static void mammoth_fuzz_process(fx_pedal_instance_t *p, float *buf, int n, float sr) {
    mammoth_fuzz_state_t *s = (mammoth_fuzz_state_t *)p->state;
    if (!s) return;

    float sustain = 1.0f + p->params[0] * 49.0f;   /* 1x-50x */
    float scoop   = p->params[1];                    /* 0=flat, 1=full scoop */
    float volume  = p->params[2];

    /* Update tone biquads based on scoop amount */
    float lp_freq = 200.0f + (1.0f - scoop) * 800.0f;  /* LP: 200-1000Hz */
    float hp_freq = 1000.0f + scoop * 2000.0f;           /* HP: 1000-3000Hz */
    fx_biquad_lowpass(&s->tone_lp, lp_freq, 0.707f, sr);
    fx_biquad_highpass(&s->tone_hp, hp_freq, 0.707f, sr);

    float dc_R = 1.0f - (2.0f * (float)M_PI * 10.0f / sr);

    for (int i = 0; i < n; i++) {
        float x = buf[i] * sustain;

        /* 4 cascaded tanh soft-clip stages, each ~3x gain */
        for (int stage = 0; stage < 4; stage++) {
            x *= 3.0f;
            x = tanhf(x);
            x = dc_block(x, &s->dc_z1[stage], dc_R);
        }

        /* Scooped-mid tone control: parallel LP + HP */
        float lp_out = fx_biquad_process(&s->tone_lp, x);
        float hp_out = fx_biquad_process(&s->tone_hp, x);
        /* scoop=0: flat signal, scoop=1: maximum mid-cut */
        float flat    = x * 0.5f;
        float scooped = (lp_out + hp_out) * 0.5f;
        x = flat * (1.0f - scoop) + scooped * scoop;

        buf[i] = x * volume;
    }
}

/* ══════════════════════════════════════════════════════════════════
 * ROUND FUZZ — germanium Fuzz Face-style
 * Asymmetric soft clipping (positive clips softer), cleans up with input
 * Params: [0] fuzz (gain 1-30x), [1] volume
 * ══════════════════════════════════════════════════════════════════ */

static void round_fuzz_init(fx_pedal_instance_t *p, float sr) {
    (void)sr;
    round_fuzz_state_t *s = (round_fuzz_state_t *)calloc(1, sizeof(*s));
    if (!s) return;

    s->dc_z1 = 0.0f;
    p->state = s;
    p->params[0] = 0.5f;  /* fuzz */
    p->params[1] = 0.7f;  /* volume */
}

static void round_fuzz_process(fx_pedal_instance_t *p, float *buf, int n, float sr) {
    round_fuzz_state_t *s = (round_fuzz_state_t *)p->state;
    if (!s) return;

    float fuzz   = 1.0f + p->params[0] * 29.0f;  /* 1x-30x */
    float volume = p->params[1];

    float dc_R = 1.0f - (2.0f * (float)M_PI * 10.0f / sr);

    for (int i = 0; i < n; i++) {
        float x = buf[i] * fuzz;

        /* Asymmetric clipping: positive softer (germanium transistor) */
        if (x > 0.0f)
            x = tanhf(x * 0.7f);
        else
            x = tanhf(x);

        x = dc_block(x, &s->dc_z1, dc_R);

        buf[i] = x * volume;
    }
}

/* ══════════════════════════════════════════════════════════════════
 * CHAOS FUZZ — gated/sputtery fuzz
 * Hard clip + noise gate + oscillation feedback
 * Params: [0] volume, [1] gate (threshold 0-1), [2] drive (gain), [3] stab (0-1)
 * ══════════════════════════════════════════════════════════════════ */

static void chaos_fuzz_init(fx_pedal_instance_t *p, float sr) {
    (void)sr;
    chaos_fuzz_state_t *s = (chaos_fuzz_state_t *)calloc(1, sizeof(*s));
    if (!s) return;

    s->feedback_z1 = 0.0f;
    p->state = s;
    p->params[0] = 0.7f;  /* volume */
    p->params[1] = 0.3f;  /* gate */
    p->params[2] = 0.5f;  /* drive */
    p->params[3] = 0.8f;  /* stab (high = stable) */
}

static void chaos_fuzz_process(fx_pedal_instance_t *p, float *buf, int n, float sr) {
    chaos_fuzz_state_t *s = (chaos_fuzz_state_t *)p->state;
    if (!s) return;
    (void)sr;

    float volume   = p->params[0];
    float gate_thr = p->params[1] * 0.03f;           /* threshold 0-0.03 — very subtle gate for sputtery character */
    float drive    = 1.0f + p->params[2] * 29.0f;  /* 1x-30x */
    float stab     = p->params[3];
    /* Low stab = strong feedback (oscillation) */
    float fb_amount = (1.0f - stab) * 0.6f;

    for (int i = 0; i < n; i++) {
        /* Inject feedback from previous output (oscillation) */
        float x = buf[i] + s->feedback_z1 * fb_amount;

        /* Drive */
        x *= drive;

        /* Hard clip */
        if      (x >  1.0f) x =  1.0f;
        else if (x < -1.0f) x = -1.0f;

        /* Gate: below threshold → silence (sputtery decay) */
        if (fabsf(x) < gate_thr) x = 0.0f;

        s->feedback_z1 = x;

        buf[i] = x * volume;
    }
}

/* ══════════════════════════════════════════════════════════════════
 * GRIT CRUSH — bitcrusher
 * Reduce bit depth (quantize) + reduce sample rate (sample-and-hold)
 * Params: [0] bits (1-16), [1] downsample (1-32 factor), [2] mix
 * ══════════════════════════════════════════════════════════════════ */

static void grit_crush_init(fx_pedal_instance_t *p, float sr) {
    (void)sr;
    grit_crush_state_t *s = (grit_crush_state_t *)calloc(1, sizeof(*s));
    if (!s) return;

    s->hold_sample  = 0.0f;
    s->hold_counter = 0;
    p->state = s;
    p->params[0] = 0.5f;  /* bits */
    p->params[1] = 0.2f;  /* downsample */
    p->params[2] = 1.0f;  /* mix */
}

static void grit_crush_process(fx_pedal_instance_t *p, float *buf, int n, float sr) {
    grit_crush_state_t *s = (grit_crush_state_t *)p->state;
    if (!s) return;
    (void)sr;

    /* Map params */
    float bits_f   = 1.0f + p->params[0] * 15.0f;    /* 1-16 bits */
    int   ds_steps = 1 + (int)(p->params[1] * 31.0f); /* 1-32x downsample */
    float mix      = p->params[2];

    /* Quantization step size (2^bits levels over -1..1) */
    float levels = powf(2.0f, bits_f);
    float step   = 2.0f / levels;

    for (int i = 0; i < n; i++) {
        float dry = buf[i];

        /* Sample-and-hold (rate reduction) */
        if (s->hold_counter <= 0) {
            s->hold_sample  = dry;
            s->hold_counter = ds_steps;
        }
        s->hold_counter--;
        float x = s->hold_sample;

        /* Bit depth reduction */
        if (step > 0.0f)
            x = floorf(x / step + 0.5f) * step;

        buf[i] = dry * (1.0f - mix) + x * mix;
    }
}

/* ══════════════════════════════════════════════════════════════════
 * RING TONE — ring modulator
 * Multiply signal by sine carrier wave
 * Params: [0] freq (20-5000Hz), [1] mix
 * ══════════════════════════════════════════════════════════════════ */

static void ring_tone_init(fx_pedal_instance_t *p, float sr) {
    (void)sr;
    ring_tone_state_t *s = (ring_tone_state_t *)calloc(1, sizeof(*s));
    if (!s) return;

    s->phase = 0.0f;
    p->state = s;
    p->params[0] = 0.2f;  /* freq */
    p->params[1] = 0.7f;  /* mix */
}

static void ring_tone_process(fx_pedal_instance_t *p, float *buf, int n, float sr) {
    ring_tone_state_t *s = (ring_tone_state_t *)p->state;
    if (!s) return;

    /* Map 0-1 → 20Hz-5000Hz */
    float freq = 20.0f + p->params[0] * 4980.0f;
    float mix  = p->params[1];

    float phase_inc = (float)(2.0 * M_PI * freq / sr);

    for (int i = 0; i < n; i++) {
        float dry = buf[i];

        float carrier = sinf(s->phase);
        s->phase += phase_inc;
        if (s->phase > (float)(2.0 * M_PI)) s->phase -= (float)(2.0 * M_PI);

        float wet = dry * carrier;
        buf[i] = dry * (1.0f - mix) + wet * mix;
    }
}

/* ══════════════════════════════════════════════════════════════════
 * WARM TAPE — tape saturation
 * Gentle tanh saturation + warmth LP filter (rolls off highs)
 * Params: [0] drive (gain 1-10x), [1] warmth (LP 20kHz→2kHz), [2] mix
 * ══════════════════════════════════════════════════════════════════ */

static void warm_tape_init(fx_pedal_instance_t *p, float sr) {
    (void)sr;
    warm_tape_state_t *s = (warm_tape_state_t *)calloc(1, sizeof(*s));
    if (!s) return;

    s->warmth_z1 = 0.0f;
    p->state = s;
    p->params[0] = 0.4f;  /* drive */
    p->params[1] = 0.4f;  /* warmth */
    p->params[2] = 0.8f;  /* mix */
}

static void warm_tape_process(fx_pedal_instance_t *p, float *buf, int n, float sr) {
    warm_tape_state_t *s = (warm_tape_state_t *)p->state;
    if (!s) return;

    float drive  = 1.0f + p->params[0] * 9.0f;        /* 1x-10x */
    /* Warmth: 0=20kHz (bright), 1=2kHz (dark) */
    float cutoff = 20000.0f - p->params[1] * 18000.0f; /* 20kHz-2kHz */
    float mix    = p->params[2];

    /* One-pole LP coefficient */
    float rc = 1.0f / (2.0f * (float)M_PI * cutoff / sr + 1.0f);

    for (int i = 0; i < n; i++) {
        float dry = buf[i];

        /* Tape saturation: gentle tanh with gain normalization */
        float x = tanhf(dry * drive) / drive;

        /* One-pole LP warmth filter */
        s->warmth_z1 = s->warmth_z1 * rc + x * (1.0f - rc);
        x = s->warmth_z1;

        buf[i] = dry * (1.0f - mix) + x * mix;
    }
}

/* ══════════════════════════════════════════════════════════════════
 * DRIFT VIBRATO — true pitch vibrato (100% wet, no dry signal)
 * Modulated delay line: LFO varies delay time → pitch wobble
 * Params: [0] rate (0.5-8Hz), [1] depth (0-1, up to ~5ms mod)
 * ══════════════════════════════════════════════════════════════════ */

static void drift_vibrato_init(fx_pedal_instance_t *p, float sr) {
    (void)sr;
    drift_vibrato_state_t *s = (drift_vibrato_state_t *)calloc(1, sizeof(*s));
    if (!s) return;

    memset(s->buffer, 0, sizeof(s->buffer));
    s->write_pos = 0;
    s->lfo_phase = 0.0f;

    p->state = s;
    p->params[0] = 0.3f;  /* rate */
    p->params[1] = 0.5f;  /* depth */
}

static void drift_vibrato_process(fx_pedal_instance_t *p, float *buf, int n, float sr) {
    drift_vibrato_state_t *s = (drift_vibrato_state_t *)p->state;
    if (!s) return;

    /* Rate: 0.5-8Hz */
    float rate  = 0.5f + p->params[0] * 7.5f;
    float depth = p->params[1];

    float lfo_inc = (float)(2.0 * M_PI * rate / sr);

    /* Base delay: ~5ms, depth modulates up to another ~5ms */
    float base_delay  = sr * 0.005f;
    float depth_samps = depth * sr * 0.005f;

    for (int i = 0; i < n; i++) {
        float dry = buf[i];

        s->lfo_phase += lfo_inc;
        if (s->lfo_phase > (float)(2.0 * M_PI)) s->lfo_phase -= (float)(2.0 * M_PI);
        float lfo = sinf(s->lfo_phase);

        /* Modulated delay (always positive: base ± depth, clamped) */
        float delay_f = base_delay + lfo * depth_samps;
        if (delay_f < 1.0f) delay_f = 1.0f;

        int   delay_i = (int)delay_f;
        float frac    = delay_f - (float)delay_i;

        /* Linear interpolation from circular buffer */
        int r0 = s->write_pos - delay_i;
        if (r0 < 0) r0 += DRIFT_BUF_LEN;
        int r1 = r0 - 1;
        if (r1 < 0) r1 += DRIFT_BUF_LEN;

        float wet = s->buffer[r0] * (1.0f - frac) + s->buffer[r1] * frac;

        s->buffer[s->write_pos] = dry;
        s->write_pos = (s->write_pos + 1) % DRIFT_BUF_LEN;

        /* 100% wet — pure pitch vibrato, NO dry signal */
        buf[i] = wet;
    }
}

/* ══════════════════════════════════════════════════════════════════
 * JET FLANGER — through-zero flanging
 * Short modulated delay (0.1-7ms) with feedback (can go negative)
 * Params: [0] rate (0.1-2Hz), [1] depth, [2] feedback (-0.9 to 0.9),
 *         [3] mix
 * ══════════════════════════════════════════════════════════════════ */

static void jet_flanger_init(fx_pedal_instance_t *p, float sr) {
    (void)sr;
    jet_flanger_state_t *s = (jet_flanger_state_t *)calloc(1, sizeof(*s));
    if (!s) return;

    memset(s->buffer, 0, sizeof(s->buffer));
    s->write_pos = 0;
    s->lfo_phase = 0.0f;

    p->state = s;
    p->params[0] = 0.2f;   /* rate */
    p->params[1] = 0.7f;   /* depth */
    p->params[2] = 0.7f;   /* feedback (0.7 → negative: through-zero) */
    p->params[3] = 0.5f;   /* mix */
}

static void jet_flanger_process(fx_pedal_instance_t *p, float *buf, int n, float sr) {
    jet_flanger_state_t *s = (jet_flanger_state_t *)p->state;
    if (!s) return;

    /* Rate: 0.1-2Hz */
    float rate     = 0.1f + p->params[0] * 1.9f;
    float depth    = p->params[1];
    /* Feedback: param 0-1 → -0.9 to +0.9 (0.5 = 0) */
    float feedback = (p->params[2] - 0.5f) * 1.8f;
    float mix      = p->params[3];

    float lfo_inc = (float)(2.0 * M_PI * rate / sr);

    /* Delay sweeps 0.1ms to 7ms */
    float delay_min_samps = sr * 0.0001f;
    float delay_max_samps = sr * 0.007f;
    if (delay_max_samps >= (float)(FLANGER_BUF_LEN - 2))
        delay_max_samps = (float)(FLANGER_BUF_LEN - 2);

    float delay_range = (delay_max_samps - delay_min_samps) * depth;

    for (int i = 0; i < n; i++) {
        float dry = buf[i];

        s->lfo_phase += lfo_inc;
        if (s->lfo_phase > (float)(2.0 * M_PI)) s->lfo_phase -= (float)(2.0 * M_PI);
        /* Use sine LFO: 0-1 sweep for classic jet effect */
        float lfo = 0.5f + 0.5f * sinf(s->lfo_phase);

        float delay_f = delay_min_samps + lfo * delay_range;
        if (delay_f < 0.1f) delay_f = 0.1f;

        int   delay_i = (int)delay_f;
        float frac    = delay_f - (float)delay_i;

        int r0 = s->write_pos - delay_i;
        if (r0 < 0) r0 += FLANGER_BUF_LEN;
        int r1 = r0 - 1;
        if (r1 < 0) r1 += FLANGER_BUF_LEN;

        float delayed = s->buffer[r0] * (1.0f - frac) + s->buffer[r1] * frac;

        /* Write input + feedback (negative feedback → through-zero jet sweep) */
        s->buffer[s->write_pos] = dry + delayed * feedback;
        s->write_pos = (s->write_pos + 1) % FLANGER_BUF_LEN;

        buf[i] = dry * (1.0f - mix) + delayed * mix;
    }
}

/* ══════════════════════════════════════════════════════════════════
 * PLATE VERB — dense plate reverb
 * 4 allpass diffusers in series → 2 parallel feedback delay lines
 * with lowpass damping in each feedback path
 * Params: [0] decay (0.5-0.98), [1] damping (LP in feedback), [2] mix
 * ══════════════════════════════════════════════════════════════════ */

/* Allpass and feedback delay lengths at 44.1kHz */
static const int plate_ap_lens_44k[PLATE_NUM_ALLPASS] = { 210, 322, 441, 583 };
/* Longer delays than spring/hall for plate character */
static const int plate_dl_lens_44k[PLATE_NUM_DELAYS]  = { 2053, 2389 };

static void plate_verb_init(fx_pedal_instance_t *p, float sr) {
    plate_verb_state_t *s = (plate_verb_state_t *)calloc(1, sizeof(*s));
    if (!s) return;

    float scale = sr / 44100.0f;

    for (int i = 0; i < PLATE_NUM_ALLPASS; i++) {
        s->ap_len[i] = (int)((float)plate_ap_lens_44k[i] * scale) + 1;
        s->ap_buf[i] = (float *)calloc((size_t)s->ap_len[i], sizeof(float));
        s->ap_pos[i] = 0;
    }
    for (int i = 0; i < PLATE_NUM_DELAYS; i++) {
        s->dl_len[i] = (int)((float)plate_dl_lens_44k[i] * scale) + 1;
        s->dl_buf[i] = (float *)calloc((size_t)s->dl_len[i], sizeof(float));
        s->dl_pos[i] = 0;
        s->dl_filt[i] = 0.0f;
        s->dl_fb[i]   = 0.0f;
    }

    p->state = s;
    p->params[0] = 0.6f;  /* decay */
    p->params[1] = 0.5f;  /* damping */
    p->params[2] = 0.3f;  /* mix */
}

static void plate_verb_process(fx_pedal_instance_t *p, float *buf, int n, float sr) {
    plate_verb_state_t *s = (plate_verb_state_t *)p->state;
    if (!s) return;
    (void)sr;

    float decay  = 0.5f + p->params[0] * 0.48f; /* 0.5-0.98 */
    float damp   = p->params[1] * 0.5f;          /* 0-0.5 LP coefficient */
    float mix    = p->params[2];

    for (int i = 0; i < n; i++) {
        float dry = buf[i];

        /* Feed input + cross-feedback from both delay lines into diffusers */
        float x = dry + s->dl_fb[0] * 0.5f + s->dl_fb[1] * 0.5f;

        /* 4 series allpass diffusers (coefficient 0.5) */
        for (int a = 0; a < PLATE_NUM_ALLPASS; a++) {
            float *ab  = s->ap_buf[a];
            int    pos = s->ap_pos[a];
            float  out = ab[pos];
            float  in  = x + out * 0.5f;
            ab[pos] = in;
            x = out - in * 0.5f;
            s->ap_pos[a] = (pos + 1) % s->ap_len[a];
        }

        /* Two parallel feedback delay lines with LP damping */
        float wet = 0.0f;
        for (int d = 0; d < PLATE_NUM_DELAYS; d++) {
            float *db  = s->dl_buf[d];
            int    pos = s->dl_pos[d];
            float  out = db[pos];

            /* LP damp in feedback */
            s->dl_filt[d] = out * (1.0f - damp) + s->dl_filt[d] * damp;
            db[pos] = x + s->dl_filt[d] * decay;
            s->dl_pos[d] = (pos + 1) % s->dl_len[d];

            s->dl_fb[d] = out;
            wet += out;
        }
        wet *= 0.5f;

        buf[i] = dry * (1.0f - mix) + wet * mix;
    }
}

static void plate_verb_free(fx_pedal_instance_t *p) {
    plate_verb_state_t *s = (plate_verb_state_t *)p->state;
    if (s) {
        for (int i = 0; i < PLATE_NUM_ALLPASS; i++) free(s->ap_buf[i]);
        for (int i = 0; i < PLATE_NUM_DELAYS;  i++) free(s->dl_buf[i]);
    }
}

/* ══════════════════════════════════════════════════════════════════
 * SHIMMER VERB — hall reverb with octave-up shimmer in feedback
 * Simple octave shift: read comb buffer at 2x speed (pitch up 1 octave)
 * and blend shimmer amount back into the feedback
 * Params: [0] decay, [1] shimmer (0-1 amount), [2] mix
 * ══════════════════════════════════════════════════════════════════ */

static const int shimmer_comb_lens_44k[SHIMMER_NUM_COMBS]     = { 1557, 1617, 1791, 1873 };
static const int shimmer_allpass_lens_44k[SHIMMER_NUM_ALLPASS] = { 556, 441 };

static void shimmer_verb_init(fx_pedal_instance_t *p, float sr) {
    shimmer_verb_state_t *s = (shimmer_verb_state_t *)calloc(1, sizeof(*s));
    if (!s) return;

    float scale = sr / 44100.0f;

    for (int i = 0; i < SHIMMER_NUM_COMBS; i++) {
        s->comb_len[i] = (int)((float)shimmer_comb_lens_44k[i] * scale) + 1;
        s->comb_buf[i] = (float *)calloc((size_t)s->comb_len[i], sizeof(float));
        s->comb_pos[i] = 0;
        s->comb_filt[i] = 0.0f;
    }
    for (int i = 0; i < SHIMMER_NUM_ALLPASS; i++) {
        s->ap_len[i] = (int)((float)shimmer_allpass_lens_44k[i] * scale) + 1;
        s->ap_buf[i] = (float *)calloc((size_t)s->ap_len[i], sizeof(float));
        s->ap_pos[i] = 0;
    }
    s->pitch_read_pos = 0.0f;

    p->state = s;
    p->params[0] = 0.6f;  /* decay */
    p->params[1] = 0.4f;  /* shimmer */
    p->params[2] = 0.3f;  /* mix */
}

static void shimmer_verb_process(fx_pedal_instance_t *p, float *buf, int n, float sr) {
    shimmer_verb_state_t *s = (shimmer_verb_state_t *)p->state;
    if (!s) return;
    (void)sr;

    float feedback = 0.7f + p->params[0] * 0.28f;  /* 0.7-0.98 */
    float shimmer  = p->params[1];                   /* 0-1 */
    float mix      = p->params[2];

    /* Use comb_buf[0] as the octave-shift source buffer */
    int   pitch_buf_len = s->comb_len[0];

    for (int i = 0; i < n; i++) {
        float dry = buf[i];
        float wet = 0.0f;

        /* Read octave-up: advance read pointer at 2x write speed */
        s->pitch_read_pos += 2.0f;
        if (s->pitch_read_pos >= (float)pitch_buf_len)
            s->pitch_read_pos -= (float)pitch_buf_len;

        /* Interpolated read for pitch-shifted signal */
        int   pr0    = (int)s->pitch_read_pos;
        float pr_frc = s->pitch_read_pos - (float)pr0;
        int   pr1    = (pr0 + 1) % pitch_buf_len;
        float pitched = s->comb_buf[0][pr0] * (1.0f - pr_frc)
                       + s->comb_buf[0][pr1] * pr_frc;

        /* Parallel comb filters; first comb gets shimmer mixed in */
        for (int c = 0; c < SHIMMER_NUM_COMBS; c++) {
            float *cb  = s->comb_buf[c];
            int    pos = s->comb_pos[c];
            float  out = cb[pos];

            s->comb_filt[c] = out * 0.6f + s->comb_filt[c] * 0.4f;

            /* Feedback signal: normal verb feedback + shimmer (pitched) signal */
            float fb_in = dry + s->comb_filt[c] * feedback;
            if (c == 0) {
                fb_in += pitched * shimmer * feedback * 0.5f;
            }
            cb[pos] = fb_in;
            s->comb_pos[c] = (pos + 1) % s->comb_len[c];

            wet += out;
        }
        wet *= (1.0f / SHIMMER_NUM_COMBS);

        /* Series allpass diffusion */
        for (int a = 0; a < SHIMMER_NUM_ALLPASS; a++) {
            float *ab  = s->ap_buf[a];
            int    pos = s->ap_pos[a];
            float  ap_out = ab[pos];
            float  ap_in  = wet + ap_out * 0.5f;
            ab[pos] = ap_in;
            wet = ap_out - ap_in * 0.5f;
            s->ap_pos[a] = (pos + 1) % s->ap_len[a];
        }

        buf[i] = dry * (1.0f - mix) + wet * mix;
    }
}

static void shimmer_verb_free(fx_pedal_instance_t *p) {
    shimmer_verb_state_t *s = (shimmer_verb_state_t *)p->state;
    if (s) {
        for (int i = 0; i < SHIMMER_NUM_COMBS;   i++) free(s->comb_buf[i]);
        for (int i = 0; i < SHIMMER_NUM_ALLPASS; i++) free(s->ap_buf[i]);
    }
}

/* ══════════════════════════════════════════════════════════════════
 * CLOUD VERB — ambient reverb with near-infinite / freeze feedback
 * Long comb filters + allpass diffusion + LP filter on wet
 * Params: [0] decay (up to 0.998 for freeze), [1] filter (LP on wet),
 *         [2] mix
 * ══════════════════════════════════════════════════════════════════ */

/* Longer delay lines than hall for ambient wash */
static const int cloud_comb_lens_44k[CLOUD_NUM_COMBS]     = { 3163, 3571, 3947, 4327 };
static const int cloud_allpass_lens_44k[CLOUD_NUM_ALLPASS] = { 743, 611 };

static void cloud_verb_init(fx_pedal_instance_t *p, float sr) {
    cloud_verb_state_t *s = (cloud_verb_state_t *)calloc(1, sizeof(*s));
    if (!s) return;

    float scale = sr / 44100.0f;

    for (int i = 0; i < CLOUD_NUM_COMBS; i++) {
        s->comb_len[i] = (int)((float)cloud_comb_lens_44k[i] * scale) + 1;
        s->comb_buf[i] = (float *)calloc((size_t)s->comb_len[i], sizeof(float));
        s->comb_pos[i] = 0;
        s->comb_filt[i] = 0.0f;
    }
    for (int i = 0; i < CLOUD_NUM_ALLPASS; i++) {
        s->ap_len[i] = (int)((float)cloud_allpass_lens_44k[i] * scale) + 1;
        s->ap_buf[i] = (float *)calloc((size_t)s->ap_len[i], sizeof(float));
        s->ap_pos[i] = 0;
    }
    s->lp_state = 0.0f;

    p->state = s;
    p->params[0] = 0.7f;  /* decay */
    p->params[1] = 0.4f;  /* filter */
    p->params[2] = 0.4f;  /* mix */
}

static void cloud_verb_process(fx_pedal_instance_t *p, float *buf, int n, float sr) {
    cloud_verb_state_t *s = (cloud_verb_state_t *)p->state;
    if (!s) return;
    (void)sr;

    /* Decay: 0-1 maps to 0.85-0.998 (near-freeze at top) */
    float feedback = 0.85f + p->params[0] * 0.148f;
    /* Filter: one-pole LP coefficient on wet output (0=bright, 1=dark) */
    float lp_coeff = p->params[1] * 0.92f;
    float mix      = p->params[2];

    for (int i = 0; i < n; i++) {
        float dry = buf[i];
        float wet = 0.0f;

        /* Parallel comb filters with mild internal LP (gentle damping) */
        for (int c = 0; c < CLOUD_NUM_COMBS; c++) {
            float *cb  = s->comb_buf[c];
            int    pos = s->comb_pos[c];
            float  out = cb[pos];

            /* Mild LP in comb feedback (keeps highs from ringing forever) */
            s->comb_filt[c] = out * 0.7f + s->comb_filt[c] * 0.3f;
            cb[pos] = dry + s->comb_filt[c] * feedback;
            s->comb_pos[c] = (pos + 1) % s->comb_len[c];

            wet += out;
        }
        wet *= (1.0f / CLOUD_NUM_COMBS);

        /* Series allpass diffusion */
        for (int a = 0; a < CLOUD_NUM_ALLPASS; a++) {
            float *ab  = s->ap_buf[a];
            int    pos = s->ap_pos[a];
            float  ap_out = ab[pos];
            float  ap_in  = wet + ap_out * 0.5f;
            ab[pos] = ap_in;
            wet = ap_out - ap_in * 0.5f;
            s->ap_pos[a] = (pos + 1) % s->ap_len[a];
        }

        /* Filter param: one-pole LP on wet signal */
        s->lp_state = s->lp_state * lp_coeff + wet * (1.0f - lp_coeff);
        wet = s->lp_state;

        buf[i] = dry * (1.0f - mix) + wet * mix;
    }
}

static void cloud_verb_free(fx_pedal_instance_t *p) {
    cloud_verb_state_t *s = (cloud_verb_state_t *)p->state;
    if (s) {
        for (int i = 0; i < CLOUD_NUM_COMBS;   i++) free(s->comb_buf[i]);
        for (int i = 0; i < CLOUD_NUM_ALLPASS; i++) free(s->ap_buf[i]);
    }
}

/* ══════════════════════════════════════════════════════════════════
 * OCTAVE ENGINE — polyphonic octave shifter
 * Sub-octave: read delay buffer at 0.5x speed (halves frequency)
 * Octave-up:  read delay buffer at 2.0x speed (doubles frequency)
 * Linear interpolation for smooth pitch shifts.
 * Params: [0] sub (sub-octave level), [1] dry, [2] up (octave-up level)
 * ══════════════════════════════════════════════════════════════════ */

static void octave_engine_init(fx_pedal_instance_t *p, float sr) {
    (void)sr;
    octave_engine_state_t *s = (octave_engine_state_t *)calloc(1, sizeof(*s));
    if (!s) return;

    memset(s->buffer, 0, sizeof(s->buffer));
    s->write_pos    = 0;
    s->sub_read_pos = 0.0f;
    s->up_read_pos  = 0.0f;

    p->state = s;
    p->params[0] = 0.5f;  /* sub level */
    p->params[1] = 0.7f;  /* dry level */
    p->params[2] = 0.3f;  /* up level  */
}

static void octave_engine_process(fx_pedal_instance_t *p, float *buf, int n, float sr) {
    (void)sr;
    octave_engine_state_t *s = (octave_engine_state_t *)p->state;
    if (!s) return;

    float sub_level = p->params[0];
    float dry_level = p->params[1];
    float up_level  = p->params[2];

    for (int i = 0; i < n; i++) {
        float dry = buf[i];

        /* Write current sample into the circular buffer */
        s->buffer[s->write_pos] = dry;

        /* --- Sub-octave: advance read pointer at 0.5x speed --- */
        s->sub_read_pos += 0.5f;
        if (s->sub_read_pos >= (float)OCTAVE_BUF_LEN)
            s->sub_read_pos -= (float)OCTAVE_BUF_LEN;

        int   sr0 = (int)s->sub_read_pos;
        float srf = s->sub_read_pos - (float)sr0;
        int   sr1 = (sr0 + 1) % OCTAVE_BUF_LEN;
        float sub = s->buffer[sr0] * (1.0f - srf) + s->buffer[sr1] * srf;

        /* --- Octave-up: advance read pointer at 2.0x speed --- */
        s->up_read_pos += 2.0f;
        if (s->up_read_pos >= (float)OCTAVE_BUF_LEN)
            s->up_read_pos -= (float)OCTAVE_BUF_LEN;

        int   ur0 = (int)s->up_read_pos;
        float urf = s->up_read_pos - (float)ur0;
        int   ur1 = (ur0 + 1) % OCTAVE_BUF_LEN;
        float up  = s->buffer[ur0] * (1.0f - urf) + s->buffer[ur1] * urf;

        s->write_pos = (s->write_pos + 1) % OCTAVE_BUF_LEN;

        buf[i] = dry * dry_level + sub * sub_level + up * up_level;
    }
}

/* ══════════════════════════════════════════════════════════════════
 * LOOP STATION — looper with record / play / overdub
 * Mode param: 0-0.25=idle, 0.25-0.5=record, 0.5-0.75=play,
 *             0.75-1.0=overdub
 * Params: [0] mode, [1] level (loop playback level), [2] feedback
 *         (overdub decay, mapped 0-1 → 0.9-1.0)
 * ══════════════════════════════════════════════════════════════════ */

static void loop_station_init(fx_pedal_instance_t *p, float sr) {
    loop_station_state_t *s = (loop_station_state_t *)calloc(1, sizeof(*s));
    if (!s) return;

    /* Pre-allocate the loop buffer on the heap */
    int max_len = (int)(sr * LOOP_MAX_SECONDS) + 1;
    if (max_len > LOOP_MAX_SAMPLES) max_len = LOOP_MAX_SAMPLES;

    s->buffer     = (float *)calloc((size_t)max_len, sizeof(float));
    s->max_length = max_len;
    s->length     = 0;
    s->position   = 0;
    s->mode       = 0;
    s->prev_mode  = 0;

    p->state = s;
    p->params[0] = 0.0f;   /* mode: idle */
    p->params[1] = 0.8f;   /* level */
    p->params[2] = 0.95f;  /* feedback (overdub decay) */
}

static void loop_station_process(fx_pedal_instance_t *p, float *buf, int n, float sr) {
    (void)sr;
    loop_station_state_t *s = (loop_station_state_t *)p->state;
    if (!s || !s->buffer) return;

    /* Decode mode param (0-1) → integer mode */
    float mode_param = p->params[0];
    int   mode;
    if      (mode_param < 0.25f) mode = 0;  /* idle */
    else if (mode_param < 0.50f) mode = 1;  /* record */
    else if (mode_param < 0.75f) mode = 2;  /* play */
    else                          mode = 3;  /* overdub */

    float level    = p->params[1];
    /* feedback: 0-1 param → 0.9-1.0 decay per loop iteration */
    float feedback = 0.9f + p->params[2] * 0.1f;

    /* Detect transition from record to play: lock loop length */
    if (s->prev_mode == 1 && mode != 1) {
        /* End of recording: lock the loop length */
        if (s->length == 0) s->length = s->position;
        s->position = 0;
    }
    s->prev_mode = mode;

    for (int i = 0; i < n; i++) {
        float dry = buf[i];

        switch (mode) {
            case 0: /* idle — pass through dry, don't touch loop buffer */
                buf[i] = dry;
                break;

            case 1: /* recording — write to loop buffer, pass through dry */
                if (s->position < s->max_length) {
                    s->buffer[s->position] = dry;
                    s->position++;
                    s->length = s->position;  /* grow loop length while recording */
                }
                buf[i] = dry;
                break;

            case 2: /* playing — mix loop with dry */
                if (s->length > 0) {
                    int pos = s->position % s->length;
                    float loop_out = s->buffer[pos] * level;
                    s->position = (pos + 1) % s->length;
                    buf[i] = dry + loop_out;
                } else {
                    buf[i] = dry;
                }
                break;

            case 3: /* overdub — add input to existing loop, read + output */
                if (s->length > 0) {
                    int pos = s->position % s->length;
                    float loop_out = s->buffer[pos];
                    /* Write: existing content * feedback + new input */
                    s->buffer[pos] = loop_out * feedback + dry;
                    s->position = (pos + 1) % s->length;
                    buf[i] = dry + loop_out * level;
                } else {
                    /* No loop recorded yet: fall back to pass-through */
                    buf[i] = dry;
                }
                break;

            default:
                buf[i] = dry;
                break;
        }
    }
}

static void loop_station_free(fx_pedal_instance_t *p) {
    loop_station_state_t *s = (loop_station_state_t *)p->state;
    if (s) {
        free(s->buffer);
        s->buffer = NULL;
    }
}

/* ══════════════════════════════════════════════════════════════════
 * INFINITE HOLD — freeze / drone pedal
 * Captures HOLD_BUF_LEN samples then loops them continuously.
 * Params: [0] hold  (0-0.5=passthrough, 0.5-1.0=frozen)
 *         [1] decay (frozen signal amplitude decay per sample, 0-1 → 0.999-1.0)
 *         [2] mix   (dry/wet)
 * ══════════════════════════════════════════════════════════════════ */

static void infinite_hold_init(fx_pedal_instance_t *p, float sr) {
    (void)sr;
    infinite_hold_state_t *s =
        (infinite_hold_state_t *)calloc(1, sizeof(*s));
    if (!s) return;

    s->position    = 0;
    s->capture_pos = 0;
    s->frozen      = 0;
    s->amplitude   = 1.0f;

    p->state      = s;
    p->params[0]  = 0.0f;   /* hold: off */
    p->params[1]  = 1.0f;   /* decay: sustain indefinitely */
    p->params[2]  = 0.5f;   /* mix */
}

static void infinite_hold_process(fx_pedal_instance_t *p, float *buf, int n,
                                  float sr) {
    (void)sr;
    infinite_hold_state_t *s = (infinite_hold_state_t *)p->state;
    if (!s) return;

    /* Decode hold param: below 0.5 = off, 0.5+ = frozen */
    int want_frozen = (p->params[0] >= 0.5f);

    /* Transition: entering frozen mode — reset capture */
    if (want_frozen && !s->frozen) {
        s->capture_pos = 0;
        s->frozen      = 0;  /* not yet fully frozen — still filling */
        s->amplitude   = 1.0f;
    }

    /* Decay: map 0-1 → 0.999-1.0 */
    float decay = 0.999f + p->params[1] * 0.001f;
    float mix   = p->params[2];

    for (int i = 0; i < n; i++) {
        float dry = buf[i];
        float wet = 0.0f;

        if (!want_frozen) {
            /* Passthrough mode — also continuously record into buffer */
            s->buffer[s->capture_pos % HOLD_BUF_LEN] = dry;
            s->capture_pos = (s->capture_pos + 1) % HOLD_BUF_LEN;
            s->frozen = 0;
            wet = dry;
        } else if (!s->frozen) {
            /* Filling the capture buffer */
            s->buffer[s->capture_pos] = dry;
            s->capture_pos++;
            if (s->capture_pos >= HOLD_BUF_LEN) {
                /* Buffer full: start looping */
                s->frozen   = 1;
                s->position = 0;
            }
            wet = dry;  /* pass through while capturing */
        } else {
            /* Looping frozen buffer */
            wet = s->buffer[s->position] * s->amplitude;
            s->position = (s->position + 1) % HOLD_BUF_LEN;
            s->amplitude *= decay;
        }

        buf[i] = dry * (1.0f - mix) + wet * mix;
    }
}

/* ══════════════════════════════════════════════════════════════════
 * GRAIN CLOUD — granular delay
 * Records into a 1-second circular buffer, then fires grains
 * (short slices) at random positions with configurable pitch rate.
 * Params: [0] density (0-1 → 1-20 grains/sec)
 *         [1] size    (0-1 → grain length 10ms-200ms)
 *         [2] pitch   (0-1 → playback rate 0.5x-2.0x)
 *         [3] mix     (dry/wet blend)
 * ══════════════════════════════════════════════════════════════════ */

/* LCG random in [0, GRAIN_REC_LEN) */
static unsigned int g_grain_rng = 12345u;
static inline int grain_rand_pos(void) {
    g_grain_rng = g_grain_rng * 1664525u + 1013904223u;
    return (int)(g_grain_rng % (unsigned int)GRAIN_REC_LEN);
}

static void grain_cloud_init(fx_pedal_instance_t *p, float sr) {
    (void)sr;
    grain_cloud_state_t *s =
        (grain_cloud_state_t *)calloc(1, sizeof(*s));
    if (!s) return;

    s->write_pos     = 0;
    s->trigger_accum = 0.0f;
    for (int v = 0; v < GRAIN_VOICES; v++) {
        s->voices[v].remaining  = 0;
        s->voices[v].read_pos   = 0.0f;
        s->voices[v].pitch_rate = 1.0f;
    }

    p->state     = s;
    p->params[0] = 0.3f;  /* density */
    p->params[1] = 0.3f;  /* size (~60ms) */
    p->params[2] = 0.5f;  /* pitch (1.0x) */
    p->params[3] = 0.5f;  /* mix */
}

static void grain_cloud_process(fx_pedal_instance_t *p, float *buf, int n,
                                float sr) {
    grain_cloud_state_t *s = (grain_cloud_state_t *)p->state;
    if (!s) return;

    /* Map params */
    float density_hz   = 1.0f + p->params[0] * 19.0f;       /* 1-20 grains/sec */
    float size_ms      = 10.0f + p->params[1] * 190.0f;      /* 10ms-200ms */
    int   grain_len    = (int)(size_ms * sr / 1000.0f);
    if (grain_len < 1) grain_len = 1;
    if (grain_len > GRAIN_REC_LEN) grain_len = GRAIN_REC_LEN;
    float pitch_rate   = 0.5f + p->params[2] * 1.5f;         /* 0.5-2.0 */
    float mix          = p->params[3];

    /* Samples per trigger interval */
    float trigger_interval = sr / density_hz;

    for (int i = 0; i < n; i++) {
        float dry = buf[i];

        /* Write input into circular record buffer */
        s->rec_buf[s->write_pos] = dry;
        s->write_pos = (s->write_pos + 1) % GRAIN_REC_LEN;

        /* Check if a new grain should fire */
        s->trigger_accum += 1.0f;
        if (s->trigger_accum >= trigger_interval) {
            s->trigger_accum -= trigger_interval;

            /* Find a free voice (one with remaining==0) */
            for (int v = 0; v < GRAIN_VOICES; v++) {
                if (s->voices[v].remaining == 0) {
                    /* Pick a random read position in the record buffer */
                    int start = grain_rand_pos();
                    s->voices[v].read_pos   = (float)start;
                    s->voices[v].remaining  = grain_len;
                    s->voices[v].pitch_rate = pitch_rate;
                    break;
                }
            }
        }

        /* Sum all active grain voices */
        float wet = 0.0f;
        for (int v = 0; v < GRAIN_VOICES; v++) {
            if (s->voices[v].remaining > 0) {
                /* Linear interpolation read from record buffer */
                int   ri0 = (int)s->voices[v].read_pos % GRAIN_REC_LEN;
                int   ri1 = (ri0 + 1) % GRAIN_REC_LEN;
                float frac = s->voices[v].read_pos - (float)(int)s->voices[v].read_pos;
                wet += s->rec_buf[ri0] * (1.0f - frac) + s->rec_buf[ri1] * frac;

                /* Advance read position by pitch rate */
                s->voices[v].read_pos += s->voices[v].pitch_rate;
                if ((int)s->voices[v].read_pos >= GRAIN_REC_LEN)
                    s->voices[v].read_pos -= (float)GRAIN_REC_LEN;

                s->voices[v].remaining--;
            }
        }

        /* Normalise by voice count to avoid clipping */
        wet *= (1.0f / (float)GRAIN_VOICES);

        buf[i] = dry * (1.0f - mix) + wet * mix;
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
static const char *jade_drive_params[]     = { "Drive", "Tone", "Level" };
static const char *gold_drive_params[]     = { "Gain", "Treble", "Output" };
static const char *rodent_params[]         = { "Distortion", "Filter", "Volume" };
static const char *echo_delay_params[]     = { "Time", "Feedback", "Mix", "Sync" };
static const char *hall_verb_params[]      = { "Decay", "Damping", "Mix" };
static const char *squeeze_box_params[]    = { "Output", "Sensitivity" };
static const char *drip_verb_params[]      = { "Dwell", "Tone", "Mix" };
static const char *carbon_delay_params[]   = { "Time", "Feedback", "Tone", "Mod", "Mix" };
static const char *tape_machine_params[]   = { "Time", "Feedback", "Wow", "Flutter", "Mix" };
static const char *howl_wah_params[]       = { "Freq", "Q", "Volume" };
static const char *quack_filter_params[]   = { "Sens", "Decay", "Q", "Mix" };
static const char *liquid_chorus_params[]  = { "Rate", "Depth", "Mix" };
static const char *phase_sweep_params[]    = { "Rate", "Depth", "Feedback", "Mix" };
static const char *pulse_trem_params[]     = { "Rate", "Depth", "Wave" };
static const char *tone_sculptor_params[]  = { "100Hz", "200Hz", "400Hz", "800Hz",
                                               "1.6kHz", "3.2kHz", "6.4kHz", "Output" };
static const char *mammoth_fuzz_params[]   = { "Sustain", "Tone", "Volume" };
static const char *round_fuzz_params[]     = { "Fuzz", "Volume" };
static const char *chaos_fuzz_params[]     = { "Volume", "Gate", "Drive", "Stab" };
static const char *grit_crush_params[]     = { "Bits", "Rate", "Mix" };
static const char *ring_tone_params[]      = { "Freq", "Mix" };
static const char *warm_tape_params[]      = { "Drive", "Warmth", "Mix" };
static const char *drift_vibrato_params[]  = { "Rate", "Depth" };
static const char *jet_flanger_params[]    = { "Rate", "Depth", "Feedback", "Mix" };
static const char *plate_verb_params[]     = { "Decay", "Damping", "Mix" };
static const char *shimmer_verb_params[]   = { "Decay", "Shimmer", "Mix" };
static const char *cloud_verb_params[]     = { "Decay", "Filter", "Mix" };
static const char *octave_engine_params[]  = { "Sub", "Dry", "Up" };
static const char *loop_station_params[]   = { "Mode", "Level", "Feedback" };
static const char *infinite_hold_params[]  = { "Hold", "Decay", "Mix" };
static const char *grain_cloud_params[]    = { "Density", "Size", "Pitch", "Mix" };

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
        case FX_PEDAL_CARBON_DELAY:  return 5;
        case FX_PEDAL_TAPE_MACHINE:  return 5;
        case FX_PEDAL_HOWL_WAH:      return 3;
        case FX_PEDAL_QUACK_FILTER:  return 4;
        case FX_PEDAL_LIQUID_CHORUS: return 3;
        case FX_PEDAL_PHASE_SWEEP:   return 4;
        case FX_PEDAL_PULSE_TREM:    return 3;
        case FX_PEDAL_SQUEEZE_BOX:   return 2;
        case FX_PEDAL_NOISE_GATE:      return 4;
        case FX_PEDAL_TONE_SCULPTOR:   return 8;
        case FX_PEDAL_MAMMOTH_FUZZ:    return 3;
        case FX_PEDAL_ROUND_FUZZ:      return 2;
        case FX_PEDAL_CHAOS_FUZZ:      return 4;
        case FX_PEDAL_GRIT_CRUSH:      return 3;
        case FX_PEDAL_RING_TONE:       return 2;
        case FX_PEDAL_WARM_TAPE:       return 3;
        case FX_PEDAL_DRIFT_VIBRATO:   return 2;
        case FX_PEDAL_JET_FLANGER:     return 4;
        case FX_PEDAL_PLATE_VERB:      return 3;
        case FX_PEDAL_SHIMMER_VERB:    return 3;
        case FX_PEDAL_CLOUD_VERB:      return 3;
        case FX_PEDAL_OCTAVE_ENGINE:   return 3;
        case FX_PEDAL_LOOP_STATION:    return 3;
        case FX_PEDAL_INFINITE_HOLD:   return 3;
        case FX_PEDAL_GRAIN_CLOUD:     return 4;
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
        case FX_PEDAL_DRIP_VERB:
            if (param < 3) return drip_verb_params[param];
            break;
        case FX_PEDAL_CARBON_DELAY:
            if (param < 5) return carbon_delay_params[param];
            break;
        case FX_PEDAL_TAPE_MACHINE:
            if (param < 5) return tape_machine_params[param];
            break;
        case FX_PEDAL_HOWL_WAH:
            if (param < 3) return howl_wah_params[param];
            break;
        case FX_PEDAL_QUACK_FILTER:
            if (param < 4) return quack_filter_params[param];
            break;
        case FX_PEDAL_LIQUID_CHORUS:
            if (param < 3) return liquid_chorus_params[param];
            break;
        case FX_PEDAL_PHASE_SWEEP:
            if (param < 4) return phase_sweep_params[param];
            break;
        case FX_PEDAL_PULSE_TREM:
            if (param < 3) return pulse_trem_params[param];
            break;
        case FX_PEDAL_TONE_SCULPTOR:
            if (param < 8) return tone_sculptor_params[param];
            break;
        case FX_PEDAL_MAMMOTH_FUZZ:
            if (param < 3) return mammoth_fuzz_params[param];
            break;
        case FX_PEDAL_ROUND_FUZZ:
            if (param < 2) return round_fuzz_params[param];
            break;
        case FX_PEDAL_CHAOS_FUZZ:
            if (param < 4) return chaos_fuzz_params[param];
            break;
        case FX_PEDAL_GRIT_CRUSH:
            if (param < 3) return grit_crush_params[param];
            break;
        case FX_PEDAL_RING_TONE:
            if (param < 2) return ring_tone_params[param];
            break;
        case FX_PEDAL_WARM_TAPE:
            if (param < 3) return warm_tape_params[param];
            break;
        case FX_PEDAL_DRIFT_VIBRATO:
            if (param < 2) return drift_vibrato_params[param];
            break;
        case FX_PEDAL_JET_FLANGER:
            if (param < 4) return jet_flanger_params[param];
            break;
        case FX_PEDAL_PLATE_VERB:
            if (param < 3) return plate_verb_params[param];
            break;
        case FX_PEDAL_SHIMMER_VERB:
            if (param < 3) return shimmer_verb_params[param];
            break;
        case FX_PEDAL_CLOUD_VERB:
            if (param < 3) return cloud_verb_params[param];
            break;
        case FX_PEDAL_OCTAVE_ENGINE:
            if (param < 3) return octave_engine_params[param];
            break;
        case FX_PEDAL_LOOP_STATION:
            if (param < 3) return loop_station_params[param];
            break;
        case FX_PEDAL_INFINITE_HOLD:
            if (param < 3) return infinite_hold_params[param];
            break;
        case FX_PEDAL_GRAIN_CLOUD:
            if (param < 4) return grain_cloud_params[param];
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
        case FX_PEDAL_JADE_DRIVE:    jade_drive_init(p, sr);    break;
        case FX_PEDAL_GOLD_DRIVE:    gold_drive_init(p, sr);    break;
        case FX_PEDAL_RODENT:        rodent_init(p, sr);        break;
        case FX_PEDAL_ECHO_DELAY:    echo_delay_init(p, sr);    break;
        case FX_PEDAL_HALL_VERB:     hall_verb_init(p, sr);     break;
        case FX_PEDAL_SQUEEZE_BOX:   squeeze_box_init(p, sr);   break;
        case FX_PEDAL_DRIP_VERB:     drip_verb_init(p, sr);     break;
        case FX_PEDAL_CARBON_DELAY:  carbon_delay_init(p, sr);  break;
        case FX_PEDAL_TAPE_MACHINE:  tape_machine_init(p, sr);  break;
        case FX_PEDAL_HOWL_WAH:      howl_wah_init(p, sr);      break;
        case FX_PEDAL_QUACK_FILTER:  quack_filter_init(p, sr);  break;
        case FX_PEDAL_LIQUID_CHORUS: liquid_chorus_init(p, sr); break;
        case FX_PEDAL_PHASE_SWEEP:   phase_sweep_init(p, sr);   break;
        case FX_PEDAL_PULSE_TREM:      pulse_trem_init(p, sr);      break;
        case FX_PEDAL_TONE_SCULPTOR:   tone_sculptor_init(p, sr);   break;
        case FX_PEDAL_MAMMOTH_FUZZ:    mammoth_fuzz_init(p, sr);    break;
        case FX_PEDAL_ROUND_FUZZ:      round_fuzz_init(p, sr);      break;
        case FX_PEDAL_CHAOS_FUZZ:      chaos_fuzz_init(p, sr);      break;
        case FX_PEDAL_GRIT_CRUSH:      grit_crush_init(p, sr);      break;
        case FX_PEDAL_RING_TONE:       ring_tone_init(p, sr);       break;
        case FX_PEDAL_WARM_TAPE:       warm_tape_init(p, sr);       break;
        case FX_PEDAL_DRIFT_VIBRATO:   drift_vibrato_init(p, sr);   break;
        case FX_PEDAL_JET_FLANGER:     jet_flanger_init(p, sr);     break;
        case FX_PEDAL_PLATE_VERB:      plate_verb_init(p, sr);      break;
        case FX_PEDAL_SHIMMER_VERB:    shimmer_verb_init(p, sr);    break;
        case FX_PEDAL_CLOUD_VERB:      cloud_verb_init(p, sr);      break;
        case FX_PEDAL_OCTAVE_ENGINE:   octave_engine_init(p, sr);   break;
        case FX_PEDAL_LOOP_STATION:    loop_station_init(p, sr);    break;
        case FX_PEDAL_INFINITE_HOLD:   infinite_hold_init(p, sr);   break;
        case FX_PEDAL_GRAIN_CLOUD:     grain_cloud_init(p, sr);     break;
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
            case FX_PEDAL_ECHO_DELAY:   echo_delay_free(p);   break;
            case FX_PEDAL_HALL_VERB:    hall_verb_free(p);    break;
            case FX_PEDAL_DRIP_VERB:    drip_verb_free(p);    break;
            case FX_PEDAL_CARBON_DELAY: carbon_delay_free(p); break;
            case FX_PEDAL_TAPE_MACHINE: tape_machine_free(p); break;
            case FX_PEDAL_PLATE_VERB:   plate_verb_free(p);   break;
            case FX_PEDAL_SHIMMER_VERB: shimmer_verb_free(p);   break;
            case FX_PEDAL_CLOUD_VERB:   cloud_verb_free(p);     break;
            case FX_PEDAL_LOOP_STATION: loop_station_free(p);   break;
            default: break;
        }
        free(p->state);
        p->state = NULL;
    }
}

void fx_pedal_process(fx_pedal_instance_t *p, float *buf, int n, float sr) {
    if (!p || p->bypass || !p->state) return;

    switch (p->type) {
        case FX_PEDAL_JADE_DRIVE:    jade_drive_process(p, buf, n, sr);    break;
        case FX_PEDAL_GOLD_DRIVE:    gold_drive_process(p, buf, n, sr);    break;
        case FX_PEDAL_RODENT:        rodent_process(p, buf, n, sr);        break;
        case FX_PEDAL_ECHO_DELAY:    echo_delay_process(p, buf, n, sr);    break;
        case FX_PEDAL_HALL_VERB:     hall_verb_process(p, buf, n, sr);     break;
        case FX_PEDAL_SQUEEZE_BOX:   squeeze_box_process(p, buf, n, sr);   break;
        case FX_PEDAL_DRIP_VERB:     drip_verb_process(p, buf, n, sr);     break;
        case FX_PEDAL_CARBON_DELAY:  carbon_delay_process(p, buf, n, sr);  break;
        case FX_PEDAL_TAPE_MACHINE:  tape_machine_process(p, buf, n, sr);  break;
        case FX_PEDAL_HOWL_WAH:      howl_wah_process(p, buf, n, sr);      break;
        case FX_PEDAL_QUACK_FILTER:  quack_filter_process(p, buf, n, sr);  break;
        case FX_PEDAL_LIQUID_CHORUS: liquid_chorus_process(p, buf, n, sr); break;
        case FX_PEDAL_PHASE_SWEEP:   phase_sweep_process(p, buf, n, sr);   break;
        case FX_PEDAL_PULSE_TREM:      pulse_trem_process(p, buf, n, sr);      break;
        case FX_PEDAL_TONE_SCULPTOR:   tone_sculptor_process(p, buf, n, sr);   break;
        case FX_PEDAL_MAMMOTH_FUZZ:    mammoth_fuzz_process(p, buf, n, sr);    break;
        case FX_PEDAL_ROUND_FUZZ:      round_fuzz_process(p, buf, n, sr);      break;
        case FX_PEDAL_CHAOS_FUZZ:      chaos_fuzz_process(p, buf, n, sr);      break;
        case FX_PEDAL_GRIT_CRUSH:      grit_crush_process(p, buf, n, sr);      break;
        case FX_PEDAL_RING_TONE:       ring_tone_process(p, buf, n, sr);       break;
        case FX_PEDAL_WARM_TAPE:       warm_tape_process(p, buf, n, sr);       break;
        case FX_PEDAL_DRIFT_VIBRATO:   drift_vibrato_process(p, buf, n, sr);   break;
        case FX_PEDAL_JET_FLANGER:     jet_flanger_process(p, buf, n, sr);     break;
        case FX_PEDAL_PLATE_VERB:      plate_verb_process(p, buf, n, sr);      break;
        case FX_PEDAL_SHIMMER_VERB:    shimmer_verb_process(p, buf, n, sr);    break;
        case FX_PEDAL_CLOUD_VERB:      cloud_verb_process(p, buf, n, sr);      break;
        case FX_PEDAL_OCTAVE_ENGINE:   octave_engine_process(p, buf, n, sr);   break;
        case FX_PEDAL_LOOP_STATION:    loop_station_process(p, buf, n, sr);    break;
        case FX_PEDAL_INFINITE_HOLD:   infinite_hold_process(p, buf, n, sr);   break;
        case FX_PEDAL_GRAIN_CLOUD:     grain_cloud_process(p, buf, n, sr);     break;
        default:
            /* Unimplemented pedals: passthrough (no state, caught above) */
            break;
    }
}
