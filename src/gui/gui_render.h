/*
 * gui_render.h — Shared GUI rendering for standalone + plugin
 *
 * Extracts the per-frame ImGui rendering (toolbar, signal chain, detail
 * view, status bar) into callable functions that both the standalone
 * gui_main.cpp and the CPLUG plugin GUI can invoke.
 */
#ifndef FX_GUI_RENDER_H
#define FX_GUI_RENDER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "../engine/fx_engine.h"
#include <stdbool.h>

/* Opaque GUI state handle */
typedef struct fx_gui_state fx_gui_state_t;

/* Create / destroy GUI rendering state bound to an engine instance.
 * The engine pointer is NOT owned — caller must keep it alive. */
fx_gui_state_t *fx_gui_create(fx_engine_t *engine);
void            fx_gui_destroy(fx_gui_state_t *gui);

/* Render one ImGui frame. Call between NewFrame() and Render().
 *   win_w, win_h: display dimensions
 *   is_plugin:    true = skip standalone-only controls (LIVE, Settings,
 *                 window chrome, keyboard shortcuts that close the app) */
void fx_gui_render_frame(fx_gui_state_t *gui, float win_w, float win_h,
                         bool is_plugin);

/* After loading a preset into the engine, call this to sync the GUI's
 * internal pedal/studio ID caches with the engine state. */
void fx_gui_sync_from_engine(fx_gui_state_t *gui);

/* Apply the "worn grime" dark theme to ImGui. Call once after ImGui
 * context creation. */
void fx_gui_setup_theme(void);

#ifdef __cplusplus
}
#endif

#endif /* FX_GUI_RENDER_H */
