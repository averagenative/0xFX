/*
 * 0xFX — JACK duplex audio (Linux standalone only)
 *
 * Talks to PipeWire's libjack (or a real JACK server) through dlopen, so
 * there is no build-time dependency. One process callback receives the
 * capture buffer and fills the playback buffer in the same graph cycle —
 * no ring buffer between input and output, unlike miniaudio's duplex mode
 * on the PulseAudio backend.
 */
#ifndef FX_JACK_DUPLEX_H
#define FX_JACK_DUPLEX_H

#include <stdbool.h>

#define FX_JACK_MAX_DEVICES   64
#define FX_JACK_NAME_LEN      256
#define FX_JACK_DEVICE_PORTS  2

/* One selectable input or output and the JACK ports it connects to */
typedef struct {
    char name[FX_JACK_NAME_LEN];
    char ports[FX_JACK_DEVICE_PORTS][FX_JACK_NAME_LEN];
    int  num_ports;
} fx_jack_device_t;

/* Called from the JACK process thread — must be real-time safe */
typedef void (*fx_jack_process_fn)(const float *in, float *out, int frames, void *user);

/* Load libjack, open a client (never starts a server) and list devices.
 * Returns false when libjack or a server isn't available. */
bool fx_jack_init(fx_jack_process_fn process, void *user);
void fx_jack_shutdown(void);

unsigned int fx_jack_sample_rate(void);
int          fx_jack_buffer_frames(void);

int                     fx_jack_capture_count(void);
const fx_jack_device_t *fx_jack_capture(int index);
int                     fx_jack_playback_count(void);
const fx_jack_device_t *fx_jack_playback(int index);

/* Activate (if needed), request the buffer size and connect the selected
 * input and output. playback_idx < 0 picks the input's own device when it
 * has outputs, else the first output. Safe to call again while running to
 * switch devices. */
bool fx_jack_start(int capture_idx, int playback_idx, int buffer_frames);
void fx_jack_stop(void);
bool fx_jack_running(void);

/* Ask the server for a new period size; takes effect on the next cycle */
bool fx_jack_set_buffer_frames(int frames);

/* Estimated round trip in ms from the connected ports' reported latency */
float fx_jack_latency_ms(void);

/* Pure helpers (unit tested). ports is a NULL-terminated list of full
 * "client:port" names as returned by jack_get_ports().
 *   capture:  one entry per port, "<client>" or "<client> (In N)"
 *   playback: one entry per client, its first FX_JACK_DEVICE_PORTS ports */
int fx_jack_list_capture(const char **ports, fx_jack_device_t *out, int max);
int fx_jack_list_playback(const char **ports, fx_jack_device_t *out, int max);

/* Output for an input when none was picked: the playback device sharing
 * the most leading words of its client name (PipeWire names one card's
 * input and output by profile, e.g. "iRig HD 2 Mono" / "iRig HD 2 Analog
 * Stereo"), else the first. -1 when there are no outputs. */
int fx_jack_match_playback(const char *capture_port, const fx_jack_device_t *playback, int n);

#endif /* FX_JACK_DUPLEX_H */
