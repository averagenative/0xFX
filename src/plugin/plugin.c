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
 * Parameters (137 total):
 *   Indices  0-11  : Chain A amp knobs (Gain, Volume, Bass, Mid, Treble,
 *                    Presence, Sag, Master, Bright, Cut, Tone, Feedback)
 *   Index   12     : Chain A amp model selector (0 .. FX_AMP_COUNT-1)
 *   Index   13     : Chain A cab type selector (0 .. FX_CAB_TYPE_COUNT-1)
 *   Index   14     : Chain A cab bypass (0/1)
 *   Index   15     : Chain A mic type selector (0 .. FX_MIC_COUNT-1)
 *   Indices 16-18  : Chain A mic params (Distance, Angle, Position)
 *   Indices 19-22  : Noise gate (Threshold, Attack, Release, Hold)
 *   Index   23     : Chain mode (0=single, 1=dual)
 *   Index   24     : Chain A mix level (0.0-1.0)
 *   Index   25     : Chain B mix level (0.0-1.0)
 *   Indices 26-37  : Chain B amp knobs (mirror of Chain A layout)
 *   Index   38     : Chain B amp model selector
 *   Index   39     : Chain B cab type selector
 *   Index   40     : Chain B cab bypass
 *   Index   41     : Chain B mic type selector
 *   Indices 42-44  : Chain B mic params (Distance, Angle, Position)
 *   Indices 45-64  : Studio slots 0-3 (type + bypass + 3 params = 5 each)
 *   Indices 65-112 : Pre-pedal slots 0-5 (type + bypass + 6 params = 8 each)
 *   Indices 113-136: Post-pedal slots 0-2 (type + bypass + 6 params = 8 each)
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
#ifdef _WIN32
#include <windows.h>
#endif
#include <string.h>

/* ── Parameter layout constants ─────────────────────────────────── */

#define NUM_AMP_KNOBS    12

/* Chain A amp section */
#define IDX_A_AMP_START  0
#define IDX_A_AMP_MODEL  NUM_AMP_KNOBS          /* index 12 */
#define NUM_A_AMP_PARAMS (NUM_AMP_KNOBS + 1)    /* 13: knobs + model selector */

/* Chain A cab section */
#define IDX_A_CAB_TYPE   NUM_A_AMP_PARAMS       /* index 13 */
#define IDX_A_CAB_BYPASS (IDX_A_CAB_TYPE + 1)   /* index 14 */
#define NUM_A_CAB_PARAMS 2

/* Chain A mic section */
#define IDX_A_MIC_TYPE     (IDX_A_CAB_BYPASS + 1)   /* index 15 */
#define IDX_A_MIC_DISTANCE (IDX_A_MIC_TYPE + 1)     /* index 16 */
#define IDX_A_MIC_ANGLE    (IDX_A_MIC_TYPE + 2)     /* index 17 */
#define IDX_A_MIC_POSITION (IDX_A_MIC_TYPE + 3)     /* index 18 */
#define NUM_A_MIC_PARAMS   4

/* Noise gate section */
#define IDX_GATE_START     (IDX_A_MIC_POSITION + 1)  /* index 19 */
#define IDX_GATE_THRESHOLD IDX_GATE_START             /* index 19 */
#define IDX_GATE_ATTACK    (IDX_GATE_START + 1)       /* index 20 */
#define IDX_GATE_RELEASE   (IDX_GATE_START + 2)       /* index 21 */
#define IDX_GATE_HOLD      (IDX_GATE_START + 3)       /* index 22 */
#define NUM_GATE_PARAMS    4

/* Chain mode + mix section */
#define IDX_CHAIN_MODE     (IDX_GATE_START + NUM_GATE_PARAMS)  /* index 23 */
#define IDX_MIX_A          (IDX_CHAIN_MODE + 1)                /* index 24 */
#define IDX_MIX_B          (IDX_CHAIN_MODE + 2)                /* index 25 */
#define NUM_CHAIN_PARAMS   3

/* Chain B amp section */
#define IDX_B_AMP_START    (IDX_MIX_B + 1)           /* index 26 */
#define IDX_B_AMP_MODEL    (IDX_B_AMP_START + NUM_AMP_KNOBS)  /* index 38 */

/* Chain B cab section */
#define IDX_B_CAB_TYPE     (IDX_B_AMP_MODEL + 1)     /* index 39 */
#define IDX_B_CAB_BYPASS   (IDX_B_CAB_TYPE + 1)      /* index 40 */

/* Chain B mic section */
#define IDX_B_MIC_TYPE     (IDX_B_CAB_BYPASS + 1)    /* index 41 */
#define IDX_B_MIC_DISTANCE (IDX_B_MIC_TYPE + 1)      /* index 42 */
#define IDX_B_MIC_ANGLE    (IDX_B_MIC_TYPE + 2)      /* index 43 */
#define IDX_B_MIC_POSITION (IDX_B_MIC_TYPE + 3)      /* index 44 */

/* Studio slots */
#define NUM_STUDIO_SLOTS   4
#define PARAMS_PER_STUDIO  5   /* type + bypass + 3 params */
#define IDX_STUDIO_START   (IDX_B_MIC_POSITION + 1)  /* index 45 */

/* Pedal slots */
#define NUM_PRE_PEDAL_SLOTS  6
#define NUM_POST_PEDAL_SLOTS 3
#define NUM_PEDAL_SLOTS      (NUM_PRE_PEDAL_SLOTS + NUM_POST_PEDAL_SLOTS) /* 9 */
#define PARAMS_PER_PEDAL     8   /* type + bypass + 6 params */
#define IDX_PEDAL_START      (IDX_STUDIO_START + NUM_STUDIO_SLOTS * PARAMS_PER_STUDIO)  /* index 65 */

#define NUM_PARAMS           (IDX_PEDAL_START + NUM_PEDAL_SLOTS * PARAMS_PER_PEDAL)
/* = 65 + 9 * 8 = 65 + 72 = 137 */

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

/* Chain A param IDs (original, backward-compatible) */
#define PARAM_ID_AMP_MODEL   'axAM'
#define PARAM_ID_CAB_TYPE    'cxTY'
#define PARAM_ID_CAB_BYPASS  'cxBP'
#define PARAM_ID_MIC_TYPE    'mxTY'
#define PARAM_ID_MIC_DIST    'mx00'
#define PARAM_ID_MIC_ANGLE   'mx01'
#define PARAM_ID_MIC_POS     'mx02'

/* Noise gate param IDs */
#define PARAM_ID_GATE_THRESH 'gx00'
#define PARAM_ID_GATE_ATTACK 'gx01'
#define PARAM_ID_GATE_RELEASE 'gx02'
#define PARAM_ID_GATE_HOLD   'gx03'

/* Chain mode + mix param IDs */
#define PARAM_ID_CHAIN_MODE  'chMD'
#define PARAM_ID_MIX_A       'chMA'
#define PARAM_ID_MIX_B       'chMB'

/* Chain B param IDs */
#define PARAM_ID_B_AMP_MODEL 'bxAM'
#define PARAM_ID_B_CAB_TYPE  'bxTY'
#define PARAM_ID_B_CAB_BYPASS 'bxBP'
#define PARAM_ID_B_MIC_TYPE  'bxMT'
#define PARAM_ID_B_MIC_DIST  'bx00'
#define PARAM_ID_B_MIC_ANGLE 'bx01'
#define PARAM_ID_B_MIC_POS   'bx02'

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

/* Chain B amp knob IDs — 'bx' prefix + knob index as 2-char hex */
static uint32_t chain_b_amp_knob_id(int knob)
{
    return (uint32_t)('b' | ((uint32_t)'a' << 8) |
                      ((uint32_t)('0' + knob / 10) << 16) |
                      ((uint32_t)('0' + knob % 10) << 24));
}

/* ── Pedal slot descriptors ─────────────────────────────────────── */

/*
 * Pre-pedal slots 0-5 map to engine FX_CHAIN_POS_PRE.
 * Post-pedal slots 6-8 map to engine FX_CHAIN_POS_POST.
 * Param IDs for pedal slots are built from 'px' prefix + slot + sub-index.
 *   e.g. slot 0 param 0 => 'px00', slot 0 param 1 => 'px01', ...
 *        slot 1 param 0 => 'px10', etc.
 */

static uint32_t pedal_param_id(int slot, int sub)
{
    /* slot 0-8, sub 0-7 -> unique 4-byte ID */
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
    return (slot < NUM_PRE_PEDAL_SLOTS) ? FX_CHAIN_POS_PRE : FX_CHAIN_POS_POST;
}

/* ── Factory preset table ────────────────────────────────────────── */

typedef struct {
    const char *name;       /* display name in host */
    const char *path;       /* file path, or NULL for "init/reset" */
} FactoryPreset;

static const FactoryPreset FACTORY_PRESETS[] = {
    { "Clean Sparkle",     "presets/clean_sparkle.0xfx"     },
    { "Classic Crunch",    "presets/classic_crunch.0xfx"    },
    { "Modern High Gain",  "presets/modern_high_gain.0xfx"  },
    { "Chimey British",    "presets/chimey_british.0xfx"    },
    { "Bluesy Tweed",      "presets/bluesy_tweed.0xfx"      },
    { "Init (Clean)",      NULL                             },
};

#define NUM_FACTORY_PRESETS (sizeof(FACTORY_PRESETS) / sizeof(FACTORY_PRESETS[0]))

/* ── MIDI CC constants ──────────────────────────────────────────── */

#define MIDI_CC_COUNT     128
#define MIDI_STATUS_CC    0xB0
#define MIDI_CC_UNMAPPED  (-1)

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

    /*
     * Chain B ID — -1 when in single chain mode.
     */
    fx_chain_id chain_b_id;

    /*
     * MIDI CC mapping: cc_map[cc_number] = param index, or MIDI_CC_UNMAPPED.
     * Hosts send MIDI CC events through CPLUG_EVENT_MIDI; we map them to
     * plugin parameters using this table.
     */
    int cc_map[MIDI_CC_COUNT];
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
    /* Chain A amp knobs */
    for (int i = 0; i < NUM_AMP_KNOBS; i++) {
        if (AMP_KNOBS[i].id == param_id)
            return i;
    }

    /* Chain A amp model selector */
    if (param_id == PARAM_ID_AMP_MODEL)  return IDX_A_AMP_MODEL;

    /* Chain A cab params */
    if (param_id == PARAM_ID_CAB_TYPE)   return IDX_A_CAB_TYPE;
    if (param_id == PARAM_ID_CAB_BYPASS) return IDX_A_CAB_BYPASS;

    /* Chain A mic params */
    if (param_id == PARAM_ID_MIC_TYPE)   return IDX_A_MIC_TYPE;
    if (param_id == PARAM_ID_MIC_DIST)   return IDX_A_MIC_DISTANCE;
    if (param_id == PARAM_ID_MIC_ANGLE)  return IDX_A_MIC_ANGLE;
    if (param_id == PARAM_ID_MIC_POS)    return IDX_A_MIC_POSITION;

    /* Noise gate params */
    if (param_id == PARAM_ID_GATE_THRESH)  return IDX_GATE_THRESHOLD;
    if (param_id == PARAM_ID_GATE_ATTACK)  return IDX_GATE_ATTACK;
    if (param_id == PARAM_ID_GATE_RELEASE) return IDX_GATE_RELEASE;
    if (param_id == PARAM_ID_GATE_HOLD)    return IDX_GATE_HOLD;

    /* Chain mode + mix */
    if (param_id == PARAM_ID_CHAIN_MODE) return IDX_CHAIN_MODE;
    if (param_id == PARAM_ID_MIX_A)      return IDX_MIX_A;
    if (param_id == PARAM_ID_MIX_B)      return IDX_MIX_B;

    /* Chain B amp knobs */
    for (int i = 0; i < NUM_AMP_KNOBS; i++) {
        if (chain_b_amp_knob_id(i) == param_id)
            return IDX_B_AMP_START + i;
    }

    /* Chain B amp model */
    if (param_id == PARAM_ID_B_AMP_MODEL) return IDX_B_AMP_MODEL;

    /* Chain B cab */
    if (param_id == PARAM_ID_B_CAB_TYPE)   return IDX_B_CAB_TYPE;
    if (param_id == PARAM_ID_B_CAB_BYPASS) return IDX_B_CAB_BYPASS;

    /* Chain B mic */
    if (param_id == PARAM_ID_B_MIC_TYPE)   return IDX_B_MIC_TYPE;
    if (param_id == PARAM_ID_B_MIC_DIST)   return IDX_B_MIC_DISTANCE;
    if (param_id == PARAM_ID_B_MIC_ANGLE)  return IDX_B_MIC_ANGLE;
    if (param_id == PARAM_ID_B_MIC_POS)    return IDX_B_MIC_POSITION;

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
    /* Chain A amp knobs */
    if (index < NUM_AMP_KNOBS)
        return AMP_KNOBS[index].min_val;

    /* Chain A amp model / cab / mic */
    if (index == IDX_A_AMP_MODEL)     return 0.0f;
    if (index == IDX_A_CAB_TYPE)      return 0.0f;
    if (index == IDX_A_CAB_BYPASS)    return 0.0f;
    if (index == IDX_A_MIC_TYPE)      return 0.0f;
    if (index >= IDX_A_MIC_DISTANCE && index <= IDX_A_MIC_POSITION) return 0.0f;

    /* Noise gate */
    if (index == IDX_GATE_THRESHOLD) return -80.0f;
    if (index == IDX_GATE_ATTACK)    return 0.1f;
    if (index == IDX_GATE_RELEASE)   return 5.0f;
    if (index == IDX_GATE_HOLD)      return 1.0f;

    /* Chain mode + mix */
    if (index == IDX_CHAIN_MODE) return 0.0f;
    if (index == IDX_MIX_A)      return 0.0f;
    if (index == IDX_MIX_B)      return 0.0f;

    /* Chain B amp knobs */
    if (index >= IDX_B_AMP_START && index < IDX_B_AMP_START + NUM_AMP_KNOBS)
        return AMP_KNOBS[index - IDX_B_AMP_START].min_val;

    /* Chain B amp model / cab / mic */
    if (index == IDX_B_AMP_MODEL)     return 0.0f;
    if (index == IDX_B_CAB_TYPE)      return 0.0f;
    if (index == IDX_B_CAB_BYPASS)    return 0.0f;
    if (index == IDX_B_MIC_TYPE)      return 0.0f;
    if (index >= IDX_B_MIC_DISTANCE && index <= IDX_B_MIC_POSITION) return 0.0f;

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
    /* Chain A amp knobs */
    if (index < NUM_AMP_KNOBS)
        return AMP_KNOBS[index].max_val;

    /* Chain A amp model / cab / mic */
    if (index == IDX_A_AMP_MODEL)     return (float)(FX_AMP_COUNT - 1);
    if (index == IDX_A_CAB_TYPE)      return (float)(FX_CAB_TYPE_COUNT - 1);
    if (index == IDX_A_CAB_BYPASS)    return 1.0f;
    if (index == IDX_A_MIC_TYPE)      return (float)(FX_MIC_COUNT - 1);
    if (index >= IDX_A_MIC_DISTANCE && index <= IDX_A_MIC_POSITION) return 1.0f;

    /* Noise gate */
    if (index == IDX_GATE_THRESHOLD) return 0.0f;
    if (index == IDX_GATE_ATTACK)    return 50.0f;
    if (index == IDX_GATE_RELEASE)   return 500.0f;
    if (index == IDX_GATE_HOLD)      return 100.0f;

    /* Chain mode + mix */
    if (index == IDX_CHAIN_MODE) return 1.0f;
    if (index == IDX_MIX_A)      return 1.0f;
    if (index == IDX_MIX_B)      return 1.0f;

    /* Chain B amp knobs */
    if (index >= IDX_B_AMP_START && index < IDX_B_AMP_START + NUM_AMP_KNOBS)
        return AMP_KNOBS[index - IDX_B_AMP_START].max_val;

    /* Chain B amp model / cab / mic */
    if (index == IDX_B_AMP_MODEL)     return (float)(FX_AMP_COUNT - 1);
    if (index == IDX_B_CAB_TYPE)      return (float)(FX_CAB_TYPE_COUNT - 1);
    if (index == IDX_B_CAB_BYPASS)    return 1.0f;
    if (index == IDX_B_MIC_TYPE)      return (float)(FX_MIC_COUNT - 1);
    if (index >= IDX_B_MIC_DISTANCE && index <= IDX_B_MIC_POSITION) return 1.0f;

    /* Studio sub-params */
    if (index >= IDX_STUDIO_START && index < IDX_PEDAL_START) {
        int offset = index - IDX_STUDIO_START;
        int sub    = offset % PARAMS_PER_STUDIO;
        if (sub == 0) return (float)FX_STUDIO_COUNT; /* 0 = none, 1..N = type */
        if (sub == 1) return 1.0f; /* bypass */
        return 1.0f;
    }

    /* Pedal sub-params */
    if (index >= IDX_PEDAL_START && index < NUM_PARAMS) {
        int offset = index - IDX_PEDAL_START;
        int sub    = offset % PARAMS_PER_PEDAL;
        if (sub == 0) return (float)FX_PEDAL_TYPE_COUNT; /* 0 = none, 1..N = type */
        if (sub == 1) return 1.0f; /* bypass */
        return 1.0f;
    }

    return 1.0f;
}

static float param_default(int index)
{
    /* Chain A amp knobs */
    if (index < NUM_AMP_KNOBS)
        return AMP_KNOBS[index].default_val;

    /* Chain A model / cab / mic */
    if (index == IDX_A_AMP_MODEL)     return 0.0f;
    if (index == IDX_A_CAB_TYPE)      return 0.0f;  /* 1x12 open */
    if (index == IDX_A_CAB_BYPASS)    return 0.0f;  /* cab enabled */
    if (index == IDX_A_MIC_TYPE)      return 0.0f;  /* DI (no coloration) */
    if (index >= IDX_A_MIC_DISTANCE && index <= IDX_A_MIC_POSITION) return 0.5f;

    /* Noise gate */
    if (index == IDX_GATE_THRESHOLD) return -55.0f;
    if (index == IDX_GATE_ATTACK)    return 1.0f;
    if (index == IDX_GATE_RELEASE)   return 50.0f;
    if (index == IDX_GATE_HOLD)      return 10.0f;

    /* Chain mode + mix */
    if (index == IDX_CHAIN_MODE) return 0.0f;  /* single */
    if (index == IDX_MIX_A)      return 1.0f;  /* full level */
    if (index == IDX_MIX_B)      return 0.5f;  /* half level */

    /* Chain B amp knobs */
    if (index >= IDX_B_AMP_START && index < IDX_B_AMP_START + NUM_AMP_KNOBS)
        return AMP_KNOBS[index - IDX_B_AMP_START].default_val;

    /* Chain B model / cab / mic */
    if (index == IDX_B_AMP_MODEL)     return 0.0f;
    if (index == IDX_B_CAB_TYPE)      return 0.0f;
    if (index == IDX_B_CAB_BYPASS)    return 0.0f;
    if (index == IDX_B_MIC_TYPE)      return 0.0f;
    if (index >= IDX_B_MIC_DISTANCE && index <= IDX_B_MIC_POSITION) return 0.5f;

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

    /* Restore the 6 generic param values and bypass for this slot */
    if (new_id >= 0) {
        int base = PEDAL_BLOCK_START(slot);
        /* sub 1 = bypass */
        fx_pedal_set_bypass(p->engine, new_id,
                            p->param_values[base + 1] >= 0.5f);
        /* sub 2-7 = params 0-5 */
        for (int sub = 2; sub < PARAMS_PER_PEDAL; sub++) {
            fx_pedal_set_param(p->engine, new_id, sub - 2,
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

    /* Restore bypass + 3 generic param values for this slot */
    if (new_id >= 0) {
        int base = STUDIO_BLOCK_START(slot);
        /* sub 1 = bypass */
        fx_studio_set_bypass(p->engine, new_id,
                             p->param_values[base + 1] >= 0.5f);
        /* sub 2-4 = params 0-2 */
        for (int sub = 2; sub < PARAMS_PER_STUDIO; sub++) {
            fx_studio_set_param(p->engine, new_id, sub - 2,
                                p->param_values[base + sub]);
        }
    }
    return new_id;
}

/*
 * Generate cab IR for the given chain using current shadow param values.
 */
static void apply_cab_type(OxFXPlugin *p, fx_chain_id chain, float value)
{
    int cab = (int)(value + 0.5f);
    if (cab < 0) cab = 0;
    if (cab >= FX_CAB_TYPE_COUNT) cab = FX_CAB_TYPE_COUNT - 1;
    fx_cab_params_t params;
    params.cab_type   = (fx_cab_type_t)cab;
    params.mic_pos    = FX_MIC_ON_AXIS;
    params.speaker_fs = 80.0f;
    params.brightness = 0.5f;
    params.resonance  = 0.5f;
    fx_cab_generate_ir(p->engine, chain, &params);
}

/* Apply a parameter value to the engine */
static void apply_param(OxFXPlugin *p, int index, float value)
{
    if (!p->engine) return;

    /* ── Chain A amp knob ─────────────────────────────────────────── */
    if (index < NUM_AMP_KNOBS) {
        fx_amp_set_param(p->engine, FX_CHAIN_DEFAULT,
                         AMP_KNOBS[index].amp_param, value);
        return;
    }

    /* ── Chain A amp model selector ───────────────────────────────── */
    if (index == IDX_A_AMP_MODEL) {
        int model = (int)(value + 0.5f);
        if (model < 0) model = 0;
        if (model >= FX_AMP_COUNT) model = FX_AMP_COUNT - 1;
        fx_amp_set_model(p->engine, FX_CHAIN_DEFAULT, (fx_amp_type_t)model);
        return;
    }

    /* ── Chain A cab type ─────────────────────────────────────────── */
    if (index == IDX_A_CAB_TYPE) {
        apply_cab_type(p, FX_CHAIN_DEFAULT, value);
        return;
    }

    /* ── Chain A cab bypass ───────────────────────────────────────── */
    if (index == IDX_A_CAB_BYPASS) {
        fx_cab_set_bypass(p->engine, FX_CHAIN_DEFAULT, value >= 0.5f);
        return;
    }

    /* ── Chain A mic type ─────────────────────────────────────────── */
    if (index == IDX_A_MIC_TYPE) {
        int mic = (int)(value + 0.5f);
        if (mic < 0) mic = 0;
        if (mic >= FX_MIC_COUNT) mic = FX_MIC_COUNT - 1;
        fx_mic_set_type(p->engine, FX_CHAIN_DEFAULT, (fx_mic_type_t)mic);
        return;
    }

    /* ── Chain A mic placement params ─────────────────────────────── */
    if (index == IDX_A_MIC_DISTANCE) {
        fx_mic_set_param(p->engine, FX_CHAIN_DEFAULT, FX_MIC_PARAM_DISTANCE, value);
        return;
    }
    if (index == IDX_A_MIC_ANGLE) {
        fx_mic_set_param(p->engine, FX_CHAIN_DEFAULT, FX_MIC_PARAM_ANGLE, value);
        return;
    }
    if (index == IDX_A_MIC_POSITION) {
        fx_mic_set_param(p->engine, FX_CHAIN_DEFAULT, FX_MIC_PARAM_POSITION, value);
        return;
    }

    /* ── Noise gate params ────────────────────────────────────────── */
    if (index == IDX_GATE_THRESHOLD) {
        fx_gate_set_threshold(p->engine, value);
        return;
    }
    if (index == IDX_GATE_ATTACK) {
        fx_gate_set_attack(p->engine, value);
        return;
    }
    if (index == IDX_GATE_RELEASE) {
        fx_gate_set_release(p->engine, value);
        return;
    }
    if (index == IDX_GATE_HOLD) {
        fx_gate_set_hold(p->engine, value);
        return;
    }

    /* ── Chain mode ───────────────────────────────────────────────── */
    if (index == IDX_CHAIN_MODE) {
        bool want_dual = (value >= 0.5f);
        bool have_dual = (p->chain_b_id >= 0);

        if (want_dual && !have_dual) {
            /* Create chain B */
            p->chain_b_id = fx_chain_create(p->engine);
            if (p->chain_b_id >= 0) {
                /* Apply all chain B params */
                fx_chain_set_mix(p->engine, FX_CHAIN_DEFAULT,
                                 p->param_values[IDX_MIX_A]);
                fx_chain_set_mix(p->engine, p->chain_b_id,
                                 p->param_values[IDX_MIX_B]);
                /* Apply chain B amp/cab/mic state */
                for (int i = IDX_B_AMP_START; i <= IDX_B_MIC_POSITION; i++)
                    apply_param(p, i, p->param_values[i]);
            }
        } else if (!want_dual && have_dual) {
            /* Destroy chain B */
            fx_chain_destroy(p->engine, p->chain_b_id);
            p->chain_b_id = -1;
        }
        return;
    }

    /* ── Chain A mix ──────────────────────────────────────────────── */
    if (index == IDX_MIX_A) {
        fx_chain_set_mix(p->engine, FX_CHAIN_DEFAULT, value);
        return;
    }

    /* ── Chain B mix ──────────────────────────────────────────────── */
    if (index == IDX_MIX_B) {
        if (p->chain_b_id >= 0)
            fx_chain_set_mix(p->engine, p->chain_b_id, value);
        return;
    }

    /* ── Chain B amp knobs ────────────────────────────────────────── */
    if (index >= IDX_B_AMP_START && index < IDX_B_AMP_START + NUM_AMP_KNOBS) {
        if (p->chain_b_id >= 0) {
            int knob = index - IDX_B_AMP_START;
            fx_amp_set_param(p->engine, p->chain_b_id,
                             AMP_KNOBS[knob].amp_param, value);
        }
        return;
    }

    /* ── Chain B amp model ────────────────────────────────────────── */
    if (index == IDX_B_AMP_MODEL) {
        if (p->chain_b_id >= 0) {
            int model = (int)(value + 0.5f);
            if (model < 0) model = 0;
            if (model >= FX_AMP_COUNT) model = FX_AMP_COUNT - 1;
            fx_amp_set_model(p->engine, p->chain_b_id, (fx_amp_type_t)model);
        }
        return;
    }

    /* ── Chain B cab type ─────────────────────────────────────────── */
    if (index == IDX_B_CAB_TYPE) {
        if (p->chain_b_id >= 0)
            apply_cab_type(p, p->chain_b_id, value);
        return;
    }

    /* ── Chain B cab bypass ───────────────────────────────────────── */
    if (index == IDX_B_CAB_BYPASS) {
        if (p->chain_b_id >= 0)
            fx_cab_set_bypass(p->engine, p->chain_b_id, value >= 0.5f);
        return;
    }

    /* ── Chain B mic type ─────────────────────────────────────────── */
    if (index == IDX_B_MIC_TYPE) {
        if (p->chain_b_id >= 0) {
            int mic = (int)(value + 0.5f);
            if (mic < 0) mic = 0;
            if (mic >= FX_MIC_COUNT) mic = FX_MIC_COUNT - 1;
            fx_mic_set_type(p->engine, p->chain_b_id, (fx_mic_type_t)mic);
        }
        return;
    }

    /* ── Chain B mic placement params ─────────────────────────────── */
    if (index == IDX_B_MIC_DISTANCE) {
        if (p->chain_b_id >= 0)
            fx_mic_set_param(p->engine, p->chain_b_id, FX_MIC_PARAM_DISTANCE, value);
        return;
    }
    if (index == IDX_B_MIC_ANGLE) {
        if (p->chain_b_id >= 0)
            fx_mic_set_param(p->engine, p->chain_b_id, FX_MIC_PARAM_ANGLE, value);
        return;
    }
    if (index == IDX_B_MIC_POSITION) {
        if (p->chain_b_id >= 0)
            fx_mic_set_param(p->engine, p->chain_b_id, FX_MIC_PARAM_POSITION, value);
        return;
    }

    /* ── Studio processor param ──────────────────────────────────── */
    if (index >= IDX_STUDIO_START && index < IDX_PEDAL_START) {
        int offset = index - IDX_STUDIO_START;
        int slot   = offset / PARAMS_PER_STUDIO;
        int sub    = offset % PARAMS_PER_STUDIO;

        if (sub == 0) {
            /* Type selector — may add or remove the processor */
            int type_idx = type_from_param(value, FX_STUDIO_COUNT);
            sync_studio_slot(p, slot, type_idx);
        } else if (sub == 1) {
            /* Bypass toggle */
            fx_studio_id sid = p->studio_ids[slot];
            if (sid >= 0)
                fx_studio_set_bypass(p->engine, sid, value >= 0.5f);
        } else {
            /* Generic knob param (0-indexed: sub-2) */
            fx_studio_id sid = p->studio_ids[slot];
            if (sid >= 0)
                fx_studio_set_param(p->engine, sid, sub - 2, value);
        }
        return;
    }

    /* ── Pedal param ─────────────────────────────────────────────── */
    if (index >= IDX_PEDAL_START && index < NUM_PARAMS) {
        int offset = index - IDX_PEDAL_START;
        int slot   = offset / PARAMS_PER_PEDAL;
        int sub    = offset % PARAMS_PER_PEDAL;

        if (sub == 0) {
            /* Type selector — may add or remove the pedal */
            int type_idx = type_from_param(value, FX_PEDAL_TYPE_COUNT);
            sync_pedal_slot(p, slot, type_idx);
        } else if (sub == 1) {
            /* Bypass toggle */
            fx_pedal_id pid = p->pedal_ids[slot];
            if (pid >= 0)
                fx_pedal_set_bypass(p->engine, pid, value >= 0.5f);
        } else {
            /* Generic knob param (0-indexed: sub-2) */
            fx_pedal_id pid = p->pedal_ids[slot];
            if (pid >= 0)
                fx_pedal_set_param(p->engine, pid, sub - 2, value);
        }
        return;
    }
}

/* ── Sync parameter cache from engine state ──────────────────────── */

/*
 * After loading a preset via fx_preset_load(), the engine's internal state
 * has changed. We need to read it back and update our param_values[] cache
 * so the host sees correct values.  We also rebuild pedal/studio slot IDs.
 */
static void sync_params_from_engine(OxFXPlugin *p)
{
    if (!p->engine) return;

    /* ── Chain A amp knobs ─────────────────────────────────────── */
    for (int i = 0; i < NUM_AMP_KNOBS; i++)
        p->param_values[i] = fx_amp_get_param(p->engine, FX_CHAIN_DEFAULT,
                                               AMP_KNOBS[i].amp_param);

    /* Chain A amp model */
    p->param_values[IDX_A_AMP_MODEL] = (float)fx_amp_get_model(p->engine,
                                                                 FX_CHAIN_DEFAULT);

    /* Chain A cab — cab type is synthetic so we leave cached value;
     * cab bypass we can read back */
    p->param_values[IDX_A_CAB_BYPASS] = fx_cab_get_bypass(p->engine,
                                                            FX_CHAIN_DEFAULT) ? 1.0f : 0.0f;

    /* Chain A mic */
    p->param_values[IDX_A_MIC_TYPE] = (float)fx_mic_get_type(p->engine,
                                                               FX_CHAIN_DEFAULT);
    p->param_values[IDX_A_MIC_DISTANCE] = fx_mic_get_param(p->engine,
                                                             FX_CHAIN_DEFAULT,
                                                             FX_MIC_PARAM_DISTANCE);
    p->param_values[IDX_A_MIC_ANGLE] = fx_mic_get_param(p->engine,
                                                          FX_CHAIN_DEFAULT,
                                                          FX_MIC_PARAM_ANGLE);
    p->param_values[IDX_A_MIC_POSITION] = fx_mic_get_param(p->engine,
                                                             FX_CHAIN_DEFAULT,
                                                             FX_MIC_PARAM_POSITION);

    /* ── Noise gate ───────────────────────────────────────────── */
    p->param_values[IDX_GATE_THRESHOLD] = fx_gate_get_threshold(p->engine);
    p->param_values[IDX_GATE_ATTACK]    = fx_gate_get_attack(p->engine);
    p->param_values[IDX_GATE_RELEASE]   = fx_gate_get_release(p->engine);
    p->param_values[IDX_GATE_HOLD]      = fx_gate_get_hold(p->engine);

    /* ── Chain mode ───────────────────────────────────────────── */
    int chain_count = fx_chain_get_count(p->engine);
    bool is_dual = (chain_count > 1);
    p->param_values[IDX_CHAIN_MODE] = is_dual ? 1.0f : 0.0f;

    /* ── Chain A/B mix ────────────────────────────────────────── */
    p->param_values[IDX_MIX_A] = fx_chain_get_mix(p->engine, FX_CHAIN_DEFAULT);

    if (is_dual) {
        /* Find chain B ID — it's the first chain that isn't FX_CHAIN_DEFAULT.
         * After preset load the engine may have created chain B internally.
         * We assume chain ID 1 if dual mode. */
        p->chain_b_id = 1;  /* engine creates chains sequentially */
        p->param_values[IDX_MIX_B] = fx_chain_get_mix(p->engine, p->chain_b_id);

        /* Chain B amp knobs */
        for (int i = 0; i < NUM_AMP_KNOBS; i++)
            p->param_values[IDX_B_AMP_START + i] =
                fx_amp_get_param(p->engine, p->chain_b_id,
                                 AMP_KNOBS[i].amp_param);

        p->param_values[IDX_B_AMP_MODEL] = (float)fx_amp_get_model(p->engine,
                                                                     p->chain_b_id);
        p->param_values[IDX_B_CAB_BYPASS] = fx_cab_get_bypass(p->engine,
                                                                p->chain_b_id) ? 1.0f : 0.0f;
        p->param_values[IDX_B_MIC_TYPE] = (float)fx_mic_get_type(p->engine,
                                                                   p->chain_b_id);
        p->param_values[IDX_B_MIC_DISTANCE] = fx_mic_get_param(p->engine,
                                                                 p->chain_b_id,
                                                                 FX_MIC_PARAM_DISTANCE);
        p->param_values[IDX_B_MIC_ANGLE] = fx_mic_get_param(p->engine,
                                                              p->chain_b_id,
                                                              FX_MIC_PARAM_ANGLE);
        p->param_values[IDX_B_MIC_POSITION] = fx_mic_get_param(p->engine,
                                                                 p->chain_b_id,
                                                                 FX_MIC_PARAM_POSITION);
    } else {
        p->chain_b_id = -1;
    }

    /* ── Pedal slots — read back from engine ──────────────────── */
    /* Pre-pedals */
    int pre_count = fx_chain_get_pedal_count(p->engine, FX_CHAIN_POS_PRE);
    for (int slot = 0; slot < NUM_PRE_PEDAL_SLOTS; slot++) {
        int base = PEDAL_BLOCK_START(slot);
        if (slot < pre_count) {
            fx_pedal_id pid = fx_chain_get_pedal_at(p->engine,
                                                     FX_CHAIN_POS_PRE, slot);
            p->pedal_ids[slot] = pid;
            fx_pedal_type_t ptype = fx_pedal_get_type(p->engine, pid);
            p->param_values[base] = (float)(ptype + 1); /* +1: 0 = none */
            p->param_values[base + 1] = fx_pedal_get_bypass(p->engine, pid) ? 1.0f : 0.0f;
            for (int sub = 2; sub < PARAMS_PER_PEDAL; sub++)
                p->param_values[base + sub] = fx_pedal_get_param(p->engine,
                                                                   pid, sub - 2);
        } else {
            p->pedal_ids[slot] = -1;
            p->param_values[base] = 0.0f; /* none */
            p->param_values[base + 1] = 0.0f;
            for (int sub = 2; sub < PARAMS_PER_PEDAL; sub++)
                p->param_values[base + sub] = 0.0f;
        }
    }

    /* Post-pedals */
    int post_count = fx_chain_get_pedal_count(p->engine, FX_CHAIN_POS_POST);
    for (int i = 0; i < NUM_POST_PEDAL_SLOTS; i++) {
        int slot = NUM_PRE_PEDAL_SLOTS + i;
        int base = PEDAL_BLOCK_START(slot);
        if (i < post_count) {
            fx_pedal_id pid = fx_chain_get_pedal_at(p->engine,
                                                     FX_CHAIN_POS_POST, i);
            p->pedal_ids[slot] = pid;
            fx_pedal_type_t ptype = fx_pedal_get_type(p->engine, pid);
            p->param_values[base] = (float)(ptype + 1);
            p->param_values[base + 1] = fx_pedal_get_bypass(p->engine, pid) ? 1.0f : 0.0f;
            for (int sub = 2; sub < PARAMS_PER_PEDAL; sub++)
                p->param_values[base + sub] = fx_pedal_get_param(p->engine,
                                                                   pid, sub - 2);
        } else {
            p->pedal_ids[slot] = -1;
            p->param_values[base] = 0.0f;
            p->param_values[base + 1] = 0.0f;
            for (int sub = 2; sub < PARAMS_PER_PEDAL; sub++)
                p->param_values[base + sub] = 0.0f;
        }
    }

    /* Studio slots — engine doesn't expose iteration, so leave cached values.
     * Presets typically restore studio state through the engine internally. */
}

/* ── Library load/unload ────────────────────────────────────────── */

static HMODULE g_sdl2_preloaded = NULL;

void cplug_libraryLoad(void) {
#ifdef _WIN32
    /* Pre-load SDL2.dll from the plugin's own directory.
     * Windows doesn't search the plugin DLL's directory for deps,
     * so we must LoadLibrary with the full path. Once loaded,
     * implicit imports resolve from the in-memory module. */
    if (!g_sdl2_preloaded) {
        char dllPath[512] = {0};
        HMODULE hm = NULL;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)cplug_libraryLoad, &hm)) {
            GetModuleFileNameA(hm, dllPath, sizeof(dllPath));
            /* Strip filename to get directory */
            char *sep = strrchr(dllPath, '\\');
            if (!sep) sep = strrchr(dllPath, '/');
            if (sep) sep[1] = '\0';
            /* Also check parent dir for VST3 bundles (Contents/x86_64-win/) */
            char sdl2Path[512];
            snprintf(sdl2Path, sizeof(sdl2Path), "%sSDL2.dll", dllPath);
            g_sdl2_preloaded = LoadLibraryA(sdl2Path);
            if (!g_sdl2_preloaded) {
                /* Try parent directories (VST3 bundle: go up 2 levels) */
                snprintf(sdl2Path, sizeof(sdl2Path), "%s..\\..\\..\\SDL2.dll", dllPath);
                g_sdl2_preloaded = LoadLibraryA(sdl2Path);
            }
        }
    }
#endif
}
void cplug_libraryUnload(void) {}

/* ── Plugin lifecycle ───────────────────────────────────────────── */

void *cplug_createPlugin(CplugHostContext *ctx)
{
    OxFXPlugin *p = (OxFXPlugin *)calloc(1, sizeof(OxFXPlugin));
    if (!p) return NULL;

    p->host_ctx    = ctx;
    p->sample_rate = 44100.0f;
    p->chain_b_id  = -1;

    /* Initialise pedal IDs to "empty" */
    for (int i = 0; i < NUM_PEDAL_SLOTS; i++)
        p->pedal_ids[i] = -1;

    /* Initialise studio IDs to "empty" */
    for (int i = 0; i < NUM_STUDIO_SLOTS; i++)
        p->studio_ids[i] = -1;

    /* Initialise MIDI CC map to unmapped */
    for (int i = 0; i < MIDI_CC_COUNT; i++)
        p->cc_map[i] = MIDI_CC_UNMAPPED;

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

    /* Chain A amp knobs */
    if (i < NUM_AMP_KNOBS)       return AMP_KNOBS[i].id;
    if (i == IDX_A_AMP_MODEL)    return PARAM_ID_AMP_MODEL;
    if (i == IDX_A_CAB_TYPE)     return PARAM_ID_CAB_TYPE;
    if (i == IDX_A_CAB_BYPASS)   return PARAM_ID_CAB_BYPASS;
    if (i == IDX_A_MIC_TYPE)     return PARAM_ID_MIC_TYPE;
    if (i == IDX_A_MIC_DISTANCE) return PARAM_ID_MIC_DIST;
    if (i == IDX_A_MIC_ANGLE)    return PARAM_ID_MIC_ANGLE;
    if (i == IDX_A_MIC_POSITION) return PARAM_ID_MIC_POS;

    /* Noise gate */
    if (i == IDX_GATE_THRESHOLD) return PARAM_ID_GATE_THRESH;
    if (i == IDX_GATE_ATTACK)    return PARAM_ID_GATE_ATTACK;
    if (i == IDX_GATE_RELEASE)   return PARAM_ID_GATE_RELEASE;
    if (i == IDX_GATE_HOLD)      return PARAM_ID_GATE_HOLD;

    /* Chain mode + mix */
    if (i == IDX_CHAIN_MODE) return PARAM_ID_CHAIN_MODE;
    if (i == IDX_MIX_A)      return PARAM_ID_MIX_A;
    if (i == IDX_MIX_B)      return PARAM_ID_MIX_B;

    /* Chain B amp knobs */
    if (i >= IDX_B_AMP_START && i < IDX_B_AMP_START + NUM_AMP_KNOBS)
        return chain_b_amp_knob_id(i - IDX_B_AMP_START);
    if (i == IDX_B_AMP_MODEL)    return PARAM_ID_B_AMP_MODEL;
    if (i == IDX_B_CAB_TYPE)     return PARAM_ID_B_CAB_TYPE;
    if (i == IDX_B_CAB_BYPASS)   return PARAM_ID_B_CAB_BYPASS;
    if (i == IDX_B_MIC_TYPE)     return PARAM_ID_B_MIC_TYPE;
    if (i == IDX_B_MIC_DISTANCE) return PARAM_ID_B_MIC_DIST;
    if (i == IDX_B_MIC_ANGLE)    return PARAM_ID_B_MIC_ANGLE;
    if (i == IDX_B_MIC_POSITION) return PARAM_ID_B_MIC_POS;

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

    /* Integer enum selectors */
    if (index == IDX_A_AMP_MODEL || index == IDX_B_AMP_MODEL ||
        index == IDX_A_CAB_TYPE  || index == IDX_B_CAB_TYPE  ||
        index == IDX_A_MIC_TYPE  || index == IDX_B_MIC_TYPE) {
        flags |= CPLUG_FLAG_PARAMETER_IS_INTEGER;
        return flags;
    }

    /* Chain mode — integer (0 or 1) */
    if (index == IDX_CHAIN_MODE) {
        flags |= CPLUG_FLAG_PARAMETER_IS_INTEGER;
        return flags;
    }

    /* Boolean bypass toggles */
    if (index == IDX_A_CAB_BYPASS || index == IDX_B_CAB_BYPASS) {
        flags |= CPLUG_FLAG_PARAMETER_IS_BOOL;
        return flags;
    }

    /* Bright switch — boolean */
    if (index < NUM_AMP_KNOBS &&
        AMP_KNOBS[index].amp_param == FX_AMP_PARAM_BRIGHT) {
        flags |= CPLUG_FLAG_PARAMETER_IS_BOOL;
        return flags;
    }

    /* Chain B bright switch */
    if (index >= IDX_B_AMP_START && index < IDX_B_AMP_START + NUM_AMP_KNOBS &&
        AMP_KNOBS[index - IDX_B_AMP_START].amp_param == FX_AMP_PARAM_BRIGHT) {
        flags |= CPLUG_FLAG_PARAMETER_IS_BOOL;
        return flags;
    }

    /* Studio type selectors — integer enum; bypass — boolean */
    if (index >= IDX_STUDIO_START && index < IDX_PEDAL_START) {
        int offset = index - IDX_STUDIO_START;
        int sub    = offset % PARAMS_PER_STUDIO;
        if (sub == 0)
            flags |= CPLUG_FLAG_PARAMETER_IS_INTEGER;
        else if (sub == 1)
            flags |= CPLUG_FLAG_PARAMETER_IS_BOOL;
    }

    /* Pedal type selectors — integer enum; bypass — boolean */
    if (index >= IDX_PEDAL_START && index < NUM_PARAMS) {
        int offset = index - IDX_PEDAL_START;
        int sub    = offset % PARAMS_PER_PEDAL;
        if (sub == 0)
            flags |= CPLUG_FLAG_PARAMETER_IS_INTEGER;
        else if (sub == 1)
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

    /* Chain A amp knobs */
    if (index < NUM_AMP_KNOBS) {
        snprintf(buf, buflen, "%s", AMP_KNOBS[index].name);
        return;
    }
    if (index == IDX_A_AMP_MODEL) {
        snprintf(buf, buflen, "Amp Model");
        return;
    }
    if (index == IDX_A_CAB_TYPE) {
        snprintf(buf, buflen, "Cab Type");
        return;
    }
    if (index == IDX_A_CAB_BYPASS) {
        snprintf(buf, buflen, "Cab Bypass");
        return;
    }
    if (index == IDX_A_MIC_TYPE) {
        snprintf(buf, buflen, "Mic Type");
        return;
    }
    if (index == IDX_A_MIC_DISTANCE) {
        snprintf(buf, buflen, "Mic Distance");
        return;
    }
    if (index == IDX_A_MIC_ANGLE) {
        snprintf(buf, buflen, "Mic Angle");
        return;
    }
    if (index == IDX_A_MIC_POSITION) {
        snprintf(buf, buflen, "Mic Position");
        return;
    }

    /* Noise gate */
    if (index == IDX_GATE_THRESHOLD) {
        snprintf(buf, buflen, "Gate Threshold");
        return;
    }
    if (index == IDX_GATE_ATTACK) {
        snprintf(buf, buflen, "Gate Attack");
        return;
    }
    if (index == IDX_GATE_RELEASE) {
        snprintf(buf, buflen, "Gate Release");
        return;
    }
    if (index == IDX_GATE_HOLD) {
        snprintf(buf, buflen, "Gate Hold");
        return;
    }

    /* Chain mode + mix */
    if (index == IDX_CHAIN_MODE) {
        snprintf(buf, buflen, "Chain Mode");
        return;
    }
    if (index == IDX_MIX_A) {
        snprintf(buf, buflen, "Chain A Mix");
        return;
    }
    if (index == IDX_MIX_B) {
        snprintf(buf, buflen, "Chain B Mix");
        return;
    }

    /* Chain B amp knobs */
    if (index >= IDX_B_AMP_START && index < IDX_B_AMP_START + NUM_AMP_KNOBS) {
        int knob = index - IDX_B_AMP_START;
        snprintf(buf, buflen, "B %s", AMP_KNOBS[knob].name);
        return;
    }
    if (index == IDX_B_AMP_MODEL) {
        snprintf(buf, buflen, "B Amp Model");
        return;
    }
    if (index == IDX_B_CAB_TYPE) {
        snprintf(buf, buflen, "B Cab Type");
        return;
    }
    if (index == IDX_B_CAB_BYPASS) {
        snprintf(buf, buflen, "B Cab Bypass");
        return;
    }
    if (index == IDX_B_MIC_TYPE) {
        snprintf(buf, buflen, "B Mic Type");
        return;
    }
    if (index == IDX_B_MIC_DISTANCE) {
        snprintf(buf, buflen, "B Mic Distance");
        return;
    }
    if (index == IDX_B_MIC_ANGLE) {
        snprintf(buf, buflen, "B Mic Angle");
        return;
    }
    if (index == IDX_B_MIC_POSITION) {
        snprintf(buf, buflen, "B Mic Position");
        return;
    }

    /* Studio slot params */
    if (index >= IDX_STUDIO_START && index < IDX_PEDAL_START) {
        int offset   = index - IDX_STUDIO_START;
        int slot     = offset / PARAMS_PER_STUDIO;
        int sub      = offset % PARAMS_PER_STUDIO;
        if (sub == 0) {
            snprintf(buf, buflen, "Rack %d Type", slot + 1);
        } else if (sub == 1) {
            snprintf(buf, buflen, "Rack %d Bypass", slot + 1);
        } else {
            snprintf(buf, buflen, "Rack %d P%d", slot + 1, sub - 1);
        }
        return;
    }

    /* Pedal slot params */
    if (index >= IDX_PEDAL_START && index < NUM_PARAMS) {
        int offset   = index - IDX_PEDAL_START;
        int slot     = offset / PARAMS_PER_PEDAL;
        int sub      = offset % PARAMS_PER_PEDAL;
        const char *pos_name = (slot < NUM_PRE_PEDAL_SLOTS) ? "Pre" : "Post";
        int slot_num = (slot < NUM_PRE_PEDAL_SLOTS) ? slot + 1 : slot - NUM_PRE_PEDAL_SLOTS + 1;
        if (sub == 0) {
            snprintf(buf, buflen, "%s Pedal %d Type", pos_name, slot_num);
        } else if (sub == 1) {
            snprintf(buf, buflen, "%s Pedal %d Bypass", pos_name, slot_num);
        } else {
            snprintf(buf, buflen, "%s Pedal %d P%d", pos_name, slot_num, sub - 1);
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
    if (index == IDX_A_AMP_MODEL || index == IDX_B_AMP_MODEL)
        return (double)atoi(str);
    if (index == IDX_A_CAB_TYPE || index == IDX_B_CAB_TYPE)
        return (double)atoi(str);
    if (index == IDX_A_CAB_BYPASS || index == IDX_B_CAB_BYPASS)
        return (double)atoi(str);
    if (index == IDX_A_MIC_TYPE || index == IDX_B_MIC_TYPE)
        return (double)atoi(str);
    if (index == IDX_CHAIN_MODE)
        return (double)atoi(str);

    /* Bright switches */
    if (index < NUM_AMP_KNOBS &&
        AMP_KNOBS[index].amp_param == FX_AMP_PARAM_BRIGHT)
        return (double)atoi(str);
    if (index >= IDX_B_AMP_START && index < IDX_B_AMP_START + NUM_AMP_KNOBS &&
        AMP_KNOBS[index - IDX_B_AMP_START].amp_param == FX_AMP_PARAM_BRIGHT)
        return (double)atoi(str);

    /* Studio type selectors + bypass */
    if (index >= IDX_STUDIO_START && index < IDX_PEDAL_START) {
        int offset = index - IDX_STUDIO_START;
        int sub    = offset % PARAMS_PER_STUDIO;
        if (sub <= 1) return (double)atoi(str);
    }

    /* Pedal type selectors + bypass */
    if (index >= IDX_PEDAL_START && index < NUM_PARAMS) {
        int offset = index - IDX_PEDAL_START;
        int sub    = offset % PARAMS_PER_PEDAL;
        if (sub <= 1) return (double)atoi(str);
    }

    return atof(str);
}

void cplug_parameterValueToString(void *ptr, uint32_t param_id,
                                  char *buf, size_t bufsize, double value)
{
    (void)ptr;
    int index = find_param_index(param_id);
    if (index >= NUM_PARAMS) { snprintf(buf, bufsize, "0"); return; }

    /* Amp model name (Chain A or B) */
    if (index == IDX_A_AMP_MODEL || index == IDX_B_AMP_MODEL) {
        int model = (int)(value + 0.5);
        if (model < 0) model = 0;
        if (model >= FX_AMP_COUNT) model = FX_AMP_COUNT - 1;
        static const char *amp_names[] = {
            "Fullerton Clean", "Brit Crunch", "Southwest Lead",
            "Essex Chime", "Tweed Blues", "Meridian High Gain",
            "Citrus Roar", "Citrus Terror", "Regent 800",
            "Solar Monolith", "Eclipse Drone", "Emerald Ratrod Deluxe"
        };
        snprintf(buf, bufsize, "%s", amp_names[model]);
        return;
    }

    /* Cab type name (Chain A or B) */
    if (index == IDX_A_CAB_TYPE || index == IDX_B_CAB_TYPE) {
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

    /* Cab bypass (Chain A or B) */
    if (index == IDX_A_CAB_BYPASS || index == IDX_B_CAB_BYPASS) {
        snprintf(buf, bufsize, "%s", value >= 0.5 ? "On" : "Off");
        return;
    }

    /* Mic type name (Chain A or B) */
    if (index == IDX_A_MIC_TYPE || index == IDX_B_MIC_TYPE) {
        int mic = (int)(value + 0.5);
        if (mic < 0) mic = 0;
        if (mic >= FX_MIC_COUNT) mic = FX_MIC_COUNT - 1;
        snprintf(buf, bufsize, "%s",
                 fx_mic_get_type_name((fx_mic_type_t)mic));
        return;
    }

    /* Mic placement params (Chain A) */
    if (index >= IDX_A_MIC_DISTANCE && index <= IDX_A_MIC_POSITION) {
        snprintf(buf, bufsize, "%.0f%%", value * 100.0);
        return;
    }

    /* Mic placement params (Chain B) */
    if (index >= IDX_B_MIC_DISTANCE && index <= IDX_B_MIC_POSITION) {
        snprintf(buf, bufsize, "%.0f%%", value * 100.0);
        return;
    }

    /* Noise gate params */
    if (index == IDX_GATE_THRESHOLD) {
        snprintf(buf, bufsize, "%.1f dB", value);
        return;
    }
    if (index == IDX_GATE_ATTACK) {
        snprintf(buf, bufsize, "%.1f ms", value);
        return;
    }
    if (index == IDX_GATE_RELEASE) {
        snprintf(buf, bufsize, "%.0f ms", value);
        return;
    }
    if (index == IDX_GATE_HOLD) {
        snprintf(buf, bufsize, "%.0f ms", value);
        return;
    }

    /* Chain mode */
    if (index == IDX_CHAIN_MODE) {
        snprintf(buf, bufsize, "%s", value >= 0.5 ? "Dual" : "Single");
        return;
    }

    /* Chain mix levels */
    if (index == IDX_MIX_A || index == IDX_MIX_B) {
        snprintf(buf, bufsize, "%.0f%%", value * 100.0);
        return;
    }

    /* Bright switch (Chain A) */
    if (index < NUM_AMP_KNOBS &&
        AMP_KNOBS[index].amp_param == FX_AMP_PARAM_BRIGHT) {
        snprintf(buf, bufsize, "%s", value >= 0.5 ? "On" : "Off");
        return;
    }

    /* Bright switch (Chain B) */
    if (index >= IDX_B_AMP_START && index < IDX_B_AMP_START + NUM_AMP_KNOBS &&
        AMP_KNOBS[index - IDX_B_AMP_START].amp_param == FX_AMP_PARAM_BRIGHT) {
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
        if (sub == 1) {
            snprintf(buf, bufsize, "%s", value >= 0.5 ? "On" : "Off");
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
        if (sub == 1) {
            snprintf(buf, bufsize, "%s", value >= 0.5 ? "On" : "Off");
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

    /* Clear chain B — belongs to old engine */
    p->chain_b_id = -1;

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

        case CPLUG_EVENT_MIDI: {
            /*
             * Parse MIDI CC messages and map to plugin parameters.
             * Status byte 0xBn = CC on channel n.
             */
            uint8_t status = event.midi.status;
            if ((status & 0xF0) == MIDI_STATUS_CC) {
                int cc    = event.midi.data1 & 0x7F;
                int value = event.midi.data2 & 0x7F;
                int param_index = p->cc_map[cc];

                if (param_index >= 0 && param_index < NUM_PARAMS) {
                    /* Map 0-127 MIDI value to parameter range */
                    float lo  = param_min(param_index);
                    float hi  = param_max(param_index);
                    float val = lo + ((float)value / 127.0f) * (hi - lo);
                    uint32_t pid = cplug_getParameterID(p, (uint32_t)param_index);
                    cplug_setParameterValue(p, pid, (double)val);
                }
            }
            break;
        }

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

/* ── Factory preset enumeration ─────────────────────────────────── */

/*
 * CPLUG does not have built-in preset enumeration callbacks.
 * These functions provide the preset API that the embedded GUI and
 * host-specific extensions can call.  The CLAP preset-load extension
 * or VST3 program list can be wired up to these in the future.
 */

uint32_t cplug_getNumPresets(void *user_plugin)
{
    (void)user_plugin;
    return (uint32_t)NUM_FACTORY_PRESETS;
}

const char *cplug_getPresetName(void *user_plugin, uint32_t index)
{
    (void)user_plugin;
    if (index >= NUM_FACTORY_PRESETS) return NULL;
    return FACTORY_PRESETS[index].name;
}

void cplug_setPreset(void *user_plugin, uint32_t index)
{
    OxFXPlugin *p = (OxFXPlugin *)user_plugin;
    if (!p || !p->engine) return;
    if (index >= NUM_FACTORY_PRESETS) return;

    const FactoryPreset *preset = &FACTORY_PRESETS[index];

    if (preset->path) {
        /* Load preset file — try direct path first, then parent dir */
        bool loaded = fx_preset_load(p->engine, preset->path);
        if (!loaded) {
            /* Try with ../ prefix (common when running from build dir) */
            char alt_path[256];
            snprintf(alt_path, sizeof(alt_path), "../%s", preset->path);
            loaded = fx_preset_load(p->engine, alt_path);
        }

        if (loaded) {
            /* Sync parameter cache from engine state */
            sync_params_from_engine(p);
        }
    } else {
        /* "Init (Clean)" — reset engine to defaults */
        fx_engine_destroy(p->engine);

        /* Clear cached IDs */
        for (int i = 0; i < NUM_PEDAL_SLOTS; i++)
            p->pedal_ids[i] = -1;
        for (int i = 0; i < NUM_STUDIO_SLOTS; i++)
            p->studio_ids[i] = -1;
        p->chain_b_id = -1;

        p->engine = fx_engine_create(p->sample_rate);

        /* Reset all params to defaults and apply */
        if (p->engine) {
            for (int i = 0; i < NUM_PARAMS; i++) {
                p->param_values[i] = param_default(i);
                apply_param(p, i, p->param_values[i]);
            }
        }
    }
}

/* ── MIDI CC mapping — plugin-level API ──────────────────────────── */

/*
 * Map a MIDI CC number (0-127) to a plugin parameter index.
 * The GUI or host extension can call these to configure CC mappings.
 */

void oxfx_plugin_map_cc(void *user_plugin, int cc_number, int param_index)
{
    OxFXPlugin *p = (OxFXPlugin *)user_plugin;
    if (!p) return;
    if (cc_number >= 0 && cc_number < MIDI_CC_COUNT)
        p->cc_map[cc_number] = param_index;
}

void oxfx_plugin_unmap_cc(void *user_plugin, int cc_number)
{
    OxFXPlugin *p = (OxFXPlugin *)user_plugin;
    if (!p) return;
    if (cc_number >= 0 && cc_number < MIDI_CC_COUNT)
        p->cc_map[cc_number] = MIDI_CC_UNMAPPED;
}

int oxfx_plugin_get_cc_mapping(void *user_plugin, int cc_number)
{
    OxFXPlugin *p = (OxFXPlugin *)user_plugin;
    if (!p) return MIDI_CC_UNMAPPED;
    if (cc_number < 0 || cc_number >= MIDI_CC_COUNT) return MIDI_CC_UNMAPPED;
    return p->cc_map[cc_number];
}

/* ── GUI (CPLUG — embedded ImGui+SDL2+OpenGL) ──────────────────── */

#ifdef OXFX_PLUGIN_HAS_GUI

/*
 * Plugin GUI — SDL2 window embedded inside DAW host window.
 *
 * The actual implementation lives in gui_plugin_bridge.cpp which manages:
 * - SDL2 window creation + OpenGL context
 * - Win32 reparenting (SetParent + WS_CHILD) / Linux X11
 * - Dedicated render thread running ImGui + fx_gui_render_frame()
 * - WndProc subclass for keyboard/mouse capture on Windows
 *
 * These C-callable functions are the bridge API:
 */
void *oxfx_gui_create(void *engine);
void  oxfx_gui_destroy(void *gui);
void  oxfx_gui_attach(void *gui, void *parent_hwnd);
void  oxfx_gui_detach(void *gui);
void  oxfx_gui_set_visible(void *gui, bool visible);
void  oxfx_gui_get_size(void *gui, uint32_t *w, uint32_t *h);
bool  oxfx_gui_set_size(void *gui, uint32_t w, uint32_t h);

typedef struct {
    OxFXPlugin *plugin;
    void       *bridge_gui;  /* PluginGUI* from gui_plugin_bridge.cpp */
} PluginGUIWrapper;

void *cplug_createGUI(void *user_plugin)
{
    OxFXPlugin *plugin = (OxFXPlugin *)user_plugin;
    PluginGUIWrapper *wrap = (PluginGUIWrapper *)calloc(1, sizeof(PluginGUIWrapper));
    if (!wrap) return NULL;
    wrap->plugin = plugin;
    wrap->bridge_gui = oxfx_gui_create(plugin->engine);
    return wrap;
}

void cplug_destroyGUI(void *user_gui)
{
    if (!user_gui) return;
    PluginGUIWrapper *wrap = (PluginGUIWrapper *)user_gui;
    oxfx_gui_destroy(wrap->bridge_gui);
    free(wrap);
}

void cplug_setParent(void *user_gui, void *parent)
{
    if (!user_gui) return;
    PluginGUIWrapper *wrap = (PluginGUIWrapper *)user_gui;

    if (parent) {
        oxfx_gui_attach(wrap->bridge_gui, parent);
    } else {
        oxfx_gui_detach(wrap->bridge_gui);
    }
}

void cplug_setVisible(void *user_gui, bool visible)
{
    if (!user_gui) return;
    PluginGUIWrapper *wrap = (PluginGUIWrapper *)user_gui;
    oxfx_gui_set_visible(wrap->bridge_gui, visible);
}

void cplug_setScaleFactor(void *user_gui, float scale)
{
    (void)user_gui;
    (void)scale;
    /* Scale factor support — future enhancement */
}

void cplug_getSize(void *user_gui, uint32_t *w, uint32_t *h)
{
    if (!user_gui) { *w = 1200; *h = 700; return; }
    PluginGUIWrapper *wrap = (PluginGUIWrapper *)user_gui;
    oxfx_gui_get_size(wrap->bridge_gui, w, h);
}

void cplug_checkSize(void *user_gui, uint32_t *w, uint32_t *h)
{
    (void)user_gui;
    /* Enforce minimum size */
    if (*w < 800)  *w = 800;
    if (*h < 500)  *h = 500;
    /* Cap maximum */
    if (*w > 3840) *w = 3840;
    if (*h > 2160) *h = 2160;
}

bool cplug_setSize(void *user_gui, uint32_t w, uint32_t h)
{
    if (!user_gui) return false;
    PluginGUIWrapper *wrap = (PluginGUIWrapper *)user_gui;
    return oxfx_gui_set_size(wrap->bridge_gui, w, h);
}

#else /* !OXFX_PLUGIN_HAS_GUI */

/* ── GUI stubs (no GUI support compiled) ───────────────────────── */

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

#endif /* OXFX_PLUGIN_HAS_GUI */
