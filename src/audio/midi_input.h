/*
 * 0xFX — MIDI input manager (standalone only)
 *
 * Provides MIDI CC control of engine parameters and MIDI learn.
 * CC messages only — no notes, no sysex.
 */
#ifndef FX_MIDI_INPUT_H
#define FX_MIDI_INPUT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize MIDI subsystem, enumerate devices */
void fx_midi_init(void);
void fx_midi_shutdown(void);

/* Device enumeration */
int fx_midi_get_device_count(void);
const char *fx_midi_get_device_name(int index);

/* Open/close a MIDI input device */
bool fx_midi_open(int device_index);
void fx_midi_close(void);
bool fx_midi_is_open(void);

/* MIDI CC callback — fires from OS thread, keep it fast */
typedef void (*fx_midi_cc_callback_t)(int channel, int cc, int value,
                                      void *userdata);
void fx_midi_set_callback(fx_midi_cc_callback_t cb, void *userdata);

/* MIDI learn: set a target param, next CC received maps to it */
void fx_midi_learn_start(int param_target_id);
void fx_midi_learn_cancel(void);
bool fx_midi_learn_active(void);
int  fx_midi_learn_target(void);

/* CC mapping table (128 entries, one per CC number) */
void fx_midi_map_cc(int cc_number, int param_target_id);
void fx_midi_unmap_cc(int cc_number);
int  fx_midi_get_mapped_param(int cc_number);

#ifdef __cplusplus
}
#endif

#endif /* FX_MIDI_INPUT_H */
