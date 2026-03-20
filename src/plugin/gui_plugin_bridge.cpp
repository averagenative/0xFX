/*
 * gui_plugin_bridge.cpp — C++ bridge for plugin GUI
 *
 * Provides thin C-linkage wrappers around ImGui/SDL2/OpenGL initialization
 * and per-frame rendering so that plugin.c (compiled as C) can call them.
 *
 * The actual rendering is done by fx_gui_render_frame() from gui_render.h.
 */
#include <SDL.h>
#include <SDL_opengl.h>

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"

extern "C" {
#include "../gui/gui_render.h"
}

/* ── Theme setup (re-exported for plugin init) ─────────────── */

extern "C" void oxfx_plugin_gui_setup_theme(void) {
    /* Forward to the theme setup in gui_render.cpp */
    extern void fx_gui_setup_theme(void);
    fx_gui_setup_theme();
}

/* ── ImGui init / shutdown wrappers ────────────────────────── */

extern "C" void oxfx_plugin_gui_init_imgui(void *sdl_window, void *gl_ctx) {
    SDL_Window *window = (SDL_Window *)sdl_window;
    SDL_GLContext ctx = (SDL_GLContext)gl_ctx;

    SDL_GL_MakeCurrent(window, ctx);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    oxfx_plugin_gui_setup_theme();

    ImGui_ImplSDL2_InitForOpenGL(window, ctx);
    ImGui_ImplOpenGL3_Init("#version 330");
}

extern "C" void oxfx_plugin_gui_shutdown_imgui(void) {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
}

extern "C" void oxfx_plugin_gui_new_frame(void) {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
}

extern "C" void oxfx_plugin_gui_render(void) {
    ImGui::Render();
    ImGuiIO &io = ImGui::GetIO();
    glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
    glClearColor(0.06f, 0.05f, 0.04f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}
