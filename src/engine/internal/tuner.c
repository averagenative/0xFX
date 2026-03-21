/*
 * 0xFX — Chromatic tuner
 *
 * NSDF (Normalized Square Difference Function) pitch detection
 * with parabolic interpolation for sub-sample accuracy.
 * Designed for guitar: detects fundamentals from ~60Hz (drop D) to ~1200Hz.
 */
#include "engine_internal.h"

#define TUNER_BUF_SIZE 4096
#define TUNER_UPDATE_INTERVAL 2048  /* ~21Hz update rate at 44.1kHz */

void fx_tuner_init(fx_tuner_state_t *t) {
    memset(t, 0, sizeof(*t));
}

void fx_tuner_feed(fx_tuner_state_t *t, const float *buf, int n, float sr) {
    /* Accumulate input samples into ring buffer */
    for (int i = 0; i < n; i++) {
        t->buffer[t->write_pos] = buf[i];
        t->write_pos = (t->write_pos + 1) % TUNER_BUF_SIZE;
    }

    t->samples_since_update += n;
    if (t->samples_since_update < TUNER_UPDATE_INTERVAL) return;
    t->samples_since_update = 0;

    /* Check signal level — skip if too quiet */
    int window = TUNER_BUF_SIZE / 2;
    int start = (t->write_pos + TUNER_BUF_SIZE - window) % TUNER_BUF_SIZE;

    float rms = 0.0f;
    for (int j = 0; j < window; j++) {
        float s = t->buffer[(start + j) % TUNER_BUF_SIZE];
        rms += s * s;
    }
    rms = sqrtf(rms / (float)window);
    if (rms < 0.005f) {
        /* Signal too weak — don't update, let display show last reading */
        return;
    }

    /* NSDF: search for fundamental between ~55Hz (A1) and ~1200Hz (D6) */
    int min_lag = (int)(sr / 1200.0f);
    int max_lag = (int)(sr / 55.0f);
    if (max_lag > window) max_lag = window;
    if (min_lag < 2) min_lag = 2;

    /* Compute NSDF values */
    float nsdf[2048]; /* max_lag can't exceed window (2048) */
    for (int lag = min_lag; lag < max_lag && lag < 2048; lag++) {
        float acf = 0.0f;
        float energy_a = 0.0f;
        float energy_b = 0.0f;
        int len = window - lag;

        for (int j = 0; j < len; j++) {
            float a = t->buffer[(start + j) % TUNER_BUF_SIZE];
            float b = t->buffer[(start + j + lag) % TUNER_BUF_SIZE];
            acf += a * b;
            energy_a += a * a;
            energy_b += b * b;
        }

        float denom = energy_a + energy_b;
        nsdf[lag] = (denom > 1e-10f) ? (2.0f * acf / denom) : 0.0f;
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

    for (int lag = min_lag; lag < max_lag && lag < 2048; lag++) {
        float v = nsdf[lag];
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
    if (best_lag > min_lag && best_lag < max_lag - 1 && best_lag < 2047) {
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
