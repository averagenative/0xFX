/*
 * 0xFX Engine — Core implementation
 *
 * Engine lifecycle, signal routing, and audio processing.
 * Phase 1: passthrough only. Amp/effects wired in Phase 2+.
 */
#include "engine_internal.h"
#include <stdio.h>

/* ── Amp defaults per model ───────────────────────────────────── */

/* Per-model defaults — used in fx_amp_init when we add model-specific defaults */
/* static const float amp_defaults[FX_AMP_COUNT][FX_AMP_PARAM_COUNT] = { ... }; */

/* ── Engine lifecycle ─────────────────────────────────────────── */

fx_engine_t *fx_engine_create(float sample_rate) {
    fx_engine_t *e = (fx_engine_t *)calloc(1, sizeof(fx_engine_t));
    if (!e) return NULL;

    e->sample_rate = sample_rate;
    e->next_pedal_id = 1;

    /* Initialize noise gate with sensible defaults */
    fx_gate_init(&e->gate);

    /* Create default chain (chain 0 always exists) */
    e->num_chains = 1;
    e->chains[0].active = true;
    e->chains[0].mix = 1.0f;
    fx_amp_init(&e->chains[0].amp, FX_AMP_FULLERTON_CLEAN);
    fx_cab_init(&e->chains[0].cab);

    /* Initialize tuner */
    fx_tuner_init(&e->tuner);

    return e;
}

void fx_engine_destroy(fx_engine_t *engine) {
    if (!engine) return;

    /* Free all pedal states */
    for (int i = 0; i < engine->num_pedals; i++) {
        fx_pedal_free_state(&engine->pedals[i]);
    }

    /* Free cab IR buffers */
    for (int i = 0; i < engine->num_chains; i++) {
        fx_cab_free(&engine->chains[i].cab);
    }

    free(engine);
}

/*
 * Main audio processing callback.
 * Called from the audio thread — must be real-time safe.
 * No malloc, no printf, no file I/O, no locks.
 *
 * Phase 1: passthrough (input → output)
 * Phase 2+: input → gate → pre-pedals → amp → cab → post-pedals → output
 */
void fx_engine_process(fx_engine_t *engine,
                       const float *input, float *output,
                       int num_frames) {
    if (!engine || !input || !output || num_frames <= 0) return;
    if (num_frames > FX_MAX_BLOCK_SIZE) num_frames = FX_MAX_BLOCK_SIZE;

    /* Copy input to scratch buffer for in-place processing */
    float *buf = engine->scratch_a;
    memcpy(buf, input, (size_t)num_frames * sizeof(float));

    /* Feed tuner (always on, reads input before processing) */
    fx_tuner_feed(&engine->tuner, input, num_frames, engine->sample_rate);

    float sr = engine->sample_rate;

    /* ── Input stage: noise gate ─────────────────────────────── */
    fx_gate_process(&engine->gate, buf, num_frames, sr);

    /* ── Pre-amp pedals (in order) ───────────────────────────── */
    for (int i = 0; i < engine->num_pedals; i++) {
        if (engine->pedals[i].position == FX_CHAIN_POS_PRE) {
            fx_pedal_process(&engine->pedals[i], buf, num_frames, sr);
        }
    }

    /* ── Amp + cab processing per chain, with mix ────────────── */
    if (engine->num_chains == 1) {
        /* Single chain: process in-place, no mixing needed */
        fx_amp_process(&engine->chains[0].amp, buf, num_frames, sr);
        fx_cab_process(&engine->chains[0].cab, buf, num_frames);

        /* Post-amp pedals */
        for (int i = 0; i < engine->num_pedals; i++) {
            if (engine->pedals[i].position == FX_CHAIN_POS_POST) {
                fx_pedal_process(&engine->pedals[i], buf, num_frames, sr);
            }
        }
    } else {
        /* Multi-chain: split, process each, sum with mix levels */
        float *mix_buf = engine->scratch_b;
        memset(mix_buf, 0, (size_t)num_frames * sizeof(float));

        for (int c = 0; c < engine->num_chains; c++) {
            if (!engine->chains[c].active) continue;

            /* Copy pre-pedal signal into scratch for this chain */
            float chain_buf[FX_MAX_BLOCK_SIZE];
            memcpy(chain_buf, buf, (size_t)num_frames * sizeof(float));

            /* Amp + cab for this chain */
            fx_amp_process(&engine->chains[c].amp, chain_buf, num_frames, sr);
            fx_cab_process(&engine->chains[c].cab, chain_buf, num_frames);

            /* TODO: per-chain post-fx (needs chain-specific pedal assignment) */

            /* Mix into output with chain level */
            float mix = engine->chains[c].mix;
            for (int i = 0; i < num_frames; i++) {
                mix_buf[i] += chain_buf[i] * mix;
            }
        }

        memcpy(buf, mix_buf, (size_t)num_frames * sizeof(float));

        /* Global post-amp pedals (after mix) */
        for (int i = 0; i < engine->num_pedals; i++) {
            if (engine->pedals[i].position == FX_CHAIN_POS_POST) {
                fx_pedal_process(&engine->pedals[i], buf, num_frames, sr);
            }
        }
    }

    /* ── Output ──────────────────────────────────────────────── */
    memcpy(output, buf, (size_t)num_frames * sizeof(float));
}

/* ── Signal chain — pedal management ──────────────────────────── */

fx_pedal_id fx_chain_add_pedal(fx_engine_t *engine,
                               fx_pedal_type_t type,
                               fx_chain_pos_t pos) {
    if (!engine || engine->num_pedals >= FX_MAX_PEDALS_TOTAL) return -1;
    if (type < 0 || type >= FX_PEDAL_TYPE_COUNT) return -1;

    fx_pedal_instance_t *p = &engine->pedals[engine->num_pedals];
    memset(p, 0, sizeof(*p));

    p->type = type;
    p->id = engine->next_pedal_id++;
    p->bypass = false;
    p->position = pos;
    p->order = fx_chain_get_pedal_count(engine, pos);

    fx_pedal_init_state(p, engine->sample_rate);

    engine->num_pedals++;
    return p->id;
}

void fx_chain_remove_pedal(fx_engine_t *engine, fx_pedal_id id) {
    if (!engine) return;

    for (int i = 0; i < engine->num_pedals; i++) {
        if (engine->pedals[i].id == id) {
            fx_pedal_free_state(&engine->pedals[i]);
            /* Shift remaining pedals down */
            for (int j = i; j < engine->num_pedals - 1; j++) {
                engine->pedals[j] = engine->pedals[j + 1];
            }
            engine->num_pedals--;
            return;
        }
    }
}

void fx_chain_move_pedal(fx_engine_t *engine, fx_pedal_id id,
                         fx_chain_pos_t pos, int index) {
    if (!engine) return;

    for (int i = 0; i < engine->num_pedals; i++) {
        if (engine->pedals[i].id == id) {
            engine->pedals[i].position = pos;
            engine->pedals[i].order = index;
            /* TODO: re-sort pedals by position+order for correct processing order */
            return;
        }
    }
}

int fx_chain_get_pedal_count(fx_engine_t *engine, fx_chain_pos_t pos) {
    if (!engine) return 0;
    int count = 0;
    for (int i = 0; i < engine->num_pedals; i++) {
        if (engine->pedals[i].position == pos) count++;
    }
    return count;
}

/* ── Pedal parameter access ───────────────────────────────────── */

static fx_pedal_instance_t *find_pedal(fx_engine_t *engine, fx_pedal_id id) {
    if (!engine) return NULL;
    for (int i = 0; i < engine->num_pedals; i++) {
        if (engine->pedals[i].id == id) return &engine->pedals[i];
    }
    return NULL;
}

void fx_pedal_set_param(fx_engine_t *engine, fx_pedal_id id,
                        int param, float value) {
    fx_pedal_instance_t *p = find_pedal(engine, id);
    if (!p || param < 0 || param >= FX_MAX_PARAMS) return;
    p->params[param] = value;
}

float fx_pedal_get_param(fx_engine_t *engine, fx_pedal_id id, int param) {
    fx_pedal_instance_t *p = find_pedal(engine, id);
    if (!p || param < 0 || param >= FX_MAX_PARAMS) return 0.0f;
    return p->params[param];
}

void fx_pedal_set_bypass(fx_engine_t *engine, fx_pedal_id id, bool bypass) {
    fx_pedal_instance_t *p = find_pedal(engine, id);
    if (p) p->bypass = bypass;
}

bool fx_pedal_get_bypass(fx_engine_t *engine, fx_pedal_id id) {
    fx_pedal_instance_t *p = find_pedal(engine, id);
    return p ? p->bypass : false;
}

fx_pedal_type_t fx_pedal_get_type(fx_engine_t *engine, fx_pedal_id id) {
    fx_pedal_instance_t *p = find_pedal(engine, id);
    return p ? p->type : FX_PEDAL_TYPE_COUNT;
}

/* ── Multi-chain management ───────────────────────────────────── */

fx_chain_id fx_chain_create(fx_engine_t *engine) {
    if (!engine || engine->num_chains >= FX_MAX_CHAINS) return -1;

    int id = engine->num_chains;
    fx_signal_chain_t *c = &engine->chains[id];
    memset(c, 0, sizeof(*c));
    c->active = true;
    c->mix = 0.5f;
    fx_amp_init(&c->amp, FX_AMP_FULLERTON_CLEAN);
    fx_cab_init(&c->cab);

    engine->num_chains++;
    return id;
}

void fx_chain_destroy(fx_engine_t *engine, fx_chain_id id) {
    if (!engine || id <= 0 || id >= engine->num_chains) return;
    /* Don't allow destroying chain 0 (default) */
    fx_cab_free(&engine->chains[id].cab);
    engine->chains[id].active = false;
}

void fx_chain_set_mix(fx_engine_t *engine, fx_chain_id id, float level) {
    if (!engine || id < 0 || id >= engine->num_chains) return;
    engine->chains[id].mix = level < 0.0f ? 0.0f : (level > 1.0f ? 1.0f : level);
}

float fx_chain_get_mix(fx_engine_t *engine, fx_chain_id id) {
    if (!engine || id < 0 || id >= engine->num_chains) return 0.0f;
    return engine->chains[id].mix;
}

int fx_chain_get_count(fx_engine_t *engine) {
    return engine ? engine->num_chains : 0;
}

/* ── Amp model — per chain ────────────────────────────────────── */

void fx_amp_set_model(fx_engine_t *engine, fx_chain_id chain,
                      fx_amp_type_t type) {
    if (!engine || chain < 0 || chain >= engine->num_chains) return;
    if (type < 0 || type >= FX_AMP_COUNT) return;
    fx_amp_init(&engine->chains[chain].amp, type);
}

fx_amp_type_t fx_amp_get_model(fx_engine_t *engine, fx_chain_id chain) {
    if (!engine || chain < 0 || chain >= engine->num_chains)
        return FX_AMP_FULLERTON_CLEAN;
    return engine->chains[chain].amp.type;
}

void fx_amp_set_param(fx_engine_t *engine, fx_chain_id chain,
                      fx_amp_param_t param, float value) {
    if (!engine || chain < 0 || chain >= engine->num_chains) return;
    if (param < 0 || param >= FX_AMP_PARAM_COUNT) return;
    engine->chains[chain].amp.params[param] =
        value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}

float fx_amp_get_param(fx_engine_t *engine, fx_chain_id chain,
                       fx_amp_param_t param) {
    if (!engine || chain < 0 || chain >= engine->num_chains) return 0.0f;
    if (param < 0 || param >= FX_AMP_PARAM_COUNT) return 0.0f;
    return engine->chains[chain].amp.params[param];
}

/* ── Cabinet IR — per chain ───────────────────────────────────── */

bool fx_cab_load_ir(fx_engine_t *engine, fx_chain_id chain,
                    const char *wav_path) {
    if (!engine || chain < 0 || chain >= engine->num_chains || !wav_path)
        return false;
    /* TODO Phase 4: load .wav IR, compute FFT, store in cab state */
    (void)wav_path;
    return false;
}

void fx_cab_set_bypass(fx_engine_t *engine, fx_chain_id chain, bool bypass) {
    if (!engine || chain < 0 || chain >= engine->num_chains) return;
    engine->chains[chain].cab.bypass = bypass;
}

bool fx_cab_get_bypass(fx_engine_t *engine, fx_chain_id chain) {
    if (!engine || chain < 0 || chain >= engine->num_chains) return false;
    return engine->chains[chain].cab.bypass;
}

/* ── Presets ──────────────────────────────────────────────────── */

bool fx_preset_save(fx_engine_t *engine, const char *path) {
    /* TODO Phase 4 */
    (void)engine; (void)path;
    return false;
}

bool fx_preset_load(fx_engine_t *engine, const char *path) {
    /* TODO Phase 4 */
    (void)engine; (void)path;
    return false;
}

/* ── Audio device management stubs (implemented in audio_device.c) */

/* These are defined here as weak symbols; standalone overrides them */
__attribute__((weak)) int fx_audio_get_device_count(void) { return 0; }
__attribute__((weak)) const char *fx_audio_get_device_name(int index) {
    (void)index; return NULL;
}
__attribute__((weak)) bool fx_audio_set_device(fx_engine_t *e, int i) {
    (void)e; (void)i; return false;
}
__attribute__((weak)) bool fx_audio_set_buffer_size(fx_engine_t *e, int f) {
    (void)e; (void)f; return false;
}
__attribute__((weak)) bool fx_audio_set_sample_rate(fx_engine_t *e, float r) {
    (void)e; (void)r; return false;
}
