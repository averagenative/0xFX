/*
 * recorder.h — Audio recording to WAV, MP3, or FLAC.
 *
 * Records the processed guitar output using a lock-free ring buffer.
 * Audio callback only writes to the ring buffer (RT-safe).
 * Disk I/O happens in fx_recorder_stop().
 */

#ifndef FX_RECORDER_H
#define FX_RECORDER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FX_RECORD_WAV_16 = 0,
    FX_RECORD_WAV_24,
    FX_RECORD_MP3_192,
    FX_RECORD_MP3_320,
    FX_RECORD_FLAC_16,
    FX_RECORD_FLAC_24,
    FX_RECORD_FORMAT_COUNT
} fx_record_format_t;

/* Start recording. Allocates ring buffer, sets active flag.
 * path: output file path (extension doesn't matter — format determines encoding).
 * format: desired output format.
 * sample_rate: audio sample rate (e.g. 44100 or 48000).
 * Returns true on success. */
bool fx_recorder_start(const char *path, fx_record_format_t format, float sample_rate);

/* Feed processed audio into the ring buffer.
 * Called from the audio callback — RT-safe (no alloc, no I/O).
 * output: mono float samples from the engine.
 * frames: number of samples. */
void fx_recorder_feed(const float *output, int frames);

/* Stop recording, encode, and write to disk.
 * All disk I/O happens here (not in the audio callback). */
void fx_recorder_stop(void);

/* Is recording currently active? */
bool fx_recorder_active(void);

/* Duration of audio recorded so far, in seconds. */
float fx_recorder_duration(void);

/* Human-readable name for a format enum value. */
const char *fx_recorder_format_name(fx_record_format_t fmt);

#ifdef __cplusplus
}
#endif

#endif /* FX_RECORDER_H */
