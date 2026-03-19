/*
 * 0xFX — Standalone application entry point
 *
 * Phase 1: Console app with audio passthrough.
 * Phase 3+: ImGui GUI window.
 */
#include "../engine/fx_engine.h"
#include "../audio/audio_device.h"
#include "log.h"
#include "crash.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

#ifdef _WIN32
#include <windows.h>
#define SLEEP_MS(ms) Sleep(ms)
#else
#include <unistd.h>
#define SLEEP_MS(ms) usleep((ms) * 1000)
#endif

static volatile int g_running = 1;

static void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    fx_log_init(NULL);
    fx_crash_init();

    printf("╔══════════════════════════════════════╗\n");
    printf("║  0xFX — Guitar Amp Sim & Pedalboard  ║\n");
    printf("║  Phase 1: Audio Passthrough           ║\n");
    printf("╚══════════════════════════════════════╝\n\n");

    FX_INFO("0xFX standalone starting up");

    signal(SIGINT, signal_handler);

    /* Initialize audio device manager */
    if (!fx_audio_init()) {
        FX_ERROR("Failed to initialize audio system");
        fx_log_shutdown();
        return 1;
    }

    int num_devices = fx_audio_get_device_count();
    if (num_devices == 0) {
        FX_ERROR("No audio input devices found. Connect an audio interface (iRig, Scarlett, etc.) and try again.");
        fx_audio_shutdown();
        fx_log_shutdown();
        return 1;
    }

    /* Create engine */
    fx_engine_t *engine = fx_engine_create(44100.0f);
    if (!engine) {
        FX_ERROR("Failed to create engine");
        fx_audio_shutdown();
        fx_log_shutdown();
        return 1;
    }

    FX_INFO("Engine created. Amp: %s",
            fx_amp_get_type_name(fx_amp_get_model(engine, FX_CHAIN_DEFAULT)));

    printf("\nEngine created. Amp: %s\n",
           fx_amp_get_type_name(fx_amp_get_model(engine, FX_CHAIN_DEFAULT)));

    /* Select first available input device */
    int device_index = 0;
    if (argc > 1) {
        device_index = atoi(argv[1]);
        if (device_index < 0 || device_index >= num_devices) {
            FX_WARN("Invalid device index %d, using 0", device_index);
            device_index = 0;
        }
    }

    FX_INFO("Selecting audio device [%d]: %s",
            device_index, fx_audio_get_device_name(device_index));

    printf("Selecting device [%d]: %s\n",
           device_index, fx_audio_get_device_name(device_index));

    if (!fx_audio_set_device(engine, device_index)) {
        FX_ERROR("Failed to open audio device");
        fx_engine_destroy(engine);
        fx_audio_shutdown();
        fx_log_shutdown();
        return 1;
    }

    printf("\n[Playing — press Ctrl+C to quit]\n");
    printf("Audio is passing through (no effects yet — Phase 1).\n");

    /* Main loop — just wait until Ctrl+C */
    while (g_running) {
        SLEEP_MS(100);

        /* Print tuner info periodically */
        float freq = fx_tuner_get_frequency(engine);
        if (freq > 20.0f) {
            printf("\r  Tuner: %s  %.1f Hz  %+.0f cents    ",
                   fx_tuner_get_note_name(engine),
                   freq,
                   fx_tuner_get_cents(engine));
            fflush(stdout);
        }
    }

    printf("\n\nShutting down...\n");
    FX_INFO("Shutting down");
    fx_audio_shutdown();
    fx_engine_destroy(engine);
    printf("Done.\n");
    fx_log_shutdown();
    return 0;
}
