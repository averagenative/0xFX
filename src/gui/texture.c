/*
 * 0xFX — OpenGL texture loader
 *
 * Loads PNG files as OpenGL textures using stb_image.
 * Caches by path; second call with the same path returns the cached ID.
 */

#define STB_IMAGE_IMPLEMENTATION
#include "../../deps/stb_image.h"

#ifdef _WIN32
#include <windows.h>
#include <GL/gl.h>
/* GL_CLAMP_TO_EDGE is GL 1.2+ — not in Windows' ancient gl.h */
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#elif defined(__APPLE__)
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include "texture.h"
#include "embedded_assets.h"
#include "../core/log.h"

#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

/* ── Cache ─────────────────────────────────────────────────────── */

#define MAX_TEXTURES 512  /* larger to support multiple plugin instances */

/* Cap user-supplied images to this max dimension on either axis. The cab
 * image renders at ~220px, pedal/amp at ~400px, so 1024 is plenty. Anything
 * larger is downscaled with a box filter before GPU upload — saves VRAM,
 * avoids GL_MAX_TEXTURE_SIZE issues on low-end GPUs, and prevents aliasing
 * from no-mipmap downsampling. */
#define FX_TEXTURE_MAX_DIM 1024

/* Box-filter downscale from src (sw,sh) to dst (dw,dh), RGBA8. Averages the
 * block of source pixels that map to each destination pixel. */
static void tex_box_downscale(const unsigned char *src, int sw, int sh,
                              unsigned char *dst, int dw, int dh)
{
    for (int y = 0; y < dh; y++) {
        int y0 = (int)((long long)y * sh / dh);
        int y1 = (int)((long long)(y + 1) * sh / dh);
        if (y1 <= y0) y1 = y0 + 1;
        for (int x = 0; x < dw; x++) {
            int x0 = (int)((long long)x * sw / dw);
            int x1 = (int)((long long)(x + 1) * sw / dw);
            if (x1 <= x0) x1 = x0 + 1;
            unsigned int r = 0, g = 0, b = 0, a = 0, n = 0;
            for (int yy = y0; yy < y1; yy++) {
                const unsigned char *row = src + (yy * sw + x0) * 4;
                for (int xx = x0; xx < x1; xx++) {
                    r += row[0]; g += row[1]; b += row[2]; a += row[3];
                    row += 4; n++;
                }
            }
            unsigned char *d = dst + (y * dw + x) * 4;
            d[0] = (unsigned char)(r / n);
            d[1] = (unsigned char)(g / n);
            d[2] = (unsigned char)(b / n);
            d[3] = (unsigned char)(a / n);
        }
    }
}

typedef struct {
    char      path[512];
    uintptr_t gl_id;
    int       width;
    int       height;
    uintptr_t owner_thread;  /* thread ID that created this entry */
} TexEntry;

static TexEntry s_cache[MAX_TEXTURES];
static int      s_count = 0;

#ifdef _WIN32
#include <windows.h>
static CRITICAL_SECTION s_tex_cs;
static bool s_tex_cs_init = false;
static void tex_lock(void) {
    if (!s_tex_cs_init) { InitializeCriticalSection(&s_tex_cs); s_tex_cs_init = true; }
    EnterCriticalSection(&s_tex_cs);
}
static void tex_unlock(void) { LeaveCriticalSection(&s_tex_cs); }
static uintptr_t tex_thread_id(void) { return (uintptr_t)GetCurrentThreadId(); }
#else
#include <pthread.h>
static pthread_mutex_t s_tex_mutex = PTHREAD_MUTEX_INITIALIZER;
static void tex_lock(void) { pthread_mutex_lock(&s_tex_mutex); }
static void tex_unlock(void) { pthread_mutex_unlock(&s_tex_mutex); }
static uintptr_t tex_thread_id(void) { return (uintptr_t)pthread_self(); }
#endif

/* ── Public API ─────────────────────────────────────────────────── */

uintptr_t fx_texture_load(const char *path)
{
    if (!path) return 0;

    uintptr_t tid = tex_thread_id();

    tex_lock();

    /* Check cache — only return hits created by the same thread (same GL context) */
    for (int i = 0; i < s_count; i++) {
        if (s_cache[i].owner_thread == tid &&
            strncmp(s_cache[i].path, path, sizeof(s_cache[i].path) - 1) == 0) {
            uintptr_t id = s_cache[i].gl_id;
            tex_unlock();
            return id;
        }
    }

    if (s_count >= MAX_TEXTURES) {
        FX_WARN("fx_texture_load: cache full (%d entries), cannot load %s",
                MAX_TEXTURES, path);
        tex_unlock();
        return 0;
    }

    /* Try embedded assets first (for plugin builds where no filesystem) */
    int w, h, channels;
    unsigned char *data = NULL;
    const fx_embedded_asset_t *embedded = fx_embedded_find(path);
    if (embedded) {
        data = stbi_load_from_memory(embedded->data, (int)embedded->size, &w, &h, &channels, 4);
    }
    /* Fall back to disk */
    if (!data) {
        data = stbi_load(path, &w, &h, &channels, 4);
    }
    if (!data) {
        tex_unlock();
        /* Don't cache failures */
        static char s_warned[32][512];
        static int s_warn_count = 0;
        bool already_warned = false;
        for (int wi = 0; wi < s_warn_count; wi++) {
            if (strncmp(s_warned[wi], path, 511) == 0) { already_warned = true; break; }
        }
        if (!already_warned && s_warn_count < 32) {
            strncpy(s_warned[s_warn_count], path, 511);
            s_warned[s_warn_count][511] = '\0';
            s_warn_count++;
            FX_WARN("fx_texture_load: failed to load '%s': %s", path, stbi_failure_reason());
        }
        return 0;
    }

    /* Downscale oversized images before GPU upload. stb_image returns the
     * full source resolution; we swap in a box-filtered copy if either axis
     * exceeds FX_TEXTURE_MAX_DIM. */
    int orig_w = w, orig_h = h;
    int max_dim = (w > h) ? w : h;
    if (max_dim > FX_TEXTURE_MAX_DIM) {
        float scale = (float)FX_TEXTURE_MAX_DIM / (float)max_dim;
        int nw = (int)(w * scale);
        int nh = (int)(h * scale);
        if (nw < 1) nw = 1;
        if (nh < 1) nh = 1;
        unsigned char *resized = (unsigned char *)malloc((size_t)nw * nh * 4);
        if (resized) {
            tex_box_downscale(data, w, h, resized, nw, nh);
            stbi_image_free(data);
            data = resized;
            w = nw;
            h = nh;
        }
        /* If malloc failed, upload original — GL may reject it, that's fine. */
    }

    /* Upload to OpenGL */
    GLuint tex_id = 0;
    glGenTextures(1, &tex_id);
    glBindTexture(GL_TEXTURE_2D, tex_id);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, data);

    /* The downscaled buffer was allocated with malloc; stb_image buffers with
     * stbi_image_free. Both are free() under the hood in stb_image's default
     * config, so either call is fine — use stbi_image_free for consistency. */
    if (w == orig_w && h == orig_h) {
        stbi_image_free(data);
    } else {
        free(data);
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    /* Store in cache with owner thread */
    TexEntry *e = &s_cache[s_count++];
    strncpy(e->path, path, sizeof(e->path) - 1);
    e->path[sizeof(e->path) - 1] = '\0';
    e->gl_id = (uintptr_t)tex_id;
    e->width  = w;
    e->height = h;
    e->owner_thread = tid;

    tex_unlock();

    if (w != orig_w || h != orig_h) {
        FX_INFO("fx_texture_load: loaded '%s' (%dx%d, downscaled from %dx%d) -> GL id %u",
                path, w, h, orig_w, orig_h, tex_id);
    } else {
        FX_INFO("fx_texture_load: loaded '%s' (%dx%d) -> GL id %u", path, w, h, tex_id);
    }
    return e->gl_id;
}

bool fx_texture_get_size(uintptr_t gl_id, int *out_w, int *out_h)
{
    uintptr_t tid = tex_thread_id();
    for (int i = 0; i < s_count; i++) {
        if (s_cache[i].gl_id == gl_id && s_cache[i].owner_thread == tid) {
            if (out_w) *out_w = s_cache[i].width;
            if (out_h) *out_h = s_cache[i].height;
            return true;
        }
    }
    return false;
}

void fx_texture_shutdown(void)
{
    for (int i = 0; i < s_count; i++) {
        GLuint id = (GLuint)s_cache[i].gl_id;
        glDeleteTextures(1, &id);
    }
    s_count = 0;
}

void fx_texture_cache_clear(void)
{
    /* Reset cache without deleting GL textures — for multi-instance plugins
     * where GL IDs belong to a specific context that may already be destroyed
     * or belong to a different instance */
    s_count = 0;
}

unsigned char *fx_image_load_rgba(const char *path, int *out_w, int *out_h)
{
    if (!path || !out_w || !out_h) return NULL;
    int w = 0, h = 0, channels = 0;
    unsigned char *data = NULL;
    const fx_embedded_asset_t *embedded = fx_embedded_find(path);
    if (embedded) {
        data = stbi_load_from_memory(embedded->data, (int)embedded->size,
                                     &w, &h, &channels, 4);
    }
    if (!data) {
        data = stbi_load(path, &w, &h, &channels, 4);
    }
    if (!data) return NULL;
    *out_w = w;
    *out_h = h;
    return data;
}

void fx_image_free_pixels(unsigned char *pixels)
{
    if (pixels) stbi_image_free(pixels);
}
