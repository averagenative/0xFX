/*
 * 0xFX — Microphone simulation DSP
 *
 * Post-cab microphone modeling: frequency response character per mic model,
 * plus placement-dependent filtering (distance/angle/position).
 *
 * Each mic model is defined by 2-4 biquad filters that shape the character.
 * Placement parameters add additional filtering:
 *   - Position: cone center (bright) to edge (dark) — LP filter cutoff
 *   - Angle: on-axis (flat) to off-axis (HF rolloff) — LP filter
 *   - Distance: proximity effect — low shelf boost for close mics
 *
 * DI mode (default) = bypass, no processing.
 * All processing is RT-safe: no malloc, no locks, no I/O.
 */
#include "engine_internal.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ══════════════════════════════════════════════════════════════════
 * Mic model frequency response profiles
 *
 * Each profile sets up 2-4 biquad filters to approximate the mic's
 * inherent frequency response character.
 * ══════════════════════════════════════════════════════════════════ */

typedef struct {
    float proximity_cutoff;  /* Hz — below this, proximity boost applies */
    float proximity_max_db;  /* max proximity boost at distance=0 */
} fx_mic_proximity_t;

/* Proximity effect characteristics per mic type */
static const fx_mic_proximity_t mic_proximity[FX_MIC_COUNT] = {
    [FX_MIC_DI]               = { 0.0f,   0.0f },  /* no processing */
    [FX_MIC_STAGE_WORKHORSE]  = { 300.0f, 6.0f },   /* moderate proximity */
    [FX_MIC_ROADIE_VOCAL]     = { 250.0f, 4.0f },   /* less than SM57 */
    [FX_MIC_BERLIN_DYNAMIC]   = { 200.0f, 4.0f },   /* moderate */
    [FX_MIC_SILVER_BULLET]    = { 200.0f, 1.0f },   /* Variable-D: minimal */
    [FX_MIC_VELVET_RIBBON]    = { 400.0f, 10.0f },  /* strong — ribbon */
    [FX_MIC_HERITAGE_RIBBON]  = { 400.0f, 8.0f },   /* strong — ribbon */
    [FX_MIC_STUDIO_LARGE]     = { 350.0f, 8.0f },   /* pronounced — LDC cardioid */
    [FX_MIC_AUSTRIAN_PENCIL]  = { 300.0f, 6.0f },   /* moderate — SDC */
    [FX_MIC_ROOM_PENCIL]      = { 200.0f, 3.0f },   /* less — small diaphragm */
};

/*
 * Configure the character biquads for a given mic model.
 * Called only when mic type changes.
 */
static void mic_setup_character(fx_mic_state_t *mic, float sr) {
    /* Reset all character filters to unity */
    for (int i = 0; i < FX_MIC_MAX_BIQUADS; i++) {
        mic->character[i].b0 = 1.0f;
        mic->character[i].b1 = 0.0f;
        mic->character[i].b2 = 0.0f;
        mic->character[i].a1 = 0.0f;
        mic->character[i].a2 = 0.0f;
        mic->character[i].z1 = 0.0f;
        mic->character[i].z2 = 0.0f;
    }

    switch (mic->type) {
        case FX_MIC_DI:
            mic->num_character = 0;
            return;

        case FX_MIC_STAGE_WORKHORSE:
            /* SM57: rolloff below 200Hz, presence peak +5dB at 5.5kHz, rolloff >12kHz */
            fx_biquad_highpass(&mic->character[0], 200.0f, 0.5f, sr);
            fx_biquad_peak(&mic->character[1], 5500.0f, 5.0f, 1.2f, sr);
            fx_biquad_lowpass(&mic->character[2], 12000.0f, 0.707f, sr);
            mic->num_character = 3;
            break;

        case FX_MIC_ROADIE_VOCAL:
            /* SM58: rolloff below 150Hz, gentler presence +3dB at 5kHz, rolloff >14kHz */
            fx_biquad_highpass(&mic->character[0], 150.0f, 0.5f, sr);
            fx_biquad_peak(&mic->character[1], 5000.0f, 3.0f, 1.0f, sr);
            fx_biquad_lowpass(&mic->character[2], 14000.0f, 0.707f, sr);
            mic->num_character = 3;
            break;

        case FX_MIC_BERLIN_DYNAMIC:
            /* e609: flat to 100Hz, scoop at 600Hz, presence 3-8kHz, rolloff >14kHz */
            fx_biquad_highpass(&mic->character[0], 100.0f, 0.5f, sr);
            fx_biquad_peak(&mic->character[1], 600.0f, -2.0f, 0.8f, sr);
            fx_biquad_peak(&mic->character[2], 5000.0f, 3.0f, 0.6f, sr);
            fx_biquad_lowpass(&mic->character[3], 14000.0f, 0.707f, sr);
            mic->num_character = 4;
            break;

        case FX_MIC_SILVER_BULLET:
            /* RE20: flat 45Hz-15kHz, gentle presence +2dB at 4kHz, rolloff >15kHz */
            fx_biquad_highpass(&mic->character[0], 45.0f, 0.5f, sr);
            fx_biquad_peak(&mic->character[1], 4000.0f, 2.0f, 0.8f, sr);
            fx_biquad_lowpass(&mic->character[2], 15000.0f, 0.707f, sr);
            mic->num_character = 3;
            break;

        case FX_MIC_VELVET_RIBBON:
            /* R-121: extended lows to 50Hz, flat mids, HF rolloff from 8kHz, -6dB@12kHz */
            fx_biquad_highpass(&mic->character[0], 50.0f, 0.5f, sr);
            fx_biquad_lowpass(&mic->character[1], 10000.0f, 0.5f, sr);
            fx_biquad_highshelf(&mic->character[2], 8000.0f, -6.0f, sr);
            mic->num_character = 3;
            break;

        case FX_MIC_HERITAGE_RIBBON:
            /* Coles 4038: low-mid emphasis 150-500Hz +3dB, steep HF rolloff >6kHz */
            fx_biquad_peak(&mic->character[0], 300.0f, 3.0f, 0.6f, sr);
            fx_biquad_lowpass(&mic->character[1], 8000.0f, 0.5f, sr);
            fx_biquad_highshelf(&mic->character[2], 6000.0f, -8.0f, sr);
            mic->num_character = 3;
            break;

        case FX_MIC_STUDIO_LARGE:
            /* U87: extended flat 20Hz-16kHz, subtle presence +2dB at 9kHz */
            fx_biquad_highpass(&mic->character[0], 20.0f, 0.5f, sr);
            fx_biquad_peak(&mic->character[1], 9000.0f, 2.0f, 1.0f, sr);
            fx_biquad_lowpass(&mic->character[2], 18000.0f, 0.707f, sr);
            mic->num_character = 3;
            break;

        case FX_MIC_AUSTRIAN_PENCIL:
            /* C414: flat 30Hz-10kHz, presence peak +3dB at 11kHz, extended to 20kHz */
            fx_biquad_highpass(&mic->character[0], 30.0f, 0.5f, sr);
            fx_biquad_peak(&mic->character[1], 11000.0f, 3.0f, 1.2f, sr);
            mic->num_character = 2;
            break;

        case FX_MIC_ROOM_PENCIL:
            /* C451/KM84: rolloff below 100Hz, presence +4dB at 10kHz */
            fx_biquad_highpass(&mic->character[0], 100.0f, 0.707f, sr);
            fx_biquad_peak(&mic->character[1], 10000.0f, 4.0f, 1.0f, sr);
            mic->num_character = 2;
            break;

        default:
            mic->num_character = 0;
            break;
    }
}

/*
 * Recalculate placement-dependent filter coefficients.
 * Called only when placement params change.
 */
static void mic_update_placement(fx_mic_state_t *mic, float sr) {
    float distance = mic->params[FX_MIC_PARAM_DISTANCE];
    float angle    = mic->params[FX_MIC_PARAM_ANGLE];
    float position = mic->params[FX_MIC_PARAM_POSITION];

    /* ── Position filter: cone center (bright) → edge (dark) ──
     * Center = ~16kHz LP cutoff (wide open)
     * Edge = ~4kHz LP cutoff (dark, warm)
     */
    float pos_cutoff = 16000.0f - position * 12000.0f;  /* 16kHz → 4kHz */
    if (pos_cutoff < 2000.0f) pos_cutoff = 2000.0f;
    fx_biquad_lowpass(&mic->position_lp, pos_cutoff, 0.707f, sr);

    /* ── Angle filter: on-axis (flat) → off-axis (HF rolloff) ──
     * On-axis = ~18kHz LP (transparent)
     * 90° off-axis = ~3kHz LP (very muffled)
     * HF attenuates faster off-axis — this is the key physical behavior
     */
    float angle_cutoff = 18000.0f - angle * 15000.0f;  /* 18kHz → 3kHz */
    if (angle_cutoff < 2000.0f) angle_cutoff = 2000.0f;
    fx_biquad_lowpass(&mic->angle_lp, angle_cutoff, 0.707f, sr);

    /* ── Proximity effect: close = bass boost, far = flat ──
     * Boost = proximity_max_db * (1.0 - distance)
     * Applied as a low shelf at the mic's proximity cutoff frequency
     */
    const fx_mic_proximity_t *prox = &mic_proximity[mic->type];
    if (prox->proximity_max_db > 0.0f && prox->proximity_cutoff > 0.0f) {
        float prox_gain_db = prox->proximity_max_db * (1.0f - distance);
        fx_biquad_lowshelf(&mic->proximity, prox->proximity_cutoff,
                           prox_gain_db, sr);
    } else {
        /* No proximity effect — set to unity */
        mic->proximity.b0 = 1.0f;
        mic->proximity.b1 = 0.0f;
        mic->proximity.b2 = 0.0f;
        mic->proximity.a1 = 0.0f;
        mic->proximity.a2 = 0.0f;
    }
}

/* ══════════════════════════════════════════════════════════════════
 * Public internal functions (called from engine.c)
 * ══════════════════════════════════════════════════════════════════ */

void fx_mic_init(fx_mic_state_t *mic) {
    if (!mic) return;
    memset(mic, 0, sizeof(*mic));
    mic->type = FX_MIC_DI;
    mic->num_character = 0;

    /* Default placement: close mic, on-axis, center of cone */
    mic->params[FX_MIC_PARAM_DISTANCE] = 0.3f;
    mic->params[FX_MIC_PARAM_ANGLE]    = 0.0f;
    mic->params[FX_MIC_PARAM_POSITION] = 0.0f;

    /* Force recalculation on first use */
    mic->cached_type = FX_MIC_COUNT;  /* invalid — forces rebuild */
    mic->cached_sr   = 0.0f;
    for (int i = 0; i < FX_MIC_PARAM_COUNT; i++) {
        mic->cached_params[i] = -1.0f;
    }
}

void fx_mic_process(fx_mic_state_t *mic, float *buf, int n, float sr) {
    if (!mic || mic->type == FX_MIC_DI) return;  /* DI = bypass */

    /* Check if mic type changed — rebuild character filters */
    if (mic->type != mic->cached_type || sr != mic->cached_sr) {
        mic->cached_type = mic->type;
        mic->cached_sr = sr;
        mic_setup_character(mic, sr);
        /* Force placement recalc too */
        for (int i = 0; i < FX_MIC_PARAM_COUNT; i++) {
            mic->cached_params[i] = -1.0f;
        }
    }

    /* Check if placement params changed — recalculate placement filters */
    bool placement_changed = false;
    for (int i = 0; i < FX_MIC_PARAM_COUNT; i++) {
        if (mic->params[i] != mic->cached_params[i]) {
            mic->cached_params[i] = mic->params[i];
            placement_changed = true;
        }
    }
    if (placement_changed) {
        mic_update_placement(mic, sr);
    }

    /* Process audio through all filters */
    for (int i = 0; i < n; i++) {
        float x = buf[i];

        /* Mic character filters */
        for (int f = 0; f < mic->num_character; f++) {
            x = fx_biquad_process(&mic->character[f], x);
        }

        /* Placement filters */
        x = fx_biquad_process(&mic->position_lp, x);
        x = fx_biquad_process(&mic->angle_lp, x);
        x = fx_biquad_process(&mic->proximity, x);

        buf[i] = x;
    }
}

/* ══════════════════════════════════════════════════════════════════
 * Type name table
 * ══════════════════════════════════════════════════════════════════ */

static const char *mic_type_names[FX_MIC_COUNT] = {
    "DI (Direct)",
    "Stage Workhorse",
    "Roadie Vocal",
    "Berlin Dynamic",
    "Silver Bullet",
    "Velvet Ribbon",
    "Heritage Ribbon",
    "Studio Large",
    "Austrian Pencil",
    "Room Pencil",
};

const char *fx_mic_get_type_name(fx_mic_type_t type) {
    if (type < 0 || type >= FX_MIC_COUNT) return "?";
    return mic_type_names[type];
}
