/*
 * 0xFX — Audio device manager (standalone only)
 */
#ifndef FX_AUDIO_DEVICE_H
#define FX_AUDIO_DEVICE_H

#include <stdbool.h>

struct fx_engine;

bool fx_audio_init(void);
void fx_audio_shutdown(void);

/* Input devices (capture — guitar interface) */
int         fx_audio_get_device_count(void);
const char *fx_audio_get_device_name(int index);
bool        fx_audio_set_device(struct fx_engine *engine, int index);

/* Output devices (playback — speakers/headphones/monitors) */
int         fx_audio_get_output_count(void);
const char *fx_audio_get_output_name(int index);
void        fx_audio_set_output(int index);

bool        fx_audio_set_buffer_size(struct fx_engine *engine, int frames);
bool        fx_audio_set_sample_rate(struct fx_engine *engine, float rate);

#endif /* FX_AUDIO_DEVICE_H */
