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
    FX_AMP_MERIDIAN_HIGH_GAIN,   /* inspired by American high-gain metal amps */
    FX_AMP_CITRUS_ROAR,          /* inspired by British thick/fuzzy crunch amps */
    FX_AMP_CITRUS_TERROR,        /* inspired by British low-wattage Class A amps */
    FX_AMP_REGENT_800,           /* inspired by classic British rock/metal amps */
    FX_AMP_SOLAR_MONOLITH,       /* inspired by massive clean-to-doom amps */
    FX_AMP_ECLIPSE_DRONE,        /* inspired by extreme low-end drone amps */
    FX_AMP_EMERALD_DELUXE,       /* inspired by American 40W hotrod combo amps */
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
    FX_AMP_PARAM_TONE,        /* single tone control (combined EQ) */
    FX_AMP_PARAM_FEEDBACK,    /* harmonic feedback sustain */
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
fx_pedal_id  fx_chain_get_pedal_at(fx_engine_t *engine, fx_chain_pos_t pos, int index);

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
int          fx_chain_get_count(fx_engine_t *engine);
void         fx_chain_set_mix(fx_engine_t *engine, fx_chain_id id,
                              float level);
float        fx_chain_get_mix(fx_engine_t *engine, fx_chain_id id);

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

/* Custom cab metadata. Non-empty on custom cabs; empty on synthetic/bundled.
 * Presets persist all three so a reload can restore the same IR, display
 * name, and image. Get returns an internal pointer — do not free or retain
 * across subsequent set calls. */
const char  *fx_cab_get_custom_ir_path(fx_engine_t *engine, fx_chain_id chain);
const char  *fx_cab_get_custom_name(fx_engine_t *engine, fx_chain_id chain);
const char  *fx_cab_get_custom_image_path(fx_engine_t *engine, fx_chain_id chain);
void         fx_cab_set_custom_name(fx_engine_t *engine, fx_chain_id chain, const char *name);
void         fx_cab_set_custom_image_path(fx_engine_t *engine, fx_chain_id chain, const char *path);
void         fx_cab_clear_custom_ir_path(fx_engine_t *engine, fx_chain_id chain);

/* ── Microphone simulation — post-cab, pre-post-pedals ───────── */

/* Mic types */
typedef enum {
    FX_MIC_DI = 0,               /* Direct inject — no mic coloration (default) */
    FX_MIC_STAGE_WORKHORSE,      /* SM57-style dynamic */
    FX_MIC_ROADIE_VOCAL,         /* SM58-style dynamic */
    FX_MIC_BERLIN_DYNAMIC,       /* e609-style dynamic */
    FX_MIC_SILVER_BULLET,        /* RE20-style dynamic */
    FX_MIC_VELVET_RIBBON,        /* R-121-style ribbon */
    FX_MIC_HERITAGE_RIBBON,      /* Coles 4038-style ribbon */
    FX_MIC_STUDIO_LARGE,         /* U87-style condenser */
    FX_MIC_AUSTRIAN_PENCIL,      /* C414-style condenser */
    FX_MIC_ROOM_PENCIL,          /* C451/KM84-style condenser */
    FX_MIC_COUNT
} fx_mic_type_t;

/* Mic placement parameters */
typedef enum {
    FX_MIC_PARAM_DISTANCE,       /* 0.0 (touching) to 1.0 (room) */
    FX_MIC_PARAM_ANGLE,          /* 0.0 (on-axis) to 1.0 (off-axis) */
    FX_MIC_PARAM_POSITION,       /* 0.0 (cone center) to 1.0 (cone edge) */
    FX_MIC_PARAM_COUNT
} fx_mic_param_t;

void          fx_mic_set_type(fx_engine_t *engine, fx_chain_id chain,
                               fx_mic_type_t type);
fx_mic_type_t fx_mic_get_type(fx_engine_t *engine, fx_chain_id chain);
void          fx_mic_set_param(fx_engine_t *engine, fx_chain_id chain,
                                fx_mic_param_t param, float value);
float         fx_mic_get_param(fx_engine_t *engine, fx_chain_id chain,
                                fx_mic_param_t param);
const char   *fx_mic_get_type_name(fx_mic_type_t type);

/* ── Studio processors — post-amp rack gear ──────────────────── */

typedef enum {
    FX_STUDIO_IRON_SQUEEZE = 0,  /* FET compressor */
    FX_STUDIO_GLASS_EQ,          /* Passive EQ */
    FX_STUDIO_REEL_WARMTH,       /* Tape saturation */
    FX_STUDIO_BRICK_WALL,        /* Limiter */
    /* Phase 2 */
    FX_STUDIO_VELVET_PRESS,      /* Optical compressor (LA-2A style) */
    FX_STUDIO_GLUE_BUS,          /* VCA bus compressor (SSL style) */
    FX_STUDIO_VALVE_COLOR,       /* Tube saturation */
    FX_STUDIO_PRECISION_EQ,      /* Channel EQ (Neve style) */
    FX_STUDIO_ROOM_ENGINE,       /* Room simulation */
    FX_STUDIO_COUNT
} fx_studio_type_t;

typedef int fx_studio_id;

fx_studio_id  fx_studio_add(fx_engine_t *engine, fx_studio_type_t type);
void          fx_studio_remove(fx_engine_t *engine, fx_studio_id id);
void          fx_studio_set_param(fx_engine_t *engine, fx_studio_id id,
                                   int param, float value);
float         fx_studio_get_param(fx_engine_t *engine, fx_studio_id id,
                                   int param);
void          fx_studio_set_bypass(fx_engine_t *engine, fx_studio_id id,
                                    bool bypass);
bool          fx_studio_get_bypass(fx_engine_t *engine, fx_studio_id id);
fx_studio_type_t fx_studio_get_type(fx_engine_t *engine, fx_studio_id id);
const char   *fx_studio_get_type_name(fx_studio_type_t type);
int           fx_studio_get_param_count(fx_studio_type_t type);
const char   *fx_studio_get_param_name(fx_studio_type_t type, int param);

/* ── Noise gate — input stage ─────────────────────────────────── */

void         fx_gate_set_threshold(fx_engine_t *engine, float db);
float        fx_gate_get_threshold(fx_engine_t *engine);
void         fx_gate_set_attack(fx_engine_t *engine, float ms);
float        fx_gate_get_attack(fx_engine_t *engine);
void         fx_gate_set_release(fx_engine_t *engine, float ms);
float        fx_gate_get_release(fx_engine_t *engine);
void         fx_gate_set_hold(fx_engine_t *engine, float ms);
float        fx_gate_get_hold(fx_engine_t *engine);

/* ── Presets (.0xfx JSON) ─────────────────────────────────────── */

bool         fx_preset_save(fx_engine_t *engine, const char *path);
bool         fx_preset_load(fx_engine_t *engine, const char *path);

/* ── Looper — 9-slot keyboard-driven live loop module ─────────
 *
 * The looper lives OUTSIDE the live signal chain. It taps the processed
 * output (or the dry input, if toggled) into active slots, and mixes its
 * playback back into the final output AFTER the live chain. This means
 * loop playback never re-processes through reverb/delay on each repeat.
 *
 * Each of the 9 slots has an independent state machine and buffer
 * (120 s max, lazy-allocated on first record). Tap the same slot again
 * to advance it through: EMPTY → ARMED → RECORDING → PLAYING ⇄ OVERDUBBING.
 */

typedef enum {
    FX_LOOP_EMPTY = 0,
    FX_LOOP_ARMED,         /* waiting for sync boundary before recording */
    FX_LOOP_RECORDING,
    FX_LOOP_PLAYING,
    FX_LOOP_OVERDUBBING,
} fx_loop_state_t;

#define FX_LOOPER_SLOT_COUNT 9

void            fx_looper_slot_tap   (fx_engine_t *engine, int slot);    /* advance state */
void            fx_looper_slot_mute  (fx_engine_t *engine, int slot);    /* toggle mute */
void            fx_looper_slot_clear (fx_engine_t *engine, int slot);    /* erase slot */
void            fx_looper_slot_undo  (fx_engine_t *engine, int slot);    /* revert last overdub */
int             fx_looper_arm_next   (fx_engine_t *engine);              /* arm first EMPTY, returns slot or -1 */
void            fx_looper_master_toggle(fx_engine_t *engine);            /* play/pause all */
bool            fx_looper_master_is_playing(fx_engine_t *engine);
void            fx_looper_focus_next (fx_engine_t *engine);              /* Tab cycles focus */
void            fx_looper_set_focus  (fx_engine_t *engine, int slot);    /* explicit focus (pad click, number key) */
int             fx_looper_focused    (fx_engine_t *engine);

void            fx_looper_set_sync       (fx_engine_t *engine, bool on);
bool            fx_looper_get_sync       (fx_engine_t *engine);
void            fx_looper_set_tap_pre_chain(fx_engine_t *engine, bool on);
bool            fx_looper_get_tap_pre_chain(fx_engine_t *engine);
void            fx_looper_set_master_level(fx_engine_t *engine, float v); /* 0..1 */
float           fx_looper_get_master_level(fx_engine_t *engine);

fx_loop_state_t fx_looper_get_slot_state (fx_engine_t *engine, int slot);
bool            fx_looper_get_slot_muted (fx_engine_t *engine, int slot);
int             fx_looper_get_slot_length_frames(fx_engine_t *engine, int slot);
int             fx_looper_get_slot_play_pos     (fx_engine_t *engine, int slot);
int             fx_looper_get_slot_layers       (fx_engine_t *engine, int slot);

bool            fx_looper_export_slot_wav(fx_engine_t *engine, int slot, const char *path);
bool            fx_looper_export_mix_wav (fx_engine_t *engine, const char *path);

/* ── Master volume ───────────────────────────────────────────── */

void         fx_engine_set_master_volume(fx_engine_t *engine, float volume);  /* 0.0 to 1.0 */
float        fx_engine_get_master_volume(fx_engine_t *engine);

/* ── Level metering ───────────────────────────────────────────── */

float        fx_engine_get_input_level(fx_engine_t *engine);   /* peak level of last processed block */
float        fx_engine_get_output_level(fx_engine_t *engine);  /* peak level of last processed output */

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
