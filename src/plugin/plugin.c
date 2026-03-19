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
 * Parameters (35 total):
 *   Indices  0-9   : Amp knobs (Gain, Volume, Bass, Mid, Treble, Presence,
 *                               Sag, Master, Bright, Cut)
 *   Index   10     : Amp model selector (0 .. FX_AMP_COUNT-1)
 *   Indices 11-14  : Pre-pedal slot 0  (type + 3 generic params)
 *   Indices 15-18  : Pre-pedal slot 1
 *   Indices 19-22  : Pre-pedal slot 2
 *   Indices 23-26  : Post-pedal slot 0
 *   Indices 27-30  : Post-pedal slot 1
 *   Indices 31-34  : Post-pedal slot 2
 *
 * Pedal type selector value 0 means "no pedal" (slot empty).
 * Values 1 .. FX_PEDAL_TYPE_COUNT map to fx_pedal_type_t (value - 1).
 */

#include <cplug.h>
#include <fx_engine.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Parameter layout constants ─────────────────────────────────── */

#define NUM_AMP_KNOBS    10
#define NUM_AMP_PARAMS   (NUM_AMP_KNOBS + 1)   /* knobs + model selector */

#define NUM_PEDAL_SLOTS  6   /* 3 pre + 3 post */
#define PARAMS_PER_PEDAL 4   /* type selector + 3 generic knob params */

#define NUM_PARAMS       (NUM_AMP_PARAMS + NUM_PEDAL_SLOTS * PARAMS_PER_PEDAL)
/* = 11 + 24 = 35 */

/* First param index for pedal block n (0-based) */
#define PEDAL_BLOCK_START(n)  (NUM_AMP_PARAMS + (n) * PARAMS_PER_PEDAL)

/* ── Amp parameter table ─────────────────────────────────────────── */

typedef struct {
    uint32_t      id;
    fx_amp_param_t amp_param;
    float         min_val;
    float         max_val;
    float         default_val;
    const char   *name;
} AmpParamDef;

#define PARAM_ID_AMP_MODEL  'axAM'

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
    /* slot 0-5, sub 0-3 → unique 4-byte ID */
    return (uint32_t)('p' | ((uint32_t)'x' << 8) |
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

    /* Parameter shadow values — all 35 params */
    float     param_values[NUM_PARAMS];

    /*
     * Active pedal IDs for each slot.
     * -1 means empty (no pedal in this slot).
     */
    fx_pedal_id pedal_ids[NUM_PEDAL_SLOTS];
} OxFXPlugin;

/* ── Pedal type mapping helpers ─────────────────────────────────── */

/*
 * The type-selector parameter uses value 0 = "no pedal", values 1..N map
 * to fx_pedal_type_t values 0..N-1.  This keeps 0.0 as the natural default
 * (empty slot).
 */
static int pedal_type_from_param(float v)
{
    int t = (int)(v + 0.5f) - 1;
    if (t < 0) return -1;
    if (t >= FX_PEDAL_TYPE_COUNT) t = FX_PEDAL_TYPE_COUNT - 1;
    return t; /* -1 means no pedal */
}

/* ── Param index helpers ─────────────────────────────────────────── */

/* Returns the linear param index [0, NUM_PARAMS) for a given CPLUG param ID.
 * Returns NUM_PARAMS if not found. */
static int find_param_index(uint32_t param_id)
{
    /* Amp model selector */
    if (param_id == PARAM_ID_AMP_MODEL)
        return NUM_AMP_KNOBS; /* index 10 */

    /* Amp knobs */
    for (int i = 0; i < NUM_AMP_KNOBS; i++) {
        if (AMP_KNOBS[i].id == param_id)
            return i;
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
    if (index < NUM_AMP_KNOBS)    return AMP_KNOBS[index].min_val;
    if (index == NUM_AMP_KNOBS)   return 0.0f; /* amp model */

    /* Pedal sub-params */
    int offset = index - NUM_AMP_PARAMS;
    int sub    = offset % PARAMS_PER_PEDAL;
    if (sub == 0) return 0.0f; /* type selector: 0..FX_PEDAL_TYPE_COUNT */
    return 0.0f;               /* generic knob params 0..1 */
}

static float param_max(int index)
{
    if (index < NUM_AMP_KNOBS)    return AMP_KNOBS[index].max_val;
    if (index == NUM_AMP_KNOBS)   return (float)(FX_AMP_COUNT - 1);

    int offset = index - NUM_AMP_PARAMS;
    int sub    = offset % PARAMS_PER_PEDAL;
    if (sub == 0) return (float)FX_PEDAL_TYPE_COUNT; /* 0 = none, 1..N = type */
    return 1.0f;
}

static float param_default(int index)
{
    if (index < NUM_AMP_KNOBS)    return AMP_KNOBS[index].default_val;
    if (index == NUM_AMP_KNOBS)   return 0.0f;
    return 0.0f; /* pedal slots: empty / knobs at min */
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
    if (index == NUM_AMP_KNOBS) {
        int model = (int)(value + 0.5f);
        if (model < 0) model = 0;
        if (model >= FX_AMP_COUNT) model = FX_AMP_COUNT - 1;
        fx_amp_set_model(p->engine, FX_CHAIN_DEFAULT, (fx_amp_type_t)model);
        return;
    }

    /* Pedal param */
    int offset = index - NUM_AMP_PARAMS;
    int slot   = offset / PARAMS_PER_PEDAL;
    int sub    = offset % PARAMS_PER_PEDAL;

    if (sub == 0) {
        /* Type selector — may add or remove the pedal */
        int type_idx = pedal_type_from_param(value);
        sync_pedal_slot(p, slot, type_idx);
    } else {
        /* Generic knob param (0-indexed: sub-1) */
        fx_pedal_id pid = p->pedal_ids[slot];
        if (pid >= 0) {
            fx_pedal_set_param(p->engine, pid, sub - 1, value);
        }
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
    if (i < NUM_AMP_KNOBS)    return AMP_KNOBS[i].id;
    if (i == NUM_AMP_KNOBS)   return PARAM_ID_AMP_MODEL;
    if (i < NUM_PARAMS) {
        int offset = i - NUM_AMP_PARAMS;
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
    if (index == NUM_AMP_KNOBS) {
        flags |= CPLUG_FLAG_PARAMETER_IS_INTEGER;
        return flags;
    }

    /* Bright switch — boolean */
    if (index < NUM_AMP_KNOBS &&
        AMP_KNOBS[index].amp_param == FX_AMP_PARAM_BRIGHT) {
        flags |= CPLUG_FLAG_PARAMETER_IS_BOOL;
        return flags;
    }

    /* Pedal type selectors — integer enum */
    if (index >= NUM_AMP_PARAMS) {
        int offset = index - NUM_AMP_PARAMS;
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
    if (index == NUM_AMP_KNOBS) {
        snprintf(buf, buflen, "Amp Model");
        return;
    }
    if (index < NUM_PARAMS) {
        int offset = index - NUM_AMP_PARAMS;
        int slot   = offset / PARAMS_PER_PEDAL;
        int sub    = offset % PARAMS_PER_PEDAL;
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

    /* Integer params */
    if (index == NUM_AMP_KNOBS) return (double)atoi(str);
    if (index < NUM_AMP_KNOBS &&
        AMP_KNOBS[index].amp_param == FX_AMP_PARAM_BRIGHT)
        return (double)atoi(str);
    if (index >= NUM_AMP_PARAMS) {
        int offset = index - NUM_AMP_PARAMS;
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

    if (index == NUM_AMP_KNOBS) {
        /* Amp model name */
        int model = (int)(value + 0.5);
        if (model < 0) model = 0;
        if (model >= FX_AMP_COUNT) model = FX_AMP_COUNT - 1;
        static const char *amp_names[] = {
            "Fullerton Clean", "Brit Crunch", "Southwest Lead",
            "Essex Chime", "Tweed Blues"
        };
        snprintf(buf, bufsize, "%s", amp_names[model]);
        return;
    }

    if (index < NUM_AMP_KNOBS &&
        AMP_KNOBS[index].amp_param == FX_AMP_PARAM_BRIGHT) {
        snprintf(buf, bufsize, "%s", value >= 0.5 ? "On" : "Off");
        return;
    }

    if (index >= NUM_AMP_PARAMS) {
        int offset = index - NUM_AMP_PARAMS;
        int sub    = offset % PARAMS_PER_PEDAL;
        if (sub == 0) {
            /* Pedal type selector */
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
