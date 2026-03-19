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
 * Parameters:
 *   The initial parameter set covers the 10 amp parameters on the default
 *   chain.  Each parameter is identified by a stable 4-byte integer ID
 *   derived from the FX_AMP_PARAM_* enum value.
 */

#include <cplug.h>
#include <fx_engine.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Parameter table ────────────────────────────────────────────── */

/*
 * Each entry maps a stable 32-bit CPLUG parameter ID to an amp parameter
 * index.  IDs are constructed as 'axPP' where PP is the zero-padded decimal
 * amp param index.  This scheme is stable — adding new params appends to the
 * table without disturbing existing IDs.
 */
typedef struct {
    uint32_t      id;
    fx_amp_param_t amp_param;
    float         min_val;
    float         max_val;
    float         default_val;
    const char   *name;
} PluginParam;

/* Amp model selector — encoded as a float for host automation (0..4) */
#define PARAM_ID_AMP_MODEL  'axAM'

static const PluginParam AMP_PARAMS[] = {
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

/* Total number of exposed parameters = amp params + 1 model selector */
#define NUM_AMP_PARAMS ((int)(sizeof(AMP_PARAMS) / sizeof(AMP_PARAMS[0])))
#define NUM_PARAMS     (NUM_AMP_PARAMS + 1)

/* ── Plugin state struct ─────────────────────────────────────────── */

typedef struct {
    CplugHostContext *host_ctx;
    fx_engine_t      *engine;

    float     sample_rate;
    uint32_t  max_block_size;

    /* Parameter shadow values (audio-thread copy) */
    float     param_values[NUM_PARAMS]; /* index 0..NUM_AMP_PARAMS-1 = amp,
                                           index NUM_AMP_PARAMS = model */
} OxFXPlugin;

/* ── Helpers ────────────────────────────────────────────────────── */

/* Returns the index into param_values[] for a given CPLUG param ID.
 * Returns NUM_PARAMS on failure (acts as "not found"). */
static int find_param_index(uint32_t param_id)
{
    if (param_id == PARAM_ID_AMP_MODEL)
        return NUM_AMP_PARAMS; /* last slot */

    for (int i = 0; i < NUM_AMP_PARAMS; i++) {
        if (AMP_PARAMS[i].id == param_id)
            return i;
    }
    return NUM_PARAMS; /* not found */
}

static float param_min(int index)
{
    if (index == NUM_AMP_PARAMS) return 0.0f;
    return AMP_PARAMS[index].min_val;
}

static float param_max(int index)
{
    if (index == NUM_AMP_PARAMS) return (float)(FX_AMP_COUNT - 1);
    return AMP_PARAMS[index].max_val;
}

static float param_default(int index)
{
    if (index == NUM_AMP_PARAMS) return 0.0f;
    return AMP_PARAMS[index].default_val;
}

/* Apply a parameter value to the engine */
static void apply_param(OxFXPlugin *p, int index, float value)
{
    if (!p->engine) return;

    if (index == NUM_AMP_PARAMS) {
        /* Amp model selector */
        int model = (int)(value + 0.5f);
        if (model < 0) model = 0;
        if (model >= FX_AMP_COUNT) model = FX_AMP_COUNT - 1;
        fx_amp_set_model(p->engine, FX_CHAIN_DEFAULT, (fx_amp_type_t)model);
    } else {
        fx_amp_set_param(p->engine, FX_CHAIN_DEFAULT,
                         AMP_PARAMS[index].amp_param, value);
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

    p->engine = fx_engine_create(p->sample_rate);
    if (!p->engine) {
        free(p);
        return NULL;
    }

    /* Initialise parameter shadow values to defaults */
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
    if ((int)param_index == NUM_AMP_PARAMS) return PARAM_ID_AMP_MODEL;
    if ((int)param_index < NUM_AMP_PARAMS)  return AMP_PARAMS[param_index].id;
    return 0;
}

uint32_t cplug_getParameterFlags(void *ptr, uint32_t param_id)
{
    (void)ptr;
    int index = find_param_index(param_id);
    if (index >= NUM_PARAMS) return 0;

    uint32_t flags = CPLUG_FLAG_PARAMETER_IS_AUTOMATABLE;
    if (index == NUM_AMP_PARAMS) {
        /* Model selector is an integer enum */
        flags |= CPLUG_FLAG_PARAMETER_IS_INTEGER;
    }
    /* Bright switch is boolean */
    if (index < NUM_AMP_PARAMS && AMP_PARAMS[index].amp_param == FX_AMP_PARAM_BRIGHT) {
        flags |= CPLUG_FLAG_PARAMETER_IS_BOOL;
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
    if (index == NUM_AMP_PARAMS) {
        snprintf(buf, buflen, "Amp Model");
        return;
    }
    if (index < NUM_AMP_PARAMS) {
        snprintf(buf, buflen, "%s", AMP_PARAMS[index].name);
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
    if (index == NUM_AMP_PARAMS || (index < NUM_AMP_PARAMS &&
            (AMP_PARAMS[index].amp_param == FX_AMP_PARAM_BRIGHT))) {
        return (double)atoi(str);
    }
    return atof(str);
}

void cplug_parameterValueToString(void *ptr, uint32_t param_id,
                                  char *buf, size_t bufsize, double value)
{
    (void)ptr;
    int index = find_param_index(param_id);
    if (index >= NUM_PARAMS) { snprintf(buf, bufsize, "0"); return; }

    if (index == NUM_AMP_PARAMS) {
        /* Show amp model name */
        int model = (int)(value + 0.5);
        if (model < 0) model = 0;
        if (model >= FX_AMP_COUNT) model = FX_AMP_COUNT - 1;
        /* Use a temporary engine-less call — just use a static name table */
        static const char *amp_names[] = {
            "Fullerton Clean", "Brit Crunch", "Southwest Lead",
            "Essex Chime", "Tweed Blues"
        };
        snprintf(buf, bufsize, "%s", amp_names[model]);
        return;
    }
    if (index < NUM_AMP_PARAMS && AMP_PARAMS[index].amp_param == FX_AMP_PARAM_BRIGHT) {
        snprintf(buf, bufsize, "%s", value >= 0.5 ? "On" : "Off");
        return;
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
    p->engine = fx_engine_create(p->sample_rate);

    /* Restore parameter state after engine re-create */
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

    ParamState state[NUM_PARAMS * 2];
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

/* ── GUI stubs (CPLUG_WANT_GUI 0 — never called) ─────────────────── */

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
