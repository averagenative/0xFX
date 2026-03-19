/*
 * 0xFX Engine — Public API
 *
 * This is the ONLY header that GUI and plugin layers should include.
 * All types are opaque. Never access internal structs directly.
 */
#ifndef FX_ENGINE_H
#define FX_ENGINE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque engine handle */
typedef struct fx_engine fx_engine_t;

/* IDs for pedals and chains — returned by create functions */
typedef int32_t fx_pedal_id;
typedef int32_t fx_chain_id;

/* The default chain (always exists) */
#define FX_CHAIN_DEFAULT 0

/* Amp model types */
typedef enum {
    FX_AMP_FULLERTON_CLEAN = 0,  /* inspired by classic American clean amps */
    FX_AMP_BRIT_CRUNCH,          /* inspired by classic British crunch amps */
    FX_AMP_SOUTHWEST_LEAD,       /* inspired by American high-gain amps */
    FX_AMP_ESSEX_CHIME,          /* inspired by British chime amps */
    FX_AMP_TWEED_BLUES,          /* inspired by American tweed-era amps */
    FX_AMP_COUNT
} fx_amp_type_t;

/* Amp parameters — each amp model exposes a subset */
typedef enum {
    FX_AMP_PARAM_GAIN = 0,
    FX_AMP_PARAM_VOLUME,
    FX_AMP_PARAM_BASS,
    FX_AMP_PARAM_MID,
    FX_AMP_PARAM_TREBLE,
    FX_AMP_PARAM_PRESENCE,
    FX_AMP_PARAM_SAG,
    FX_AMP_PARAM_MASTER,
    FX_AMP_PARAM_BRIGHT,     /* 0.0 = off, 1.0 = on */
    FX_AMP_PARAM_CUT,
    FX_AMP_PARAM_COUNT
} fx_amp_param_t;

/* Pedal types */
typedef enum {
    /* Overdrive */
    FX_PEDAL_JADE_DRIVE = 0,     /* mid-humped soft clip */
    FX_PEDAL_GOLD_DRIVE,         /* transparent OD with clean blend */
    FX_PEDAL_BLUES_GRIT,         /* gritty FET-based OD */
    /* Distortion */
    FX_PEDAL_RODENT,             /* op-amp hard clip */
    FX_PEDAL_ORANGE_DIST,        /* bright hard clip */
    FX_PEDAL_METAL_ZONE,         /* dual gain + parametric mid */
    FX_PEDAL_AMP_BOX,            /* tight high-gain amp-in-a-box */
    /* Fuzz */
    FX_PEDAL_MAMMOTH_FUZZ,       /* 4-stage scooped-mid fuzz */
    FX_PEDAL_ROUND_FUZZ,         /* smooth germanium/silicon */
    FX_PEDAL_WRAITH_FUZZ,        /* aggressive germanium */
    FX_PEDAL_CHAOS_FUZZ,         /* gated sputter, oscillation */
    /* Delay */
    FX_PEDAL_ECHO_DELAY,         /* clean digital delay */
    FX_PEDAL_CARBON_DELAY,       /* warm analog BBD */
    FX_PEDAL_TAPE_MACHINE,       /* tape echo w/ wow+flutter */
    FX_PEDAL_MEMORY_ECHO,        /* modulated delay */
    /* Reverb */
    FX_PEDAL_DRIP_VERB,          /* spring reverb */
    FX_PEDAL_PLATE_VERB,         /* plate reverb */
    FX_PEDAL_HALL_VERB,          /* hall reverb */
    FX_PEDAL_SHIMMER_VERB,       /* octave-up shimmer */
    FX_PEDAL_CLOUD_VERB,         /* ambient/freeze reverb */
    /* Modulation */
    FX_PEDAL_LIQUID_CHORUS,      /* BBD-style chorus */
    FX_PEDAL_PHASE_SWEEP,        /* allpass phaser */
    FX_PEDAL_JET_FLANGER,        /* flanger w/ through-zero */
    FX_PEDAL_PULSE_TREM,         /* tremolo + harmonic mode */
    FX_PEDAL_DRIFT_VIBRATO,      /* true pitch vibrato */
    /* Wah / Filter */
    FX_PEDAL_HOWL_WAH,           /* expression wah */
    FX_PEDAL_QUACK_FILTER,       /* auto-wah / envelope filter */
    /* Compressor */
    FX_PEDAL_SQUEEZE_BOX,        /* squashy OTA compressor */
    FX_PEDAL_GLASS_COMP,         /* transparent w/ blend */
    FX_PEDAL_PUNCH_COMP,         /* 1176-style fast attack */
    /* EQ */
    FX_PEDAL_TONE_SCULPTOR,      /* graphic EQ */
    FX_PEDAL_PRECISION_EQ,       /* parametric EQ */
    /* Noise */
    FX_PEDAL_NOISE_GATE,         /* noise gate */
    /* Utility */
    FX_PEDAL_GRIT_CRUSH,         /* bitcrusher */
    FX_PEDAL_RING_TONE,          /* ring modulator */
    FX_PEDAL_WARM_TAPE,          /* tape saturation */
    /* Pitch */
    FX_PEDAL_OCTAVE_ENGINE,      /* polyphonic octave */
    FX_PEDAL_PITCH_WARP,         /* pitch bend / whammy */
    /* Looper */
    FX_PEDAL_LOOP_STATION,       /* looper */
    /* Experimental */
    FX_PEDAL_INFINITE_HOLD,      /* freeze / drone */
    FX_PEDAL_GRAIN_CLOUD,        /* granular delay */
    FX_PEDAL_TYPE_COUNT
} fx_pedal_type_t;

/* Chain position for pedals */
typedef enum {
    FX_CHAIN_POS_PRE = 0,       /* before amp */
    FX_CHAIN_POS_POST            /* after amp + cab */
} fx_chain_pos_t;

/* ── Engine lifecycle ─────────────────────────────────────────── */

fx_engine_t *fx_engine_create(float sample_rate);
void         fx_engine_destroy(fx_engine_t *engine);
void         fx_engine_process(fx_engine_t *engine,
                               const float *input, float *output,
                               int num_frames);

/* ── Signal chain — pedals ────────────────────────────────────── */

fx_pedal_id  fx_chain_add_pedal(fx_engine_t *engine,
                                fx_pedal_type_t type,
                                fx_chain_pos_t pos);
void         fx_chain_remove_pedal(fx_engine_t *engine, fx_pedal_id id);
void         fx_chain_move_pedal(fx_engine_t *engine, fx_pedal_id id,
                                 fx_chain_pos_t pos, int index);
int          fx_chain_get_pedal_count(fx_engine_t *engine, fx_chain_pos_t pos);

void         fx_pedal_set_param(fx_engine_t *engine, fx_pedal_id id,
                                int param, float value);
float        fx_pedal_get_param(fx_engine_t *engine, fx_pedal_id id,
                                int param);
void         fx_pedal_set_bypass(fx_engine_t *engine, fx_pedal_id id,
                                 bool bypass);
bool         fx_pedal_get_bypass(fx_engine_t *engine, fx_pedal_id id);
fx_pedal_type_t fx_pedal_get_type(fx_engine_t *engine, fx_pedal_id id);

int          fx_pedal_get_param_count(fx_pedal_type_t type);
const char  *fx_pedal_get_param_name(fx_pedal_type_t type, int param);
const char  *fx_pedal_get_type_name(fx_pedal_type_t type);

/* ── Multi-amp routing ────────────────────────────────────────── */

fx_chain_id  fx_chain_create(fx_engine_t *engine);
void         fx_chain_destroy(fx_engine_t *engine, fx_chain_id id);
void         fx_chain_set_mix(fx_engine_t *engine, fx_chain_id id,
                              float level);
float        fx_chain_get_mix(fx_engine_t *engine, fx_chain_id id);
int          fx_chain_get_count(fx_engine_t *engine);

/* ── Amp model — per chain ────────────────────────────────────── */

void         fx_amp_set_model(fx_engine_t *engine, fx_chain_id chain,
                              fx_amp_type_t type);
fx_amp_type_t fx_amp_get_model(fx_engine_t *engine, fx_chain_id chain);
void         fx_amp_set_param(fx_engine_t *engine, fx_chain_id chain,
                              fx_amp_param_t param, float value);
float        fx_amp_get_param(fx_engine_t *engine, fx_chain_id chain,
                              fx_amp_param_t param);
int          fx_amp_get_param_count(fx_amp_type_t type);
const char  *fx_amp_get_param_name(fx_amp_type_t type, fx_amp_param_t param);
const char  *fx_amp_get_type_name(fx_amp_type_t type);

/* ── Cabinet IR — per chain ───────────────────────────────────── */

/* Cabinet type for synthetic IR generation */
typedef enum {
    FX_CAB_1X12_OPEN = 0,    /* 1x12 open back — bright, chimey */
    FX_CAB_2X12_CLOSED,      /* 2x12 closed back — tighter, focused */
    FX_CAB_4X12_STRAIGHT,    /* 4x12 straight — classic rock, full */
    FX_CAB_4X12_SLANT,       /* 4x12 slant — slightly brighter top */
    FX_CAB_DIRECT,           /* direct/flat — minimal coloring */
    FX_CAB_TYPE_COUNT
} fx_cab_type_t;

/* Microphone position for synthetic IR */
typedef enum {
    FX_MIC_ON_AXIS = 0,     /* on-axis — brighter, more presence */
    FX_MIC_OFF_AXIS,        /* off-axis — darker, smoother */
    FX_MIC_EDGE,            /* edge of cone — scooped mids */
    FX_MIC_POS_COUNT
} fx_mic_pos_t;

/* Parameters for synthetic IR generation */
typedef struct {
    fx_cab_type_t cab_type;     /* cabinet type */
    fx_mic_pos_t  mic_pos;      /* microphone position */
    float         speaker_fs;   /* speaker resonant frequency (Hz), 60-120 typical */
    float         brightness;   /* 0.0 = dark, 1.0 = bright */
    float         resonance;    /* 0.0 = flat, 1.0 = resonant */
} fx_cab_params_t;

bool         fx_cab_load_ir(fx_engine_t *engine, fx_chain_id chain,
                            const char *wav_path);
bool         fx_cab_generate_ir(fx_engine_t *engine, fx_chain_id chain,
                                const fx_cab_params_t *params);
void         fx_cab_set_bypass(fx_engine_t *engine, fx_chain_id chain,
                               bool bypass);
bool         fx_cab_get_bypass(fx_engine_t *engine, fx_chain_id chain);

/* ── Presets (.0xfx JSON) ─────────────────────────────────────── */

bool         fx_preset_save(fx_engine_t *engine, const char *path);
bool         fx_preset_load(fx_engine_t *engine, const char *path);

/* ── Tuner ────────────────────────────────────────────────────── */

float        fx_tuner_get_frequency(fx_engine_t *engine);
int          fx_tuner_get_note(fx_engine_t *engine);
float        fx_tuner_get_cents(fx_engine_t *engine);
const char  *fx_tuner_get_note_name(fx_engine_t *engine);

/* ── Audio device management (standalone only) ────────────────── */

int          fx_audio_get_device_count(void);
const char  *fx_audio_get_device_name(int index);
bool         fx_audio_set_device(fx_engine_t *engine, int index);
bool         fx_audio_set_buffer_size(fx_engine_t *engine, int frames);
bool         fx_audio_set_sample_rate(fx_engine_t *engine, float rate);

#ifdef __cplusplus
}
#endif

#endif /* FX_ENGINE_H */
