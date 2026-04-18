/*
 * 0xFX — Preset save/load (.0xfx JSON format)
 *
 * Serializes/deserializes the complete engine state to/from
 * the open .0xfx JSON preset format.
 */
#include "engine_internal.h"
#include "cJSON.h"
#include "../../core/log.h"
#include <stdio.h>

/* ── String ↔ enum mapping helpers ──────────────────────────────── */

/* Lowercase snake_case identifiers used in the JSON format */
static const char *pedal_type_keys[FX_PEDAL_TYPE_COUNT] = {
    /* Overdrive */
    "jade_drive", "gold_drive", "blues_grit",
    /* Distortion */
    "rodent", "orange_dist", "metal_zone", "amp_box",
    /* Fuzz */
    "mammoth_fuzz", "round_fuzz", "wraith_fuzz", "chaos_fuzz",
    /* Delay */
    "echo_delay", "carbon_delay", "tape_machine", "memory_echo",
    /* Reverb */
    "drip_verb", "plate_verb", "hall_verb", "shimmer_verb", "cloud_verb",
    /* Modulation */
    "liquid_chorus", "phase_sweep", "jet_flanger", "pulse_trem", "drift_vibrato",
    /* Wah / Filter */
    "howl_wah", "quack_filter",
    /* Compressor */
    "squeeze_box", "glass_comp", "punch_comp",
    /* EQ */
    "tone_sculptor", "precision_eq",
    /* Noise */
    "noise_gate",
    /* Utility */
    "grit_crush", "ring_tone", "warm_tape",
    /* Pitch */
    "octave_engine", "pitch_warp",
    /* Looper */
    "loop_station",
    /* Experimental */
    "infinite_hold", "grain_cloud",
};

static const char *amp_type_keys[FX_AMP_COUNT] = {
    "fullerton_clean",
    "brit_crunch",
    "southwest_lead",
    "essex_chime",
    "tweed_blues",
    "meridian_high_gain",
    "citrus_roar",
    "citrus_terror",
    "regent_800",
    "solar_monolith",
    "eclipse_drone",
    "emerald_deluxe",
};

static const char *amp_param_keys[FX_AMP_PARAM_COUNT] = {
    "gain", "volume", "bass", "mid", "treble",
    "presence", "sag", "master", "bright", "cut",
    "tone", "feedback",
};

static fx_pedal_type_t pedal_type_from_key(const char *key) {
    if (!key) return FX_PEDAL_TYPE_COUNT;
    for (int i = 0; i < FX_PEDAL_TYPE_COUNT; i++) {
        if (strcmp(key, pedal_type_keys[i]) == 0) return (fx_pedal_type_t)i;
    }
    return FX_PEDAL_TYPE_COUNT;
}

static fx_amp_type_t amp_type_from_key(const char *key) {
    if (!key) return FX_AMP_COUNT;
    for (int i = 0; i < FX_AMP_COUNT; i++) {
        if (strcmp(key, amp_type_keys[i]) == 0) return (fx_amp_type_t)i;
    }
    return FX_AMP_COUNT;
}

static fx_amp_param_t amp_param_from_key(const char *key) {
    if (!key) return FX_AMP_PARAM_COUNT;
    for (int i = 0; i < FX_AMP_PARAM_COUNT; i++) {
        if (strcmp(key, amp_param_keys[i]) == 0) return (fx_amp_param_t)i;
    }
    return FX_AMP_PARAM_COUNT;
}

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* ── Serialize helpers ──────────────────────────────────────────── */

static cJSON *serialize_pedal(fx_pedal_instance_t *p) {
    cJSON *obj = cJSON_CreateObject();
    if (!obj) return NULL;

    cJSON_AddStringToObject(obj, "type", pedal_type_keys[p->type]);
    cJSON_AddBoolToObject(obj, "bypass", p->bypass);

    cJSON *params = cJSON_AddObjectToObject(obj, "params");
    if (params) {
        int pc = fx_pedal_get_param_count(p->type);
        for (int i = 0; i < pc; i++) {
            const char *pname = fx_pedal_get_param_name(p->type, i);
            /* Convert display name to lowercase key */
            char key[64];
            int k = 0;
            for (int j = 0; pname[j] && k < 62; j++) {
                if (pname[j] == ' ')
                    key[k++] = '_';
                else if (pname[j] >= 'A' && pname[j] <= 'Z')
                    key[k++] = (char)(pname[j] + 32);
                else
                    key[k++] = pname[j];
            }
            key[k] = '\0';
            cJSON_AddNumberToObject(params, key, (double)p->params[i]);
        }
    }

    return obj;
}

static cJSON *serialize_chain(fx_signal_chain_t *chain) {
    cJSON *obj = cJSON_CreateObject();
    if (!obj) return NULL;

    /* Amp */
    cJSON *amp = cJSON_AddObjectToObject(obj, "amp");
    if (amp) {
        cJSON_AddStringToObject(amp, "model", amp_type_keys[chain->amp.type]);
        cJSON *params = cJSON_AddObjectToObject(amp, "params");
        if (params) {
            int pc = fx_amp_get_param_count(chain->amp.type);
            for (int i = 0; i < pc && i < FX_AMP_PARAM_COUNT; i++) {
                cJSON_AddNumberToObject(params, amp_param_keys[i],
                                        (double)chain->amp.params[i]);
            }
        }
    }

    /* Cab */
    cJSON *cab = cJSON_AddObjectToObject(obj, "cab");
    if (cab) {
        cJSON_AddBoolToObject(cab, "bypass", chain->cab.bypass);
        if (chain->cab.custom_ir_path[0]) {
            cJSON_AddStringToObject(cab, "custom_ir_path",    chain->cab.custom_ir_path);
            cJSON_AddStringToObject(cab, "custom_name",       chain->cab.custom_name);
            cJSON_AddStringToObject(cab, "custom_image_path", chain->cab.custom_image_path);
        }
    }

    /* Mix */
    cJSON_AddNumberToObject(obj, "mix", (double)chain->mix);

    return obj;
}

/* ── fx_preset_save ─────────────────────────────────────────────── */

bool fx_preset_save(fx_engine_t *engine, const char *path) {
    if (!engine || !path) return false;

    cJSON *root = cJSON_CreateObject();
    if (!root) return false;

    /* Metadata — derive name from filename */
    cJSON_AddStringToObject(root, "format", "0xfx");
    cJSON_AddStringToObject(root, "version", "1.0");
    {
        /* Extract name from path: "presets/dan.0xfx" → "dan" */
        const char *slash = strrchr(path, '/');
        if (!slash) slash = strrchr(path, '\\');
        const char *basename = slash ? slash + 1 : path;
        char preset_name[128];
        strncpy(preset_name, basename, sizeof(preset_name) - 1);
        preset_name[sizeof(preset_name) - 1] = '\0';
        /* Strip .0xfx extension */
        char *dot = strstr(preset_name, ".0xfx");
        if (dot) *dot = '\0';
        cJSON_AddStringToObject(root, "name",
            preset_name[0] ? preset_name : "Untitled Preset");
    }

    /* Signal chain */
    cJSON *sc = cJSON_AddObjectToObject(root, "signal_chain");
    if (!sc) { cJSON_Delete(root); return false; }

    /* Input / noise gate */
    cJSON *input = cJSON_AddObjectToObject(sc, "input");
    if (input) {
        cJSON *ng = cJSON_AddObjectToObject(input, "noise_gate");
        if (ng) {
            cJSON_AddNumberToObject(ng, "threshold_db",
                                    (double)engine->gate.threshold);
            cJSON_AddNumberToObject(ng, "attack_ms",
                                    (double)(engine->gate.attack * 1000.0f));
            cJSON_AddNumberToObject(ng, "release_ms",
                                    (double)(engine->gate.release * 1000.0f));
            cJSON_AddNumberToObject(ng, "hold_ms",
                                    (double)(engine->gate.hold * 1000.0f));
        }
    }

    /* Pre-pedals */
    cJSON *pre = cJSON_AddArrayToObject(sc, "pre_pedals");
    if (pre) {
        for (int i = 0; i < engine->num_pedals; i++) {
            if (engine->pedals[i].position == FX_CHAIN_POS_PRE) {
                cJSON *p = serialize_pedal(&engine->pedals[i]);
                if (p) cJSON_AddItemToArray(pre, p);
            }
        }
    }

    /* Chains */
    cJSON *chains = cJSON_AddArrayToObject(sc, "chains");
    if (chains) {
        for (int i = 0; i < engine->num_chains; i++) {
            if (!engine->chains[i].active) continue;
            cJSON *c = serialize_chain(&engine->chains[i]);
            if (c) cJSON_AddItemToArray(chains, c);
        }
    }

    /* Post-pedals */
    cJSON *post = cJSON_AddArrayToObject(sc, "post_pedals");
    if (post) {
        for (int i = 0; i < engine->num_pedals; i++) {
            if (engine->pedals[i].position == FX_CHAIN_POS_POST) {
                cJSON *p = serialize_pedal(&engine->pedals[i]);
                if (p) cJSON_AddItemToArray(post, p);
            }
        }
    }

    /* Master volume */
    cJSON_AddNumberToObject(sc, "master_volume", (double)engine->master_volume);

    /* Render to string */
    char *json_str = cJSON_Print(root);
    cJSON_Delete(root);
    if (!json_str) return false;

    /* Write to file */
    FILE *f = fopen(path, "wb");
    if (!f) { cJSON_free(json_str); return false; }

    size_t len = strlen(json_str);
    size_t written = fwrite(json_str, 1, len, f);
    fclose(f);
    cJSON_free(json_str);

    return written == len;
}

/* ── Deserialize helpers ────────────────────────────────────────── */

static bool load_pedal_from_json(fx_engine_t *engine, cJSON *obj,
                                  fx_chain_pos_t pos) {
    cJSON *type_item = cJSON_GetObjectItem(obj, "type");
    if (!cJSON_IsString(type_item)) return false;

    fx_pedal_type_t type = pedal_type_from_key(type_item->valuestring);
    if (type >= FX_PEDAL_TYPE_COUNT) return false;

    fx_pedal_id id = fx_chain_add_pedal(engine, type, pos);
    if (id < 0) return false;

    /* Bypass */
    cJSON *bypass_item = cJSON_GetObjectItem(obj, "bypass");
    if (cJSON_IsBool(bypass_item)) {
        fx_pedal_set_bypass(engine, id, cJSON_IsTrue(bypass_item));
    }

    /* Params */
    cJSON *params = cJSON_GetObjectItem(obj, "params");
    if (cJSON_IsObject(params)) {
        int pc = fx_pedal_get_param_count(type);
        for (int i = 0; i < pc; i++) {
            const char *pname = fx_pedal_get_param_name(type, i);
            /* Convert display name to lowercase key */
            char key[64];
            int k = 0;
            for (int j = 0; pname[j] && k < 62; j++) {
                if (pname[j] == ' ')
                    key[k++] = '_';
                else if (pname[j] >= 'A' && pname[j] <= 'Z')
                    key[k++] = (char)(pname[j] + 32);
                else
                    key[k++] = pname[j];
            }
            key[k] = '\0';

            cJSON *val = cJSON_GetObjectItem(params, key);
            if (cJSON_IsNumber(val)) {
                float v = clampf((float)val->valuedouble, 0.0f, 1.0f);
                fx_pedal_set_param(engine, id, i, v);
            }
        }
    }

    return true;
}

static bool load_chain_from_json(fx_engine_t *engine, cJSON *obj,
                                  int chain_idx) {
    /* Amp model */
    cJSON *amp = cJSON_GetObjectItem(obj, "amp");
    if (cJSON_IsObject(amp)) {
        cJSON *model = cJSON_GetObjectItem(amp, "model");
        if (cJSON_IsString(model)) {
            fx_amp_type_t atype = amp_type_from_key(model->valuestring);
            if (atype < FX_AMP_COUNT) {
                fx_amp_set_model(engine, chain_idx, atype);
            }
        }

        cJSON *params = cJSON_GetObjectItem(amp, "params");
        if (cJSON_IsObject(params)) {
            cJSON *item = NULL;
            cJSON_ArrayForEach(item, params) {
                if (!cJSON_IsNumber(item) || !item->string) continue;
                fx_amp_param_t p = amp_param_from_key(item->string);
                if (p < FX_AMP_PARAM_COUNT) {
                    float v = clampf((float)item->valuedouble, 0.0f, 1.0f);
                    fx_amp_set_param(engine, chain_idx, p, v);
                }
            }
        }
    }

    /* Cab bypass + custom IR metadata */
    cJSON *cab = cJSON_GetObjectItem(obj, "cab");
    if (cJSON_IsObject(cab)) {
        cJSON *bypass = cJSON_GetObjectItem(cab, "bypass");
        if (cJSON_IsBool(bypass)) {
            fx_cab_set_bypass(engine, chain_idx, cJSON_IsTrue(bypass));
        }
        cJSON *ir_path_j    = cJSON_GetObjectItem(cab, "custom_ir_path");
        cJSON *cname_j      = cJSON_GetObjectItem(cab, "custom_name");
        cJSON *cimg_j       = cJSON_GetObjectItem(cab, "custom_image_path");
        const char *ir_path = (cJSON_IsString(ir_path_j) && ir_path_j->valuestring)
                              ? ir_path_j->valuestring : "";
        if (*ir_path) {
            if (fx_cab_load_ir(engine, chain_idx, ir_path)) {
                if (cJSON_IsString(cname_j) && cname_j->valuestring)
                    fx_cab_set_custom_name(engine, chain_idx, cname_j->valuestring);
                if (cJSON_IsString(cimg_j) && cimg_j->valuestring)
                    fx_cab_set_custom_image_path(engine, chain_idx, cimg_j->valuestring);
            } else {
                FX_WARN("Preset %s: custom IR not loadable — falling back to synthetic",
                        ir_path);
                fx_cab_clear_custom_ir_path(engine, chain_idx);
                fx_cab_params_t params = { FX_CAB_4X12_STRAIGHT, FX_MIC_ON_AXIS,
                                            80.0f, 0.5f, 0.5f };
                fx_cab_generate_ir(engine, chain_idx, &params);
            }
        } else {
            fx_cab_clear_custom_ir_path(engine, chain_idx);
        }
    }

    /* Mix */
    cJSON *mix = cJSON_GetObjectItem(obj, "mix");
    if (cJSON_IsNumber(mix)) {
        fx_chain_set_mix(engine, chain_idx,
                         clampf((float)mix->valuedouble, 0.0f, 1.0f));
    }

    return true;
}

/* ── fx_preset_load ─────────────────────────────────────────────── */

bool fx_preset_load(fx_engine_t *engine, const char *path) {
    if (!engine || !path) return false;

    /* Read file */
    FILE *f = fopen(path, "rb");
    if (!f) return false;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    if (fsize <= 0 || fsize > 10 * 1024 * 1024) { /* 10MB limit */
        fclose(f);
        return false;
    }
    fseek(f, 0, SEEK_SET);

    char *buf = (char *)malloc((size_t)fsize + 1);
    if (!buf) { fclose(f); return false; }

    size_t rd = fread(buf, 1, (size_t)fsize, f);
    fclose(f);
    buf[rd] = '\0';

    /* Parse JSON */
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) return false;

    /* Validate format */
    cJSON *fmt = cJSON_GetObjectItem(root, "format");
    if (!cJSON_IsString(fmt) || strcmp(fmt->valuestring, "0xfx") != 0) {
        cJSON_Delete(root);
        return false;
    }

    cJSON *sc = cJSON_GetObjectItem(root, "signal_chain");
    if (!cJSON_IsObject(sc)) {
        cJSON_Delete(root);
        return false;
    }

    /* ── Clear existing pedals ─────────────────────────────────── */
    while (engine->num_pedals > 0) {
        fx_chain_remove_pedal(engine, engine->pedals[0].id);
    }

    /* ── Destroy extra chains (keep chain 0) ───────────────────── */
    for (int i = engine->num_chains - 1; i > 0; i--) {
        fx_chain_destroy(engine, i);
    }

    /* ── Noise gate ────────────────────────────────────────────── */
    cJSON *input = cJSON_GetObjectItem(sc, "input");
    if (cJSON_IsObject(input)) {
        cJSON *ng = cJSON_GetObjectItem(input, "noise_gate");
        if (cJSON_IsObject(ng)) {
            cJSON *thresh = cJSON_GetObjectItem(ng, "threshold_db");
            if (cJSON_IsNumber(thresh))
                engine->gate.threshold = clampf((float)thresh->valuedouble,
                                                -80.0f, 0.0f);

            cJSON *atk = cJSON_GetObjectItem(ng, "attack_ms");
            if (cJSON_IsNumber(atk))
                engine->gate.attack = clampf((float)atk->valuedouble / 1000.0f,
                                             0.0001f, 1.0f);

            cJSON *rel = cJSON_GetObjectItem(ng, "release_ms");
            if (cJSON_IsNumber(rel))
                engine->gate.release = clampf((float)rel->valuedouble / 1000.0f,
                                              0.001f, 5.0f);

            cJSON *hld = cJSON_GetObjectItem(ng, "hold_ms");
            if (cJSON_IsNumber(hld))
                engine->gate.hold = clampf((float)hld->valuedouble / 1000.0f,
                                           0.0f, 5.0f);
        }
    }

    /* ── Pre-pedals ────────────────────────────────────────────── */
    cJSON *pre = cJSON_GetObjectItem(sc, "pre_pedals");
    if (cJSON_IsArray(pre)) {
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, pre) {
            load_pedal_from_json(engine, item, FX_CHAIN_POS_PRE);
        }
    }

    /* ── Chains ────────────────────────────────────────────────── */
    cJSON *chains = cJSON_GetObjectItem(sc, "chains");
    if (cJSON_IsArray(chains)) {
        int n = cJSON_GetArraySize(chains);
        for (int i = 0; i < n && i < FX_MAX_CHAINS; i++) {
            cJSON *cobj = cJSON_GetArrayItem(chains, i);
            if (!cJSON_IsObject(cobj)) continue;

            if (i == 0) {
                /* Chain 0 always exists — just configure it */
                load_chain_from_json(engine, cobj, 0);
            } else {
                /* Create additional chain */
                fx_chain_id cid = fx_chain_create(engine);
                if (cid >= 0) {
                    load_chain_from_json(engine, cobj, cid);
                }
            }
        }
    }

    /* ── Post-pedals ───────────────────────────────────────────── */
    cJSON *post = cJSON_GetObjectItem(sc, "post_pedals");
    if (cJSON_IsArray(post)) {
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, post) {
            load_pedal_from_json(engine, item, FX_CHAIN_POS_POST);
        }
    }

    /* ── Master volume ────────────────────────────────────────── */
    cJSON *mvol = cJSON_GetObjectItem(sc, "master_volume");
    if (cJSON_IsNumber(mvol))
        engine->master_volume = clampf((float)mvol->valuedouble, 0.0f, 1.0f);

    cJSON_Delete(root);
    return true;
}
