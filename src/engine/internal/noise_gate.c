/*
 * 0xFX — Noise gate
 *
 * Envelope follower with threshold, attack, release, and hold.
 * Below threshold: signal is silenced (gain → 0).
 * Above threshold: signal passes (gain → 1).
 * Hold prevents gate chatter on decaying signals.
 */
#include "engine_internal.h"

void fx_gate_init(fx_noise_gate_t *gate) {
    memset(gate, 0, sizeof(*gate));
    gate->threshold = -50.0f;  /* dB */
    gate->attack    = 0.001f;  /* 1ms — fast open */
    gate->release   = 0.050f;  /* 50ms — smooth close */
    gate->hold      = 0.010f;  /* 10ms — prevent chatter */
    gate->gain      = 0.0f;    /* start closed */
    gate->envelope  = 0.0f;
}

void fx_gate_process(fx_noise_gate_t *gate, float *buf, int n, float sr) {
    /* Convert threshold from dB to linear */
    float thresh_lin = powf(10.0f, gate->threshold / 20.0f);

    /* Envelope follower coefficients */
    float env_attack  = expf(-1.0f / (0.001f * sr));  /* 1ms for detection */
    float env_release = expf(-1.0f / (0.020f * sr));   /* 20ms for detection */

    /* Gate gain smoothing coefficients */
    float gate_attack  = expf(-1.0f / (gate->attack * sr));
    float gate_release = expf(-1.0f / (gate->release * sr));

    float hold_samples = gate->hold * sr;

    for (int i = 0; i < n; i++) {
        float abs_in = fabsf(buf[i]);

        /* Envelope follower — track signal level */
        if (abs_in > gate->envelope)
            gate->envelope = env_attack * gate->envelope +
                             (1.0f - env_attack) * abs_in;
        else
            gate->envelope = env_release * gate->envelope +
                             (1.0f - env_release) * abs_in;

        /* Gate logic with hold */
        float target;
        if (gate->envelope > thresh_lin) {
            /* Signal above threshold — open gate */
            target = 1.0f;
            gate->hold_counter = hold_samples;
        } else if (gate->hold_counter > 0) {
            /* In hold phase — keep gate open */
            target = 1.0f;
            gate->hold_counter -= 1.0f;
        } else {
            /* Below threshold, hold expired — close gate */
            target = 0.0f;
        }

        /* Smooth gain transitions to avoid clicks */
        if (target > gate->gain)
            gate->gain = gate_attack * gate->gain +
                         (1.0f - gate_attack) * target;
        else
            gate->gain = gate_release * gate->gain +
                         (1.0f - gate_release) * target;

        buf[i] *= gate->gain;
    }
}
