/*
 * 0xFX — Chromatic tuner
 *
 * Autocorrelation-based pitch detection.
 * Runs continuously, reads input before effects processing.
 */
#include "engine_internal.h"

#define TUNER_BUF_SIZE 4096
#define TUNER_UPDATE_INTERVAL 1470  /* ~30Hz at 44.1kHz */

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

    /* Autocorrelation pitch detection */
    /* Search for fundamental period between ~30Hz and ~4kHz */
    int min_lag = (int)(sr / 4000.0f);  /* highest freq */
    int max_lag = (int)(sr / 30.0f);    /* lowest freq */
    if (max_lag > TUNER_BUF_SIZE / 2) max_lag = TUNER_BUF_SIZE / 2;
    if (min_lag < 1) min_lag = 1;

    /* Compute autocorrelation, find first peak after dip */
    float best_corr = 0.0f;
    int best_lag = 0;
    float prev_corr = 1.0f;
    bool found_dip = false;

    int start = (t->write_pos + TUNER_BUF_SIZE - TUNER_BUF_SIZE / 2) % TUNER_BUF_SIZE;

    /* Compute NSDF (Normalized Square Difference Function) for robust pitch detection */
    int window = TUNER_BUF_SIZE / 2;

    for (int lag = min_lag; lag < max_lag; lag++) {
        float acf = 0.0f;   /* autocorrelation */
        float sdf_a = 0.0f; /* energy of window */
        float sdf_b = 0.0f; /* energy of lagged window */

        for (int j = 0; j < window - lag; j++) {
            int idx_a = (start + j) % TUNER_BUF_SIZE;
            int idx_b = (start + j + lag) % TUNER_BUF_SIZE;
            float a = t->buffer[idx_a];
            float b = t->buffer[idx_b];
            acf += a * b;
            sdf_a += a * a;
            sdf_b += b * b;
        }

        float denom = sdf_a + sdf_b;
        float nsdf = (denom > 1e-8f) ? (2.0f * acf / denom) : 0.0f;

        if (nsdf < prev_corr && !found_dip && prev_corr > 0.0f) {
            found_dip = true;
        }
        if (found_dip && nsdf > best_corr) {
            best_corr = nsdf;
            best_lag = lag;
            /* Once we find a strong peak, stop searching */
            if (best_corr > 0.8f) break;
        }
        prev_corr = nsdf;
    }

    if (best_lag > 0 && best_corr > 0.5f) {
        t->frequency = sr / (float)best_lag;

        /* Map to nearest MIDI note */
        float midi = 69.0f + 12.0f * log2f(t->frequency / 440.0f);
        t->midi_note = (int)(midi + 0.5f);
        t->cents = (midi - (float)t->midi_note) * 100.0f;
    }
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
