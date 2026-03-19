/*
 * 0xFX Engine — Internal structures
 *
 * PRIVATE HEADER. Only engine implementation files include this.
 * GUI and plugin layers must NEVER include this file.
 */
#ifndef FX_ENGINE_INTERNAL_H
#define FX_ENGINE_INTERNAL_H

#include "../fx_engine.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "kissfft/kiss_fftr.h"

#define FX_MAX_PEDALS_PER_POS  16
#define FX_MAX_CHAINS          4
#define FX_MAX_PEDALS_TOTAL    64
#define FX_MAX_BLOCK_SIZE      4096

/* ── Pedal instance ───────────────────────────────────────────── */

#define FX_MAX_PARAMS 12

typedef struct {
    fx_pedal_type_t type;
    fx_pedal_id     id;
    bool            bypass;
    float           params[FX_MAX_PARAMS];
    void           *state;          /* heap-allocated DSP state */
    fx_chain_pos_t  position;       /* pre or post */
    int             order;          /* index within position */
} fx_pedal_instance_t;

/* ── Biquad filter (shared DSP primitive) ─────────────────────── */

typedef struct {
    float b0, b1, b2, a1, a2;  /* coefficients (a0 normalized to 1) */
    float z1, z2;               /* state (transposed direct form II) */
} fx_biquad_t;

/* ── Amp model state ──────────────────────────────────────────── */

#define AMP_MAX_PREAMP_STAGES 4
#define AMP_TONE_BANDS        3  /* bass, mid, treble */

typedef struct {
    fx_amp_type_t type;
    float         params[FX_AMP_PARAM_COUNT];

    /* Preamp gain stages — DC blocking highpass per stage */
    float         dc_block_z1[AMP_MAX_PREAMP_STAGES];
    int           num_stages;   /* 1-4 depending on model */

    /* Tone stack — 3 biquad bands */
    fx_biquad_t   tone_bass;
    fx_biquad_t   tone_mid;
    fx_biquad_t   tone_treble;
    fx_biquad_t   presence_filter;
    float         tone_sr;      /* sample rate for coefficient caching */
    float         tone_cache[4]; /* cached param values to detect changes */

    /* Power amp */
    float         power_envelope; /* compression envelope follower */
    float         sag_voltage;    /* simulated supply voltage (0-1) */
} fx_amp_state_t;

/* ── Noise gate state ─────────────────────────────────────────── */

typedef struct {
    float threshold;    /* dB, -80 to 0 */
    float attack;       /* seconds */
    float release;      /* seconds */
    float hold;         /* seconds */
    float envelope;     /* current envelope follower value */
    float gain;         /* current gate gain (0.0 to 1.0) */
    float hold_counter; /* samples remaining in hold phase */
} fx_noise_gate_t;

/* ── Cabinet IR state ─────────────────────────────────────────── */

typedef struct {
    bool           loaded;
    bool           bypass;
    kiss_fft_cpx  *ir_fft;         /* pre-computed IR FFT (fft_size/2+1 bins) */
    float         *overlap_buf;    /* overlap-add tail buffer (fft_size floats) */
    kiss_fft_cpx  *fft_buf;        /* scratch: FFT output (fft_size/2+1 bins) */
    float         *time_buf;       /* scratch: zero-padded input (fft_size floats) */
    kiss_fftr_cfg  fft_cfg;        /* forward FFT config */
    kiss_fftr_cfg  ifft_cfg;       /* inverse FFT config */
    int            fft_size;       /* FFT size (power of 2, >= block_size + ir_len - 1) */
    int            ir_len;         /* original IR length in samples */
    int            block_size;     /* processing block size */
} fx_cab_state_t;

/* ── Signal chain (one amp+cab+post-fx path) ──────────────────── */

typedef struct {
    fx_amp_state_t amp;
    fx_cab_state_t cab;
    float          mix;     /* 0.0 to 1.0 blend level */
    bool           active;
} fx_signal_chain_t;

/* ── Tuner state ──────────────────────────────────────────────── */

typedef struct {
    float frequency;
    int   midi_note;
    float cents;
    float buffer[4096];
    int   write_pos;
    int   samples_since_update;
} fx_tuner_state_t;

/* ── The engine ───────────────────────────────────────────────── */

struct fx_engine {
    float sample_rate;

    /* Noise gate (input stage) */
    fx_noise_gate_t gate;

    /* Pedals — all instances in a flat array */
    fx_pedal_instance_t pedals[FX_MAX_PEDALS_TOTAL];
    int                 num_pedals;
    fx_pedal_id         next_pedal_id;

    /* Signal chains (amp + cab + post-fx) */
    fx_signal_chain_t   chains[FX_MAX_CHAINS];
    int                 num_chains;

    /* Tuner */
    fx_tuner_state_t    tuner;

    /* Scratch buffers for processing (pre-allocated) */
    float scratch_a[FX_MAX_BLOCK_SIZE];
    float scratch_b[FX_MAX_BLOCK_SIZE];
};

/* ── Internal DSP functions ───────────────────────────────────── */

/* Amp processing */
void fx_amp_init(fx_amp_state_t *amp, fx_amp_type_t type);
void fx_amp_process(fx_amp_state_t *amp, float *buf, int n, float sr);

/* Noise gate */
void fx_gate_init(fx_noise_gate_t *gate);
void fx_gate_process(fx_noise_gate_t *gate, float *buf, int n, float sr);

/* Tuner */
void fx_tuner_init(fx_tuner_state_t *tuner);
void fx_tuner_feed(fx_tuner_state_t *tuner, const float *buf, int n, float sr);

/* Pedal DSP dispatch */
void  fx_pedal_init_state(fx_pedal_instance_t *p, float sr);
void  fx_pedal_free_state(fx_pedal_instance_t *p);
void  fx_pedal_process(fx_pedal_instance_t *p, float *buf, int n, float sr);

/* Cab IR */
void fx_cab_init(fx_cab_state_t *cab);
void fx_cab_free(fx_cab_state_t *cab);
bool fx_cab_load_wav(fx_cab_state_t *cab, const char *wav_path, int block_size);
void fx_cab_process(fx_cab_state_t *cab, float *buf, int n);

/* Biquad helpers */
void fx_biquad_lowshelf(fx_biquad_t *bq, float freq, float gain_db, float sr);
void fx_biquad_highshelf(fx_biquad_t *bq, float freq, float gain_db, float sr);
void fx_biquad_peak(fx_biquad_t *bq, float freq, float gain_db, float q, float sr);
void fx_biquad_highpass(fx_biquad_t *bq, float freq, float q, float sr);
void fx_biquad_lowpass(fx_biquad_t *bq, float freq, float q, float sr);
static inline float fx_biquad_process(fx_biquad_t *bq, float in) {
    float out = bq->b0 * in + bq->z1;
    bq->z1 = bq->b1 * in - bq->a1 * out + bq->z2;
    bq->z2 = bq->b2 * in - bq->a2 * out;
    return out;
}

#endif /* FX_ENGINE_INTERNAL_H */
