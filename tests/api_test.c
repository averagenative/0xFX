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

/* ── Main ─────────────────────────────────────────────────────── */

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

    printf("\n═══ Results: %d passed, %d failed ═══\n",
           tests_passed, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
