/*
 * 0xFX — Chromatic tuner
 *
 * NSDF (Normalized Square Difference Function) pitch detection
 * with parabolic interpolation for sub-sample accuracy.
 * Designed for guitar: detects fundamentals from ~60Hz (drop D) to ~1200Hz.
 */
#include "engine_internal.h"
#include "../../core/log.h"

#define TUNER_UPDATE_INTERVAL 2048  /* ~21Hz update rate at 44.1kHz */

void fx_tuner_init(fx_tuner_state_t *t) {
    memset(t, 0, sizeof(*t));
    t->fft_fwd = kiss_fftr_alloc(FX_TUNER_FFT_SIZE, 0, NULL, NULL);
    t->fft_inv = kiss_fftr_alloc(FX_TUNER_FFT_SIZE, 1, NULL, NULL);
    if (!t->fft_fwd || !t->fft_inv)
        FX_ERROR("tuner: FFT plan allocation failed — pitch detection disabled");
}

void fx_tuner_free(fx_tuner_state_t *t) {
    if (t->fft_fwd) kiss_fftr_free(t->fft_fwd);
    if (t->fft_inv) kiss_fftr_free(t->fft_inv);
    t->fft_fwd = NULL;
    t->fft_inv = NULL;
}

void fx_tuner_feed(fx_tuner_state_t *t, const float *buf, int n, float sr) {
    /* Accumulate input samples into ring buffer */
    for (int i = 0; i < n; i++) {
        t->buffer[t->write_pos] = buf[i];
        t->write_pos = (t->write_pos + 1) % FX_TUNER_BUF_SIZE;
    }

    t->samples_since_update += n;
    if (t->samples_since_update < TUNER_UPDATE_INTERVAL) return;
    t->samples_since_update = 0;
    if (!t->fft_fwd || !t->fft_inv) return;

    /* Unroll the newest window out of the ring so the analysis below reads
     * contiguous memory (two copies, no per-sample modulo) */
    const int window = FX_TUNER_WINDOW;
    const int start = (t->write_pos + FX_TUNER_BUF_SIZE - window) % FX_TUNER_BUF_SIZE;
    const int first = (FX_TUNER_BUF_SIZE - start < window) ? FX_TUNER_BUF_SIZE - start : window;
    float *x = t->window;
    memcpy(x, t->buffer + start, sizeof(float) * (size_t)first);
    memcpy(x + first, t->buffer, sizeof(float) * (size_t)(window - first));

    /* Check signal level — skip if too quiet */
    float energy = 0.0f;
    for (int j = 0; j < window; j++) energy += x[j] * x[j];
    float rms = sqrtf(energy / (float)window);
    if (rms < 0.005f) {
        /* Signal too weak — don't update, let display show last reading */
        return;
    }

    /* NSDF: search for fundamental between ~55Hz (A1) and ~1200Hz (D6) */
    int min_lag = (int)(sr / 1200.0f);
    int max_lag = (int)(sr / 55.0f);
    if (max_lag > window) max_lag = window;
    if (min_lag < 2) min_lag = 2;

    /* NSDF(lag) = 2 * r(lag) / m(lag) over the overlapping part of the window:
     *   r(lag) = sum x[j] * x[j+lag]            (autocorrelation)
     *   m(lag) = sum x[j]^2 + x[j+lag]^2        (energy of both halves)
     * r comes from one FFT round trip — zero-padding to 2x the window makes
     * it linear rather than circular — and m from a running sum, instead of
     * a direct window x lag loop that cost 5-10 ms per update. */
    memset(x + window, 0, sizeof(float) * (size_t)(FX_TUNER_FFT_SIZE - window));
    kiss_fftr(t->fft_fwd, x, t->spectrum);
    for (int k = 0; k <= FX_TUNER_FFT_SIZE / 2; k++) {
        kiss_fft_cpx c = t->spectrum[k];
        t->spectrum[k].r = c.r * c.r + c.i * c.i;
        t->spectrum[k].i = 0.0f;
    }
    kiss_fftri(t->fft_inv, t->spectrum, t->acf);  /* unscaled: acf = N * r */

    float *nsdf = t->nsdf;
    const float acf_scale = 2.0f / (float)FX_TUNER_FFT_SIZE;
    float m = 2.0f * energy;  /* m(0) */
    for (int lag = 1; lag < max_lag; lag++) {
        m -= x[lag - 1] * x[lag - 1] + x[window - lag] * x[window - lag];
        if (lag < min_lag) continue;
        nsdf[lag] = (m > 1e-10f) ? acf_scale * t->acf[lag] / m : 0.0f;
    }

    /* Find peaks in NSDF using "first peak above threshold" method.
     * This correctly identifies the fundamental instead of harmonics.
     *
     * Algorithm: MPM (McLeod Pitch Method)
     * 1. Find all positive-going zero crossings
     * 2. Find the peak in each positive lobe
     * 3. Accept the first peak above a threshold (0.7)
     */
    float threshold = 0.7f;  /* minimum NSDF peak to accept */
    int best_lag = 0;
    float best_nsdf = 0.0f;

    /* Collect peaks of each positive lobe */
    typedef struct { int lag; float val; } Peak;
    Peak peaks[64];
    int num_peaks = 0;

    bool in_positive = false;
    int lobe_peak_lag = 0;
    float lobe_peak_val = 0.0f;

    /* The NSDF starts at 1 at lag 0 and stays positive until the first
     * zero crossing. For notes below ~300 Hz that lobe is still positive at
     * min_lag, and counting it made every low note read as sr / min_lag
     * (1225 Hz at 44.1k) — so lobes only count after the NSDF has gone
     * negative once. */
    bool seen_negative = false;

    for (int lag = min_lag; lag < max_lag && lag < FX_TUNER_WINDOW; lag++) {
        float v = nsdf[lag];
        if (!seen_negative) {
            if (v < 0.0f) seen_negative = true;
            continue;
        }
        if (v > 0.0f) {
            if (!in_positive) {
                /* Entering positive lobe */
                in_positive = true;
                lobe_peak_val = 0.0f;
            }
            if (v > lobe_peak_val) {
                lobe_peak_val = v;
                lobe_peak_lag = lag;
            }
        } else if (in_positive) {
            /* Leaving positive lobe — record the peak */
            if (num_peaks < 64) {
                peaks[num_peaks].lag = lobe_peak_lag;
                peaks[num_peaks].val = lobe_peak_val;
                num_peaks++;
            }
            in_positive = false;
        }
    }
    /* Catch final lobe if still positive */
    if (in_positive && num_peaks < 64) {
        peaks[num_peaks].lag = lobe_peak_lag;
        peaks[num_peaks].val = lobe_peak_val;
        num_peaks++;
    }

    if (num_peaks == 0) return;

    /* Find the maximum peak value for threshold scaling */
    float max_peak = 0.0f;
    for (int i = 0; i < num_peaks; i++) {
        if (peaks[i].val > max_peak) max_peak = peaks[i].val;
    }

    /* Accept the FIRST peak above threshold * max_peak.
     * This is key — harmonics have later (shorter period) peaks,
     * so picking the first strong one gives us the fundamental. */
    float accept_thresh = threshold * max_peak;
    for (int i = 0; i < num_peaks; i++) {
        if (peaks[i].val >= accept_thresh) {
            best_lag = peaks[i].lag;
            best_nsdf = peaks[i].val;
            break;
        }
    }

    if (best_lag < min_lag || best_nsdf < 0.3f) return;

    /* Parabolic interpolation for sub-sample accuracy */
    float refined_lag = (float)best_lag;
    if (best_lag > min_lag && best_lag < max_lag - 1 && best_lag < FX_TUNER_WINDOW - 1) {
        float y0 = nsdf[best_lag - 1];
        float y1 = nsdf[best_lag];
        float y2 = nsdf[best_lag + 1];
        float d = 2.0f * y1 - y0 - y2;
        if (fabsf(d) > 1e-10f) {
            float shift = (y0 - y2) / (2.0f * d);
            if (shift > -1.0f && shift < 1.0f) {
                refined_lag += shift;
            }
        }
    }

    t->frequency = sr / refined_lag;

    /* Map to nearest MIDI note */
    float midi = 69.0f + 12.0f * log2f(t->frequency / 440.0f);
    t->midi_note = (int)(midi + 0.5f);
    t->cents = (midi - (float)t->midi_note) * 100.0f;
}

/* ── Public tuner API ─────────────────────────────────────────── */

float fx_tuner_get_frequency(fx_engine_t *engine) {
    return engine ? engine->tuner.frequency : 0.0f;
}

int fx_tuner_get_note(fx_engine_t *engine) {
    return engine ? engine->tuner.midi_note : 0;
}

float fx_tuner_get_cents(fx_engine_t *engine) {
    return engine ? engine->tuner.cents : 0.0f;
}

static const char *note_names[] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

const char *fx_tuner_get_note_name(fx_engine_t *engine) {
    if (!engine || engine->tuner.midi_note < 0) return "---";
    return note_names[engine->tuner.midi_note % 12];
}
