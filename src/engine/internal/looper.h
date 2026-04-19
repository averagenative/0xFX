/*
 * 0xFX — Looper module (internal)
 *
 * 9-slot keyboard-driven looper. Each slot captures audio into its own
 * float buffer (mono, lazy-allocated), supports one-level undo, and has
 * an independent state machine (EMPTY/ARMED/RECORDING/PLAYING/OVERDUBBING).
 *
 * Signal flow: the engine feeds the looper a "tap" signal each block
 * (post-live-chain by default; pre-live-chain if toggled). Loop playback
 * is mixed into the engine output AFTER the live chain so loops never
 * re-process through reverb/delay on each repeat.
 */
#ifndef FX_LOOPER_INTERNAL_H
#define FX_LOOPER_INTERNAL_H

#include "../fx_engine.h"
#include <stdbool.h>

#define FX_LOOPER_SLOTS              9
#define FX_LOOPER_MAX_LOOP_SECONDS   120
/* Max samples per slot: 120s × 48kHz sentinel. 44.1/48 kHz both fit. */
#define FX_LOOPER_MAX_FRAMES_48K     (FX_LOOPER_MAX_LOOP_SECONDS * 48000)

typedef struct {
    volatile fx_loop_state_t state;   /* EMPTY/ARMED/RECORDING/PLAYING/OVERDUBBING */
    bool   muted;                     /* orthogonal flag: slot plays silently */
    float *buf;                       /* lazy-alloc mono buffer, max_frames */
    int    max_frames;                /* capacity of buf (sample_rate * 120) */
    int    length;                    /* actual recorded length in frames */
    int    play_pos;                  /* current playhead */
    int    layers;                    /* overdub count (0 = original take) */

    /* One-level undo: snapshot taken when entering OVERDUBBING. */
    float *undo_buf;
    int    undo_length;
    int    undo_layers;
    bool   has_undo;
} fx_loop_slot_t;

typedef struct {
    fx_loop_slot_t slots[FX_LOOPER_SLOTS];

    /* Master controls */
    bool   master_playing;    /* Space: true = loops advance, false = frozen */
    float  master_level;      /* 0..1 — global output volume for looper */
    bool   sync_mode;         /* true = quantize to first slot's length */
    bool   tap_pre_chain;     /* false (default) = post-chain tap */

    /* Sync state */
    int    sync_length;       /* length of the first recorded slot (samples) */
    int    sync_pos;           /* global position modulo sync_length */
    int    focused_slot;       /* Tab cycles focus for visual feedback */

    float  sample_rate;
} fx_looper_t;

/* ── Lifecycle ──────────────────────────────────────────────────── */

void looper_init(fx_looper_t *l, float sample_rate);
void looper_free(fx_looper_t *l);

/* ── Per-block audio ───────────────────────────────────────────────
 * Call tap() with the signal to record (post or pre chain per toggle).
 * Call process() to mix loop playback into the engine output bus.
 * Both take a mono block of n frames. */
void looper_tap(fx_looper_t *l, const float *in, int n);
void looper_process(fx_looper_t *l, float *out, int n);

/* ── Control (UI thread) ──────────────────────────────────────────── */

void looper_slot_tap(fx_looper_t *l, int slot);    /* advance state machine */
void looper_slot_mute(fx_looper_t *l, int slot);    /* toggle mute */
void looper_slot_clear(fx_looper_t *l, int slot);   /* back to EMPTY */
void looper_slot_undo(fx_looper_t *l, int slot);    /* revert last overdub */
int  looper_arm_next(fx_looper_t *l);               /* arms first EMPTY slot, returns index or -1 */
void looper_master_toggle(fx_looper_t *l);          /* Space */
void looper_focus_next(fx_looper_t *l);             /* Tab */
void looper_set_focus(fx_looper_t *l, int slot);    /* pad click, number key */

/* ── Export ───────────────────────────────────────────────────────── */

bool looper_export_slot_wav(fx_looper_t *l, int slot, const char *path);
bool looper_export_mix_wav(fx_looper_t *l, const char *path);

#endif /* FX_LOOPER_INTERNAL_H */
