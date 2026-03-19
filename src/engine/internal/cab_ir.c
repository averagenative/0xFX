/*
 * 0xFX — Cabinet IR convolution
 *
 * Phase 1: stub
 * Phase 4: overlap-add FFT convolution via KissFFT
 */
#include "engine_internal.h"

void fx_cab_init(fx_cab_state_t *cab) {
    memset(cab, 0, sizeof(*cab));
}

void fx_cab_free(fx_cab_state_t *cab) {
    if (!cab) return;
    free(cab->ir_fft_re);
    free(cab->ir_fft_im);
    free(cab->overlap_buf);
    cab->ir_fft_re = NULL;
    cab->ir_fft_im = NULL;
    cab->overlap_buf = NULL;
    cab->loaded = false;
}

void fx_cab_process(fx_cab_state_t *cab, float *buf, int n) {
    if (!cab || !cab->loaded || cab->bypass) return;
    (void)buf; (void)n;
    /* TODO Phase 4: FFT input → multiply with IR FFT → IFFT → overlap-add */
}
