#ifndef FX_DEBUG_RECORDER_H
#define FX_DEBUG_RECORDER_H

#include <stdbool.h>

/* Start recording input+output to a stereo WAV file.
 * Left channel = raw input, Right channel = processed output.
 * Returns true on success. */
bool fx_debug_record_start(const char *path, float sample_rate);

/* Feed a block of audio. Call from the audio callback. */
void fx_debug_record_feed(const float *input, const float *output, int frames);

/* Stop recording and finalize the WAV file. */
void fx_debug_record_stop(void);

/* Is recording active? */
bool fx_debug_record_active(void);

#endif
