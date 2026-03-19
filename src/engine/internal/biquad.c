/*
 * 0xFX — Biquad filter coefficient calculations
 *
 * Standard RBJ Audio EQ Cookbook formulas.
 * Processing is inline in engine_internal.h (fx_biquad_process).
 */
#include "engine_internal.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void fx_biquad_lowpass(fx_biquad_t *bq, float freq, float q, float sr) {
    float w0 = 2.0f * (float)M_PI * freq / sr;
    float sinw = sinf(w0);
    float cosw = cosf(w0);
    float alpha = sinw / (2.0f * q);
    float a0 = 1.0f + alpha;

    bq->b0 = ((1.0f - cosw) / 2.0f) / a0;
    bq->b1 = (1.0f - cosw) / a0;
    bq->b2 = ((1.0f - cosw) / 2.0f) / a0;
    bq->a1 = (-2.0f * cosw) / a0;
    bq->a2 = (1.0f - alpha) / a0;
}

void fx_biquad_highpass(fx_biquad_t *bq, float freq, float q, float sr) {
    float w0 = 2.0f * (float)M_PI * freq / sr;
    float sinw = sinf(w0);
    float cosw = cosf(w0);
    float alpha = sinw / (2.0f * q);
    float a0 = 1.0f + alpha;

    bq->b0 = ((1.0f + cosw) / 2.0f) / a0;
    bq->b1 = (-(1.0f + cosw)) / a0;
    bq->b2 = ((1.0f + cosw) / 2.0f) / a0;
    bq->a1 = (-2.0f * cosw) / a0;
    bq->a2 = (1.0f - alpha) / a0;
}

void fx_biquad_peak(fx_biquad_t *bq, float freq, float gain_db, float q, float sr) {
    float A = powf(10.0f, gain_db / 40.0f);
    float w0 = 2.0f * (float)M_PI * freq / sr;
    float sinw = sinf(w0);
    float cosw = cosf(w0);
    float alpha = sinw / (2.0f * q);
    float a0 = 1.0f + alpha / A;

    bq->b0 = (1.0f + alpha * A) / a0;
    bq->b1 = (-2.0f * cosw) / a0;
    bq->b2 = (1.0f - alpha * A) / a0;
    bq->a1 = (-2.0f * cosw) / a0;
    bq->a2 = (1.0f - alpha / A) / a0;
}

void fx_biquad_lowshelf(fx_biquad_t *bq, float freq, float gain_db, float sr) {
    float A = powf(10.0f, gain_db / 40.0f);
    float w0 = 2.0f * (float)M_PI * freq / sr;
    float sinw = sinf(w0);
    float cosw = cosf(w0);
    float alpha = sinw / 2.0f * sqrtf((A + 1.0f / A) * (1.0f / 0.707f - 1.0f) + 2.0f);
    float sqA = 2.0f * sqrtf(A) * alpha;
    float a0 = (A + 1.0f) + (A - 1.0f) * cosw + sqA;

    bq->b0 = (A * ((A + 1.0f) - (A - 1.0f) * cosw + sqA)) / a0;
    bq->b1 = (2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosw)) / a0;
    bq->b2 = (A * ((A + 1.0f) - (A - 1.0f) * cosw - sqA)) / a0;
    bq->a1 = (-2.0f * ((A - 1.0f) + (A + 1.0f) * cosw)) / a0;
    bq->a2 = ((A + 1.0f) + (A - 1.0f) * cosw - sqA) / a0;
}

void fx_biquad_highshelf(fx_biquad_t *bq, float freq, float gain_db, float sr) {
    float A = powf(10.0f, gain_db / 40.0f);
    float w0 = 2.0f * (float)M_PI * freq / sr;
    float sinw = sinf(w0);
    float cosw = cosf(w0);
    float alpha = sinw / 2.0f * sqrtf((A + 1.0f / A) * (1.0f / 0.707f - 1.0f) + 2.0f);
    float sqA = 2.0f * sqrtf(A) * alpha;
    float a0 = (A + 1.0f) - (A - 1.0f) * cosw + sqA;

    bq->b0 = (A * ((A + 1.0f) + (A - 1.0f) * cosw + sqA)) / a0;
    bq->b1 = (-2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosw)) / a0;
    bq->b2 = (A * ((A + 1.0f) + (A - 1.0f) * cosw - sqA)) / a0;
    bq->a1 = (2.0f * ((A - 1.0f) - (A + 1.0f) * cosw)) / a0;
    bq->a2 = ((A + 1.0f) - (A - 1.0f) * cosw - sqA) / a0;
}
