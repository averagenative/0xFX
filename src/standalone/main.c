/*
 * 0xFX — Standalone application entry point
 *
 * Phase 1: Console app with audio passthrough.
 * Phase 3+: ImGui GUI window.
 */
#include "../engine/fx_engine.h"
#include "../audio/audio_device.h"
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

    printf("╔══════════════════════════════════════╗\n");
    printf("║  0xFX — Guitar Amp Sim & Pedalboard  ║\n");
    printf("║  Phase 1: Audio Passthrough           ║\n");
    printf("╚══════════════════════════════════════╝\n\n");

    signal(SIGINT, signal_handler);

    /* Initialize audio device manager */
    if (!fx_audio_init()) {
        fprintf(stderr, "Failed to initialize audio system.\n");
        return 1;
    }

    int num_devices = fx_audio_get_device_count();
    if (num_devices == 0) {
        fprintf(stderr, "No audio input devices found.\n");
        fprintf(stderr, "Connect an audio interface (iRig, Scarlett, etc.) and try again.\n");
        fx_audio_shutdown();
        return 1;
    }

    /* Create engine */
    fx_engine_t *engine = fx_engine_create(44100.0f);
    if (!engine) {
        fprintf(stderr, "Failed to create engine.\n");
        fx_audio_shutdown();
        return 1;
    }

    printf("\nEngine created. Amp: %s\n",
           fx_amp_get_type_name(fx_amp_get_model(engine, FX_CHAIN_DEFAULT)));

    /* Select first available input device */
    int device_index = 0;
    if (argc > 1) {
        device_index = atoi(argv[1]);
        if (device_index < 0 || device_index >= num_devices) {
            fprintf(stderr, "Invalid device index %d. Using 0.\n", device_index);
            device_index = 0;
        }
    }

    printf("Selecting device [%d]: %s\n",
           device_index, fx_audio_get_device_name(device_index));

    if (!fx_audio_set_device(engine, device_index)) {
        fprintf(stderr, "Failed to open audio device.\n");
        fx_engine_destroy(engine);
        fx_audio_shutdown();
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
    fx_audio_shutdown();
    fx_engine_destroy(engine);
    printf("Done.\n");
    return 0;
}
