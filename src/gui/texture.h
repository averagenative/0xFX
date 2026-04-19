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

/* Clear the cache without deleting GL textures (for multi-instance plugins
 * where each instance has its own GL context) */
void fx_texture_cache_clear(void);

/* Load a PNG from embedded assets (or disk fallback) as raw RGBA pixels.
 * Returns malloc'd buffer of w*h*4 bytes, caller must free via
 * fx_image_free_pixels(). NULL on failure. Used for window-icon wiring
 * so we can build platform HICON / _NET_WM_ICON / SDL_Surface without
 * each frontend reaching into stb_image directly. */
unsigned char *fx_image_load_rgba(const char *path, int *out_w, int *out_h);
void           fx_image_free_pixels(unsigned char *pixels);

#endif
