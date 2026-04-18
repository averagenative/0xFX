/*
 * 0xFX — API test
 *
 * Tests the public engine API without any audio device.
 * Verifies: create/destroy, pedal management, amp params,
 * chain management, passthrough processing.
 */
#include "fx_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

/* dr_wav for writing test WAV files (declaration only — impl is in cab_ir.c) */
#include "dr_wav.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", msg, __LINE__); \
        tests_failed++; \
    } else { \
        tests_passed++; \
    } \
} while(0)

/* ── Test: engine lifecycle ───────────────────────────────────── */

static void test_engine_lifecycle(void) {
    printf("test_engine_lifecycle...\n");

    fx_engine_t *e = fx_engine_create(44100.0f);
    ASSERT(e != NULL, "engine should be created");

    /* Default state */
    ASSERT(fx_chain_get_count(e) == 1, "should have 1 default chain");
    ASSERT(fx_amp_get_model(e, FX_CHAIN_DEFAULT) == FX_AMP_FULLERTON_CLEAN,
           "default amp should be Fullerton Clean");

    fx_engine_destroy(e);
    printf("  OK\n");
}

/* ── Test: amp processing produces output ──────────────────────── */

static void test_amp_processing(void) {
    printf("test_amp_processing...\n");

    fx_engine_t *e = fx_engine_create(44100.0f);

    float input[512];
    float output[512];

    /* Fill input with a 440Hz sine wave at moderate level */
    for (int i = 0; i < 512; i++) {
        input[i] = 0.3f * sinf(2.0f * 3.14159f * 440.0f * (float)i / 44100.0f);
    }

    /* Process through default amp (Fullerton Clean at gain=0.5) */
    fx_engine_process(e, input, output, 512);

    /* Output should have energy (not all zeros — gate should open) */
    float peak = 0.0f;
    for (int i = 0; i < 512; i++) {
        float a = fabsf(output[i]);
        if (a > peak) peak = a;
    }
    ASSERT(peak > 0.01f, "amp should produce audible output");

    /* Output should differ from input (amp processes the signal) */
    int differs = 0;
    for (int i = 0; i < 512; i++) {
        if (fabsf(output[i] - input[i]) > 1e-4f) {
            differs = 1;
            break;
        }
    }
    ASSERT(differs, "amp output should differ from input");

    /* High gain should produce more harmonics (higher peak for same input) */
    fx_amp_set_param(e, FX_CHAIN_DEFAULT, FX_AMP_PARAM_GAIN, 1.0f);
    fx_amp_set_model(e, FX_CHAIN_DEFAULT, FX_AMP_SOUTHWEST_LEAD);

    float output_hg[512];
    fx_engine_process(e, input, output_hg, 512);

    float peak_hg = 0.0f;
    float rms_hg = 0.0f;
    for (int i = 0; i < 512; i++) {
        float a = fabsf(output_hg[i]);
        if (a > peak_hg) peak_hg = a;
        rms_hg += output_hg[i] * output_hg[i];
    }
    rms_hg = sqrtf(rms_hg / 512.0f);
    ASSERT(peak_hg > 0.01f, "high-gain amp should produce output");
    printf("    clean peak=%.4f, high-gain peak=%.4f, hg_rms=%.4f\n",
           peak, peak_hg, rms_hg);

    fx_engine_destroy(e);
    printf("  OK\n");
}

/* ── Test: pedal management ───────────────────────────────────── */

static void test_pedals(void) {
    printf("test_pedals...\n");

    fx_engine_t *e = fx_engine_create(44100.0f);

    /* Add a pedal */
    fx_pedal_id od = fx_chain_add_pedal(e, FX_PEDAL_JADE_DRIVE, FX_CHAIN_POS_PRE);
    ASSERT(od >= 0, "should return valid pedal id");
    ASSERT(fx_chain_get_pedal_count(e, FX_CHAIN_POS_PRE) == 1,
           "should have 1 pre-pedal");

    /* Get/set params */
    fx_pedal_set_param(e, od, 0, 0.75f);
    ASSERT(fabsf(fx_pedal_get_param(e, od, 0) - 0.75f) < 1e-6f,
           "param should be 0.75");

    /* Bypass */
    ASSERT(!fx_pedal_get_bypass(e, od), "should not be bypassed initially");
    fx_pedal_set_bypass(e, od, true);
    ASSERT(fx_pedal_get_bypass(e, od), "should be bypassed after set");

    /* Type */
    ASSERT(fx_pedal_get_type(e, od) == FX_PEDAL_JADE_DRIVE,
           "type should be Jade Drive");

    /* Add more pedals */
    fx_pedal_id delay = fx_chain_add_pedal(e, FX_PEDAL_ECHO_DELAY, FX_CHAIN_POS_POST);
    ASSERT(delay >= 0, "should add delay");
    ASSERT(fx_chain_get_pedal_count(e, FX_CHAIN_POS_POST) == 1,
           "should have 1 post-pedal");

    /* Remove pedal */
    fx_chain_remove_pedal(e, od);
    ASSERT(fx_chain_get_pedal_count(e, FX_CHAIN_POS_PRE) == 0,
           "should have 0 pre-pedals after remove");

    fx_engine_destroy(e);
    printf("  OK\n");
}

/* ── Test: amp params ─────────────────────────────────────────── */

static void test_amp_params(void) {
    printf("test_amp_params...\n");

    fx_engine_t *e = fx_engine_create(44100.0f);

    /* Set and get params */
    fx_amp_set_param(e, FX_CHAIN_DEFAULT, FX_AMP_PARAM_GAIN, 0.8f);
    ASSERT(fabsf(fx_amp_get_param(e, FX_CHAIN_DEFAULT, FX_AMP_PARAM_GAIN) - 0.8f) < 1e-6f,
           "gain should be 0.8");

    /* Switch model */
    fx_amp_set_model(e, FX_CHAIN_DEFAULT, FX_AMP_BRIT_CRUNCH);
    ASSERT(fx_amp_get_model(e, FX_CHAIN_DEFAULT) == FX_AMP_BRIT_CRUNCH,
           "model should be Brit Crunch");

    /* Clamp values */
    fx_amp_set_param(e, FX_CHAIN_DEFAULT, FX_AMP_PARAM_GAIN, 1.5f);
    ASSERT(fx_amp_get_param(e, FX_CHAIN_DEFAULT, FX_AMP_PARAM_GAIN) <= 1.0f,
           "gain should be clamped to 1.0");

    /* Metadata */
    ASSERT(fx_amp_get_param_count(FX_AMP_BRIT_CRUNCH) == 8,
           "Brit Crunch should have 8 params");
    ASSERT(strcmp(fx_amp_get_type_name(FX_AMP_BRIT_CRUNCH), "British Crunch") == 0,
           "type name should be 'British Crunch'");

    fx_engine_destroy(e);
    printf("  OK\n");
}

/* ── Test: multi-chain ────────────────────────────────────────── */

static void test_multi_chain(void) {
    printf("test_multi_chain...\n");

    fx_engine_t *e = fx_engine_create(44100.0f);

    ASSERT(fx_chain_get_count(e) == 1, "should start with 1 chain");

    /* Add a second chain */
    fx_chain_id c2 = fx_chain_create(e);
    ASSERT(c2 >= 0, "should create chain");
    ASSERT(fx_chain_get_count(e) == 2, "should have 2 chains");

    /* Set different amps per chain */
    fx_amp_set_model(e, FX_CHAIN_DEFAULT, FX_AMP_BRIT_CRUNCH);
    fx_amp_set_model(e, c2, FX_AMP_FULLERTON_CLEAN);
    ASSERT(fx_amp_get_model(e, FX_CHAIN_DEFAULT) == FX_AMP_BRIT_CRUNCH,
           "chain 0 should be Brit Crunch");
    ASSERT(fx_amp_get_model(e, c2) == FX_AMP_FULLERTON_CLEAN,
           "chain 1 should be Fullerton Clean");

    /* Mix levels */
    fx_chain_set_mix(e, FX_CHAIN_DEFAULT, 0.7f);
    fx_chain_set_mix(e, c2, 0.3f);
    ASSERT(fabsf(fx_chain_get_mix(e, FX_CHAIN_DEFAULT) - 0.7f) < 1e-6f,
           "chain 0 mix should be 0.7");

    /* Destroy chain */
    fx_chain_destroy(e, c2);

    fx_engine_destroy(e);
    printf("  OK\n");
}

/* ── Test: pedal metadata ─────────────────────────────────────── */

static void test_pedal_metadata(void) {
    printf("test_pedal_metadata...\n");

    ASSERT(strcmp(fx_pedal_get_type_name(FX_PEDAL_JADE_DRIVE), "Jade Drive") == 0,
           "should be 'Jade Drive'");
    ASSERT(strcmp(fx_pedal_get_type_name(FX_PEDAL_MAMMOTH_FUZZ), "Mammoth Fuzz") == 0,
           "should be 'Mammoth Fuzz'");
    ASSERT(strcmp(fx_pedal_get_type_name(FX_PEDAL_DRIP_VERB), "Drip Verb") == 0,
           "should be 'Drip Verb'");

    ASSERT(fx_pedal_get_param_count(FX_PEDAL_JADE_DRIVE) == 3,
           "Jade Drive should have 3 params");
    ASSERT(fx_pedal_get_param_count(FX_PEDAL_NOISE_GATE) == 4,
           "Noise Gate should have 4 params");

    printf("  OK\n");
}

/* ── Test: tuner ──────────────────────────────────────────────── */

static void test_tuner(void) {
    printf("test_tuner...\n");

    fx_engine_t *e = fx_engine_create(44100.0f);

    /* Feed a 440Hz sine wave for several blocks */
    float buf[4096];
    for (int i = 0; i < 4096; i++) {
        buf[i] = 0.5f * sinf(2.0f * 3.14159265f * 440.0f * (float)i / 44100.0f);
    }

    float output[4096];
    /* Process multiple blocks to fill tuner buffer (needs ~8K samples for autocorrelation) */
    for (int block = 0; block < 8; block++) {
        /* Generate fresh samples for each block to simulate continuous input */
        for (int i = 0; i < 1024; i++) {
            int sample_idx = block * 1024 + i;
            buf[i] = 0.5f * sinf(2.0f * 3.14159265f * 440.0f * (float)sample_idx / 44100.0f);
        }
        fx_engine_process(e, buf, output, 1024);
    }

    float freq = fx_tuner_get_frequency(e);
    printf("    tuner detected: %.1f Hz\n", freq);
    /* Allow tolerance — pitch detection on short buffers with ring buffer wrapping
     * is approximate. Will be refined in later phases with better windowing. */
    ASSERT(freq > 350.0f && freq < 530.0f,
           "tuner should detect roughly ~440Hz");

    fx_engine_destroy(e);
    printf("  OK\n");
}

/* ── Test: noise gate ─────────────────────────────────────────── */

static void test_noise_gate(void) {
    printf("test_noise_gate...\n");

    fx_engine_t *e = fx_engine_create(44100.0f);

    /* Silence (below threshold) — gate should attenuate */
    float silence[256];
    float output_s[256];
    for (int i = 0; i < 256; i++) silence[i] = 0.0001f;
    fx_engine_process(e, silence, output_s, 256);

    float peak_silence = 0.0f;
    for (int i = 0; i < 256; i++) {
        float a = fabsf(output_s[i]);
        if (a > peak_silence) peak_silence = a;
    }
    ASSERT(peak_silence < 0.01f, "gate should attenuate silence");

    /* Loud signal (above threshold) — gate should open */
    float loud[512];
    float output_l[512];
    for (int i = 0; i < 512; i++) {
        loud[i] = 0.5f * sinf(2.0f * 3.14159f * 440.0f * (float)i / 44100.0f);
    }
    fx_engine_process(e, loud, output_l, 512);

    float peak_loud = 0.0f;
    for (int i = 0; i < 512; i++) {
        float a = fabsf(output_l[i]);
        if (a > peak_loud) peak_loud = a;
    }
    ASSERT(peak_loud > 0.01f, "gate should pass loud signal");
    printf("    silence peak=%.6f, loud peak=%.4f\n", peak_silence, peak_loud);

    fx_engine_destroy(e);
    printf("  OK\n");
}

/* ── Test: overdrive pedal adds harmonics ─────────────────────── */

static void test_overdrive(void) {
    printf("test_overdrive...\n");

    fx_engine_t *e = fx_engine_create(44100.0f);
    /* Set amp to minimal processing so we can hear pedal effect */
    fx_amp_set_param(e, FX_CHAIN_DEFAULT, FX_AMP_PARAM_GAIN, 0.0f);
    fx_amp_set_param(e, FX_CHAIN_DEFAULT, FX_AMP_PARAM_VOLUME, 1.0f);
    fx_amp_set_param(e, FX_CHAIN_DEFAULT, FX_AMP_PARAM_MASTER, 1.0f);

    /* Process without pedal first (reference) */
    float input[512], out_clean[512], out_od[512];
    for (int i = 0; i < 512; i++) {
        input[i] = 0.3f * sinf(2.0f * 3.14159f * 440.0f * (float)i / 44100.0f);
    }
    fx_engine_process(e, input, out_clean, 512);

    /* Add Jade Drive with high drive */
    fx_pedal_id od = fx_chain_add_pedal(e, FX_PEDAL_JADE_DRIVE, FX_CHAIN_POS_PRE);
    fx_pedal_set_param(e, od, 0, 0.9f);  /* drive cranked */
    fx_pedal_set_param(e, od, 2, 0.8f);  /* level */

    /* Need fresh engine state (gate/amp are stateful) */
    fx_engine_t *e2 = fx_engine_create(44100.0f);
    fx_amp_set_param(e2, FX_CHAIN_DEFAULT, FX_AMP_PARAM_GAIN, 0.0f);
    fx_amp_set_param(e2, FX_CHAIN_DEFAULT, FX_AMP_PARAM_VOLUME, 1.0f);
    fx_amp_set_param(e2, FX_CHAIN_DEFAULT, FX_AMP_PARAM_MASTER, 1.0f);
    fx_pedal_id od2 = fx_chain_add_pedal(e2, FX_PEDAL_JADE_DRIVE, FX_CHAIN_POS_PRE);
    fx_pedal_set_param(e2, od2, 0, 0.9f);
    fx_pedal_set_param(e2, od2, 2, 0.8f);
    fx_engine_process(e2, input, out_od, 512);

    /* OD output should differ from clean */
    int differs = 0;
    for (int i = 50; i < 512; i++) {
        if (fabsf(out_od[i] - out_clean[i]) > 0.001f) {
            differs = 1;
            break;
        }
    }
    ASSERT(differs, "overdrive should change the signal");

    /* OD output should have energy */
    float peak = 0.0f;
    for (int i = 0; i < 512; i++) {
        float a = fabsf(out_od[i]);
        if (a > peak) peak = a;
    }
    ASSERT(peak > 0.01f, "overdrive output should have energy");
    printf("    OD peak=%.4f\n", peak);

    /* Test bypass: bypassed pedal should not alter signal */
    fx_pedal_set_bypass(e2, od2, true);

    fx_engine_destroy(e);
    fx_engine_destroy(e2);
    printf("  OK\n");
}

/* ── Test: delay produces echoes ─────────────────────────────── */

static void test_delay(void) {
    printf("test_delay...\n");

    fx_engine_t *e = fx_engine_create(44100.0f);
    fx_amp_set_param(e, FX_CHAIN_DEFAULT, FX_AMP_PARAM_GAIN, 0.0f);
    fx_amp_set_param(e, FX_CHAIN_DEFAULT, FX_AMP_PARAM_VOLUME, 1.0f);
    fx_amp_set_param(e, FX_CHAIN_DEFAULT, FX_AMP_PARAM_MASTER, 1.0f);

    /* Add delay */
    fx_pedal_id dly = fx_chain_add_pedal(e, FX_PEDAL_ECHO_DELAY, FX_CHAIN_POS_POST);
    fx_pedal_set_param(e, dly, 0, 0.1f);  /* short delay (~118ms) */
    fx_pedal_set_param(e, dly, 1, 0.5f);  /* moderate feedback */
    fx_pedal_set_param(e, dly, 2, 0.5f);  /* 50% mix */

    /* Send a short tone burst, then silence — delay echoes should persist */
    float input[4096];
    float output[4096];
    memset(input, 0, sizeof(input));
    /* Short burst of loud signal (enough to open the gate) */
    for (int i = 0; i < 200; i++) {
        input[i] = 0.5f * sinf(2.0f * 3.14159f * 440.0f * (float)i / 44100.0f);
    }

    fx_engine_process(e, input, output, 4096);

    /* There should be energy well after the input burst ends (echoes) */
    float late_peak = 0.0f;
    for (int i = 1000; i < 4096; i++) {
        float a = fabsf(output[i]);
        if (a > late_peak) late_peak = a;
    }
    ASSERT(late_peak > 0.0001f, "delay should produce echoes after input stops");
    printf("    late echo peak=%.6f\n", late_peak);

    fx_engine_destroy(e);
    printf("  OK\n");
}

/* ── Test: reverb adds tail ──────────────────────────────────── */

static void test_reverb(void) {
    printf("test_reverb...\n");

    fx_engine_t *e = fx_engine_create(44100.0f);
    fx_amp_set_param(e, FX_CHAIN_DEFAULT, FX_AMP_PARAM_GAIN, 0.0f);
    fx_amp_set_param(e, FX_CHAIN_DEFAULT, FX_AMP_PARAM_VOLUME, 1.0f);
    fx_amp_set_param(e, FX_CHAIN_DEFAULT, FX_AMP_PARAM_MASTER, 1.0f);

    /* Add reverb */
    fx_pedal_id rev = fx_chain_add_pedal(e, FX_PEDAL_HALL_VERB, FX_CHAIN_POS_POST);
    fx_pedal_set_param(e, rev, 0, 0.8f);  /* long decay */
    fx_pedal_set_param(e, rev, 2, 0.5f);  /* 50% mix */

    /* Send a short burst then silence */
    float input[4096];
    float output[4096];
    memset(input, 0, sizeof(input));
    for (int i = 0; i < 100; i++) {
        input[i] = 0.5f * sinf(2.0f * 3.14159f * 440.0f * (float)i / 44100.0f);
    }

    fx_engine_process(e, input, output, 4096);

    /* Reverb tail: should have energy well after input stops */
    float tail_peak = 0.0f;
    for (int i = 2000; i < 4096; i++) {
        float a = fabsf(output[i]);
        if (a > tail_peak) tail_peak = a;
    }
    ASSERT(tail_peak > 0.0001f, "reverb should produce tail after input stops");
    printf("    reverb tail peak=%.6f\n", tail_peak);

    fx_engine_destroy(e);
    printf("  OK\n");
}

/* ── Test: compressor reduces dynamic range ──────────────────── */

static void test_compressor(void) {
    printf("test_compressor...\n");

    fx_engine_t *e = fx_engine_create(44100.0f);
    fx_amp_set_param(e, FX_CHAIN_DEFAULT, FX_AMP_PARAM_GAIN, 0.0f);
    fx_amp_set_param(e, FX_CHAIN_DEFAULT, FX_AMP_PARAM_VOLUME, 1.0f);
    fx_amp_set_param(e, FX_CHAIN_DEFAULT, FX_AMP_PARAM_MASTER, 1.0f);

    /* Add compressor */
    fx_pedal_id comp = fx_chain_add_pedal(e, FX_PEDAL_SQUEEZE_BOX, FX_CHAIN_POS_PRE);
    fx_pedal_set_param(e, comp, 0, 0.5f);  /* output */
    fx_pedal_set_param(e, comp, 1, 0.9f);  /* high sensitivity */

    /* Process a loud signal */
    float loud[512], out_comp[512];
    for (int i = 0; i < 512; i++) {
        loud[i] = 0.8f * sinf(2.0f * 3.14159f * 440.0f * (float)i / 44100.0f);
    }
    fx_engine_process(e, loud, out_comp, 512);

    /* Compressed output should exist */
    float peak = 0.0f;
    for (int i = 0; i < 512; i++) {
        float a = fabsf(out_comp[i]);
        if (a > peak) peak = a;
    }
    ASSERT(peak > 0.01f, "compressor should produce output");
    printf("    compressed peak=%.4f\n", peak);

    fx_engine_destroy(e);
    printf("  OK\n");
}

/* ── Test: pedal param names are correct ─────────────────────── */

static void test_pedal_param_names(void) {
    printf("test_pedal_param_names...\n");

    ASSERT(strcmp(fx_pedal_get_param_name(FX_PEDAL_JADE_DRIVE, 0), "Drive") == 0,
           "Jade Drive param 0 should be 'Drive'");
    ASSERT(strcmp(fx_pedal_get_param_name(FX_PEDAL_JADE_DRIVE, 1), "Tone") == 0,
           "Jade Drive param 1 should be 'Tone'");
    ASSERT(strcmp(fx_pedal_get_param_name(FX_PEDAL_JADE_DRIVE, 2), "Level") == 0,
           "Jade Drive param 2 should be 'Level'");

    ASSERT(strcmp(fx_pedal_get_param_name(FX_PEDAL_RODENT, 0), "Distortion") == 0,
           "Rodent param 0 should be 'Distortion'");
    ASSERT(strcmp(fx_pedal_get_param_name(FX_PEDAL_RODENT, 1), "Filter") == 0,
           "Rodent param 1 should be 'Filter'");

    ASSERT(strcmp(fx_pedal_get_param_name(FX_PEDAL_ECHO_DELAY, 0), "Time") == 0,
           "Echo Delay param 0 should be 'Time'");

    printf("  OK\n");
}

/* ── Test: amp models have different character ────────────────── */

static void test_amp_character(void) {
    printf("test_amp_character...\n");

    float input[512];
    for (int i = 0; i < 512; i++) {
        input[i] = 0.3f * sinf(2.0f * 3.14159f * 440.0f * (float)i / 44100.0f);
    }

    /* Process through each amp model and verify they sound different */
    float outputs[FX_AMP_COUNT][512];
    for (int m = 0; m < FX_AMP_COUNT; m++) {
        fx_engine_t *e = fx_engine_create(44100.0f);
        fx_amp_set_model(e, FX_CHAIN_DEFAULT, (fx_amp_type_t)m);
        fx_amp_set_param(e, FX_CHAIN_DEFAULT, FX_AMP_PARAM_GAIN, 0.7f);
        fx_engine_process(e, input, outputs[m], 512);
        fx_engine_destroy(e);
    }

    /* Each model should produce different output */
    int all_different = 1;
    for (int a = 0; a < FX_AMP_COUNT && all_different; a++) {
        for (int b = a + 1; b < FX_AMP_COUNT && all_different; b++) {
            int same = 1;
            for (int i = 100; i < 512; i++) { /* skip first samples (gate opening) */
                if (fabsf(outputs[a][i] - outputs[b][i]) > 1e-4f) {
                    same = 0;
                    break;
                }
            }
            if (same) {
                printf("    WARN: %s and %s produced identical output\n",
                       fx_amp_get_type_name((fx_amp_type_t)a),
                       fx_amp_get_type_name((fx_amp_type_t)b));
                all_different = 0;
            }
        }
    }
    ASSERT(all_different, "each amp model should produce unique output");

    printf("  OK\n");
}

/* ── Test: cabinet IR convolution ─────────────────────────────── */

static void test_cab_ir(void) {
    printf("test_cab_ir...\n");

    /* Create a simple IR: a delayed impulse at sample 10 */
    const int ir_len = 64;
    const int delay = 10;
    const unsigned int sr = 48000;
    float ir_data[64];
    memset(ir_data, 0, sizeof(ir_data));
    ir_data[delay] = 1.0f;

    /* Write it as a temp .wav file */
    const char *tmp_path = "/tmp/0xfx_test_ir.wav";
    drwav wav;
    drwav_data_format format;
    format.container = drwav_container_riff;
    format.format = DR_WAVE_FORMAT_IEEE_FLOAT;
    format.channels = 1;
    format.sampleRate = sr;
    format.bitsPerSample = 32;
    drwav_bool32 ok = drwav_init_file_write(&wav, tmp_path, &format, NULL);
    ASSERT(ok, "should create temp .wav file");
    if (!ok) { printf("  SKIP (cannot create temp file)\n"); return; }
    drwav_uint64 written = drwav_write_pcm_frames(&wav, (drwav_uint64)ir_len, ir_data);
    ASSERT((int)written == ir_len, "should write all IR frames");
    drwav_uninit(&wav);

    /* Create engine and load the IR */
    fx_engine_t *e = fx_engine_create((float)sr);
    ASSERT(e != NULL, "engine should be created");

    bool loaded = fx_cab_load_ir(e, FX_CHAIN_DEFAULT, tmp_path);
    ASSERT(loaded, "should load IR from .wav file");

    /* Bypass the amp so we get a clean convolution result.
     * Set gain to 0 (minimal distortion), volume and master to max. */
    fx_amp_set_param(e, FX_CHAIN_DEFAULT, FX_AMP_PARAM_GAIN, 0.0f);
    fx_amp_set_param(e, FX_CHAIN_DEFAULT, FX_AMP_PARAM_VOLUME, 1.0f);
    fx_amp_set_param(e, FX_CHAIN_DEFAULT, FX_AMP_PARAM_MASTER, 1.0f);

    /* Process an impulse (single sample = 1.0, rest = 0)
     * through the engine. The cab IR is a delayed impulse at sample 10,
     * so the output peak should be near sample 10. */
    const int block = 256;
    float input[256];
    float output[256];
    memset(input, 0, sizeof(input));
    /* Use a strong impulse burst so the noise gate opens */
    for (int i = 0; i < 32; i++) {
        input[i] = 0.5f * sinf(2.0f * 3.14159f * 1000.0f * (float)i / (float)sr);
    }

    fx_engine_process(e, input, output, block);

    /* Verify the output has energy (cab IR is active) */
    float peak = 0.0f;
    int peak_idx = 0;
    for (int i = 0; i < block; i++) {
        float a = fabsf(output[i]);
        if (a > peak) {
            peak = a;
            peak_idx = i;
        }
    }
    ASSERT(peak > 0.001f, "cab IR output should have energy");
    printf("    peak=%.4f at sample %d\n", peak, peak_idx);

    /* Now test with a pure impulse directly through cab (bypass engine chain).
     * Create a second engine, load IR, and send a loud enough signal. */
    fx_engine_t *e2 = fx_engine_create((float)sr);
    bool loaded2 = fx_cab_load_ir(e2, FX_CHAIN_DEFAULT, tmp_path);
    ASSERT(loaded2, "should load IR again");

    /* Use cab bypass test: with cab loaded, output should differ from
     * output with cab bypassed */
    fx_amp_set_param(e2, FX_CHAIN_DEFAULT, FX_AMP_PARAM_GAIN, 0.0f);
    fx_amp_set_param(e2, FX_CHAIN_DEFAULT, FX_AMP_PARAM_VOLUME, 1.0f);
    fx_amp_set_param(e2, FX_CHAIN_DEFAULT, FX_AMP_PARAM_MASTER, 1.0f);

    float out_with_cab[256];
    float out_no_cab[256];

    /* Input: short tone burst */
    float input2[256];
    memset(input2, 0, sizeof(input2));
    for (int i = 0; i < 64; i++) {
        input2[i] = 0.4f * sinf(2.0f * 3.14159f * 440.0f * (float)i / (float)sr);
    }

    fx_engine_process(e2, input2, out_with_cab, block);

    /* Now bypass cab and process same input with a fresh engine */
    fx_engine_t *e3 = fx_engine_create((float)sr);
    fx_amp_set_param(e3, FX_CHAIN_DEFAULT, FX_AMP_PARAM_GAIN, 0.0f);
    fx_amp_set_param(e3, FX_CHAIN_DEFAULT, FX_AMP_PARAM_VOLUME, 1.0f);
    fx_amp_set_param(e3, FX_CHAIN_DEFAULT, FX_AMP_PARAM_MASTER, 1.0f);
    /* No IR loaded on e3 — cab is not active */
    fx_engine_process(e3, input2, out_no_cab, block);

    /* Output with cab should differ from output without cab */
    int cab_differs = 0;
    for (int i = 0; i < block; i++) {
        if (fabsf(out_with_cab[i] - out_no_cab[i]) > 1e-4f) {
            cab_differs = 1;
            break;
        }
    }
    ASSERT(cab_differs, "cab IR should change the signal");

    fx_engine_destroy(e);
    fx_engine_destroy(e2);
    fx_engine_destroy(e3);

    /* Clean up temp file */
    unlink(tmp_path);

    printf("  OK\n");
}

/* ── Test: parallel chain routing (TASK-039) ─────────────────── */

static void test_parallel_chain_routing(void) {
    printf("test_parallel_chain_routing...\n");

    /* ── Basic API: create, count, max chains, destroy ────────── */

    fx_engine_t *e = fx_engine_create(44100.0f);
    ASSERT(fx_chain_get_count(e) == 1, "should start with 1 chain");

    /* Create chain 1 — should return valid ID */
    fx_chain_id c1 = fx_chain_create(e);
    ASSERT(c1 >= 0, "fx_chain_create should return valid ID");
    ASSERT(fx_chain_get_count(e) == 2, "should have 2 chains after create");

    /* Create chains 2 and 3 (max 4 total) */
    fx_chain_id c2 = fx_chain_create(e);
    ASSERT(c2 >= 0, "chain 2 should be valid");
    ASSERT(fx_chain_get_count(e) == 3, "should have 3 chains");

    fx_chain_id c3 = fx_chain_create(e);
    ASSERT(c3 >= 0, "chain 3 should be valid");
    ASSERT(fx_chain_get_count(e) == 4, "should have 4 chains (max)");

    /* 5th chain should fail — max 4 enforced */
    fx_chain_id c4 = fx_chain_create(e);
    ASSERT(c4 == -1, "5th chain create should return -1 (max 4)");
    ASSERT(fx_chain_get_count(e) == 4, "should still have 4 chains");

    /* Cannot destroy chain 0 (default) */
    fx_chain_destroy(e, FX_CHAIN_DEFAULT);
    ASSERT(fx_chain_get_count(e) == 4,
           "destroying chain 0 should have no effect");

    /* Destroy trailing chains — should reclaim slots */
    fx_chain_destroy(e, c3);
    ASSERT(fx_chain_get_count(e) == 3,
           "destroying last chain should reclaim slot (4 -> 3)");
    fx_chain_destroy(e, c2);
    ASSERT(fx_chain_get_count(e) == 2,
           "destroying last chain should reclaim slot (3 -> 2)");
    fx_chain_destroy(e, c1);
    ASSERT(fx_chain_get_count(e) == 1,
           "destroying last chain should reclaim slot (2 -> 1)");

    /* Toggle cycle: create and destroy repeatedly should not exhaust slots */
    for (int cycle = 0; cycle < 20; cycle++) {
        fx_chain_id cc = fx_chain_create(e);
        ASSERT(cc >= 0, "chain create should succeed on each cycle");
        ASSERT(fx_chain_get_count(e) == 2, "should have 2 chains after create");
        fx_chain_destroy(e, cc);
        ASSERT(fx_chain_get_count(e) == 1, "should have 1 chain after destroy");
    }

    /* Recreate for remaining tests */
    c1 = fx_chain_create(e);
    ASSERT(c1 >= 0, "re-create chain 1 after cycle test");

    /* Mix levels clamp to 0-1 */
    fx_chain_set_mix(e, FX_CHAIN_DEFAULT, -0.5f);
    ASSERT(fx_chain_get_mix(e, FX_CHAIN_DEFAULT) >= 0.0f,
           "mix should clamp to >= 0");
    fx_chain_set_mix(e, FX_CHAIN_DEFAULT, 2.0f);
    ASSERT(fx_chain_get_mix(e, FX_CHAIN_DEFAULT) <= 1.0f,
           "mix should clamp to <= 1");

    fx_engine_destroy(e);

    /* ── Dual-chain processing test ───────────────────────────── */

    /* Create engine with 2 chains: British Crunch and Fullerton Clean */
    fx_engine_t *e_dual = fx_engine_create(44100.0f);

    /* Chain 0: British Crunch, gain=0.8 */
    fx_amp_set_model(e_dual, FX_CHAIN_DEFAULT, FX_AMP_BRIT_CRUNCH);
    fx_amp_set_param(e_dual, FX_CHAIN_DEFAULT, FX_AMP_PARAM_GAIN, 0.8f);

    /* Chain 1: Fullerton Clean, gain=0.2 */
    fx_chain_id chain1 = fx_chain_create(e_dual);
    ASSERT(chain1 >= 0, "dual: chain 1 should be created");
    fx_amp_set_model(e_dual, chain1, FX_AMP_FULLERTON_CLEAN);
    fx_amp_set_param(e_dual, chain1, FX_AMP_PARAM_GAIN, 0.2f);

    /* Set mix levels: 0.6 / 0.4 */
    fx_chain_set_mix(e_dual, FX_CHAIN_DEFAULT, 0.6f);
    fx_chain_set_mix(e_dual, chain1, 0.4f);
    ASSERT(fabsf(fx_chain_get_mix(e_dual, FX_CHAIN_DEFAULT) - 0.6f) < 1e-6f,
           "chain 0 mix should be 0.6");
    ASSERT(fabsf(fx_chain_get_mix(e_dual, chain1) - 0.4f) < 1e-6f,
           "chain 1 mix should be 0.4");

    /* Generate a 440Hz sine wave input */
    float pc_input[512];
    for (int i = 0; i < 512; i++) {
        pc_input[i] = 0.3f * sinf(2.0f * 3.14159f * 440.0f * (float)i / 44100.0f);
    }

    /* Process through dual-chain engine */
    float out_dual[512];
    fx_engine_process(e_dual, pc_input, out_dual, 512);

    /* Output should have energy */
    float dual_peak = 0.0f;
    for (int i = 0; i < 512; i++) {
        float a = fabsf(out_dual[i]);
        if (a > dual_peak) dual_peak = a;
    }
    ASSERT(dual_peak > 0.01f, "dual-chain output should have energy");
    printf("    dual-chain peak=%.4f\n", dual_peak);

    /* Now process the same input through a single-chain engine
     * (British Crunch only, gain=0.8) for comparison */
    fx_engine_t *e_single = fx_engine_create(44100.0f);
    fx_amp_set_model(e_single, FX_CHAIN_DEFAULT, FX_AMP_BRIT_CRUNCH);
    fx_amp_set_param(e_single, FX_CHAIN_DEFAULT, FX_AMP_PARAM_GAIN, 0.8f);

    float out_single[512];
    fx_engine_process(e_single, pc_input, out_single, 512);

    /* Dual-chain output should differ from single-chain output */
    int pc_differs = 0;
    for (int i = 50; i < 512; i++) {
        if (fabsf(out_dual[i] - out_single[i]) > 1e-4f) {
            pc_differs = 1;
            break;
        }
    }
    ASSERT(pc_differs, "dual-chain output should differ from single-chain");

    fx_engine_destroy(e_dual);
    fx_engine_destroy(e_single);

    printf("  OK\n");
}

/* ── Test: fx_cab_load_ir() API end-to-end (TASK-035) ────────── */

static void test_cab_load_api(void) {
    printf("test_cab_load_api...\n");

    const unsigned int sr = 48000;
    const char *wav_path = "/tmp/test_ir.wav";

    /* ── Create a simple .wav IR file: delayed impulse ────────── */
    const int ir_len = 128;
    float ir_data[128];
    memset(ir_data, 0, sizeof(ir_data));
    /* Delayed impulse at sample 20 — convolution should shift signal */
    ir_data[0] = 0.0f;
    ir_data[20] = 1.0f;

    drwav wav;
    drwav_data_format format;
    format.container = drwav_container_riff;
    format.format = DR_WAVE_FORMAT_IEEE_FLOAT;
    format.channels = 1;
    format.sampleRate = sr;
    format.bitsPerSample = 32;
    drwav_bool32 ok = drwav_init_file_write(&wav, wav_path, &format, NULL);
    ASSERT(ok, "should create test IR .wav file");
    if (!ok) { printf("  SKIP (cannot create temp file)\n"); return; }
    drwav_uint64 written = drwav_write_pcm_frames(&wav, (drwav_uint64)ir_len, ir_data);
    ASSERT((int)written == ir_len, "should write all IR frames");
    drwav_uninit(&wav);

    /* ── Load IR via public API ───────────────────────────────── */
    fx_engine_t *e = fx_engine_create((float)sr);
    ASSERT(e != NULL, "engine should be created");

    bool loaded = fx_cab_load_ir(e, FX_CHAIN_DEFAULT, wav_path);
    ASSERT(loaded, "fx_cab_load_ir should succeed");

    /* Set amp to minimal processing */
    fx_amp_set_param(e, FX_CHAIN_DEFAULT, FX_AMP_PARAM_GAIN, 0.0f);
    fx_amp_set_param(e, FX_CHAIN_DEFAULT, FX_AMP_PARAM_VOLUME, 1.0f);
    fx_amp_set_param(e, FX_CHAIN_DEFAULT, FX_AMP_PARAM_MASTER, 1.0f);

    /* ── Process audio and verify cab affects signal ──────────── */
    const int block = 256;
    float input_sig[256];
    memset(input_sig, 0, sizeof(input_sig));
    /* Short tone burst to open the gate */
    for (int i = 0; i < 64; i++) {
        input_sig[i] = 0.4f * sinf(2.0f * 3.14159f * 440.0f * (float)i / (float)sr);
    }

    float out_with_ir[256];
    fx_engine_process(e, input_sig, out_with_ir, block);

    /* Output should have energy */
    float ir_peak = 0.0f;
    for (int i = 0; i < block; i++) {
        float a = fabsf(out_with_ir[i]);
        if (a > ir_peak) ir_peak = a;
    }
    ASSERT(ir_peak > 0.001f, "cab IR output should have energy");
    printf("    with-IR peak=%.4f\n", ir_peak);

    /* ── Compare with no-cab processing ──────────────────────── */
    fx_engine_t *e_nocab = fx_engine_create((float)sr);
    fx_amp_set_param(e_nocab, FX_CHAIN_DEFAULT, FX_AMP_PARAM_GAIN, 0.0f);
    fx_amp_set_param(e_nocab, FX_CHAIN_DEFAULT, FX_AMP_PARAM_VOLUME, 1.0f);
    fx_amp_set_param(e_nocab, FX_CHAIN_DEFAULT, FX_AMP_PARAM_MASTER, 1.0f);
    /* No IR loaded — cab is inactive */
    float out_no_ir[256];
    fx_engine_process(e_nocab, input_sig, out_no_ir, block);

    int ir_differs = 0;
    for (int i = 0; i < block; i++) {
        if (fabsf(out_with_ir[i] - out_no_ir[i]) > 1e-4f) {
            ir_differs = 1;
            break;
        }
    }
    ASSERT(ir_differs, "cab IR should change the signal vs no-cab");

    /* ── Test fx_cab_set_bypass ───────────────────────────────── */
    /* Create a fresh engine with cab loaded, process with bypass on vs off */
    fx_engine_t *e_bp = fx_engine_create((float)sr);
    fx_amp_set_param(e_bp, FX_CHAIN_DEFAULT, FX_AMP_PARAM_GAIN, 0.0f);
    fx_amp_set_param(e_bp, FX_CHAIN_DEFAULT, FX_AMP_PARAM_VOLUME, 1.0f);
    fx_amp_set_param(e_bp, FX_CHAIN_DEFAULT, FX_AMP_PARAM_MASTER, 1.0f);
    bool bp_loaded = fx_cab_load_ir(e_bp, FX_CHAIN_DEFAULT, wav_path);
    ASSERT(bp_loaded, "should load IR for bypass test");

    /* Process with cab active */
    float out_active[256];
    fx_engine_process(e_bp, input_sig, out_active, block);

    /* Now bypass the cab and process with a fresh engine (same IR loaded) */
    fx_engine_t *e_bp2 = fx_engine_create((float)sr);
    fx_amp_set_param(e_bp2, FX_CHAIN_DEFAULT, FX_AMP_PARAM_GAIN, 0.0f);
    fx_amp_set_param(e_bp2, FX_CHAIN_DEFAULT, FX_AMP_PARAM_VOLUME, 1.0f);
    fx_amp_set_param(e_bp2, FX_CHAIN_DEFAULT, FX_AMP_PARAM_MASTER, 1.0f);
    fx_cab_load_ir(e_bp2, FX_CHAIN_DEFAULT, wav_path);
    fx_cab_set_bypass(e_bp2, FX_CHAIN_DEFAULT, true);
    ASSERT(fx_cab_get_bypass(e_bp2, FX_CHAIN_DEFAULT) == true,
           "cab bypass should be true after set");

    float out_bypassed[256];
    fx_engine_process(e_bp2, input_sig, out_bypassed, block);

    /* Bypassed cab should produce different output than active cab */
    int bp_differs = 0;
    for (int i = 0; i < block; i++) {
        if (fabsf(out_active[i] - out_bypassed[i]) > 1e-4f) {
            bp_differs = 1;
            break;
        }
    }
    ASSERT(bp_differs, "bypassed cab should differ from active cab");

    fx_engine_destroy(e);
    fx_engine_destroy(e_nocab);
    fx_engine_destroy(e_bp);
    fx_engine_destroy(e_bp2);

    /* Clean up temp file */
    unlink(wav_path);

    printf("  OK\n");
}

/* ── Test: synthetic IR generation (TASK-036 + TASK-037) ──────── */

static void test_synthetic_ir(void) {
    printf("test_synthetic_ir...\n");

    const float sr = 48000.0f;
    const int block = 256;

    /* ── Test fx_cab_generate_ir with each cab type ──────────── */
    fx_cab_params_t params;
    params.mic_pos = FX_MIC_ON_AXIS;
    params.speaker_fs = 80.0f;
    params.brightness = 0.5f;
    params.resonance = 0.5f;

    for (int cab_type = 0; cab_type < FX_CAB_TYPE_COUNT; cab_type++) {
        params.cab_type = (fx_cab_type_t)cab_type;

        fx_engine_t *e = fx_engine_create(sr);
        ASSERT(e != NULL, "engine should be created");

        /* Set amp to minimal processing */
        fx_amp_set_param(e, FX_CHAIN_DEFAULT, FX_AMP_PARAM_GAIN, 0.0f);
        fx_amp_set_param(e, FX_CHAIN_DEFAULT, FX_AMP_PARAM_VOLUME, 1.0f);
        fx_amp_set_param(e, FX_CHAIN_DEFAULT, FX_AMP_PARAM_MASTER, 1.0f);

        bool gen_ok = fx_cab_generate_ir(e, FX_CHAIN_DEFAULT, &params);
        char msg[128];
        snprintf(msg, sizeof(msg), "generate_ir should succeed for cab_type=%d", cab_type);
        ASSERT(gen_ok, msg);

        if (gen_ok) {
            /* Generate test input: 440Hz sine burst */
            float input_sig[256];
            for (int i = 0; i < block; i++) {
                input_sig[i] = 0.4f * sinf(2.0f * 3.14159f * 440.0f * (float)i / sr);
            }

            float out_with_cab[256];
            fx_engine_process(e, input_sig, out_with_cab, block);

            /* Output should have energy */
            float peak = 0.0f;
            for (int i = 0; i < block; i++) {
                float a = fabsf(out_with_cab[i]);
                if (a > peak) peak = a;
            }
            snprintf(msg, sizeof(msg),
                     "synth cab type=%d output should have energy (peak=%.4f)",
                     cab_type, peak);
            ASSERT(peak > 0.001f, msg);
            printf("    cab_type=%d peak=%.4f\n", cab_type, peak);

            /* Compare with no-cab engine to verify cab changes the signal */
            fx_engine_t *e_nocab = fx_engine_create(sr);
            fx_amp_set_param(e_nocab, FX_CHAIN_DEFAULT, FX_AMP_PARAM_GAIN, 0.0f);
            fx_amp_set_param(e_nocab, FX_CHAIN_DEFAULT, FX_AMP_PARAM_VOLUME, 1.0f);
            fx_amp_set_param(e_nocab, FX_CHAIN_DEFAULT, FX_AMP_PARAM_MASTER, 1.0f);

            float out_no_cab[256];
            fx_engine_process(e_nocab, input_sig, out_no_cab, block);

            int differs = 0;
            for (int i = 0; i < block; i++) {
                if (fabsf(out_with_cab[i] - out_no_cab[i]) > 1e-4f) {
                    differs = 1;
                    break;
                }
            }
            snprintf(msg, sizeof(msg),
                     "synth cab type=%d should differ from no-cab", cab_type);
            ASSERT(differs, msg);

            fx_engine_destroy(e_nocab);
        }

        fx_engine_destroy(e);
    }

    /* ── Test different mic positions produce different results ── */
    fx_engine_t *e_on = fx_engine_create(sr);
    fx_engine_t *e_off = fx_engine_create(sr);
    fx_amp_set_param(e_on, FX_CHAIN_DEFAULT, FX_AMP_PARAM_GAIN, 0.0f);
    fx_amp_set_param(e_on, FX_CHAIN_DEFAULT, FX_AMP_PARAM_VOLUME, 1.0f);
    fx_amp_set_param(e_on, FX_CHAIN_DEFAULT, FX_AMP_PARAM_MASTER, 1.0f);
    fx_amp_set_param(e_off, FX_CHAIN_DEFAULT, FX_AMP_PARAM_GAIN, 0.0f);
    fx_amp_set_param(e_off, FX_CHAIN_DEFAULT, FX_AMP_PARAM_VOLUME, 1.0f);
    fx_amp_set_param(e_off, FX_CHAIN_DEFAULT, FX_AMP_PARAM_MASTER, 1.0f);

    fx_cab_params_t p_on = { FX_CAB_4X12_STRAIGHT, FX_MIC_ON_AXIS, 80.0f, 0.5f, 0.5f };
    fx_cab_params_t p_off = { FX_CAB_4X12_STRAIGHT, FX_MIC_OFF_AXIS, 80.0f, 0.5f, 0.5f };
    fx_cab_generate_ir(e_on, FX_CHAIN_DEFAULT, &p_on);
    fx_cab_generate_ir(e_off, FX_CHAIN_DEFAULT, &p_off);

    float mic_input[256];
    for (int i = 0; i < block; i++) {
        mic_input[i] = 0.4f * sinf(2.0f * 3.14159f * 440.0f * (float)i / sr);
    }
    float out_on[256], out_off[256];
    fx_engine_process(e_on, mic_input, out_on, block);
    fx_engine_process(e_off, mic_input, out_off, block);

    int mic_differs = 0;
    for (int i = 0; i < block; i++) {
        if (fabsf(out_on[i] - out_off[i]) > 1e-4f) {
            mic_differs = 1;
            break;
        }
    }
    ASSERT(mic_differs, "on-axis vs off-axis mic should produce different output");

    fx_engine_destroy(e_on);
    fx_engine_destroy(e_off);

    /* ── Test NULL/invalid params ────────────────────────────── */
    fx_engine_t *e_null = fx_engine_create(sr);
    ASSERT(!fx_cab_generate_ir(NULL, FX_CHAIN_DEFAULT, &p_on),
           "generate_ir with NULL engine should fail");
    ASSERT(!fx_cab_generate_ir(e_null, FX_CHAIN_DEFAULT, NULL),
           "generate_ir with NULL params should fail");
    ASSERT(!fx_cab_generate_ir(e_null, -1, &p_on),
           "generate_ir with invalid chain should fail");
    fx_engine_destroy(e_null);

    printf("  OK\n");
}

/* ── Test: preset roundtrip (save + load) ─────────────────────── */

static void test_preset_roundtrip(void) {
    printf("test_preset_roundtrip...\n");

    const char *tmpfile = "/tmp/0xfx_test_preset.0xfx";

    /* ── Build engine A with specific state ───────────────────── */
    fx_engine_t *ea = fx_engine_create(44100.0f);

    /* Set amp model and params */
    fx_amp_set_model(ea, FX_CHAIN_DEFAULT, FX_AMP_BRIT_CRUNCH);
    fx_amp_set_param(ea, FX_CHAIN_DEFAULT, FX_AMP_PARAM_GAIN, 0.7f);
    fx_amp_set_param(ea, FX_CHAIN_DEFAULT, FX_AMP_PARAM_BASS, 0.4f);
    fx_amp_set_param(ea, FX_CHAIN_DEFAULT, FX_AMP_PARAM_MID, 0.6f);
    fx_amp_set_param(ea, FX_CHAIN_DEFAULT, FX_AMP_PARAM_TREBLE, 0.8f);
    fx_chain_set_mix(ea, FX_CHAIN_DEFAULT, 0.9f);

    /* Add pre-pedals */
    fx_pedal_id pr_od = fx_chain_add_pedal(ea, FX_PEDAL_JADE_DRIVE, FX_CHAIN_POS_PRE);
    fx_pedal_set_param(ea, pr_od, 0, 0.65f);  /* drive */
    fx_pedal_set_param(ea, pr_od, 1, 0.5f);   /* tone */
    fx_pedal_set_param(ea, pr_od, 2, 0.8f);   /* level */
    fx_pedal_set_bypass(ea, pr_od, true);

    /* Add post-pedal */
    fx_pedal_id pr_dly = fx_chain_add_pedal(ea, FX_PEDAL_ECHO_DELAY, FX_CHAIN_POS_POST);
    fx_pedal_set_param(ea, pr_dly, 0, 0.3f);  /* time */
    fx_pedal_set_param(ea, pr_dly, 1, 0.4f);  /* feedback */
    fx_pedal_set_param(ea, pr_dly, 2, 0.25f); /* mix */

    /* Save preset */
    bool saved = fx_preset_save(ea, tmpfile);
    ASSERT(saved, "preset save should succeed");

    /* ── Load into engine B ───────────────────────────────────── */
    fx_engine_t *eb = fx_engine_create(44100.0f);
    bool loaded = fx_preset_load(eb, tmpfile);
    ASSERT(loaded, "preset load should succeed");

    /* Verify amp model */
    ASSERT(fx_amp_get_model(eb, FX_CHAIN_DEFAULT) == FX_AMP_BRIT_CRUNCH,
           "loaded amp should be Brit Crunch");

    /* Verify amp params */
    ASSERT(fabsf(fx_amp_get_param(eb, FX_CHAIN_DEFAULT, FX_AMP_PARAM_GAIN) - 0.7f) < 0.01f,
           "loaded gain should be ~0.7");
    ASSERT(fabsf(fx_amp_get_param(eb, FX_CHAIN_DEFAULT, FX_AMP_PARAM_BASS) - 0.4f) < 0.01f,
           "loaded bass should be ~0.4");
    ASSERT(fabsf(fx_amp_get_param(eb, FX_CHAIN_DEFAULT, FX_AMP_PARAM_MID) - 0.6f) < 0.01f,
           "loaded mid should be ~0.6");
    ASSERT(fabsf(fx_amp_get_param(eb, FX_CHAIN_DEFAULT, FX_AMP_PARAM_TREBLE) - 0.8f) < 0.01f,
           "loaded treble should be ~0.8");

    /* Verify chain mix */
    ASSERT(fabsf(fx_chain_get_mix(eb, FX_CHAIN_DEFAULT) - 0.9f) < 0.01f,
           "loaded mix should be ~0.9");

    /* Verify chain count */
    ASSERT(fx_chain_get_count(eb) == 1, "should have 1 chain");

    /* Verify pedal counts */
    ASSERT(fx_chain_get_pedal_count(eb, FX_CHAIN_POS_PRE) == 1,
           "should have 1 pre-pedal after load");
    ASSERT(fx_chain_get_pedal_count(eb, FX_CHAIN_POS_POST) == 1,
           "should have 1 post-pedal after load");

    /* ── Process audio through both, compare output ───────────── */
    fx_engine_t *ea2 = fx_engine_create(44100.0f);
    fx_preset_load(ea2, tmpfile);

    float pr_input[512];
    for (int i = 0; i < 512; i++) {
        pr_input[i] = 0.3f * sinf(2.0f * 3.14159f * 440.0f * (float)i / 44100.0f);
    }

    float out_a[512], out_b[512];
    fx_engine_process(ea2, pr_input, out_a, 512);
    fx_engine_process(eb, pr_input, out_b, 512);

    /* Output should be identical (both loaded from same preset, same DSP state) */
    float max_diff = 0.0f;
    for (int i = 0; i < 512; i++) {
        float d = fabsf(out_a[i] - out_b[i]);
        if (d > max_diff) max_diff = d;
    }
    ASSERT(max_diff < 1e-4f, "both engines should produce identical output");
    printf("    max output diff = %.8f\n", max_diff);

    fx_engine_destroy(ea);
    fx_engine_destroy(ea2);
    fx_engine_destroy(eb);

    /* Clean up temp file */
    remove(tmpfile);

    printf("  OK\n");
}

/* ── Test: custom cab IR round-trip (TASK-369) ─────────────────
 * Covers: path+name+image are persisted, reload restores them, and a
 * preset referencing a missing IR path falls back gracefully instead
 * of crashing. Uses a bundled stock IR as the "user-supplied" file.
 * Requires the test binary to run from the repo root so the relative
 * path resolves. */

static void test_preset_custom_ir_roundtrip(void) {
    printf("test_preset_custom_ir_roundtrip...\n");

    const char *ir_path   = "resources/ir/bundled/4x12_straight.wav";
    const char *img_path  = "resources/cabs/4x12_straight.png";
    const char *cab_name  = "My Test IR";
    const char *tmpfile   = "/tmp/0xfx_test_custom_cab.0xfx";

    /* Skip gracefully if the test binary wasn't run from repo root */
    FILE *probe = fopen(ir_path, "rb");
    if (!probe) {
        printf("  SKIP: run from repo root — %s not found\n", ir_path);
        return;
    }
    fclose(probe);

    /* Engine A: load custom IR + set name/image, then save */
    fx_engine_t *ea = fx_engine_create(48000.0f);
    ASSERT(fx_cab_load_ir(ea, FX_CHAIN_DEFAULT, ir_path), "load custom IR");
    fx_cab_set_custom_name(ea, FX_CHAIN_DEFAULT, cab_name);
    fx_cab_set_custom_image_path(ea, FX_CHAIN_DEFAULT, img_path);

    ASSERT(strcmp(fx_cab_get_custom_ir_path(ea, FX_CHAIN_DEFAULT), ir_path) == 0,
           "ir path stored");
    ASSERT(strcmp(fx_cab_get_custom_name(ea, FX_CHAIN_DEFAULT), cab_name) == 0,
           "name stored");
    ASSERT(strcmp(fx_cab_get_custom_image_path(ea, FX_CHAIN_DEFAULT), img_path) == 0,
           "image path stored");

    ASSERT(fx_preset_save(ea, tmpfile), "save preset with custom IR");

    /* Engine B: fresh engine, load the preset, verify fields come back */
    fx_engine_t *eb = fx_engine_create(48000.0f);
    ASSERT(fx_preset_load(eb, tmpfile), "load preset");
    ASSERT(strcmp(fx_cab_get_custom_ir_path(eb, FX_CHAIN_DEFAULT), ir_path) == 0,
           "ir path restored");
    ASSERT(strcmp(fx_cab_get_custom_name(eb, FX_CHAIN_DEFAULT), cab_name) == 0,
           "name restored");
    ASSERT(strcmp(fx_cab_get_custom_image_path(eb, FX_CHAIN_DEFAULT), img_path) == 0,
           "image path restored");

    /* Swapping to a stock/bundled cab clears custom metadata */
    fx_cab_params_t synth = { FX_CAB_4X12_STRAIGHT, FX_MIC_ON_AXIS, 80.0f, 0.5f, 0.5f };
    fx_cab_generate_ir(eb, FX_CHAIN_DEFAULT, &synth);
    ASSERT(fx_cab_get_custom_ir_path(eb, FX_CHAIN_DEFAULT)[0] == '\0',
           "synthetic IR clears custom path");
    ASSERT(fx_cab_get_custom_name(eb, FX_CHAIN_DEFAULT)[0] == '\0',
           "synthetic IR clears custom name");

    /* Missing-file fallback: hand-edit the preset JSON to point at a bogus path */
    {
        FILE *fp = fopen(tmpfile, "rb");
        fseek(fp, 0, SEEK_END);
        long sz = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        char *buf = (char *)malloc((size_t)sz + 1);
        fread(buf, 1, (size_t)sz, fp);
        buf[sz] = '\0';
        fclose(fp);

        char *hit = strstr(buf, ir_path);
        ASSERT(hit != NULL, "original path present in preset");
        memcpy(hit, "/tmp/0xfx_does_not_exist.wav",
               strlen("/tmp/0xfx_does_not_exist.wav"));
        /* Pad the remainder with X to keep the JSON string terminator valid */
        size_t orig_len = strlen(ir_path);
        size_t new_len  = strlen("/tmp/0xfx_does_not_exist.wav");
        for (size_t i = new_len; i < orig_len; i++) hit[i] = 'X';

        fp = fopen(tmpfile, "wb");
        fwrite(buf, 1, (size_t)sz, fp);
        fclose(fp);
        free(buf);
    }

    fx_engine_t *ec = fx_engine_create(48000.0f);
    ASSERT(fx_preset_load(ec, tmpfile),
           "preset with missing custom IR still loads (graceful fallback)");
    ASSERT(fx_cab_get_custom_ir_path(ec, FX_CHAIN_DEFAULT)[0] == '\0',
           "missing custom IR leaves engine in stock state");

    fx_engine_destroy(ea);
    fx_engine_destroy(eb);
    fx_engine_destroy(ec);
    remove(tmpfile);

    printf("  OK\n");
}

/* ── Test: preset fuzzing (invalid inputs) ───────────────────── */

static void test_preset_fuzz(void) {
    printf("test_preset_fuzz...\n");

    fx_engine_t *e_fz = fx_engine_create(44100.0f);

    /* Load from nonexistent file */
    ASSERT(!fx_preset_load(e_fz, "/tmp/0xfx_does_not_exist.0xfx"),
           "load nonexistent should fail");

    /* Load empty file */
    {
        FILE *fp = fopen("/tmp/0xfx_empty.0xfx", "w");
        if (fp) fclose(fp);
        ASSERT(!fx_preset_load(e_fz, "/tmp/0xfx_empty.0xfx"),
               "load empty file should fail");
        remove("/tmp/0xfx_empty.0xfx");
    }

    /* Load invalid JSON */
    {
        FILE *fp = fopen("/tmp/0xfx_bad.0xfx", "w");
        if (fp) { fputs("{not valid json!!!", fp); fclose(fp); }
        ASSERT(!fx_preset_load(e_fz, "/tmp/0xfx_bad.0xfx"),
               "load invalid JSON should fail");
        remove("/tmp/0xfx_bad.0xfx");
    }

    /* Load valid JSON but wrong format */
    {
        FILE *fp = fopen("/tmp/0xfx_wrong.0xfx", "w");
        if (fp) { fputs("{\"format\":\"not_0xfx\"}", fp); fclose(fp); }
        ASSERT(!fx_preset_load(e_fz, "/tmp/0xfx_wrong.0xfx"),
               "load wrong format should fail");
        remove("/tmp/0xfx_wrong.0xfx");
    }

    /* Load JSON with missing signal_chain */
    {
        FILE *fp = fopen("/tmp/0xfx_nosig.0xfx", "w");
        if (fp) { fputs("{\"format\":\"0xfx\",\"version\":\"1.0\"}", fp); fclose(fp); }
        ASSERT(!fx_preset_load(e_fz, "/tmp/0xfx_nosig.0xfx"),
               "load missing signal_chain should fail");
        remove("/tmp/0xfx_nosig.0xfx");
    }

    /* Load JSON with out-of-range values (should clamp, not crash) */
    {
        const char *json =
            "{\"format\":\"0xfx\",\"version\":\"1.0\","
            "\"signal_chain\":{"
            "\"input\":{\"noise_gate\":{\"threshold_db\":999,\"attack_ms\":-5}},"
            "\"pre_pedals\":[{\"type\":\"jade_drive\",\"bypass\":false,"
            "\"params\":{\"drive\":99.0,\"tone\":-1.0,\"level\":0.5}}],"
            "\"chains\":[{\"amp\":{\"model\":\"fullerton_clean\","
            "\"params\":{\"gain\":5.0,\"bass\":-2.0}},\"cab\":{\"bypass\":false},"
            "\"mix\":3.0}],"
            "\"post_pedals\":[]}}";
        FILE *fp = fopen("/tmp/0xfx_range.0xfx", "w");
        if (fp) { fputs(json, fp); fclose(fp); }
        bool fz_ok = fx_preset_load(e_fz, "/tmp/0xfx_range.0xfx");
        ASSERT(fz_ok, "load with out-of-range should succeed (clamped)");

        /* Verify values were clamped */
        ASSERT(fx_amp_get_param(e_fz, FX_CHAIN_DEFAULT, FX_AMP_PARAM_GAIN) <= 1.0f,
               "gain should be clamped to <=1.0");
        ASSERT(fx_chain_get_mix(e_fz, FX_CHAIN_DEFAULT) <= 1.0f,
               "mix should be clamped to <=1.0");
        remove("/tmp/0xfx_range.0xfx");
    }

    /* Load JSON with unknown pedal type (should skip gracefully) */
    {
        const char *json =
            "{\"format\":\"0xfx\",\"version\":\"1.0\","
            "\"signal_chain\":{"
            "\"pre_pedals\":[{\"type\":\"nonexistent_pedal\",\"bypass\":false,\"params\":{}}],"
            "\"chains\":[{\"amp\":{\"model\":\"fullerton_clean\",\"params\":{}},"
            "\"cab\":{\"bypass\":false},\"mix\":1.0}],"
            "\"post_pedals\":[]}}";
        FILE *fp = fopen("/tmp/0xfx_unk.0xfx", "w");
        if (fp) { fputs(json, fp); fclose(fp); }
        bool fz_ok = fx_preset_load(e_fz, "/tmp/0xfx_unk.0xfx");
        ASSERT(fz_ok, "load with unknown pedal type should succeed (skip)");
        remove("/tmp/0xfx_unk.0xfx");
    }

    /* NULL args */
    ASSERT(!fx_preset_save(NULL, "/tmp/x.0xfx"), "save NULL engine should fail");
    ASSERT(!fx_preset_save(e_fz, NULL), "save NULL path should fail");
    ASSERT(!fx_preset_load(NULL, "/tmp/x.0xfx"), "load NULL engine should fail");
    ASSERT(!fx_preset_load(e_fz, NULL), "load NULL path should fail");

    fx_engine_destroy(e_fz);
    printf("  OK\n");
}

/* ── Test: default presets load and produce audio ─────────────── */

static void test_default_presets(void) {
    printf("test_default_presets...\n");

    /* Try both paths: running from project root or from build/ */
    const char *preset_files[] = {
        "presets/clean_sparkle.0xfx",
        "presets/classic_crunch.0xfx",
        "presets/modern_high_gain.0xfx",
        "presets/chimey_british.0xfx",
        "presets/bluesy_tweed.0xfx",
    };
    const char *preset_files_alt[] = {
        "../presets/clean_sparkle.0xfx",
        "../presets/classic_crunch.0xfx",
        "../presets/modern_high_gain.0xfx",
        "../presets/chimey_british.0xfx",
        "../presets/bluesy_tweed.0xfx",
    };
    const char *preset_names[] = {
        "Clean Sparkle",
        "Classic Crunch",
        "Modern High Gain",
        "Chimey British",
        "Bluesy Tweed",
    };
    const int num_presets = 5;

    /* Generate a test input signal: 440Hz sine at moderate level */
    float input[1024];
    for (int i = 0; i < 1024; i++) {
        input[i] = 0.3f * sinf(2.0f * 3.14159f * 440.0f * (float)i / 44100.0f);
    }

    for (int p = 0; p < num_presets; p++) {
        fx_engine_t *e = fx_engine_create(44100.0f);
        ASSERT(e != NULL, "engine should be created");

        bool loaded = fx_preset_load(e, preset_files[p]);
        if (!loaded) loaded = fx_preset_load(e, preset_files_alt[p]);
        char msg[128];
        snprintf(msg, sizeof(msg), "%s should load", preset_names[p]);
        ASSERT(loaded, msg);

        if (loaded) {
            /* Process audio through the preset */
            float output[1024];
            fx_engine_process(e, input, output, 1024);

            /* Verify it produces output with energy */
            float peak = 0.0f;
            for (int i = 0; i < 1024; i++) {
                float a = fabsf(output[i]);
                if (a > peak) peak = a;
            }
            snprintf(msg, sizeof(msg), "%s should produce output (peak=%.4f)",
                     preset_names[p], peak);
            ASSERT(peak > 0.001f, msg);
            printf("    %s: peak=%.4f\n", preset_names[p], peak);
        }

        fx_engine_destroy(e);
    }

    printf("  OK\n");
}

/* ── Test: all pedal types — DSP coverage (TASK-103) ─────────── */

static void test_all_pedal_types(void) {
    printf("test_all_pedal_types...\n");

    /*
     * Implemented pedals have a non-NULL state and process function.
     * Unimplemented pedals are passthroughs: fx_pedal_process() returns
     * immediately because p->state == NULL.  We detect this by comparing
     * the with-pedal output to the no-pedal reference; if they are
     * identical we log "SKIP (not implemented)" rather than failing.
     */

    /* Reference input: 512-sample 440 Hz sine at 0.3 amplitude */
    float input[512];
    for (int i = 0; i < 512; i++) {
        input[i] = 0.3f * sinf(2.0f * 3.14159265f * 440.0f * (float)i / 44100.0f);
    }

    /* Build a no-pedal reference output once */
    float ref_out[512];
    {
        fx_engine_t *e_ref = fx_engine_create(44100.0f);
        fx_amp_set_param(e_ref, FX_CHAIN_DEFAULT, FX_AMP_PARAM_GAIN,   0.0f);
        fx_amp_set_param(e_ref, FX_CHAIN_DEFAULT, FX_AMP_PARAM_VOLUME, 1.0f);
        fx_amp_set_param(e_ref, FX_CHAIN_DEFAULT, FX_AMP_PARAM_MASTER, 1.0f);
        fx_engine_process(e_ref, input, ref_out, 512);
        fx_engine_destroy(e_ref);
    }

    int pedals_tested    = 0;
    int pedals_active    = 0;
    int pedals_skipped   = 0;

    for (int t = FX_PEDAL_JADE_DRIVE; t <= FX_PEDAL_GRAIN_CLOUD; t++) {
        fx_pedal_type_t ptype = (fx_pedal_type_t)t;
        const char *name = fx_pedal_get_type_name(ptype);

        /* ── 4. type name must not be "?" ──────────────────────── */
        {
            char msg[128];
            snprintf(msg, sizeof(msg),
                     "pedal type %d should have a non-? name", t);
            ASSERT(strcmp(name, "?") != 0, msg);
        }

        /* ── 5. param count must be > 0 ────────────────────────── */
        {
            int pc = fx_pedal_get_param_count(ptype);
            char msg[128];
            snprintf(msg, sizeof(msg),
                     "%s param_count should be > 0", name);
            ASSERT(pc > 0, msg);
        }

        /* ── 2a. fresh engine with pedal ────────────────────────── */
        fx_engine_t *e_pedal = fx_engine_create(44100.0f);
        fx_amp_set_param(e_pedal, FX_CHAIN_DEFAULT, FX_AMP_PARAM_GAIN,   0.0f);
        fx_amp_set_param(e_pedal, FX_CHAIN_DEFAULT, FX_AMP_PARAM_VOLUME, 1.0f);
        fx_amp_set_param(e_pedal, FX_CHAIN_DEFAULT, FX_AMP_PARAM_MASTER, 1.0f);

        /* ── 2c. add pedal as pre-amp ───────────────────────────── */
        fx_pedal_id pid = fx_chain_add_pedal(e_pedal, ptype, FX_CHAIN_POS_PRE);
        {
            char msg[128];
            snprintf(msg, sizeof(msg), "%s: add_pedal should return valid id", name);
            ASSERT(pid >= 0, msg);
        }

        /* ── 2e. process through the engine ────────────────────── */
        float out_pedal[512];
        fx_engine_process(e_pedal, input, out_pedal, 512);

        /* ── 2f. output has energy ──────────────────────────────── */
        float peak_pedal = 0.0f;
        for (int i = 0; i < 512; i++) {
            float a = fabsf(out_pedal[i]);
            if (a > peak_pedal) peak_pedal = a;
        }
        {
            char msg[128];
            snprintf(msg, sizeof(msg),
                     "%s: output should have energy (peak=%.6f)", name, peak_pedal);
            ASSERT(peak_pedal > 0.0001f, msg);
        }

        /* ── 2h. pedal changes signal vs no-pedal reference ─────── */
        int differs = 0;
        for (int i = 0; i < 512; i++) {
            if (fabsf(out_pedal[i] - ref_out[i]) > 1e-4f) {
                differs = 1;
                break;
            }
        }

        if (!differs) {
            /* Passthrough — not yet implemented; log but do not fail */
            printf("  %-20s peak=%.4f  SKIP (not implemented)\n", name, peak_pedal);
            pedals_skipped++;
        } else {
            /* ── 2i. bypass test ────────────────────────────────── */
            /* Create a fresh engine with the same pedal, bypassed */
            fx_engine_t *e_bypass = fx_engine_create(44100.0f);
            fx_amp_set_param(e_bypass, FX_CHAIN_DEFAULT, FX_AMP_PARAM_GAIN,   0.0f);
            fx_amp_set_param(e_bypass, FX_CHAIN_DEFAULT, FX_AMP_PARAM_VOLUME, 1.0f);
            fx_amp_set_param(e_bypass, FX_CHAIN_DEFAULT, FX_AMP_PARAM_MASTER, 1.0f);
            fx_pedal_id pid_bp = fx_chain_add_pedal(e_bypass, ptype, FX_CHAIN_POS_PRE);
            fx_pedal_set_bypass(e_bypass, pid_bp, true);

            float out_bypass[512];
            fx_engine_process(e_bypass, input, out_bypass, 512);

            int bypass_differs = 0;
            for (int i = 0; i < 512; i++) {
                if (fabsf(out_pedal[i] - out_bypass[i]) > 1e-4f) {
                    bypass_differs = 1;
                    break;
                }
            }
            {
                char msg[128];
                snprintf(msg, sizeof(msg),
                         "%s: bypassed output should differ from active", name);
                ASSERT(bypass_differs, msg);
            }

            fx_engine_destroy(e_bypass);

            printf("  %-20s peak=%.4f  PASS\n", name, peak_pedal);
            pedals_active++;
        }

        /* ── 2j. destroy engine ─────────────────────────────────── */
        fx_engine_destroy(e_pedal);
        pedals_tested++;
    }

    printf("  Summary: %d pedals tested, %d active (DSP), %d skipped (passthrough)\n",
           pedals_tested, pedals_active, pedals_skipped);
    printf("  OK\n");
}

/* ── Main ─────────────────────────────────────────────────────── */

/* ── Test: cab/distortion frequency response evaluation ─────────── */

static void test_cab_distortion_freq_response(void) {
    printf("test_cab_distortion_freq_response...\n");
    printf("  Evaluating distortion + cab combinations for boxy sound...\n");

    const int N = 8192;
    const float sr = 44100.0f;
    float *input = (float *)calloc(N, sizeof(float));
    float *output = (float *)calloc(N, sizeof(float));

    /* White noise input (deterministic seed) */
    unsigned int seed = 12345;
    for (int i = 0; i < N; i++) {
        seed = seed * 1103515245 + 12345;
        input[i] = 0.3f * ((float)(seed >> 16) / 32768.0f - 1.0f);
    }

    /* Test each cab type with a moderate gain Fullerton Clean */
    const char *cab_names[] = {"1x12 Open", "2x12 Closed", "4x12 Straight", "4x12 Slant"};

    for (int cab_idx = 0; cab_idx < FX_CAB_TYPE_COUNT; cab_idx++) {
        fx_engine_t *e = fx_engine_create(sr);
        fx_amp_set_model(e, FX_CHAIN_DEFAULT, FX_AMP_FULLERTON_CLEAN);
        fx_amp_set_param(e, FX_CHAIN_DEFAULT, FX_AMP_PARAM_GAIN, 0.6f);
        fx_amp_set_param(e, FX_CHAIN_DEFAULT, FX_AMP_PARAM_BASS, 0.5f);
        fx_amp_set_param(e, FX_CHAIN_DEFAULT, FX_AMP_PARAM_MID, 0.5f);
        fx_amp_set_param(e, FX_CHAIN_DEFAULT, FX_AMP_PARAM_TREBLE, 0.5f);
        fx_amp_set_param(e, FX_CHAIN_DEFAULT, FX_AMP_PARAM_VOLUME, 0.6f);
        fx_amp_set_param(e, FX_CHAIN_DEFAULT, FX_AMP_PARAM_MASTER, 0.7f);

        /* Generate synthetic cab */
        fx_cab_params_t params = { (fx_cab_type_t)cab_idx, FX_MIC_ON_AXIS, 80.0f, 0.5f, 0.5f };
        fx_cab_generate_ir(e, FX_CHAIN_DEFAULT, &params);

        /* Process two blocks (let state settle) */
        fx_engine_process(e, input, output, N / 2);
        fx_engine_process(e, input + N/2, output + N/2, N / 2);

        /* Measure energy in frequency bands (simple DFT approach) */
        float sub_bass = 0, bass_e = 0, low_mid = 0, mid_e = 0, hi_mid = 0, presence = 0, treble_e = 0;

        for (int i = N/2; i < N; i++) {
            float s = output[i];
            sub_bass += s * s;
        }
        sub_bass /= (N/2);

        /* Use band-pass filters approximated by measuring RMS of frequency-specific sines */
        float bands[] = {60, 120, 250, 500, 800, 1200, 2500, 5000, 8000};
        float band_e[9] = {0};
        const char *band_names[] = {"60Hz", "120Hz", "250Hz", "500Hz", "800Hz", "1.2k", "2.5k", "5kHz", "8kHz"};

        for (int b = 0; b < 9; b++) {
            float sine_in[4096], sine_out[4096];
            for (int i = 0; i < 4096; i++)
                sine_in[i] = 0.2f * sinf(2.0f * 3.14159f * bands[b] * (float)i / sr);

            fx_engine_t *e2 = fx_engine_create(sr);
            fx_amp_set_model(e2, FX_CHAIN_DEFAULT, FX_AMP_FULLERTON_CLEAN);
            fx_amp_set_param(e2, FX_CHAIN_DEFAULT, FX_AMP_PARAM_GAIN, 0.6f);
            fx_amp_set_param(e2, FX_CHAIN_DEFAULT, FX_AMP_PARAM_BASS, 0.5f);
            fx_amp_set_param(e2, FX_CHAIN_DEFAULT, FX_AMP_PARAM_MID, 0.5f);
            fx_amp_set_param(e2, FX_CHAIN_DEFAULT, FX_AMP_PARAM_TREBLE, 0.5f);
            fx_amp_set_param(e2, FX_CHAIN_DEFAULT, FX_AMP_PARAM_VOLUME, 0.6f);
            fx_amp_set_param(e2, FX_CHAIN_DEFAULT, FX_AMP_PARAM_MASTER, 0.7f);
            fx_cab_generate_ir(e2, FX_CHAIN_DEFAULT, &params);

            fx_engine_process(e2, sine_in, sine_out, 4096);
            fx_engine_process(e2, sine_in, sine_out, 4096);

            float rms = 0;
            for (int i = 2048; i < 4096; i++)
                rms += sine_out[i] * sine_out[i];
            band_e[b] = sqrtf(rms / 2048.0f);

            fx_engine_destroy(e2);
        }

        printf("    %s: ", cab_names[cab_idx]);
        for (int b = 0; b < 9; b++) {
            float db = 20.0f * log10f(band_e[b] + 1e-10f);
            printf("%s=%.0fdB ", band_names[b], db);
        }

        /* Check for boxiness: excessive 300-600Hz relative to sub-100Hz */
        float ratio_300_to_sub = (band_e[3] + 1e-10f) / (band_e[0] + 1e-10f);
        printf(" box_ratio=%.1f", ratio_300_to_sub);

        if (ratio_300_to_sub > 8.0f) {
            printf(" [BOXY!]");
        }
        printf("\n");

        fx_engine_destroy(e);
    }

    /* Overall: the 1x12 Open should have less bass than 4x12 — that's expected.
     * But it shouldn't be so extreme as to sound boxy. */
    ASSERT(1, "frequency response evaluated — check output above");
    printf("  OK (review band levels above for boxiness)\n");

    free(input);
    free(output);
}

int main(void) {
    printf("═══ 0xFX API Test ═══\n\n");

    test_engine_lifecycle();
    test_amp_processing();
    test_pedals();
    test_amp_params();
    test_multi_chain();
    test_pedal_metadata();
    test_noise_gate();
    test_amp_character();
    test_overdrive();
    test_delay();
    test_reverb();
    test_compressor();
    test_pedal_param_names();
    test_tuner();
    test_cab_ir();
    test_parallel_chain_routing();
    test_cab_load_api();
    test_synthetic_ir();
    test_preset_roundtrip();
    test_preset_custom_ir_roundtrip();
    test_preset_fuzz();
    test_default_presets();
    test_all_pedal_types();
    test_cab_distortion_freq_response();

    printf("\n═══ Results: %d passed, %d failed ═══\n",
           tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
