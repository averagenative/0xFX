/*
 * 0xFX — Audio device manager
 *
 * Handles device enumeration, selection, and hot-plug via miniaudio.
 * Standalone only — in plugin mode, the host provides audio I/O.
 */
#define MINIAUDIO_IMPLEMENTATION
#include "../../deps/miniaudio.h"
#include "../engine/fx_engine.h"
#include "../core/log.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __linux__
#include "jack_duplex.h"
#endif

/* ── Forward decls ────────────────────────────────────────────── */

static bool open_audio_device(fx_engine_t *engine);

/* ── State ────────────────────────────────────────────────────── */

#define MAX_DEVICES 64

typedef struct {
    ma_context   context;
    ma_device    device;
    bool         context_init;
    bool         device_init;
    fx_engine_t *engine;

    /* Cached device info */
    ma_device_info playback_devices[MAX_DEVICES];
    ma_device_info capture_devices[MAX_DEVICES];
    int            num_playback;
    int            num_capture;

    /* Device name lists for the public API */
    char           capture_names[MAX_DEVICES][256];
    char           playback_names[MAX_DEVICES][256];

    /* Selected indices */
    int            selected_capture;
    int            selected_playback;

    /* Settings */
    int            buffer_frames;
    float          sample_rate;

    /* JACK (PipeWire) duplex path instead of miniaudio — Linux only */
    bool           use_jack;
} audio_manager_t;

static audio_manager_t g_audio = {0};

/* ── Audio callback ───────────────────────────────────────────── */

static bool s_mute_output = false;

void fx_audio_set_mute_output(bool mute) {
    s_mute_output = mute;
}

/* Input gain trim — standalone only.
 * Written by the GUI thread, read by the audio callback thread.
 * volatile float: aligned float store/load is naturally atomic on x86 and ARM
 * (guaranteed single-instruction). volatile prevents the compiler from caching
 * the value in a register across the callback boundary. This is a user-control
 * update path (GUI knob at ~60 Hz), not parameter automation — volatile is
 * sufficient and C99-compatible. No locks, no mallocs.
 *
 * s_input_gain_linear is the precomputed effective multiplier combining both
 * the dB slider and the pad toggle. The callback reads only this one value. */
static volatile float s_input_gain_linear = 1.0f;  /* default: 0 dB, no pad */

/* Backing state for the setter helpers — GUI thread only. */
static float s_input_gain_db_val = 0.0f;
static int   s_input_pad_val     = 0;

static void update_input_gain(void) {
    float linear = powf(10.0f, s_input_gain_db_val / 20.0f);
    if (s_input_pad_val) linear *= 0.1f;  /* -20 dB pad: 10^(-20/20) = 0.1 */
    s_input_gain_linear = linear;          /* single float write, naturally atomic */
}

void fx_audio_set_input_gain_db(float gain_db) {
    if (gain_db < -24.0f) gain_db = -24.0f;
    if (gain_db >  12.0f) gain_db =  12.0f;
    s_input_gain_db_val = gain_db;
    update_input_gain();
}

void fx_audio_set_input_pad(bool pad_enabled) {
    s_input_pad_val = pad_enabled ? 1 : 0;
    update_input_gain();
}

/* Largest block handed to the engine at once. Backends can deliver more
 * (a PipeWire quantum can reach 8192), so bigger buffers are split. */
#define ENGINE_CHUNK_FRAMES 1024

/* Shared by the miniaudio and JACK callbacks. Mono in, mono out. */
static void process_block(audio_manager_t *mgr, const float *input,
                          float *output, int frames) {
    if (!mgr || !mgr->engine) {
        memset(output, 0, (size_t)frames * sizeof(float));
        return;
    }

    for (int pos = 0; pos < frames; pos += ENGINE_CHUNK_FRAMES) {
        int n = frames - pos < ENGINE_CHUNK_FRAMES ? frames - pos : ENGINE_CHUNK_FRAMES;
        const float *in = input + pos;
        float *out = output + pos;

        /* Apply input gain trim (set by GUI, real-time safe volatile read).
         * We need a mutable copy of the input samples — use the output buffer as
         * scratch before the engine overwrites it with processed audio. */
        float gain = s_input_gain_linear;
        const float *engine_input = in;
        if (gain != 1.0f) {
            for (int i = 0; i < n; i++) {
                out[i] = in[i] * gain;
            }
            engine_input = out;
        }

        fx_engine_process(mgr->engine, engine_input, out, n);
    }

    /* Monitor mode: mute output but engine still processes (tuner + meters) */
    if (s_mute_output) {
        memset(output, 0, (size_t)frames * sizeof(float));
    }

    /* Audio recorder: capture processed output */
    {
        extern void fx_recorder_feed(const float *, int);
        fx_recorder_feed((const float *)output, frames);
    }
}

static void audio_callback(ma_device *device, void *output,
                           const void *input, ma_uint32 frame_count) {
    process_block((audio_manager_t *)device->pUserData, (const float *)input,
                  (float *)output, (int)frame_count);
}

#ifdef __linux__
static void jack_callback(const float *in, float *out, int frames, void *user) {
    process_block((audio_manager_t *)user, in, out, frames);
}
#endif

/* ── Device enumeration ───────────────────────────────────────── */

/*
 * On PipeWire (via PulseAudio compatibility shim), pa_context_get_source_info_list
 * returns both real hardware sources AND "monitor" sources — software loopbacks of
 * the playback sinks. Monitor sources have ".monitor" appended to their pulse ID and
 * "Monitor of" in their human-readable description. They are useless as guitar inputs
 * and appear first, hiding actual hardware interfaces (iRig, Scarlett, etc.) at higher
 * indices. This filter drops them.
 */
static bool is_monitor_source(const ma_device_info *info) {
    /*
     * On PulseAudio backend (including PipeWire's PulseAudio compatibility shim),
     * monitor sources have ".monitor" at the end of their pulse source name ID.
     * Check id.pulse directly — this is only called when backend == pulseaudio.
     */
    const char *id = info->id.pulse;
    size_t id_len = strlen(id);
    const char monitor_suffix[] = ".monitor";
    const size_t sfx_len = sizeof(monitor_suffix) - 1;
    if (id_len >= sfx_len &&
        strcmp(id + id_len - sfx_len, monitor_suffix) == 0) {
        return true;
    }
    /* Belt-and-suspenders: description starts with "Monitor of" */
    if (strncmp(info->name, "Monitor of", 10) == 0) {
        return true;
    }
    return false;
}

static void enumerate_devices(void) {
    if (!g_audio.context_init) return;

    /* Log which backend miniaudio selected */
    FX_INFO("Audio backend: %s", ma_get_backend_name(g_audio.context.backend));

    ma_device_info *playback_infos;
    ma_uint32 playback_count;
    ma_device_info *capture_infos;
    ma_uint32 capture_count;

    if (ma_context_get_devices(&g_audio.context,
                               &playback_infos, &playback_count,
                               &capture_infos, &capture_count) != MA_SUCCESS) {
        FX_ERROR("ma_context_get_devices failed — no devices will be available");
        return;
    }

    FX_DEBUG("Raw device counts from miniaudio: %u capture, %u playback",
             capture_count, playback_count);

    /* Store capture devices (guitar input interfaces).
     * On the PulseAudio backend (used by PipeWire's compatibility shim on Fedora/Ubuntu),
     * pa_context_get_source_info_list returns both real hardware sources AND "monitor"
     * sources — software loopbacks of the output sinks. Monitor sources are useless as
     * guitar inputs and appear first in the list, hiding the actual interface (iRig,
     * Scarlett, etc.) at higher indices. Filter them when using PulseAudio backend. */
    const bool filter_monitors = (g_audio.context.backend == ma_backend_pulseaudio);
    g_audio.num_capture = 0;
    for (ma_uint32 i = 0; i < capture_count && g_audio.num_capture < MAX_DEVICES; i++) {
        if (filter_monitors && is_monitor_source(&capture_infos[i])) {
            FX_DEBUG("Skipping PulseAudio monitor source: \"%s\" (id: %s)",
                     capture_infos[i].name, capture_infos[i].id.pulse);
            continue;
        }
        g_audio.capture_devices[g_audio.num_capture] = capture_infos[i];
        snprintf(g_audio.capture_names[g_audio.num_capture], 256,
                 "%s", capture_infos[i].name);
        FX_DEBUG("  Capture [%d]: \"%s\"%s",
                 g_audio.num_capture,
                 g_audio.capture_names[g_audio.num_capture],
                 capture_infos[i].isDefault ? " [default]" : "");
        g_audio.num_capture++;
    }

    /* Store playback devices (speakers, headphones, monitors) */
    g_audio.num_playback = (int)(playback_count < MAX_DEVICES ? playback_count : MAX_DEVICES);
    for (int i = 0; i < g_audio.num_playback; i++) {
        g_audio.playback_devices[i] = playback_infos[i];
        snprintf(g_audio.playback_names[i], 256, "%s", playback_infos[i].name);
        FX_DEBUG("  Playback [%d]: \"%s\"%s",
                 i, g_audio.playback_names[i],
                 playback_infos[i].isDefault ? " [default]" : "");
    }

    if (g_audio.num_capture == 0) {
        FX_WARN("No hardware capture devices found after filtering monitors. "
                "Connect an audio interface (iRig, Scarlett, etc.) and check that "
                "pipewire-pulseaudio (or pulseaudio) is running.");
    }
}

/* ── Public API ───────────────────────────────────────────────── */

/* ── Input device API ─────────────────────────────────────────── */

int fx_audio_get_device_count(void) {
    return g_audio.num_capture;
}

const char *fx_audio_get_device_name(int index) {
    if (index < 0 || index >= g_audio.num_capture) return NULL;
    return g_audio.capture_names[index];
}

/* ── Output device API ────────────────────────────────────────── */

int fx_audio_get_output_count(void) {
    return g_audio.num_playback;
}

const char *fx_audio_get_output_name(int index) {
    if (index < 0 || index >= g_audio.num_playback) return NULL;
    return g_audio.playback_names[index];
}

void fx_audio_set_output(int index) {
    if (index < 0 || index >= g_audio.num_playback) return;
    if (g_audio.selected_playback == index) return;
    g_audio.selected_playback = index;
    /* If a device is currently running, reopen it on the new output so the
     * switch takes effect immediately. No-op before the first open. */
    if (g_audio.device_init && g_audio.engine) {
        open_audio_device(g_audio.engine);
    }
}

/* ── Open duplex device (input + output) ──────────────────────── */

static bool open_audio_device(fx_engine_t *engine) {
#ifdef __linux__
    if (g_audio.use_jack) {
        /* Rewires a running client in place — no stop/start gap */
        g_audio.engine = engine;
        g_audio.device_init = fx_jack_start(g_audio.selected_capture,
                                            g_audio.selected_playback,
                                            g_audio.buffer_frames);
        return g_audio.device_init;
    }
#endif
    /* Stop existing device if running */
    if (g_audio.device_init) {
        ma_device_uninit(&g_audio.device);
        g_audio.device_init = false;
    }

    g_audio.engine = engine;

    int cap_idx = g_audio.selected_capture;
    int play_idx = g_audio.selected_playback;

    if (cap_idx < 0 || cap_idx >= g_audio.num_capture) return false;

    ma_device_config config = ma_device_config_init(ma_device_type_duplex);
    config.capture.pDeviceID  = &g_audio.capture_devices[cap_idx].id;
    config.capture.format     = ma_format_f32;
    config.capture.channels   = 1;
    /* Set output device (if valid index, otherwise system default) */
    if (play_idx >= 0 && play_idx < g_audio.num_playback) {
        config.playback.pDeviceID = &g_audio.playback_devices[play_idx].id;
    }
    config.playback.format    = ma_format_f32;
    config.playback.channels  = 1;
    config.sampleRate         = (ma_uint32)g_audio.sample_rate;
    config.periodSizeInFrames = (ma_uint32)g_audio.buffer_frames;
    config.dataCallback       = audio_callback;
    config.pUserData          = &g_audio;

    if (ma_device_init(&g_audio.context, &config, &g_audio.device) != MA_SUCCESS) {
        FX_ERROR("Failed to init audio device: in=%s out=%s",
                 g_audio.capture_names[cap_idx],
                 play_idx >= 0 ? g_audio.playback_names[play_idx] : "(default)");
        return false;
    }

    g_audio.device_init = true;

    if (ma_device_start(&g_audio.device) != MA_SUCCESS) {
        FX_ERROR("Failed to start audio device");
        ma_device_uninit(&g_audio.device);
        g_audio.device_init = false;
        return false;
    }

    /* Log what the backend actually granted — the requested period size is a
     * hint, and a device rate that differs from ours means a resampler (and
     * its latency) sits in the path. */
    FX_INFO("Audio started: in=%s out=%s (%.0f Hz, %d frames requested)",
            g_audio.capture_names[cap_idx],
            play_idx >= 0 ? g_audio.playback_names[play_idx] : "(default)",
            g_audio.sample_rate, g_audio.buffer_frames);
    FX_INFO("  %s: capture %u Hz x %u frames x %u periods, playback %u Hz x %u frames x %u periods",
            ma_get_backend_name(g_audio.context.backend),
            g_audio.device.capture.internalSampleRate,
            g_audio.device.capture.internalPeriodSizeInFrames,
            g_audio.device.capture.internalPeriods,
            g_audio.device.playback.internalSampleRate,
            g_audio.device.playback.internalPeriodSizeInFrames,
            g_audio.device.playback.internalPeriods);
    return true;
}

bool fx_audio_set_device(fx_engine_t *engine, int index) {
    if (index < 0 || index >= g_audio.num_capture) return false;
    g_audio.selected_capture = index;
    return open_audio_device(engine);
}

bool fx_audio_set_buffer_size(fx_engine_t *engine, int frames) {
    (void)engine;
    if (frames < 32 || frames > 4096) return false;
    if (g_audio.buffer_frames == frames) return true;
    g_audio.buffer_frames = frames;
#ifdef __linux__
    /* JACK changes the period on the fly */
    if (g_audio.use_jack) {
        return !g_audio.device_init || fx_jack_set_buffer_frames(frames);
    }
#endif
    /* Period size is fixed at device init — reopen a running device so the
     * new size takes effect now. No-op before the first open. */
    if (g_audio.device_init && g_audio.engine) {
        return open_audio_device(g_audio.engine);
    }
    return true;
}

/* Must match the rate the engine was created with — the engine's DSP is
 * tuned to it. Call before opening the device; not meant for live changes. */
bool fx_audio_set_sample_rate(fx_engine_t *engine, float rate) {
    (void)engine;
    if (rate < 22050.0f || rate > 192000.0f) return false;
    /* The JACK graph sets the rate — only a matching request succeeds */
    if (g_audio.use_jack) return fabsf(rate - g_audio.sample_rate) < 0.5f;
    g_audio.sample_rate = rate;
    return true;
}

float fx_audio_get_native_rate(void) {
    return g_audio.use_jack ? g_audio.sample_rate : 0.0f;
}

const char *fx_audio_get_backend_name(void) {
    if (g_audio.use_jack) return "JACK / PipeWire";
    return g_audio.context_init ? ma_get_backend_name(g_audio.context.backend) : "none";
}

int fx_audio_get_actual_buffer(void) {
#ifdef __linux__
    if (g_audio.use_jack) return g_audio.device_init ? fx_jack_buffer_frames() : 0;
#endif
    return g_audio.device_init ? (int)g_audio.device.playback.internalPeriodSizeInFrames : 0;
}

float fx_audio_get_latency_ms(void) {
#ifdef __linux__
    if (g_audio.use_jack) return fx_jack_latency_ms();
#endif
    return 0.0f;
}

/* ── Init / shutdown (called from standalone main) ────────────── */

#ifdef __linux__
/* Low-latency path: a JACK server — PipeWire provides one — runs capture,
 * the engine and playback in one graph cycle. FX_AUDIO_BACKEND=miniaudio
 * forces the PulseAudio/ALSA path. */
static bool init_jack(void) {
    const char *forced = getenv("FX_AUDIO_BACKEND");
    if (forced && strcmp(forced, "miniaudio") == 0) {
        FX_INFO("FX_AUDIO_BACKEND=miniaudio — skipping JACK");
        return false;
    }
    if (!fx_jack_init(jack_callback, &g_audio)) return false;

    g_audio.use_jack      = true;
    g_audio.sample_rate   = (float)fx_jack_sample_rate();
    g_audio.num_capture   = fx_jack_capture_count();
    g_audio.num_playback  = fx_jack_playback_count();
    for (int i = 0; i < g_audio.num_capture; i++)
        snprintf(g_audio.capture_names[i], 256, "%s", fx_jack_capture(i)->name);
    for (int i = 0; i < g_audio.num_playback; i++)
        snprintf(g_audio.playback_names[i], 256, "%s", fx_jack_playback(i)->name);
    g_audio.selected_playback = -1;
    return true;
}
#endif

bool fx_audio_init(void) {
    /* Defaults only on first init — Stop Audio re-inits and must keep the
     * user's buffer size and the rate the engine was created at */
    if (g_audio.buffer_frames == 0) g_audio.buffer_frames = 256;
    if (g_audio.sample_rate == 0.0f) g_audio.sample_rate = 44100.0f;
#ifdef __linux__
    if (init_jack()) {
        FX_INFO("Audio initialized (JACK, %.0f Hz). Found %d input, %d output device(s)",
                g_audio.sample_rate, g_audio.num_capture, g_audio.num_playback);
        for (int i = 0; i < g_audio.num_capture; i++)
            FX_INFO("  Input  [%d]: %s", i, g_audio.capture_names[i]);
        for (int i = 0; i < g_audio.num_playback; i++)
            FX_INFO("  Output [%d]: %s", i, g_audio.playback_names[i]);
        return true;
    }
#endif
    if (ma_context_init(NULL, 0, NULL, &g_audio.context) != MA_SUCCESS) {
        FX_ERROR("Failed to init audio context — no audio backend available");
        return false;
    }
    g_audio.context_init = true;

    enumerate_devices();

    FX_INFO("Audio initialized (%s). Found %d input, %d output device(s)",
            ma_get_backend_name(g_audio.context.backend),
            g_audio.num_capture, g_audio.num_playback);
    for (int i = 0; i < g_audio.num_capture; i++) {
        FX_INFO("  Input  [%d]: %s", i, g_audio.capture_names[i]);
    }
    for (int i = 0; i < g_audio.num_playback; i++) {
        FX_INFO("  Output [%d]: %s", i, g_audio.playback_names[i]);
    }

    return true;
}

void fx_audio_shutdown(void) {
#ifdef __linux__
    if (g_audio.use_jack) fx_jack_shutdown();
#endif
    if (g_audio.device_init && !g_audio.use_jack) {
        ma_device_uninit(&g_audio.device);
        g_audio.device_init = false;
    }
    if (g_audio.context_init) {
        ma_context_uninit(&g_audio.context);
        g_audio.context_init = false;
    }
    const int   buffer_frames = g_audio.buffer_frames;
    const float sample_rate   = g_audio.sample_rate;
    memset(&g_audio, 0, sizeof(g_audio));
    g_audio.buffer_frames = buffer_frames;
    g_audio.sample_rate   = sample_rate;
}
