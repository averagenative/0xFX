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

#ifdef _WIN32
#include <windows.h>
static void cab_brief_wait(void) { Sleep(20); }
#else
#include <time.h>
static void cab_brief_wait(void) {
    struct timespec ts = { 0, 20 * 1000 * 1000 };  /* 20 ms */
    nanosleep(&ts, NULL);
}
#endif

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

    /* Load via fx_cab_load_buffer which handles thread-safe swap */
    bool ok = fx_cab_load_buffer(cab, ir_data, ir_len, block_size);

    /* Cleanup temp data */
    free(ir_mono);
    drwav_free(ir_samples, NULL);

    return ok;
}

/* ── Load IR from raw float buffer ───────────────────────────── */

bool fx_cab_load_buffer(fx_cab_state_t *cab, const float *ir_data, int ir_len, int block_size) {
    if (!cab || !ir_data || ir_len <= 0 || block_size <= 0) return false;

    if (ir_len > 4096) ir_len = 4096;  /* cap IR length */

    /* Compute FFT size: next power of 2 >= block_size + ir_len - 1 */
    int fft_size = next_power_of_2(block_size + ir_len - 1);
    int n_bins = fft_size / 2 + 1;

    /* ── Step 1: Allocate and prepare ALL new state before touching cab ── */
    kiss_fft_cpx *new_ir_fft  = (kiss_fft_cpx *)calloc((size_t)n_bins, sizeof(kiss_fft_cpx));
    kiss_fft_cpx *new_fft_buf = (kiss_fft_cpx *)calloc((size_t)n_bins, sizeof(kiss_fft_cpx));
    float        *new_overlap = (float *)calloc((size_t)fft_size, sizeof(float));
    float        *new_time    = (float *)calloc((size_t)fft_size, sizeof(float));
    kiss_fftr_cfg new_fft_cfg = kiss_fftr_alloc(fft_size, 0, NULL, NULL);
    kiss_fftr_cfg new_ifft    = kiss_fftr_alloc(fft_size, 1, NULL, NULL);

    if (!new_ir_fft || !new_fft_buf || !new_overlap || !new_time ||
        !new_fft_cfg || !new_ifft) {
        free(new_ir_fft); free(new_fft_buf); free(new_overlap); free(new_time);
        if (new_fft_cfg) kiss_fftr_free(new_fft_cfg);
        if (new_ifft)    kiss_fftr_free(new_ifft);
        return false;
    }

    /* Zero-pad IR and compute its FFT (using new buffers, cab untouched).
     * Peak-normalize to 0.4 so bundled/user WAVs sit at the same output
     * level as synthetic cabs (which also peak-normalize to 0.4). */
    float peak = 0.0f;
    for (int i = 0; i < ir_len; i++) {
        float v = fabsf(ir_data[i]);
        if (v > peak) peak = v;
    }
    float ir_gain = (peak > 1e-6f) ? (0.4f / peak) : 1.0f;
    for (int i = 0; i < ir_len; i++) new_time[i] = ir_data[i] * ir_gain;
    /* rest is already zeroed from calloc */
    kiss_fftr(new_fft_cfg, new_time, new_ir_fft);

    /* ── Step 2: Disable cab processing (audio thread will pass through) ── */
    cab->loaded = false;

    /* Brief wait so any in-flight fx_cab_process() call finishes with the
     * old pointers before we swap or free them. Without this, rapid cab
     * swaps (e.g. mouse-wheel scrolling the cab list) can double-free or
     * corrupt the heap. 20 ms covers any reasonable audio block. */
    cab_brief_wait();

    /* ── Step 3: Save old pointers for deferred free ── */
    kiss_fft_cpx  *old_ir_fft  = cab->ir_fft;
    kiss_fft_cpx  *old_fft_buf = cab->fft_buf;
    float         *old_overlap = cab->overlap_buf;
    float         *old_time    = cab->time_buf;
    kiss_fftr_cfg  old_fft_cfg = cab->fft_cfg;
    kiss_fftr_cfg  old_ifft    = cab->ifft_cfg;

    /* ── Step 4: Swap in fully-prepared new state ── */
    cab->ir_fft      = new_ir_fft;
    cab->fft_buf     = new_fft_buf;
    cab->overlap_buf = new_overlap;
    cab->time_buf    = new_time;
    cab->fft_cfg     = new_fft_cfg;
    cab->ifft_cfg    = new_ifft;
    cab->fft_size    = fft_size;
    cab->ir_len      = ir_len;
    cab->block_size  = block_size;
    cab->bypass      = false;

    /* ── Step 5: Re-enable processing — new state is complete ── */
    cab->loaded = true;

    /* ── Step 6: Free old buffers (audio thread now uses new ones) ── */
    free(old_ir_fft);
    free(old_fft_buf);
    free(old_overlap);
    free(old_time);
    if (old_fft_cfg) kiss_fftr_free(old_fft_cfg);
    if (old_ifft)    kiss_fftr_free(old_ifft);

    return true;
}

/* ── Synthetic IR generation ────────────────────────────────── */

/*
 * Approach:
 * 1. Build a frequency-domain magnitude response:
 *    - Speaker resonant lowpass based on Fs
 *    - Cabinet: open-back adds comb filtering from rear path
 *    - Microphone: SM57-style presence bump ~5kHz, roll-off >10kHz
 * 2. Minimum-phase reconstruction via log-magnitude -> Hilbert -> exp
 * 3. IFFT to time domain, window to 2048 samples
 */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SYNTH_IR_LEN   2048
#define SYNTH_FFT_SIZE 4096  /* must be >= 2 * SYNTH_IR_LEN */

void fx_cab_synth_ir_generate(const fx_cab_params_t *params, float *ir_out, int ir_len,
                               float sample_rate) {
    int fft_size = SYNTH_FFT_SIZE;
    int n_bins = fft_size / 2 + 1;

    /* Allocate scratch for frequency domain work */
    float *mag = (float *)calloc((size_t)n_bins, sizeof(float));
    float *phase = (float *)calloc((size_t)n_bins, sizeof(float));
    kiss_fft_cpx *spectrum = (kiss_fft_cpx *)calloc((size_t)n_bins, sizeof(kiss_fft_cpx));
    float *time_buf = (float *)calloc((size_t)fft_size, sizeof(float));
    kiss_fftr_cfg ifft_cfg = kiss_fftr_alloc(fft_size, 1, NULL, NULL);

    if (!mag || !phase || !spectrum || !time_buf || !ifft_cfg) {
        free(mag); free(phase); free(spectrum); free(time_buf);
        if (ifft_cfg) kiss_fftr_free(ifft_cfg);
        /* Output a unit impulse as fallback */
        memset(ir_out, 0, sizeof(float) * (size_t)ir_len);
        ir_out[0] = 1.0f;
        return;
    }

    float fs = params->speaker_fs;
    if (fs < 30.0f)  fs = 80.0f;
    if (fs > 200.0f) fs = 200.0f;

    float brightness = params->brightness;
    if (brightness < 0.0f) brightness = 0.5f;
    if (brightness > 1.0f) brightness = 1.0f;

    float resonance = params->resonance;
    if (resonance < 0.0f) resonance = 0.5f;
    if (resonance > 1.0f) resonance = 1.0f;

    /* ── Step 1: Build magnitude response ──────────────────── */

    for (int i = 0; i < n_bins; i++) {
        float freq = (float)i * sample_rate / (float)fft_size;
        float m = 1.0f;

        /* Speaker: 2nd-order resonant lowpass at Fs
         * H(f) = 1 / sqrt(1 + (f/fc)^4) with resonant bump */
        {
            float fc = fs * (1.5f + brightness * 2.0f);  /* cutoff: 90-360Hz range */
            float ratio = freq / fc;
            float ratio2 = ratio * ratio;
            /* Resonant bump near Fs */
            float q = 0.5f + resonance * 3.0f;  /* Q: 0.5 to 3.5 */
            float denom = (1.0f - ratio2) * (1.0f - ratio2) + ratio2 / (q * q);
            if (denom < 0.001f) denom = 0.001f;
            m *= 1.0f / sqrtf(denom);
        }

        /* Speaker high-frequency rolloff (12dB/oct above speaker bandwidth) */
        {
            float hf_cutoff = 4000.0f + brightness * 4000.0f;  /* 4-8kHz */
            if (freq > hf_cutoff) {
                float ratio = freq / hf_cutoff;
                m *= 1.0f / (ratio * ratio);
            }
        }

        /* Cabinet type modeling */
        switch (params->cab_type) {
        case FX_CAB_1X12_OPEN: {
            /* Open back: comb filter from rear wave path (~0.3ms delay) */
            float delay_sec = 0.0003f;
            float comb_freq = freq * delay_sec * 2.0f * (float)M_PI;
            float comb = 1.0f - 0.3f * cosf(comb_freq);
            m *= comb;
            /* Open back has less bass reinforcement */
            if (freq < 120.0f) {
                m *= 0.6f + 0.4f * (freq / 120.0f);
            }
            break;
        }
        case FX_CAB_2X12_CLOSED: {
            /* Closed back: tighter bass, slight mid focus */
            if (freq < 80.0f) {
                m *= 0.8f + 0.2f * (freq / 80.0f);
            }
            /* Mid focus bump around 800Hz */
            float mid_ratio = (freq - 800.0f) / 400.0f;
            m *= 1.0f + 0.15f * expf(-mid_ratio * mid_ratio);
            break;
        }
        case FX_CAB_4X12_STRAIGHT: {
            /* 4x12 straight: full bass, classic mid scoop */
            if (freq < 60.0f) {
                m *= 0.9f + 0.1f * (freq / 60.0f);
            }
            /* Slight mid scoop around 400Hz */
            float mid_ratio = (freq - 400.0f) / 200.0f;
            m *= 1.0f - 0.1f * expf(-mid_ratio * mid_ratio);
            /* Presence bump around 2.5kHz */
            float pres_ratio = (freq - 2500.0f) / 800.0f;
            m *= 1.0f + 0.2f * expf(-pres_ratio * pres_ratio);
            break;
        }
        case FX_CAB_4X12_SLANT: {
            /* 4x12 slant: like straight but brighter top end */
            if (freq < 60.0f) {
                m *= 0.9f + 0.1f * (freq / 60.0f);
            }
            /* Broader presence bump */
            float pres_ratio = (freq - 3500.0f) / 1200.0f;
            m *= 1.0f + 0.25f * expf(-pres_ratio * pres_ratio);
            break;
        }
        default:
            /* Unknown cab type: leave magnitude unchanged */
            break;
        }

        /* Microphone modeling: SM57-style */
        switch (params->mic_pos) {
        case FX_MIC_ON_AXIS: {
            /* Presence bump ~5kHz, brighter overall */
            float pres_ratio = (freq - 5000.0f) / 1500.0f;
            m *= 1.0f + 0.3f * expf(-pres_ratio * pres_ratio);
            /* Roll-off above 12kHz */
            if (freq > 12000.0f) {
                float ratio = freq / 12000.0f;
                m *= 1.0f / (ratio * ratio);
            }
            break;
        }
        case FX_MIC_OFF_AXIS: {
            /* Darker, smoother — reduced presence */
            float pres_ratio = (freq - 5000.0f) / 2000.0f;
            m *= 1.0f - 0.15f * expf(-pres_ratio * pres_ratio);
            /* Earlier roll-off above 8kHz */
            if (freq > 8000.0f) {
                float ratio = freq / 8000.0f;
                m *= 1.0f / (ratio * ratio);
            }
            break;
        }
        case FX_MIC_EDGE:
        default: {
            /* Edge of cone: scooped mids, less presence */
            float mid_ratio = (freq - 2000.0f) / 1000.0f;
            m *= 1.0f - 0.2f * expf(-mid_ratio * mid_ratio);
            /* Roll-off above 10kHz */
            if (freq > 10000.0f) {
                float ratio = freq / 10000.0f;
                m *= 1.0f / (ratio * ratio);
            }
            break;
        }
        }

        /* Clamp magnitude to avoid zeros (for log) */
        if (m < 1e-6f) m = 1e-6f;
        mag[i] = m;
    }

    /* Find magnitude at 200 Hz (guitar fundamental range) for normalization.
     * Using a fixed reference frequency keeps all cab types level-matched
     * instead of each cab normalizing by its own peak (which varies). */
    int ref_bin = (int)(200.0f * (float)fft_size / sample_rate);
    if (ref_bin < 1) ref_bin = 1;
    if (ref_bin >= n_bins) ref_bin = n_bins - 1;
    float mag_ref = mag[ref_bin];

    /* ── Step 2: Minimum-phase reconstruction ──────────────── */
    /* Phase = -Hilbert(log(|H(f)|))
     * Simplified: use the cepstral method.
     * log_mag -> IFFT -> causal window -> FFT -> imag part = min phase */
    {
        /* Compute log-magnitude */
        float *log_mag = (float *)calloc((size_t)n_bins, sizeof(float));
        if (!log_mag) {
            /* Fallback: linear phase (just use magnitude, zero phase) */
            for (int i = 0; i < n_bins; i++) phase[i] = 0.0f;
        } else {
            for (int i = 0; i < n_bins; i++) {
                log_mag[i] = logf(mag[i]);
            }

            /* Build symmetric log-magnitude spectrum for real IFFT */
            kiss_fft_cpx *log_spec = (kiss_fft_cpx *)calloc((size_t)n_bins, sizeof(kiss_fft_cpx));
            float *cepstrum = (float *)calloc((size_t)fft_size, sizeof(float));
            kiss_fftr_cfg cep_ifft = kiss_fftr_alloc(fft_size, 1, NULL, NULL);
            kiss_fftr_cfg cep_fft  = kiss_fftr_alloc(fft_size, 0, NULL, NULL);

            if (log_spec && cepstrum && cep_ifft && cep_fft) {
                /* log-mag as real part, zero imaginary */
                for (int i = 0; i < n_bins; i++) {
                    log_spec[i].r = log_mag[i];
                    log_spec[i].i = 0.0f;
                }

                /* IFFT to get cepstrum */
                kiss_fftri(cep_ifft, log_spec, cepstrum);

                /* Scale by 1/N */
                float scale = 1.0f / (float)fft_size;
                for (int i = 0; i < fft_size; i++) {
                    cepstrum[i] *= scale;
                }

                /* Apply causal window: keep n=0, double n=1..N/2-1, zero n=N/2..N-1 */
                /* cepstrum[0] stays as is */
                for (int i = 1; i < fft_size / 2; i++) {
                    cepstrum[i] *= 2.0f;
                }
                /* cepstrum[fft_size/2] stays as is (Nyquist) */
                for (int i = fft_size / 2 + 1; i < fft_size; i++) {
                    cepstrum[i] = 0.0f;
                }

                /* FFT back: result has log-mag in real, min-phase in imag */
                kiss_fftr(cep_fft, cepstrum, log_spec);

                /* Extract phase from imaginary part */
                for (int i = 0; i < n_bins; i++) {
                    phase[i] = log_spec[i].i;
                }
            } else {
                for (int i = 0; i < n_bins; i++) phase[i] = 0.0f;
            }

            free(log_spec);
            free(cepstrum);
            if (cep_ifft) kiss_fftr_free(cep_ifft);
            if (cep_fft)  kiss_fftr_free(cep_fft);
            free(log_mag);
        }
    }

    /* ── Step 3: Reconstruct complex spectrum and IFFT ─────── */
    for (int i = 0; i < n_bins; i++) {
        spectrum[i].r = mag[i] * cosf(phase[i]);
        spectrum[i].i = mag[i] * sinf(phase[i]);
    }

    kiss_fftri(ifft_cfg, spectrum, time_buf);

    /* Scale by 1/N */
    float scale = 1.0f / (float)fft_size;
    for (int i = 0; i < fft_size; i++) {
        time_buf[i] *= scale;
    }

    /* ── Step 4: Window to ir_len samples ──────────────────── */
    /* Apply half-Hann fade-out over last 256 samples */
    int fade_len = 256;
    if (fade_len > ir_len / 2) fade_len = ir_len / 2;

    /* Peak normalization happens in fx_cab_load_buffer so synthetic and
     * WAV-loaded IRs land at the same level. Here we just apply the
     * fade-out window and pass the raw time-domain IR through. */
    (void)mag_ref;

    for (int i = 0; i < ir_len; i++) {
        float w = 1.0f;
        if (i >= ir_len - fade_len) {
            /* Half-Hann window for fade-out */
            float t = (float)(i - (ir_len - fade_len)) / (float)fade_len;
            w = 0.5f * (1.0f + cosf((float)M_PI * t));
        }
        ir_out[i] = time_buf[i] * w;
    }

    free(mag);
    free(phase);
    free(spectrum);
    free(time_buf);
    kiss_fftr_free(ifft_cfg);
}

/* ── Bundled preset IRs ─────────────────────────────────────── */

static const fx_cab_params_t bundled_presets[4] = {
    /* 0: 1x12 open back — bright, chimey */
    { FX_CAB_1X12_OPEN,    FX_MIC_ON_AXIS,  75.0f, 0.8f, 0.6f },
    /* 1: 2x12 closed — tighter, more focused */
    { FX_CAB_2X12_CLOSED,  FX_MIC_ON_AXIS,  80.0f, 0.5f, 0.5f },
    /* 2: 4x12 straight — classic rock, full */
    { FX_CAB_4X12_STRAIGHT, FX_MIC_ON_AXIS, 70.0f, 0.5f, 0.6f },
    /* 3: 4x12 slant — slightly brighter top */
    { FX_CAB_4X12_SLANT,   FX_MIC_ON_AXIS,  70.0f, 0.65f, 0.5f },
};

bool fx_cab_load_bundled(fx_cab_state_t *cab, int preset_idx, int block_size) {
    if (!cab || preset_idx < 0 || preset_idx >= 4 || block_size <= 0)
        return false;

    float ir_buf[SYNTH_IR_LEN];
    fx_cab_synth_ir_generate(&bundled_presets[preset_idx], ir_buf, SYNTH_IR_LEN, 48000.0f);
    return fx_cab_load_buffer(cab, ir_buf, SYNTH_IR_LEN, block_size);
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
