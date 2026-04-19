/*
 * custom_cabs.h — Shared user-library of custom cabinet IRs.
 *
 * Persists to `~/.0xfx/custom_cabs.json` (or `%APPDATA%\0xFX` on Windows).
 * Both the standalone and plugin targets read/write this library. All
 * library mutations are serialized with an internal mutex so multiple
 * plugin instances can safely add/remove entries concurrently.
 */
#ifndef FX_CUSTOM_CABS_H
#define FX_CUSTOM_CABS_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FX_MAX_CUSTOM_CABS 512

typedef struct {
    char ir_path[1024];
    char name[64];
    char image_path[1024];
} fx_custom_cab_t;

/* Lazy-load the library from disk on first call. Safe to call many times. */
void fx_custom_cabs_load(void);

/* Persist current in-memory library to disk. */
void fx_custom_cabs_save(void);

/* Snapshot the library into a caller-provided buffer. Returns the number
 * of entries copied (<= max_entries). Takes the library mutex for the
 * duration of the copy, so the snapshot is internally consistent. */
int  fx_custom_cabs_snapshot(fx_custom_cab_t *out, int max_entries);

/* Returns index of the entry with matching ir_path, or -1 if not found. */
int  fx_custom_cabs_find(const char *ir_path);

/* Adds a new entry if not already present. Returns the entry index (old
 * or new), or -1 on capacity-full. Does NOT persist; call save() after. */
int  fx_custom_cabs_add(const char *ir_path);

/* Removes entry at idx (shifts the array). */
void fx_custom_cabs_remove_by_index(int idx);

/* Convenience: look up by ir_path and remove. No-op if not found. */
void fx_custom_cabs_remove_by_path(const char *ir_path);

/* Update the display name / image path for the entry matching ir_path.
 * Returns true if the entry was found. Does NOT persist; call save(). */
bool fx_custom_cabs_set_name(const char *ir_path, const char *name);
bool fx_custom_cabs_set_image(const char *ir_path, const char *image_path);

#ifdef __cplusplus
}
#endif

#endif /* FX_CUSTOM_CABS_H */
