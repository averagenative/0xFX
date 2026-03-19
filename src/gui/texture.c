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
#else
#include <GL/gl.h>
#endif

#include "texture.h"
#include "../core/log.h"

#include <string.h>
#include <stdlib.h>

/* ── Cache ─────────────────────────────────────────────────────── */

#define MAX_TEXTURES 128

typedef struct {
    char      path[512];
    uintptr_t gl_id;
} TexEntry;

static TexEntry s_cache[MAX_TEXTURES];
static int      s_count = 0;

/* ── Public API ─────────────────────────────────────────────────── */

uintptr_t fx_texture_load(const char *path)
{
    if (!path) return 0;

    /* Check cache first */
    for (int i = 0; i < s_count; i++) {
        if (strncmp(s_cache[i].path, path, sizeof(s_cache[i].path) - 1) == 0) {
            return s_cache[i].gl_id;
        }
    }

    if (s_count >= MAX_TEXTURES) {
        FX_WARN("fx_texture_load: cache full (%d entries), cannot load %s",
                MAX_TEXTURES, path);
        return 0;
    }

    /* Load image with stb_image */
    int w, h, channels;
    unsigned char *data = stbi_load(path, &w, &h, &channels, 4); /* force RGBA */
    if (!data) {
        FX_WARN("fx_texture_load: failed to load '%s': %s", path, stbi_failure_reason());
        return 0;
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

    stbi_image_free(data);
    glBindTexture(GL_TEXTURE_2D, 0);

    /* Store in cache */
    TexEntry *e = &s_cache[s_count++];
    strncpy(e->path, path, sizeof(e->path) - 1);
    e->path[sizeof(e->path) - 1] = '\0';
    e->gl_id = (uintptr_t)tex_id;

    FX_INFO("fx_texture_load: loaded '%s' (%dx%d) -> GL id %u", path, w, h, tex_id);
    return e->gl_id;
}

void fx_texture_shutdown(void)
{
    for (int i = 0; i < s_count; i++) {
        GLuint id = (GLuint)s_cache[i].gl_id;
        glDeleteTextures(1, &id);
    }
    s_count = 0;
}
