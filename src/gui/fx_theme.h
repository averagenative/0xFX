#ifndef FX_THEME_H
#define FX_THEME_H

#include "imgui.h"

/* Theme identifiers. Keep THEME_COUNT last. */
typedef enum {
    FX_THEME_GRIME_DARK = 0,
    FX_THEME_HACKER_GREEN,
    FX_THEME_CYBERPUNK_NEON,
    FX_THEME_STUDIO_PALE,
    FX_THEME_BLUEPRINT,
    FX_THEME_NATURE_DIRT,
    FX_THEME_VAPORWAVE,
    FX_THEME_TOXIC_SLIME,
    FX_THEME_BLOOD_MOON,
    FX_THEME_AMBER_CRT,
    FX_THEME_MOLTEN_FORGE,
    FX_THEME_COUNT
} fx_theme_id_t;

/* Palette. All colors are sRGB ImVec4 (alpha meaningful for overlays only). */
typedef struct {
    const char *name;
    const char *description;

    /* Core ImGui backgrounds / frames */
    ImVec4 bg, panel, popup;
    ImVec4 frame, frame_hover, frame_active;
    ImVec4 title_bg, title_bg_active, menu_bar_bg;
    ImVec4 header, header_hover, header_active;

    /* Text */
    ImVec4 text, text_dim;

    /* Primary accent (LEDs, knobs, buttons) */
    ImVec4 accent, accent_hover, accent_active, accent_glow;

    /* Borders / separators */
    ImVec4 border, separator;

    /* Corner dirt — vignette overlay in the four corners of the main canvas.
     * `dirt_inner` is opaque at the corner itself; fades to `dirt_outer`
     * (typically 0-alpha) at `dirt_radius` pixels into the viewport. */
    ImVec4 dirt_inner;
    ImVec4 dirt_outer;
    float  dirt_radius;     /* pixels — how far the vignette reaches */
} fx_theme_t;

/* Palette lookup — returns a pointer to a static table entry. */
const fx_theme_t *fx_theme_get(fx_theme_id_t id);
const char       *fx_theme_name(fx_theme_id_t id);

/* Apply palette to a style object. Writes only the color slots the palette
 * defines — shape/padding settings are left alone. */
void fx_theme_apply(ImGuiStyle &style, fx_theme_id_t id);

/* Draw the 4 corner-dirt vignettes on the given ImDrawList. Intended to be
 * called on the background drawlist (GetBackgroundDrawList) after the main
 * window clears, before widget rendering. */
void fx_theme_draw_corner_dirt(ImDrawList *dl,
                                float x, float y, float w, float h,
                                fx_theme_id_t id);

#endif
