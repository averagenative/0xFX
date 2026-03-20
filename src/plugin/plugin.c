/*
 * 0xFX — CPLUG plugin implementation
 *
 * Implements all cplug_* callbacks declared in <cplug.h>.
 * Compiled into both the CLAP and VST3 shared-library targets; each target
 * links the corresponding CPLUG format wrapper (cplug_clap.c / cplug_vst3.c)
 * which provides the host-facing entry points and calls back into these
 * functions.
 *
 * Audio layout:
 *   Input  bus 0 — 1 channel  (mono guitar)
 *   Output bus 0 — 2 channels (stereo, engine output duplicated L+R)
 *
 * Parameters (59 total):
 *   Indices  0-11  : Amp knobs (Gain, Volume, Bass, Mid, Treble, Presence,
 *                               Sag, Master, Bright, Cut, Tone, Feedback)
 *   Index   12     : Amp model selector (0 .. FX_AMP_COUNT-1)
 *   Index   13     : Cab type selector (0 .. FX_CAB_TYPE_COUNT-1)
 *   Index   14     : Cab bypass (0/1)
 *   Index   15     : Mic type selector (0 .. FX_MIC_COUNT-1)
 *   Indices 16-18  : Mic params (Distance, Angle, Position)
 *   Indices 19-22  : Studio slot 0  (type + 3 generic params)
 *   Indices 23-26  : Studio slot 1
 *   Indices 27-30  : Studio slot 2
 *   Indices 31-34  : Studio slot 3
 *   Indices 35-38  : Pre-pedal slot 0  (type + 3 generic params)
 *   Indices 39-42  : Pre-pedal slot 1
 *   Indices 43-46  : Pre-pedal slot 2
 *   Indices 47-50  : Post-pedal slot 0
 *   Indices 51-54  : Post-pedal slot 1
 *   Indices 55-58  : Post-pedal slot 2
 *
 * Pedal/studio type selector value 0 means "no pedal/processor" (slot empty).
 * Values 1 .. TYPE_COUNT map to the corresponding type enum (value - 1).
 */

#include <cplug.h>
#include <fx_engine.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Parameter layout constants ─────────────────────────────────── */

#define NUM_AMP_KNOBS    12
#define IDX_AMP_MODEL    NUM_AMP_KNOBS          /* index 12 */
#define NUM_AMP_PARAMS   (NUM_AMP_KNOBS + 1)    /* 13: knobs + model selector */

#define IDX_CAB_TYPE     NUM_AMP_PARAMS         /* index 13 */
#define IDX_CAB_BYPASS   (IDX_CAB_TYPE + 1)     /* index 14 */
#define NUM_CAB_PARAMS   2

#define IDX_MIC_TYPE     (IDX_CAB_BYPASS + 1)   /* index 15 */
#define IDX_MIC_DISTANCE (IDX_MIC_TYPE + 1)     /* index 16 */
#define IDX_MIC_ANGLE    (IDX_MIC_TYPE + 2)     /* index 17 */
#define IDX_MIC_POSITION (IDX_MIC_TYPE + 3)     /* index 18 */
#define NUM_MIC_PARAMS   4                      /* type + 3 placement */

#define NUM_STUDIO_SLOTS   4
#define PARAMS_PER_STUDIO  4   /* type selector + 3 generic params */
#define IDX_STUDIO_START   (IDX_MIC_TYPE + NUM_MIC_PARAMS)  /* index 19 */

#define NUM_PEDAL_SLOTS  6   /* 3 pre + 3 post */
#define PARAMS_PER_PEDAL 4   /* type selector + 3 generic knob params */
#define IDX_PEDAL_START  (IDX_STUDIO_START + NUM_STUDIO_SLOTS * PARAMS_PER_STUDIO)  /* index 35 */

#define NUM_PARAMS       (IDX_PEDAL_START + NUM_PEDAL_SLOTS * PARAMS_PER_PEDAL)
/* = 35 + 24 = 59 */

/* First param index for pedal block n (0-based) */
#define PEDAL_BLOCK_START(n)   (IDX_PEDAL_START + (n) * PARAMS_PER_PEDAL)

/* First param index for studio block n (0-based) */
#define STUDIO_BLOCK_START(n)  (IDX_STUDIO_START + (n) * PARAMS_PER_STUDIO)

/* ── Amp parameter table ─────────────────────────────────────────── */

typedef struct {
    uint32_t      id;
    fx_amp_param_t amp_param;
    float         min_val;
    float         max_val;
    float         default_val;
    const char   *name;
} AmpParamDef;

#define PARAM_ID_AMP_MODEL   'axAM'
#define PARAM_ID_CAB_TYPE    'cxTY'
#define PARAM_ID_CAB_BYPASS  'cxBP'
#define PARAM_ID_MIC_TYPE    'mxTY'
#define PARAM_ID_MIC_DIST    'mx00'
#define PARAM_ID_MIC_ANGLE   'mx01'
#define PARAM_ID_MIC_POS     'mx02'

static const AmpParamDef AMP_KNOBS[NUM_AMP_KNOBS] = {
    { 'ax00', FX_AMP_PARAM_GAIN,     0.0f, 10.0f, 5.0f, "Gain"     },
    { 'ax01', FX_AMP_PARAM_VOLUME,   0.0f, 10.0f, 5.0f, "Volume"   },
    { 'ax02', FX_AMP_PARAM_BASS,     0.0f, 10.0f, 5.0f, "Bass"     },
    { 'ax03', FX_AMP_PARAM_MID,      0.0f, 10.0f, 5.0f, "Mid"      },
    { 'ax04', FX_AMP_PARAM_TREBLE,   0.0f, 10.0f, 5.0f, "Treble"   },
    { 'ax05', FX_AMP_PARAM_PRESENCE, 0.0f, 10.0f, 5.0f, "Presence" },
    { 'ax06', FX_AMP_PARAM_SAG,      0.0f,  1.0f, 0.5f, "Sag"      },
    { 'ax07', FX_AMP_PARAM_MASTER,   0.0f, 10.0f, 5.0f, "Master"   },
    { 'ax08', FX_AMP_PARAM_BRIGHT,   0.0f,  1.0f, 0.0f, "Bright"   },
    { 'ax09', FX_AMP_PARAM_CUT,      0.0f, 10.0f, 5.0f, "Cut"      },
    { 'ax10', FX_AMP_PARAM_TONE,     0.0f, 10.0f, 5.0f, "Tone"     },
    { 'ax11', FX_AMP_PARAM_FEEDBACK, 0.0f, 10.0f, 0.0f, "Feedback" },
};

/* ── Pedal slot descriptors ─────────────────────────────────────── */

/*
 * Slots 0-2 are PRE, slots 3-5 are POST.
 * Param IDs for pedal slots are built from 'px' prefix + slot + sub-index.
 *   e.g. slot 0 param 0 => 'px00', slot 0 param 1 => 'px01', ...
 *        slot 1 param 0 => 'px10', etc.
 */

static uint32_t pedal_param_id(int slot, int sub)
{
    /* slot 0-5, sub 0-3 -> unique 4-byte ID */
    return (uint32_t)('p' | ((uint32_t)'x' << 8) |
                      ((uint32_t)('0' + slot) << 16) |
                      ((uint32_t)('0' + sub) << 24));
}

/* Studio slot param IDs: 'sx' prefix + slot + sub */
static uint32_t studio_param_id(int slot, int sub)
{
    return (uint32_t)('s' | ((uint32_t)'x' << 8) |
                      ((uint32_t)('0' + slot) << 16) |
                      ((uint32_t)('0' + sub) << 24));
}

static fx_chain_pos_t slot_pos(int slot)
{
    return (slot < 3) ? FX_CHAIN_POS_PRE : FX_CHAIN_POS_POST;
}

/* ── Plugin state struct ─────────────────────────────────────────── */

typedef struct {
    CplugHostContext *host_ctx;
    fx_engine_t      *engine;

    float     sample_rate;
    uint32_t  max_block_size;

    /* Parameter shadow values — all params */
    float     param_values[NUM_PARAMS];

    /*
     * Active pedal IDs for each slot.
     * -1 means empty (no pedal in this slot).
     */
    fx_pedal_id pedal_ids[NUM_PEDAL_SLOTS];

    /*
     * Active studio processor IDs for each slot.
     * -1 means empty (no processor in this slot).
     */
    fx_studio_id studio_ids[NUM_STUDIO_SLOTS];
} OxFXPlugin;

/* ── Type-from-param helpers ────────────────────────────────────── */

/*
 * Type-selector parameters use value 0 = "none", values 1..N map
 * to the corresponding type enum (value - 1).
 */
static int type_from_param(float v, int type_count)
{
    int t = (int)(v + 0.5f) - 1;
    if (t < 0) return -1;
    if (t >= type_count) t = type_count - 1;
    return t; /* -1 means empty */
}

/* ── Param index helpers ─────────────────────────────────────────── */

/* Returns the linear param index [0, NUM_PARAMS) for a given CPLUG param ID.
 * Returns NUM_PARAMS if not found. */
static int find_param_index(uint32_t param_id)
{
    /* Amp model selector */
    if (param_id == PARAM_ID_AMP_MODEL)
        return IDX_AMP_MODEL;

    /* Cab params */
    if (param_id == PARAM_ID_CAB_TYPE)   return IDX_CAB_TYPE;
    if (param_id == PARAM_ID_CAB_BYPASS) return IDX_CAB_BYPASS;

    /* Mic params */
    if (param_id == PARAM_ID_MIC_TYPE)   return IDX_MIC_TYPE;
    if (param_id == PARAM_ID_MIC_DIST)   return IDX_MIC_DISTANCE;
    if (param_id == PARAM_ID_MIC_ANGLE)  return IDX_MIC_ANGLE;
    if (param_id == PARAM_ID_MIC_POS)    return IDX_MIC_POSITION;

    /* Amp knobs */
    for (int i = 0; i < NUM_AMP_KNOBS; i++) {
        if (AMP_KNOBS[i].id == param_id)
            return i;
    }

    /* Studio params */
    for (int slot = 0; slot < NUM_STUDIO_SLOTS; slot++) {
        for (int sub = 0; sub < PARAMS_PER_STUDIO; sub++) {
            if (studio_param_id(slot, sub) == param_id)
                return STUDIO_BLOCK_START(slot) + sub;
        }
    }

    /* Pedal params */
    for (int slot = 0; slot < NUM_PEDAL_SLOTS; slot++) {
        for (int sub = 0; sub < PARAMS_PER_PEDAL; sub++) {
            if (pedal_param_id(slot, sub) == param_id)
                return PEDAL_BLOCK_START(slot) + sub;
        }
    }

    return NUM_PARAMS; /* not found */
}

static float param_min(int index)
{
    if (index < NUM_AMP_KNOBS)      return AMP_KNOBS[index].min_val;
    if (index == IDX_AMP_MODEL)     return 0.0f;
    if (index == IDX_CAB_TYPE)      return 0.0f;
    if (index == IDX_CAB_BYPASS)    return 0.0f;
    if (index == IDX_MIC_TYPE)      return 0.0f;
    if (index >= IDX_MIC_DISTANCE && index <= IDX_MIC_POSITION) return 0.0f;

    /* Studio sub-params */
    if (index >= IDX_STUDIO_START && index < IDX_PEDAL_START)
        return 0.0f;

    /* Pedal sub-params */
    if (index >= IDX_PEDAL_START && index < NUM_PARAMS)
        return 0.0f;

    return 0.0f;
}

static float param_max(int index)
{
    if (index < NUM_AMP_KNOBS)      return AMP_KNOBS[index].max_val;
    if (index == IDX_AMP_MODEL)     return (float)(FX_AMP_COUNT - 1);
    if (index == IDX_CAB_TYPE)      return (float)(FX_CAB_TYPE_COUNT - 1);
    if (index == IDX_CAB_BYPASS)    return 1.0f;
    if (index == IDX_MIC_TYPE)      return (float)(FX_MIC_COUNT - 1);
    if (index >= IDX_MIC_DISTANCE && index <= IDX_MIC_POSITION) return 1.0f;

    /* Studio sub-params */
    if (index >= IDX_STUDIO_START && index < IDX_PEDAL_START) {
        int offset = index - IDX_STUDIO_START;
        int sub    = offset % PARAMS_PER_STUDIO;
        if (sub == 0) return (float)FX_STUDIO_COUNT; /* 0 = none, 1..N = type */
        return 1.0f;
    }

    /* Pedal sub-params */
    if (index >= IDX_PEDAL_START && index < NUM_PARAMS) {
        int offset = index - IDX_PEDAL_START;
        int sub    = offset % PARAMS_PER_PEDAL;
        if (sub == 0) return (float)FX_PEDAL_TYPE_COUNT; /* 0 = none, 1..N = type */
        return 1.0f;
    }

    return 1.0f;
}

static float param_default(int index)
{
    if (index < NUM_AMP_KNOBS)      return AMP_KNOBS[index].default_val;
    if (index == IDX_AMP_MODEL)     return 0.0f;
    if (index == IDX_CAB_TYPE)      return 0.0f;  /* 1x12 open */
    if (index == IDX_CAB_BYPASS)    return 0.0f;  /* cab enabled */
    if (index == IDX_MIC_TYPE)      return 0.0f;  /* DI (no coloration) */
    if (index >= IDX_MIC_DISTANCE && index <= IDX_MIC_POSITION) return 0.5f;
    return 0.0f; /* studio/pedal slots: empty / knobs at min */
}

/* ── Engine apply helpers ────────────────────────────────────────── */

/*
 * Ensure a pedal slot matches the requested type.
 * If type_idx == -1, remove any existing pedal.
 * Otherwise, if the slot's current pedal is of a different type, remove it
 * and add a new one.
 * Returns the (possibly new) pedal ID, or -1 if slot is empty.
 */
static fx_pedal_id sync_pedal_slot(OxFXPlugin *p, int slot, int type_idx)
{
    fx_pedal_id cur_id = p->pedal_ids[slot];

    /* Check if already correct */
    if (cur_id >= 0) {
        fx_pedal_type_t cur_type = fx_pedal_get_type(p->engine, cur_id);
        if (type_idx < 0) {
            /* Remove */
            fx_chain_remove_pedal(p->engine, cur_id);
            p->pedal_ids[slot] = -1;
            return -1;
        }
        if ((int)cur_type == type_idx) {
            return cur_id; /* already correct type */
        }
        /* Wrong type — remove old */
        fx_chain_remove_pedal(p->engine, cur_id);
        p->pedal_ids[slot] = -1;
    }

    if (type_idx < 0) return -1;

    /* Add new pedal */
    fx_chain_pos_t pos = slot_pos(slot);
    fx_pedal_id new_id = fx_chain_add_pedal(p->engine,
                                             (fx_pedal_type_t)type_idx, pos);
    p->pedal_ids[slot] = new_id;

    /* Restore the 3 generic param values for this slot */
    if (new_id >= 0) {
        int base = PEDAL_BLOCK_START(slot);
        for (int sub = 1; sub < PARAMS_PER_PEDAL; sub++) {
            fx_pedal_set_param(p->engine, new_id, sub - 1,
                               p->param_values[base + sub]);
        }
    }
    return new_id;
}

/*
 * Ensure a studio slot matches the requested type.
 * Same pattern as sync_pedal_slot.
 */
static fx_studio_id sync_studio_slot(OxFXPlugin *p, int slot, int type_idx)
{
    fx_studio_id cur_id = p->studio_ids[slot];

    if (cur_id >= 0) {
        fx_studio_type_t cur_type = fx_studio_get_type(p->engine, cur_id);
        if (type_idx < 0) {
            fx_studio_remove(p->engine, cur_id);
            p->studio_ids[slot] = -1;
            return -1;
        }
        if ((int)cur_type == type_idx) {
            return cur_id;
        }
        fx_studio_remove(p->engine, cur_id);
        p->studio_ids[slot] = -1;
    }

    if (type_idx < 0) return -1;

    fx_studio_id new_id = fx_studio_add(p->engine,
                                         (fx_studio_type_t)type_idx);
    p->studio_ids[slot] = new_id;

    /* Restore the 3 generic param values for this slot */
    if (new_id >= 0) {
        int base = STUDIO_BLOCK_START(slot);
        for (int sub = 1; sub < PARAMS_PER_STUDIO; sub++) {
            fx_studio_set_param(p->engine, new_id, sub - 1,
                                p->param_values[base + sub]);
        }
    }
    return new_id;
}

/* Apply a parameter value to the engine */
static void apply_param(OxFXPlugin *p, int index, float value)
{
    if (!p->engine) return;

    /* Amp knob */
    if (index < NUM_AMP_KNOBS) {
        fx_amp_set_param(p->engine, FX_CHAIN_DEFAULT,
                         AMP_KNOBS[index].amp_param, value);
        return;
    }

    /* Amp model selector */
    if (index == IDX_AMP_MODEL) {
        int model = (int)(value + 0.5f);
        if (model < 0) model = 0;
        if (model >= FX_AMP_COUNT) model = FX_AMP_COUNT - 1;
        fx_amp_set_model(p->engine, FX_CHAIN_DEFAULT, (fx_amp_type_t)model);
        return;
    }

    /* Cab type */
    if (index == IDX_CAB_TYPE) {
        int cab = (int)(value + 0.5f);
        if (cab < 0) cab = 0;
        if (cab >= FX_CAB_TYPE_COUNT) cab = FX_CAB_TYPE_COUNT - 1;
        /* Generate a synthetic IR with the selected cab type and current mic pos */
        fx_cab_params_t params;
        params.cab_type   = (fx_cab_type_t)cab;
        params.mic_pos    = FX_MIC_ON_AXIS;
        params.speaker_fs = 80.0f;
        params.brightness = 0.5f;
        params.resonance  = 0.5f;
        fx_cab_generate_ir(p->engine, FX_CHAIN_DEFAULT, &params);
        return;
    }

    /* Cab bypass */
    if (index == IDX_CAB_BYPASS) {
        fx_cab_set_bypass(p->engine, FX_CHAIN_DEFAULT, value >= 0.5f);
        return;
    }

    /* Mic type */
    if (index == IDX_MIC_TYPE) {
        int mic = (int)(value + 0.5f);
        if (mic < 0) mic = 0;
        if (mic >= FX_MIC_COUNT) mic = FX_MIC_COUNT - 1;
        fx_mic_set_type(p->engine, FX_CHAIN_DEFAULT, (fx_mic_type_t)mic);
        return;
    }

    /* Mic placement params */
    if (index == IDX_MIC_DISTANCE) {
        fx_mic_set_param(p->engine, FX_CHAIN_DEFAULT, FX_MIC_PARAM_DISTANCE, value);
        return;
    }
    if (index == IDX_MIC_ANGLE) {
        fx_mic_set_param(p->engine, FX_CHAIN_DEFAULT, FX_MIC_PARAM_ANGLE, value);
        return;
    }
    if (index == IDX_MIC_POSITION) {
        fx_mic_set_param(p->engine, FX_CHAIN_DEFAULT, FX_MIC_PARAM_POSITION, value);
        return;
    }

    /* Studio processor param */
    if (index >= IDX_STUDIO_START && index < IDX_PEDAL_START) {
        int offset = index - IDX_STUDIO_START;
        int slot   = offset / PARAMS_PER_STUDIO;
        int sub    = offset % PARAMS_PER_STUDIO;

        if (sub == 0) {
            /* Type selector — may add or remove the processor */
            int type_idx = type_from_param(value, FX_STUDIO_COUNT);
            sync_studio_slot(p, slot, type_idx);
        } else {
            /* Generic knob param (0-indexed: sub-1) */
            fx_studio_id sid = p->studio_ids[slot];
            if (sid >= 0) {
                fx_studio_set_param(p->engine, sid, sub - 1, value);
            }
        }
        return;
    }

    /* Pedal param */
    if (index >= IDX_PEDAL_START && index < NUM_PARAMS) {
        int offset = index - IDX_PEDAL_START;
        int slot   = offset / PARAMS_PER_PEDAL;
        int sub    = offset % PARAMS_PER_PEDAL;

        if (sub == 0) {
            /* Type selector — may add or remove the pedal */
            int type_idx = type_from_param(value, FX_PEDAL_TYPE_COUNT);
            sync_pedal_slot(p, slot, type_idx);
        } else {
            /* Generic knob param (0-indexed: sub-1) */
            fx_pedal_id pid = p->pedal_ids[slot];
            if (pid >= 0) {
                fx_pedal_set_param(p->engine, pid, sub - 1, value);
            }
        }
        return;
    }
}

/* ── Library load/unload ────────────────────────────────────────── */

void cplug_libraryLoad(void)   {}
void cplug_libraryUnload(void) {}

/* ── Plugin lifecycle ───────────────────────────────────────────── */

void *cplug_createPlugin(CplugHostContext *ctx)
{
    OxFXPlugin *p = (OxFXPlugin *)calloc(1, sizeof(OxFXPlugin));
    if (!p) return NULL;

    p->host_ctx    = ctx;
    p->sample_rate = 44100.0f;

    /* Initialise pedal IDs to "empty" */
    for (int i = 0; i < NUM_PEDAL_SLOTS; i++)
        p->pedal_ids[i] = -1;

    /* Initialise studio IDs to "empty" */
    for (int i = 0; i < NUM_STUDIO_SLOTS; i++)
        p->studio_ids[i] = -1;

    p->engine = fx_engine_create(p->sample_rate);
    if (!p->engine) {
        free(p);
        return NULL;
    }

    /* Initialise parameter shadow values to defaults and apply to engine */
    for (int i = 0; i < NUM_PARAMS; i++) {
        p->param_values[i] = param_default(i);
        apply_param(p, i, p->param_values[i]);
    }

    return p;
}

void cplug_destroyPlugin(void *ptr)
{
    OxFXPlugin *p = (OxFXPlugin *)ptr;
    if (!p) return;
    if (p->engine) fx_engine_destroy(p->engine);
    free(p);
}

/* ── Bus layout ─────────────────────────────────────────────────── */

uint32_t cplug_getNumInputBusses(void *ptr)                       { (void)ptr; return 1; }
uint32_t cplug_getNumOutputBusses(void *ptr)                      { (void)ptr; return 1; }
uint32_t cplug_getInputBusChannelCount(void *ptr, uint32_t idx)   { (void)ptr; (void)idx; return 1; }
uint32_t cplug_getOutputBusChannelCount(void *ptr, uint32_t idx)  { (void)ptr; (void)idx; return 2; }

void cplug_getInputBusName(void *ptr, uint32_t idx, char *buf, size_t buflen)
{
    (void)ptr; (void)idx;
    snprintf(buf, buflen, "Mono Input");
}

void cplug_getOutputBusName(void *ptr, uint32_t idx, char *buf, size_t buflen)
{
    (void)ptr; (void)idx;
    snprintf(buf, buflen, "Stereo Output");
}

/* ── Parameters ─────────────────────────────────────────────────── */

uint32_t cplug_getNumParameters(void *ptr)
{
    (void)ptr;
    return (uint32_t)NUM_PARAMS;
}

uint32_t cplug_getParameterID(void *ptr, uint32_t param_index)
{
    (void)ptr;
    int i = (int)param_index;

    if (i < NUM_AMP_KNOBS)      return AMP_KNOBS[i].id;
    if (i == IDX_AMP_MODEL)     return PARAM_ID_AMP_MODEL;
    if (i == IDX_CAB_TYPE)      return PARAM_ID_CAB_TYPE;
    if (i == IDX_CAB_BYPASS)    return PARAM_ID_CAB_BYPASS;
    if (i == IDX_MIC_TYPE)      return PARAM_ID_MIC_TYPE;
    if (i == IDX_MIC_DISTANCE)  return PARAM_ID_MIC_DIST;
    if (i == IDX_MIC_ANGLE)     return PARAM_ID_MIC_ANGLE;
    if (i == IDX_MIC_POSITION)  return PARAM_ID_MIC_POS;

    /* Studio params */
    if (i >= IDX_STUDIO_START && i < IDX_PEDAL_START) {
        int offset = i - IDX_STUDIO_START;
        int slot   = offset / PARAMS_PER_STUDIO;
        int sub    = offset % PARAMS_PER_STUDIO;
        return studio_param_id(slot, sub);
    }

    /* Pedal params */
    if (i >= IDX_PEDAL_START && i < NUM_PARAMS) {
        int offset = i - IDX_PEDAL_START;
        int slot   = offset / PARAMS_PER_PEDAL;
        int sub    = offset % PARAMS_PER_PEDAL;
        return pedal_param_id(slot, sub);
    }

    return 0;
}

uint32_t cplug_getParameterFlags(void *ptr, uint32_t param_id)
{
    (void)ptr;
    int index = find_param_index(param_id);
    if (index >= NUM_PARAMS) return 0;

    uint32_t flags = CPLUG_FLAG_PARAMETER_IS_AUTOMATABLE;

    /* Amp model selector — integer enum */
    if (index == IDX_AMP_MODEL) {
        flags |= CPLUG_FLAG_PARAMETER_IS_INTEGER;
        return flags;
    }

    /* Cab type selector — integer enum */
    if (index == IDX_CAB_TYPE) {
        flags |= CPLUG_FLAG_PARAMETER_IS_INTEGER;
        return flags;
    }

    /* Cab bypass — boolean */
    if (index == IDX_CAB_BYPASS) {
        flags |= CPLUG_FLAG_PARAMETER_IS_BOOL;
        return flags;
    }

    /* Mic type selector — integer enum */
    if (index == IDX_MIC_TYPE) {
        flags |= CPLUG_FLAG_PARAMETER_IS_INTEGER;
        return flags;
    }

    /* Bright switch — boolean */
    if (index < NUM_AMP_KNOBS &&
        AMP_KNOBS[index].amp_param == FX_AMP_PARAM_BRIGHT) {
        flags |= CPLUG_FLAG_PARAMETER_IS_BOOL;
        return flags;
    }

    /* Studio type selectors — integer enum */
    if (index >= IDX_STUDIO_START && index < IDX_PEDAL_START) {
        int offset = index - IDX_STUDIO_START;
        int sub    = offset % PARAMS_PER_STUDIO;
        if (sub == 0)
            flags |= CPLUG_FLAG_PARAMETER_IS_INTEGER;
    }

    /* Pedal type selectors — integer enum */
    if (index >= IDX_PEDAL_START && index < NUM_PARAMS) {
        int offset = index - IDX_PEDAL_START;
        int sub    = offset % PARAMS_PER_PEDAL;
        if (sub == 0)
            flags |= CPLUG_FLAG_PARAMETER_IS_INTEGER;
    }

    return flags;
}

void cplug_getParameterRange(void *ptr, uint32_t param_id, double *min, double *max)
{
    (void)ptr;
    int index = find_param_index(param_id);
    if (index >= NUM_PARAMS) { *min = 0.0; *max = 1.0; return; }
    *min = (double)param_min(index);
    *max = (double)param_max(index);
}

void cplug_getParameterName(void *ptr, uint32_t param_id, char *buf, size_t buflen)
{
    (void)ptr;
    int index = find_param_index(param_id);

    if (index < NUM_AMP_KNOBS) {
        snprintf(buf, buflen, "%s", AMP_KNOBS[index].name);
        return;
    }
    if (index == IDX_AMP_MODEL) {
        snprintf(buf, buflen, "Amp Model");
        return;
    }
    if (index == IDX_CAB_TYPE) {
        snprintf(buf, buflen, "Cab Type");
        return;
    }
    if (index == IDX_CAB_BYPASS) {
        snprintf(buf, buflen, "Cab Bypass");
        return;
    }
    if (index == IDX_MIC_TYPE) {
        snprintf(buf, buflen, "Mic Type");
        return;
    }
    if (index == IDX_MIC_DISTANCE) {
        snprintf(buf, buflen, "Mic Distance");
        return;
    }
    if (index == IDX_MIC_ANGLE) {
        snprintf(buf, buflen, "Mic Angle");
        return;
    }
    if (index == IDX_MIC_POSITION) {
        snprintf(buf, buflen, "Mic Position");
        return;
    }

    /* Studio slot params */
    if (index >= IDX_STUDIO_START && index < IDX_PEDAL_START) {
        int offset   = index - IDX_STUDIO_START;
        int slot     = offset / PARAMS_PER_STUDIO;
        int sub      = offset % PARAMS_PER_STUDIO;
        if (sub == 0) {
            snprintf(buf, buflen, "Studio %d Type", slot + 1);
        } else {
            snprintf(buf, buflen, "Studio %d P%d", slot + 1, sub);
        }
        return;
    }

    /* Pedal slot params */
    if (index >= IDX_PEDAL_START && index < NUM_PARAMS) {
        int offset   = index - IDX_PEDAL_START;
        int slot     = offset / PARAMS_PER_PEDAL;
        int sub      = offset % PARAMS_PER_PEDAL;
        const char *pos_name = (slot < 3) ? "Pre" : "Post";
        int slot_num = (slot < 3) ? slot + 1 : slot - 2;
        if (sub == 0) {
            snprintf(buf, buflen, "%s Pedal %d Type", pos_name, slot_num);
        } else {
            snprintf(buf, buflen, "%s Pedal %d P%d", pos_name, slot_num, sub);
        }
        return;
    }

    snprintf(buf, buflen, "Unknown");
}

double cplug_getParameterValue(void *ptr, uint32_t param_id)
{
    OxFXPlugin *p = (OxFXPlugin *)ptr;
    int index = find_param_index(param_id);
    if (index >= NUM_PARAMS) return 0.0;
    return (double)p->param_values[index];
}

double cplug_getDefaultParameterValue(void *ptr, uint32_t param_id)
{
    (void)ptr;
    int index = find_param_index(param_id);
    if (index >= NUM_PARAMS) return 0.0;
    return (double)param_default(index);
}

void cplug_setParameterValue(void *ptr, uint32_t param_id, double value)
{
    OxFXPlugin *p = (OxFXPlugin *)ptr;
    int index = find_param_index(param_id);
    if (index >= NUM_PARAMS) return;

    float fmin = param_min(index);
    float fmax = param_max(index);
    float fval = (float)value;
    if (fval < fmin) fval = fmin;
    if (fval > fmax) fval = fmax;

    p->param_values[index] = fval;
    apply_param(p, index, fval);
}

double cplug_normaliseParameterValue(void *ptr, uint32_t param_id, double denormalised)
{
    (void)ptr;
    int index = find_param_index(param_id);
    if (index >= NUM_PARAMS) return 0.0;

    double lo    = (double)param_min(index);
    double hi    = (double)param_max(index);
    double range = hi - lo;
    if (range <= 0.0) return 0.0;

    double normalised = (denormalised - lo) / range;
    if (normalised < 0.0) normalised = 0.0;
    if (normalised > 1.0) normalised = 1.0;
    return normalised;
}

double cplug_denormaliseParameterValue(void *ptr, uint32_t param_id, double normalised)
{
    (void)ptr;
    int index = find_param_index(param_id);
    if (index >= NUM_PARAMS) return 0.0;

    double lo = (double)param_min(index);
    double hi = (double)param_max(index);
    double v  = normalised * (hi - lo) + lo;
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    return v;
}

double cplug_parameterStringToValue(void *ptr, uint32_t param_id, const char *str)
{
    (void)ptr;
    int index = find_param_index(param_id);
    if (index >= NUM_PARAMS) return 0.0;

    /* Integer / boolean params */
    if (index == IDX_AMP_MODEL)  return (double)atoi(str);
    if (index == IDX_CAB_TYPE)   return (double)atoi(str);
    if (index == IDX_CAB_BYPASS) return (double)atoi(str);
    if (index == IDX_MIC_TYPE)   return (double)atoi(str);
    if (index < NUM_AMP_KNOBS &&
        AMP_KNOBS[index].amp_param == FX_AMP_PARAM_BRIGHT)
        return (double)atoi(str);

    /* Studio type selectors */
    if (index >= IDX_STUDIO_START && index < IDX_PEDAL_START) {
        int offset = index - IDX_STUDIO_START;
        int sub    = offset % PARAMS_PER_STUDIO;
        if (sub == 0) return (double)atoi(str);
    }

    /* Pedal type selectors */
    if (index >= IDX_PEDAL_START && index < NUM_PARAMS) {
        int offset = index - IDX_PEDAL_START;
        int sub    = offset % PARAMS_PER_PEDAL;
        if (sub == 0) return (double)atoi(str);
    }

    return atof(str);
}

void cplug_parameterValueToString(void *ptr, uint32_t param_id,
                                  char *buf, size_t bufsize, double value)
{
    (void)ptr;
    int index = find_param_index(param_id);
    if (index >= NUM_PARAMS) { snprintf(buf, bufsize, "0"); return; }

    /* Amp model name */
    if (index == IDX_AMP_MODEL) {
        int model = (int)(value + 0.5);
        if (model < 0) model = 0;
        if (model >= FX_AMP_COUNT) model = FX_AMP_COUNT - 1;
        static const char *amp_names[] = {
            "Fullerton Clean", "Brit Crunch", "Southwest Lead",
            "Essex Chime", "Tweed Blues", "Meridian High Gain",
            "Citrus Roar", "Citrus Terror", "Regent 800",
            "Solar Monolith", "Eclipse Drone"
        };
        snprintf(buf, bufsize, "%s", amp_names[model]);
        return;
    }

    /* Cab type name */
    if (index == IDX_CAB_TYPE) {
        int cab = (int)(value + 0.5);
        if (cab < 0) cab = 0;
        if (cab >= FX_CAB_TYPE_COUNT) cab = FX_CAB_TYPE_COUNT - 1;
        static const char *cab_names[] = {
            "1x12 Open", "2x12 Closed", "4x12 Straight",
            "4x12 Slant", "Direct"
        };
        snprintf(buf, bufsize, "%s", cab_names[cab]);
        return;
    }

    /* Cab bypass */
    if (index == IDX_CAB_BYPASS) {
        snprintf(buf, bufsize, "%s", value >= 0.5 ? "On" : "Off");
        return;
    }

    /* Mic type name */
    if (index == IDX_MIC_TYPE) {
        int mic = (int)(value + 0.5);
        if (mic < 0) mic = 0;
        if (mic >= FX_MIC_COUNT) mic = FX_MIC_COUNT - 1;
        snprintf(buf, bufsize, "%s",
                 fx_mic_get_type_name((fx_mic_type_t)mic));
        return;
    }

    /* Mic placement params */
    if (index >= IDX_MIC_DISTANCE && index <= IDX_MIC_POSITION) {
        snprintf(buf, bufsize, "%.0f%%", value * 100.0);
        return;
    }

    /* Bright switch */
    if (index < NUM_AMP_KNOBS &&
        AMP_KNOBS[index].amp_param == FX_AMP_PARAM_BRIGHT) {
        snprintf(buf, bufsize, "%s", value >= 0.5 ? "On" : "Off");
        return;
    }

    /* Studio type selector */
    if (index >= IDX_STUDIO_START && index < IDX_PEDAL_START) {
        int offset = index - IDX_STUDIO_START;
        int sub    = offset % PARAMS_PER_STUDIO;
        if (sub == 0) {
            int t = (int)(value + 0.5);
            if (t <= 0) {
                snprintf(buf, bufsize, "None");
            } else {
                int type = t - 1;
                if (type >= FX_STUDIO_COUNT) type = FX_STUDIO_COUNT - 1;
                snprintf(buf, bufsize, "%s",
                         fx_studio_get_type_name((fx_studio_type_t)type));
            }
            return;
        }
    }

    /* Pedal type selector */
    if (index >= IDX_PEDAL_START && index < NUM_PARAMS) {
        int offset = index - IDX_PEDAL_START;
        int sub    = offset % PARAMS_PER_PEDAL;
        if (sub == 0) {
            int t = (int)(value + 0.5);
            if (t <= 0) {
                snprintf(buf, bufsize, "None");
            } else {
                int type = t - 1;
                if (type >= FX_PEDAL_TYPE_COUNT) type = FX_PEDAL_TYPE_COUNT - 1;
                snprintf(buf, bufsize, "%s",
                         fx_pedal_get_type_name((fx_pedal_type_t)type));
            }
            return;
        }
    }

    snprintf(buf, bufsize, "%.2f", value);
}

/* ── Audio / MIDI processing ─────────────────────────────────────── */

uint32_t cplug_getLatencyInSamples(void *ptr) { (void)ptr; return 0; }
uint32_t cplug_getTailInSamples(void *ptr)    { (void)ptr; return 0; }

void cplug_setSampleRateAndBlockSize(void *ptr, double sample_rate, uint32_t max_block_size)
{
    OxFXPlugin *p = (OxFXPlugin *)ptr;
    p->sample_rate    = (float)sample_rate;
    p->max_block_size = max_block_size;

    /* Re-create the engine at the new sample rate */
    if (p->engine) {
        fx_engine_destroy(p->engine);
    }

    /* Clear cached pedal IDs — they belong to the old engine */
    for (int i = 0; i < NUM_PEDAL_SLOTS; i++)
        p->pedal_ids[i] = -1;

    /* Clear cached studio IDs */
    for (int i = 0; i < NUM_STUDIO_SLOTS; i++)
        p->studio_ids[i] = -1;

    p->engine = fx_engine_create(p->sample_rate);

    /* Restore full parameter state after engine re-create */
    if (p->engine) {
        for (int i = 0; i < NUM_PARAMS; i++)
            apply_param(p, i, p->param_values[i]);
    }
}

void cplug_process(void *ptr, CplugProcessContext *ctx)
{
    OxFXPlugin *p = (OxFXPlugin *)ptr;

    /* Sample-accurate event loop */
    CplugEvent event;
    uint32_t   frame = 0;

    while (ctx->dequeueEvent(ctx, &event, frame)) {
        switch (event.type) {

        case CPLUG_EVENT_PARAM_CHANGE_UPDATE:
            cplug_setParameterValue(p, event.parameter.id, event.parameter.value);
            break;

        case CPLUG_EVENT_PROCESS_AUDIO: {
            uint32_t end_frame = event.processAudio.endFrame;
            uint32_t n         = end_frame - frame;

            float **input  = ctx->getAudioInput(ctx, 0);
            float **output = ctx->getAudioOutput(ctx, 0);

            if (input && input[0] && output && output[0] && output[1] && p->engine) {
                /* Engine is mono in / mono out; output is duplicated to L+R */
                fx_engine_process(p->engine, &input[0][frame],
                                  &output[0][frame], (int)n);
                /* Copy left channel to right */
                memcpy(&output[1][frame], &output[0][frame],
                       n * sizeof(float));
            } else if (output && output[0] && output[1]) {
                /* Silence on error */
                memset(&output[0][frame], 0, n * sizeof(float));
                memset(&output[1][frame], 0, n * sizeof(float));
            }

            frame = end_frame;
            break;
        }

        default:
            break;
        }
    }
}

/* ── State save / load ───────────────────────────────────────────── */

typedef struct { uint32_t id; float value; } ParamState;

void cplug_saveState(void *user_plugin, const void *state_ctx, cplug_writeProc write_proc)
{
    OxFXPlugin *p = (OxFXPlugin *)user_plugin;

    ParamState state[NUM_PARAMS];
    for (int i = 0; i < NUM_PARAMS; i++) {
        state[i].id    = cplug_getParameterID(p, (uint32_t)i);
        state[i].value = p->param_values[i];
    }
    write_proc(state_ctx, state, sizeof(state));
}

void cplug_loadState(void *user_plugin, const void *state_ctx, cplug_readProc read_proc)
{
    OxFXPlugin *p = (OxFXPlugin *)user_plugin;

    ParamState state[NUM_PARAMS * 2]; /* generous read buffer */
    int64_t bytes_read = read_proc(state_ctx, state, sizeof(state));
    int     count      = (int)(bytes_read / (int64_t)sizeof(ParamState));

    for (int i = 0; i < count; i++) {
        int index = find_param_index(state[i].id);
        if (index < NUM_PARAMS) {
            p->param_values[index] = state[i].value;
            apply_param(p, index, state[i].value);
        }
    }
}

/* ── GUI stubs (CPLUG_WANT_GUI 1 — but no real GUI yet) ─────────── */

void *cplug_createGUI(void *user_plugin) { (void)user_plugin; return NULL; }
void  cplug_destroyGUI(void *user_gui)   { (void)user_gui; }
void  cplug_setParent(void *user_gui, void *parent) { (void)user_gui; (void)parent; }
void  cplug_setVisible(void *user_gui, bool visible) { (void)user_gui; (void)visible; }
void  cplug_setScaleFactor(void *user_gui, float scale) { (void)user_gui; (void)scale; }
void  cplug_getSize(void *user_gui, uint32_t *w, uint32_t *h)
    { (void)user_gui; *w = 0; *h = 0; }
void  cplug_checkSize(void *user_gui, uint32_t *w, uint32_t *h)
    { (void)user_gui; (void)w; (void)h; }
bool  cplug_setSize(void *user_gui, uint32_t w, uint32_t h)
    { (void)user_gui; (void)w; (void)h; return false; }
