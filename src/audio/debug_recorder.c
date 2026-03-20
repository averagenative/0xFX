/*
 * Debug audio recorder — captures input+output as stereo WAV.
 * Left = raw input, Right = processed output.
 * NOT real-time safe (writes to file) — but audio callback just
 * copies to a ring buffer, and a flush happens on stop.
 */
#include "debug_recorder.h"
#include "../core/log.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Simple ring buffer for lock-free audio capture */
#define RING_SIZE (44100 * 30 * 2)  /* 30 seconds stereo */

static struct {
    FILE  *file;
    float  ring[RING_SIZE];
    int    write_pos;
    int    total_frames;
    float  sample_rate;
    bool   active;
} s_rec = {0};

/* WAV header (44 bytes) */
static void write_wav_header(FILE *f, int num_samples, float sr) {
    int channels = 2;
    int bits = 16;
    int byte_rate = (int)sr * channels * bits / 8;
    int block_align = channels * bits / 8;
    int data_size = num_samples * channels * bits / 8;
    int file_size = 36 + data_size;

    fwrite("RIFF", 1, 4, f);
    fwrite(&file_size, 4, 1, f);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    int fmt_size = 16;
    fwrite(&fmt_size, 4, 1, f);
    short audio_fmt = 1; /* PCM */
    fwrite(&audio_fmt, 2, 1, f);
    short ch = (short)channels;
    fwrite(&ch, 2, 1, f);
    int sr_int = (int)sr;
    fwrite(&sr_int, 4, 1, f);
    fwrite(&byte_rate, 4, 1, f);
    short ba = (short)block_align;
    fwrite(&ba, 2, 1, f);
    short bps = (short)bits;
    fwrite(&bps, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&data_size, 4, 1, f);
}

bool fx_debug_record_start(const char *path, float sample_rate) {
    if (s_rec.active) return false;

    s_rec.file = fopen(path, "wb");
    if (!s_rec.file) {
        FX_ERROR("debug_recorder: cannot open %s", path);
        return false;
    }

    s_rec.write_pos = 0;
    s_rec.total_frames = 0;
    s_rec.sample_rate = sample_rate;
    s_rec.active = true;

    /* Write placeholder header (will be updated on stop) */
    write_wav_header(s_rec.file, 0, sample_rate);

    FX_INFO("debug_recorder: recording to %s", path);
    return true;
}

void fx_debug_record_feed(const float *input, const float *output, int frames) {
    if (!s_rec.active) return;

    /* Interleave input (L) and output (R) into ring buffer */
    for (int i = 0; i < frames; i++) {
        if (s_rec.write_pos + 1 >= RING_SIZE) return; /* buffer full */
        s_rec.ring[s_rec.write_pos++] = input ? input[i] : 0.0f;
        s_rec.ring[s_rec.write_pos++] = output ? output[i] : 0.0f;
    }
    s_rec.total_frames += frames;
}

void fx_debug_record_stop(void) {
    if (!s_rec.active) return;
    s_rec.active = false;

    /* Write samples as 16-bit PCM */
    for (int i = 0; i < s_rec.write_pos; i++) {
        float s = s_rec.ring[i];
        if (s > 1.0f) s = 1.0f;
        if (s < -1.0f) s = -1.0f;
        short pcm = (short)(s * 32767.0f);
        fwrite(&pcm, 2, 1, s_rec.file);
    }

    /* Rewrite header with correct size */
    fseek(s_rec.file, 0, SEEK_SET);
    write_wav_header(s_rec.file, s_rec.total_frames, s_rec.sample_rate);

    fclose(s_rec.file);
    s_rec.file = NULL;

    FX_INFO("debug_recorder: saved %d frames (%.1f sec)",
            s_rec.total_frames, (float)s_rec.total_frames / s_rec.sample_rate);
}

bool fx_debug_record_active(void) {
    return s_rec.active;
}
