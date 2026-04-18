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

/* Monitor mode: process engine (for tuner/meters) but mute output */
void        fx_audio_set_mute_output(bool mute);

/* Input gain trim (standalone only — DAW hosts provide their own channel gain).
 * gain_db: continuous, -24.0 to +12.0 dB, default 0.0 dB.
 * pad:     fixed -20 dB attenuation applied on top of gain_db when true.
 * Both are applied in the capture callback before samples reach fx_engine_process.
 * Real-time safe: reads an _Atomic float — no malloc, no lock. */
void        fx_audio_set_input_gain_db(float gain_db);
void        fx_audio_set_input_pad(bool pad_enabled);

#endif /* FX_AUDIO_DEVICE_H */
