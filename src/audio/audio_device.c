/*
 * 0xFX — Audio device manager
 *
 * Handles device enumeration, selection, and hot-plug via miniaudio.
 * Standalone only — in plugin mode, the host provides audio I/O.
 */
#define MINIAUDIO_IMPLEMENTATION
#include "../../deps/miniaudio.h"
#include "../engine/fx_engine.h"
#include <stdio.h>
#include <string.h>

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
} audio_manager_t;

static audio_manager_t g_audio = {0};

/* ── Audio callback ───────────────────────────────────────────── */

static void audio_callback(ma_device *device, void *output,
                           const void *input, ma_uint32 frame_count) {
    audio_manager_t *mgr = (audio_manager_t *)device->pUserData;
    if (!mgr || !mgr->engine) {
        /* Silence output if no engine */
        memset(output, 0, frame_count * sizeof(float));
        return;
    }

    /* Mono input → engine → mono output */
    fx_engine_process(mgr->engine, (const float *)input,
                      (float *)output, (int)frame_count);

    /* Audio recorder: capture processed output */
    {
        extern void fx_recorder_feed(const float *, int);
        fx_recorder_feed((const float *)output, (int)frame_count);
    }
}

/* ── Device enumeration ───────────────────────────────────────── */

static void enumerate_devices(void) {
    if (!g_audio.context_init) return;

    ma_device_info *playback_infos;
    ma_uint32 playback_count;
    ma_device_info *capture_infos;
    ma_uint32 capture_count;

    if (ma_context_get_devices(&g_audio.context,
                               &playback_infos, &playback_count,
                               &capture_infos, &capture_count) != MA_SUCCESS) {
        return;
    }

    /* Store capture devices (guitar input interfaces) */
    g_audio.num_capture = (int)(capture_count < MAX_DEVICES ? capture_count : MAX_DEVICES);
    for (int i = 0; i < g_audio.num_capture; i++) {
        g_audio.capture_devices[i] = capture_infos[i];
        snprintf(g_audio.capture_names[i], 256, "%s", capture_infos[i].name);
    }

    /* Store playback devices (speakers, headphones, monitors) */
    g_audio.num_playback = (int)(playback_count < MAX_DEVICES ? playback_count : MAX_DEVICES);
    for (int i = 0; i < g_audio.num_playback; i++) {
        g_audio.playback_devices[i] = playback_infos[i];
        snprintf(g_audio.playback_names[i], 256, "%s", playback_infos[i].name);
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
    if (index >= 0 && index < g_audio.num_playback) {
        g_audio.selected_playback = index;
    }
}

/* ── Open duplex device (input + output) ──────────────────────── */

static bool open_audio_device(fx_engine_t *engine) {
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
        fprintf(stderr, "[0xFX] Failed to init audio device: in=%s out=%s\n",
                g_audio.capture_names[cap_idx],
                play_idx >= 0 ? g_audio.playback_names[play_idx] : "(default)");
        return false;
    }

    g_audio.device_init = true;

    if (ma_device_start(&g_audio.device) != MA_SUCCESS) {
        fprintf(stderr, "[0xFX] Failed to start audio device\n");
        ma_device_uninit(&g_audio.device);
        g_audio.device_init = false;
        return false;
    }

    printf("[0xFX] Audio started: in=%s out=%s (%.0f Hz, %d frames)\n",
           g_audio.capture_names[cap_idx],
           play_idx >= 0 ? g_audio.playback_names[play_idx] : "(default)",
           g_audio.sample_rate, g_audio.buffer_frames);
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
    g_audio.buffer_frames = frames;
    return true;
}

bool fx_audio_set_sample_rate(fx_engine_t *engine, float rate) {
    (void)engine;
    if (rate < 22050.0f || rate > 192000.0f) return false;
    g_audio.sample_rate = rate;
    return true;
}

/* ── Init / shutdown (called from standalone main) ────────────── */

bool fx_audio_init(void) {
    if (ma_context_init(NULL, 0, NULL, &g_audio.context) != MA_SUCCESS) {
        fprintf(stderr, "[0xFX] Failed to init audio context\n");
        return false;
    }
    g_audio.context_init = true;
    g_audio.sample_rate = 44100.0f;
    g_audio.buffer_frames = 256;

    enumerate_devices();

    printf("[0xFX] Audio initialized. Found %d input, %d output device(s):\n",
           g_audio.num_capture, g_audio.num_playback);
    printf("  Input devices:\n");
    for (int i = 0; i < g_audio.num_capture; i++) {
        printf("    [%d] %s\n", i, g_audio.capture_names[i]);
    }
    printf("  Output devices:\n");
    for (int i = 0; i < g_audio.num_playback; i++) {
        printf("    [%d] %s\n", i, g_audio.playback_names[i]);
    }

    return true;
}

void fx_audio_shutdown(void) {
    if (g_audio.device_init) {
        ma_device_uninit(&g_audio.device);
        g_audio.device_init = false;
    }
    if (g_audio.context_init) {
        ma_context_uninit(&g_audio.context);
        g_audio.context_init = false;
    }
    memset(&g_audio, 0, sizeof(g_audio));
}
