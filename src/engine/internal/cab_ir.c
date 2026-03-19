/*
 * 0xFX — Cabinet IR convolution
 *
 * Overlap-add FFT convolution via KissFFT.
 * - Loads .wav IR via dr_wav (16/24/32-bit, mono, 44.1/48kHz)
 * - Pre-computes IR FFT at load time
 * - Per-block: FFT input -> complex multiply with IR FFT -> IFFT -> overlap-add
 * - All allocations happen at load time; process path is real-time safe
 */
#include "engine_internal.h"

#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"

/* ── Helpers ─────────────────────────────────────────────────── */

static int next_power_of_2(int n) {
    int p = 1;
    while (p < n) p <<= 1;
    return p;
}

/* ── Init / Free ─────────────────────────────────────────────── */

void fx_cab_init(fx_cab_state_t *cab) {
    memset(cab, 0, sizeof(*cab));
}

void fx_cab_free(fx_cab_state_t *cab) {
    if (!cab) return;
    free(cab->ir_fft);
    free(cab->overlap_buf);
    free(cab->fft_buf);
    free(cab->time_buf);
    if (cab->fft_cfg)  kiss_fftr_free(cab->fft_cfg);
    if (cab->ifft_cfg) kiss_fftr_free(cab->ifft_cfg);
    memset(cab, 0, sizeof(*cab));
}

/* ── Load IR from WAV file ───────────────────────────────────── */

bool fx_cab_load_wav(fx_cab_state_t *cab, const char *wav_path, int block_size) {
    if (!cab || !wav_path || block_size <= 0) return false;

    /* Free any previous IR */
    fx_cab_free(cab);

    /* Load WAV file — dr_wav handles 16/24/32-bit PCM and IEEE float,
     * converting everything to float32 for us */
    unsigned int channels = 0;
    unsigned int sample_rate = 0;
    drwav_uint64 total_frames = 0;
    float *ir_samples = drwav_open_file_and_read_pcm_frames_f32(
        wav_path, &channels, &sample_rate, &total_frames, NULL);

    if (!ir_samples || total_frames == 0) {
        if (ir_samples) drwav_free(ir_samples, NULL);
        return false;
    }

    /* Validate: mono only, 44.1 or 48kHz */
    if (channels < 1 || (sample_rate != 44100 && sample_rate != 48000)) {
        drwav_free(ir_samples, NULL);
        return false;
    }

    /* If stereo+, downmix to mono */
    int ir_len = (int)total_frames;
    if (ir_len > 4096) ir_len = 4096;  /* cap IR length */

    float *ir_mono = NULL;
    if (channels > 1) {
        ir_mono = (float *)malloc(sizeof(float) * (size_t)ir_len);
        if (!ir_mono) {
            drwav_free(ir_samples, NULL);
            return false;
        }
        for (int i = 0; i < ir_len; i++) {
            float sum = 0.0f;
            for (unsigned int c = 0; c < channels; c++) {
                sum += ir_samples[i * channels + c];
            }
            ir_mono[i] = sum / (float)channels;
        }
    }

    const float *ir_data = ir_mono ? ir_mono : ir_samples;

    /* Compute FFT size: next power of 2 >= block_size + ir_len - 1 */
    int fft_size = next_power_of_2(block_size + ir_len - 1);
    int n_bins = fft_size / 2 + 1;

    /* Allocate all buffers */
    kiss_fft_cpx *ir_fft     = (kiss_fft_cpx *)calloc((size_t)n_bins, sizeof(kiss_fft_cpx));
    kiss_fft_cpx *fft_buf    = (kiss_fft_cpx *)calloc((size_t)n_bins, sizeof(kiss_fft_cpx));
    float        *overlap    = (float *)calloc((size_t)fft_size, sizeof(float));
    float        *time_buf   = (float *)calloc((size_t)fft_size, sizeof(float));
    kiss_fftr_cfg fft_cfg    = kiss_fftr_alloc(fft_size, 0, NULL, NULL);
    kiss_fftr_cfg ifft_cfg   = kiss_fftr_alloc(fft_size, 1, NULL, NULL);

    if (!ir_fft || !fft_buf || !overlap || !time_buf || !fft_cfg || !ifft_cfg) {
        free(ir_fft); free(fft_buf); free(overlap); free(time_buf);
        if (fft_cfg)  kiss_fftr_free(fft_cfg);
        if (ifft_cfg) kiss_fftr_free(ifft_cfg);
        free(ir_mono);
        drwav_free(ir_samples, NULL);
        return false;
    }

    /* Zero-pad IR and compute its FFT */
    memset(time_buf, 0, sizeof(float) * (size_t)fft_size);
    memcpy(time_buf, ir_data, sizeof(float) * (size_t)ir_len);
    kiss_fftr(fft_cfg, time_buf, ir_fft);

    /* Store state */
    cab->ir_fft     = ir_fft;
    cab->fft_buf    = fft_buf;
    cab->overlap_buf = overlap;
    cab->time_buf   = time_buf;
    cab->fft_cfg    = fft_cfg;
    cab->ifft_cfg   = ifft_cfg;
    cab->fft_size   = fft_size;
    cab->ir_len     = ir_len;
    cab->block_size = block_size;
    cab->loaded     = true;
    cab->bypass     = false;

    /* Cleanup temp data */
    free(ir_mono);
    drwav_free(ir_samples, NULL);

    return true;
}

/* ── Per-block overlap-add convolution (real-time safe) ───────── */

void fx_cab_process(fx_cab_state_t *cab, float *buf, int n) {
    if (!cab || !cab->loaded || cab->bypass) return;
    if (n <= 0 || n > cab->block_size) return;

    int fft_size = cab->fft_size;
    int n_bins = fft_size / 2 + 1;
    float scale = 1.0f / (float)fft_size;

    /* 1. Zero-pad input into time_buf */
    memcpy(cab->time_buf, buf, sizeof(float) * (size_t)n);
    memset(cab->time_buf + n, 0, sizeof(float) * (size_t)(fft_size - n));

    /* 2. Forward FFT of input block */
    kiss_fftr(cab->fft_cfg, cab->time_buf, cab->fft_buf);

    /* 3. Complex multiply: fft_buf *= ir_fft */
    for (int i = 0; i < n_bins; i++) {
        float re = cab->fft_buf[i].r * cab->ir_fft[i].r
                 - cab->fft_buf[i].i * cab->ir_fft[i].i;
        float im = cab->fft_buf[i].r * cab->ir_fft[i].i
                 + cab->fft_buf[i].i * cab->ir_fft[i].r;
        cab->fft_buf[i].r = re;
        cab->fft_buf[i].i = im;
    }

    /* 4. Inverse FFT */
    kiss_fftri(cab->ifft_cfg, cab->fft_buf, cab->time_buf);

    /* 5. Overlap-add: add previous tail, then store new tail */
    for (int i = 0; i < n; i++) {
        buf[i] = cab->time_buf[i] * scale + cab->overlap_buf[i];
    }

    /* Store the tail (samples n..fft_size-1) for next block */
    int tail_len = fft_size - n;
    for (int i = 0; i < tail_len; i++) {
        cab->overlap_buf[i] = cab->time_buf[n + i] * scale;
    }
    /* Zero the rest of overlap in case block size changes */
    for (int i = tail_len; i < fft_size; i++) {
        cab->overlap_buf[i] = 0.0f;
    }
}
