/*
 * 0xFX — GUI application entry point
 *
 * SDL2 + OpenGL 3.3 + Dear ImGui
 * Phase 3B: basic window with amp panel and pedalboard placeholders
 */
#include <SDL.h>
#include <SDL_opengl.h>

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"

extern "C" {
#include "../engine/fx_engine.h"
#include "../audio/audio_device.h"
#include "knobs.h"
}

#include <stdio.h>

/* ── Colors — "worn grime" dark theme ─────────────────────────── */

static void setup_theme(void) {
    ImGuiStyle &style = ImGui::GetStyle();
    ImVec4 *colors = style.Colors;

    /* Dark base with warm undertones */
    colors[ImGuiCol_WindowBg]         = ImVec4(0.08f, 0.07f, 0.06f, 1.0f);
    colors[ImGuiCol_ChildBg]          = ImVec4(0.10f, 0.09f, 0.08f, 1.0f);
    colors[ImGuiCol_PopupBg]          = ImVec4(0.12f, 0.10f, 0.09f, 0.95f);
    colors[ImGuiCol_Border]           = ImVec4(0.25f, 0.22f, 0.18f, 0.5f);
    colors[ImGuiCol_FrameBg]          = ImVec4(0.14f, 0.12f, 0.10f, 1.0f);
    colors[ImGuiCol_FrameBgHovered]   = ImVec4(0.20f, 0.17f, 0.14f, 1.0f);
    colors[ImGuiCol_FrameBgActive]    = ImVec4(0.25f, 0.20f, 0.15f, 1.0f);
    colors[ImGuiCol_TitleBg]          = ImVec4(0.06f, 0.05f, 0.04f, 1.0f);
    colors[ImGuiCol_TitleBgActive]    = ImVec4(0.12f, 0.10f, 0.08f, 1.0f);
    colors[ImGuiCol_MenuBarBg]        = ImVec4(0.10f, 0.08f, 0.07f, 1.0f);
    colors[ImGuiCol_Header]           = ImVec4(0.18f, 0.15f, 0.12f, 1.0f);
    colors[ImGuiCol_HeaderHovered]    = ImVec4(0.30f, 0.24f, 0.18f, 1.0f);
    colors[ImGuiCol_HeaderActive]     = ImVec4(0.40f, 0.30f, 0.20f, 1.0f);
    /* Amber accent (LED / active indicator) */
    colors[ImGuiCol_Button]           = ImVec4(0.20f, 0.16f, 0.12f, 1.0f);
    colors[ImGuiCol_ButtonHovered]    = ImVec4(0.60f, 0.40f, 0.15f, 1.0f);
    colors[ImGuiCol_ButtonActive]     = ImVec4(0.80f, 0.55f, 0.15f, 1.0f);
    colors[ImGuiCol_CheckMark]        = ImVec4(0.90f, 0.65f, 0.20f, 1.0f);
    colors[ImGuiCol_SliderGrab]       = ImVec4(0.70f, 0.50f, 0.20f, 1.0f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.90f, 0.65f, 0.20f, 1.0f);
    colors[ImGuiCol_Text]            = ImVec4(0.85f, 0.80f, 0.72f, 1.0f);
    colors[ImGuiCol_TextDisabled]    = ImVec4(0.45f, 0.40f, 0.35f, 1.0f);
    colors[ImGuiCol_Tab]             = ImVec4(0.14f, 0.12f, 0.10f, 1.0f);
    colors[ImGuiCol_TabHovered]      = ImVec4(0.35f, 0.28f, 0.18f, 1.0f);
    colors[ImGuiCol_TabActive]       = ImVec4(0.25f, 0.20f, 0.14f, 1.0f);
    colors[ImGuiCol_Separator]       = ImVec4(0.25f, 0.22f, 0.18f, 0.5f);

    /* Rounded corners for a hardware feel */
    style.WindowRounding    = 4.0f;
    style.FrameRounding     = 3.0f;
    style.GrabRounding      = 3.0f;
    style.TabRounding       = 3.0f;
    style.WindowPadding     = ImVec2(10, 10);
    style.FramePadding      = ImVec2(6, 4);
    style.ItemSpacing       = ImVec2(8, 6);
}

/* ── Main ─────────────────────────────────────────────────────── */

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;

    /* SDL init */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init error: %s\n", SDL_GetError());
        return 1;
    }

    /* OpenGL 3.3 */
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    SDL_Window *window = SDL_CreateWindow(
        "0xFX — Guitar Amp Sim & Pedalboard",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1400, 800,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
    );
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1); /* vsync */

    /* ImGui init */
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    setup_theme();

    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 330");

    /* Audio + engine init */
    fx_audio_init();
    fx_engine_t *engine = fx_engine_create(44100.0f);

    /* Select first audio device if available */
    int num_devices = fx_audio_get_device_count();
    if (num_devices > 0) {
        fx_audio_set_device(engine, 0);
    }

    /* ── Main loop ────────────────────────────────────────────── */
    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_WINDOWEVENT &&
                event.window.event == SDL_WINDOWEVENT_CLOSE &&
                event.window.windowID == SDL_GetWindowID(window))
                running = false;
        }

        /* Start ImGui frame */
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        /* ── Toolbar ──────────────────────────────────────────── */
        {
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, 50));
            ImGui::Begin("##toolbar", NULL,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);

            ImGui::Text("0xFX");
            ImGui::SameLine(100);

            /* Tuner */
            {
                float freq = fx_tuner_get_frequency(engine);
                bool active = (freq > 20.0f);
                const char *note = active ? fx_tuner_get_note_name(engine) : "--";
                float cents = active ? fx_tuner_get_cents(engine) : 0.0f;

                /* Note name color: green if |cents|<5, yellow if <15, red otherwise */
                ImVec4 note_color;
                if (!active) {
                    note_color = ImVec4(0.45f, 0.40f, 0.35f, 1.0f); /* disabled gray */
                } else if (cents < 0.0f ? -cents < 5.0f : cents < 5.0f) {
                    note_color = ImVec4(0.20f, 0.90f, 0.30f, 1.0f); /* green */
                } else if (cents < 0.0f ? -cents < 15.0f : cents < 15.0f) {
                    note_color = ImVec4(0.95f, 0.85f, 0.10f, 1.0f); /* yellow */
                } else {
                    note_color = ImVec4(0.95f, 0.25f, 0.20f, 1.0f); /* red */
                }

                /* Note name in large text */
                ImGui::PushStyleColor(ImGuiCol_Text, note_color);
                ImGui::SetWindowFontScale(1.4f);
                ImGui::Text("%s", note);
                ImGui::SetWindowFontScale(1.0f);
                ImGui::PopStyleColor();

                ImGui::SameLine();

                /* Cents bar — drawn with the window draw list */
                {
                    const float bar_w    = 200.0f;
                    const float bar_h    = 10.0f;
                    const float dot_r    = 6.0f;
                    const float padding  = dot_r; /* room for dot to extend past bar edge */

                    /* Reserve a region tall enough for bar + dot */
                    ImVec2 cursor = ImGui::GetCursorScreenPos();
                    /* Vertically centre the bar+dot in the toolbar (toolbar h=50) */
                    float toolbar_top = ImGui::GetWindowPos().y;
                    float bar_cx_y   = toolbar_top + 25.0f; /* mid-point of toolbar */
                    float bar_top_y  = bar_cx_y - bar_h * 0.5f;
                    float bar_bot_y  = bar_cx_y + bar_h * 0.5f;

                    float bar_x0 = cursor.x + padding;
                    float bar_x1 = bar_x0 + bar_w;

                    ImDrawList *dl = ImGui::GetWindowDrawList();

                    /* Background track */
                    dl->AddRectFilled(
                        ImVec2(bar_x0, bar_top_y),
                        ImVec2(bar_x1, bar_bot_y),
                        IM_COL32(50, 45, 40, 255), 3.0f
                    );

                    /* Centre tick mark */
                    float mid_x = bar_x0 + bar_w * 0.5f;
                    dl->AddRectFilled(
                        ImVec2(mid_x - 1.0f, bar_top_y - 2.0f),
                        ImVec2(mid_x + 1.0f, bar_bot_y + 2.0f),
                        IM_COL32(120, 110, 90, 255)
                    );

                    /* Indicator dot */
                    if (active) {
                        /* Map cents [-50, +50] → [bar_x0, bar_x1] */
                        float t        = (cents + 50.0f) / 100.0f;
                        if (t < 0.0f) t = 0.0f;
                        if (t > 1.0f) t = 1.0f;
                        float dot_x = bar_x0 + t * bar_w;

                        ImU32 dot_col = IM_COL32(
                            (int)(note_color.x * 255),
                            (int)(note_color.y * 255),
                            (int)(note_color.z * 255),
                            255
                        );
                        dl->AddCircleFilled(ImVec2(dot_x, bar_cx_y), dot_r, dot_col);
                    }

                    /* Advance cursor past the bar area */
                    ImGui::Dummy(ImVec2(bar_w + padding * 2.0f, bar_h + dot_r * 2.0f));
                }
            }

            ImGui::SameLine(400);

            /* Amp selector */
            static const char *amp_names[] = {
                "Fullerton Clean", "British Crunch", "Southwest Lead",
                "Essex Chime", "Tweed Blues"
            };
            int current_amp = (int)fx_amp_get_model(engine, FX_CHAIN_DEFAULT);
            ImGui::SetNextItemWidth(180);
            if (ImGui::Combo("Amp", &current_amp, amp_names, FX_AMP_COUNT)) {
                fx_amp_set_model(engine, FX_CHAIN_DEFAULT, (fx_amp_type_t)current_amp);
            }

            ImGui::SameLine(650);

            /* Audio device */
            if (num_devices > 0) {
                ImGui::TextDisabled("Device: %s", fx_audio_get_device_name(0));
            } else {
                ImGui::TextDisabled("No audio device");
            }

            ImGui::End();
        }

        /* ── Amp panel ────────────────────────────────────────── */
        {
            ImGui::SetNextWindowPos(ImVec2(10, 60));
            ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x - 20, 200));
            ImGui::Begin("Amp", NULL,
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);

            fx_amp_type_t amp_type = fx_amp_get_model(engine, FX_CHAIN_DEFAULT);
            int param_count = fx_amp_get_param_count(amp_type);

            ImGui::Text("%s", fx_amp_get_type_name(amp_type));
            ImGui::Separator();

            /* Rotary knobs for each amp param */
            for (int p = 0; p < param_count; p++) {
                const char *name = fx_amp_get_param_name(amp_type, (fx_amp_param_t)p);
                float val = fx_amp_get_param(engine, FX_CHAIN_DEFAULT, (fx_amp_param_t)p);
                if (knob_float(name, &val, 0.0f, 1.0f, 0.5f, 0.01f)) {
                    fx_amp_set_param(engine, FX_CHAIN_DEFAULT, (fx_amp_param_t)p, val);
                }
                if (p < param_count - 1) ImGui::SameLine();
            }

            ImGui::End();
        }

        /* ── Pedalboard (placeholder) ─────────────────────────── */
        {
            ImGui::SetNextWindowPos(ImVec2(10, 270));
            ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x - 20, 250));
            ImGui::Begin("Pedalboard", NULL,
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);

            ImGui::Text("Pre-Amp Pedals");
            ImGui::Separator();

            int pre_count = fx_chain_get_pedal_count(engine, FX_CHAIN_POS_PRE);
            if (pre_count == 0) {
                ImGui::TextDisabled("No pedals. Click [+] to add.");
            }

            /* Add pedal button */
            if (ImGui::Button("[+] Add Pedal")) {
                ImGui::OpenPopup("add_pedal_popup");
            }

            if (ImGui::BeginPopup("add_pedal_popup")) {
                static const struct { fx_pedal_type_t type; const char *name; } pedal_menu[] = {
                    { FX_PEDAL_JADE_DRIVE,    "Jade Drive (OD)" },
                    { FX_PEDAL_GOLD_DRIVE,    "Gold Drive (Transparent OD)" },
                    { FX_PEDAL_RODENT,        "Rodent (Distortion)" },
                    { FX_PEDAL_ECHO_DELAY,    "Echo Delay" },
                    { FX_PEDAL_HALL_VERB,     "Hall Verb (Reverb)" },
                    { FX_PEDAL_SQUEEZE_BOX,   "Squeeze Box (Compressor)" },
                };
                for (int i = 0; i < 6; i++) {
                    if (ImGui::MenuItem(pedal_menu[i].name)) {
                        fx_chain_add_pedal(engine, pedal_menu[i].type, FX_CHAIN_POS_PRE);
                    }
                }
                ImGui::EndPopup();
            }

            ImGui::Spacing();
            ImGui::Text("Post-Amp Pedals");
            ImGui::Separator();
            ImGui::TextDisabled("(coming soon)");

            ImGui::End();
        }

        /* ── Status bar ───────────────────────────────────────── */
        {
            ImGui::SetNextWindowPos(ImVec2(0, io.DisplaySize.y - 30));
            ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, 30));
            ImGui::Begin("##status", NULL,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);
            ImGui::TextDisabled("0xFX v0.1.0 | Phase 3B MVP | %.0f FPS",
                               io.Framerate);
            ImGui::End();
        }

        /* Render */
        ImGui::Render();
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
        glClearColor(0.06f, 0.05f, 0.04f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    /* Cleanup */
    fx_audio_shutdown();
    fx_engine_destroy(engine);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
