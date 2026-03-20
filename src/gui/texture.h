#ifndef FX_TEXTURE_H
#define FX_TEXTURE_H
#include <stdbool.h>
#include <stdint.h>

/* Load a PNG file as an OpenGL texture. Returns the GL texture ID (cast to
 * uintptr_t for portability). Caches by path — second call with same path
 * returns the cached ID. Returns 0 on failure. */
uintptr_t fx_texture_load(const char *path);

/* Query the pixel dimensions of a previously-loaded texture.
 * Returns false if the texture was never loaded via fx_texture_load. */
bool fx_texture_get_size(uintptr_t gl_id, int *out_w, int *out_h);

/* Free all cached textures (call at shutdown) */
void fx_texture_shutdown(void);

#endif
