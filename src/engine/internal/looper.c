/*
 * 0xFX — Looper module implementation
 *
 * State machine per slot:
 *   EMPTY ──tap──▶ ARMED ──(sync boundary or immediate)──▶ RECORDING
 *   RECORDING ──tap──▶ PLAYING        (closes the loop)
 *   PLAYING ──tap──▶ OVERDUBBING      (snapshot for undo)
 *   OVERDUBBING ──tap──▶ PLAYING
 *   Any state + clear ──▶ EMPTY
 *
 * Sync mode: the first slot to close from RECORDING defines the bar length.
 * Subsequent slots armed in sync mode begin recording on the next sync
 * boundary and wrap their length to match. In free mode, every slot
 * records immediately and uses its own length.
 */
#include "looper.h"
#include "engine_internal.h"
#include "../../core/log.h"

/* dr_wav implementation lives in cab_ir.c; we only need the API here. */
#include "dr_wav.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ── Lifecycle ────────────────────────────────────────────────── */

void looper_init(fx_looper_t *l, float sample_rate) {
    if (!l) return;
    memset(l, 0, sizeof(*l));
    l->sample_rate   = sample_rate > 0 ? sample_rate : 48000.0f;
    l->master_level  = 1.0f;
    l->master_playing = true;
    l->sync_mode     = false;
    l->tap_pre_chain = false;
    l->focused_slot  = 0;
    for (int i = 0; i < FX_LOOPER_SLOTS; i++) {
        l->slots[i].state = FX_LOOP_EMPTY;
        l->slots[i].max_frames =
            (int)(FX_LOOPER_MAX_LOOP_SECONDS * l->sample_rate);
    }
}

void looper_free(fx_looper_t *l) {
    if (!l) return;
    for (int i = 0; i < FX_LOOPER_SLOTS; i++) {
        free(l->slots[i].buf);
        free(l->slots[i].undo_buf);
        l->slots[i].buf = NULL;
        l->slots[i].undo_buf = NULL;
    }
}

/* Lazy alloc a slot's main buffer on first use. Returns true on success. */
static bool slot_ensure_buf(fx_loop_slot_t *s) {
    if (s->buf) return true;
    s->buf = (float *)calloc((size_t)s->max_frames, sizeof(float));
    if (!s->buf) {
        FX_WARN("Looper: failed to allocate %d frames for slot", s->max_frames);
        return false;
    }
    return true;
}

/* Lazy alloc a slot's undo buffer. */
static bool slot_ensure_undo(fx_loop_slot_t *s) {
    if (s->undo_buf) return true;
    s->undo_buf = (float *)calloc((size_t)s->max_frames, sizeof(float));
    return s->undo_buf != NULL;
}

static void slot_snapshot_for_undo(fx_loop_slot_t *s) {
    if (!s->buf || s->length <= 0) { s->has_undo = false; return; }
    if (!slot_ensure_undo(s)) { s->has_undo = false; return; }
    memcpy(s->undo_buf, s->buf, (size_t)s->length * sizeof(float));
    s->undo_length = s->length;
    s->undo_layers = s->layers;
    s->has_undo = true;
}

/* ── Per-block audio path ─────────────────────────────────────── */

/* Capture n frames of input into any slots that are RECORDING or OVERDUBBING.
 * Called once per engine block. */
void looper_tap(fx_looper_t *l, const float *in, int n) {
    if (!l || !in || n <= 0) return;

    for (int i = 0; i < FX_LOOPER_SLOTS; i++) {
        fx_loop_slot_t *s = &l->slots[i];
        if (s->state != FX_LOOP_RECORDING &&
            s->state != FX_LOOP_OVERDUBBING) continue;
        if (!slot_ensure_buf(s)) { s->state = FX_LOOP_EMPTY; continue; }

        if (s->state == FX_LOOP_RECORDING) {
            /* Append until we hit max_frames (truncate if user records > 120s) */
            int room = s->max_frames - s->length;
            int take = (n < room) ? n : room;
            memcpy(s->buf + s->length, in, (size_t)take * sizeof(float));
            s->length += take;
            if (take < n) {
                /* Buffer full — force close */
                s->state = FX_LOOP_PLAYING;
                s->play_pos = 0;
                FX_WARN("Looper: slot %d hit 120s cap, auto-closed", i);
            }
        } else {
            /* OVERDUBBING: sum input into the existing buffer at play_pos */
            if (s->length <= 0) continue;
            int pos = s->play_pos;
            for (int k = 0; k < n; k++) {
                s->buf[pos] += in[k];
                pos++;
                if (pos >= s->length) pos = 0;
            }
        }
    }
}

/* Advance playback and mix looping slots into out. */
void looper_process(fx_looper_t *l, float *out, int n) {
    if (!l || !out || n <= 0) return;
    /* We additively mix — caller pre-fills or zero-fills as appropriate.
     * The engine passes in a zero buffer for looper output. */
    memset(out, 0, (size_t)n * sizeof(float));

    if (!l->master_playing) return;

    float gain = l->master_level;
    if (gain <= 0.0f) return;

    /* Sync position advance once per block — tracks bar position when
     * sync mode is enabled. */
    if (l->sync_mode && l->sync_length > 0) {
        l->sync_pos = (l->sync_pos + n) % l->sync_length;
    }

    for (int i = 0; i < FX_LOOPER_SLOTS; i++) {
        fx_loop_slot_t *s = &l->slots[i];
        if (s->state != FX_LOOP_PLAYING &&
            s->state != FX_LOOP_OVERDUBBING) continue;
        if (s->muted) {
            /* Still advance play_pos so position tracking stays accurate */
            s->play_pos = (s->play_pos + n) % (s->length > 0 ? s->length : 1);
            continue;
        }
        if (!s->buf || s->length <= 0) continue;

        int pos = s->play_pos;
        for (int k = 0; k < n; k++) {
            out[k] += s->buf[pos] * gain;
            pos++;
            if (pos >= s->length) pos = 0;
        }
        s->play_pos = pos;
    }
}

/* ── State machine control ────────────────────────────────────── */

static bool valid_slot(int slot) {
    return slot >= 0 && slot < FX_LOOPER_SLOTS;
}

void looper_slot_tap(fx_looper_t *l, int slot) {
    if (!l || !valid_slot(slot)) return;
    fx_loop_slot_t *s = &l->slots[slot];

    switch (s->state) {
    case FX_LOOP_EMPTY:
        /* EMPTY → ARMED (sync) or directly RECORDING (free) */
        if (l->sync_mode && l->sync_length > 0) {
            s->state = FX_LOOP_ARMED;
        } else {
            s->state = FX_LOOP_RECORDING;
            s->length = 0;
            s->layers = 0;
        }
        break;
    case FX_LOOP_ARMED:
        /* Cancel arming */
        s->state = FX_LOOP_EMPTY;
        break;
    case FX_LOOP_RECORDING:
        /* RECORDING → PLAYING (close loop). In sync mode, first slot
         * to close sets the bar length; subsequent slots snap to it. */
        if (l->sync_mode) {
            if (l->sync_length <= 0) {
                l->sync_length = s->length;
                l->sync_pos    = 0;
            } else if (s->length != l->sync_length) {
                /* Snap length to sync_length — truncate if too long,
                 * zero-pad if too short */
                if (s->length > l->sync_length) {
                    s->length = l->sync_length;
                } else if (s->buf) {
                    memset(s->buf + s->length, 0,
                           (size_t)(l->sync_length - s->length) * sizeof(float));
                    s->length = l->sync_length;
                }
            }
        }
        s->state = FX_LOOP_PLAYING;
        s->play_pos = 0;
        break;
    case FX_LOOP_PLAYING:
        slot_snapshot_for_undo(s);
        s->state = FX_LOOP_OVERDUBBING;
        s->layers++;
        break;
    case FX_LOOP_OVERDUBBING:
        s->state = FX_LOOP_PLAYING;
        break;
    }
}

void looper_slot_mute(fx_looper_t *l, int slot) {
    if (!l || !valid_slot(slot)) return;
    l->slots[slot].muted = !l->slots[slot].muted;
}

void looper_slot_clear(fx_looper_t *l, int slot) {
    if (!l || !valid_slot(slot)) return;
    fx_loop_slot_t *s = &l->slots[slot];
    s->state      = FX_LOOP_EMPTY;
    s->length     = 0;
    s->play_pos   = 0;
    s->layers     = 0;
    s->has_undo   = false;
    s->undo_length = 0;
    s->muted      = false;
    /* Keep buf/undo_buf allocated for reuse — avoids realloc churn if
     * the user records into the same slot again. */

    /* If this was the slot that set sync_length and no other slot holds
     * audio, reset the bar. */
    if (l->sync_mode) {
        bool any_content = false;
        for (int i = 0; i < FX_LOOPER_SLOTS; i++) {
            if (l->slots[i].state != FX_LOOP_EMPTY && l->slots[i].length > 0) {
                any_content = true; break;
            }
        }
        if (!any_content) {
            l->sync_length = 0;
            l->sync_pos = 0;
        }
    }
}

void looper_slot_undo(fx_looper_t *l, int slot) {
    if (!l || !valid_slot(slot)) return;
    fx_loop_slot_t *s = &l->slots[slot];
    if (!s->has_undo || !s->undo_buf || s->undo_length <= 0) return;
    if (!s->buf) return;
    memcpy(s->buf, s->undo_buf, (size_t)s->undo_length * sizeof(float));
    s->length = s->undo_length;
    s->layers = s->undo_layers;
    s->has_undo = false;
    if (s->play_pos >= s->length) s->play_pos = 0;
}

int looper_arm_next(fx_looper_t *l) {
    if (!l) return -1;
    for (int i = 0; i < FX_LOOPER_SLOTS; i++) {
        if (l->slots[i].state == FX_LOOP_EMPTY) {
            looper_slot_tap(l, i);
            return i;
        }
    }
    return -1;
}

void looper_master_toggle(fx_looper_t *l) {
    if (!l) return;
    l->master_playing = !l->master_playing;
}

void looper_focus_next(fx_looper_t *l) {
    if (!l) return;
    /* Advance to the next non-empty slot; wrap around. */
    for (int tries = 0; tries < FX_LOOPER_SLOTS; tries++) {
        l->focused_slot = (l->focused_slot + 1) % FX_LOOPER_SLOTS;
        if (l->slots[l->focused_slot].state != FX_LOOP_EMPTY) return;
    }
    /* All empty — leave focus at 0 */
    l->focused_slot = 0;
}

/* ── WAV export ───────────────────────────────────────────────── */

static bool write_wav_mono(const char *path, const float *samples, int n,
                           float sample_rate) {
    if (!path || !samples || n <= 0) return false;
    drwav_data_format fmt;
    fmt.container     = drwav_container_riff;
    fmt.format        = DR_WAVE_FORMAT_IEEE_FLOAT;
    fmt.channels      = 1;
    fmt.sampleRate    = (drwav_uint32)sample_rate;
    fmt.bitsPerSample = 32;

    drwav wav;
    if (!drwav_init_file_write(&wav, path, &fmt, NULL)) return false;
    drwav_uint64 wrote = drwav_write_pcm_frames(&wav, (drwav_uint64)n, samples);
    drwav_uninit(&wav);
    return wrote == (drwav_uint64)n;
}

bool looper_export_slot_wav(fx_looper_t *l, int slot, const char *path) {
    if (!l || !valid_slot(slot) || !path) return false;
    fx_loop_slot_t *s = &l->slots[slot];
    if (!s->buf || s->length <= 0) {
        FX_WARN("Looper export: slot %d is empty", slot);
        return false;
    }
    return write_wav_mono(path, s->buf, s->length, l->sample_rate);
}

bool looper_export_mix_wav(fx_looper_t *l, const char *path) {
    if (!l || !path) return false;

    /* Find the longest slot — that's our mix length */
    int mix_len = 0;
    for (int i = 0; i < FX_LOOPER_SLOTS; i++) {
        if (l->slots[i].buf && l->slots[i].length > mix_len)
            mix_len = l->slots[i].length;
    }
    if (mix_len <= 0) {
        FX_WARN("Looper export: no content to mix");
        return false;
    }

    float *mix = (float *)calloc((size_t)mix_len, sizeof(float));
    if (!mix) return false;

    for (int i = 0; i < FX_LOOPER_SLOTS; i++) {
        fx_loop_slot_t *s = &l->slots[i];
        if (!s->buf || s->length <= 0 || s->muted) continue;
        for (int k = 0; k < mix_len; k++) {
            mix[k] += s->buf[k % s->length];
        }
    }

    /* Soft-clip to avoid export peaks > 1.0 */
    float peak = 0.0f;
    for (int k = 0; k < mix_len; k++) {
        float a = fabsf(mix[k]);
        if (a > peak) peak = a;
    }
    if (peak > 1.0f) {
        float g = 1.0f / peak;
        for (int k = 0; k < mix_len; k++) mix[k] *= g;
    }

    bool ok = write_wav_mono(path, mix, mix_len, l->sample_rate);
    free(mix);
    return ok;
}
