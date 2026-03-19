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
#include "../core/log.h"
#include "../core/crash.h"
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

    fx_log_init(NULL);
    fx_crash_init();
    FX_INFO("GUI started");

    /* SDL init */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        FX_ERROR("SDL_Init error: %s", SDL_GetError());
        fx_log_shutdown();
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
    FX_INFO("Engine created");

    /* Audio device / settings state */
    int num_input_devices = fx_audio_get_device_count();
    int num_output_devices = fx_audio_get_output_count();
    static int  s_selected_input   = -1;   /* -1 = no input selected (muted) */
    static int  s_selected_output  = -1;   /* -1 = system default output */
    static int  s_selected_buf_idx = 2;    /* default: 256 frames */
    static int  s_selected_sr_idx  = 0;    /* default: 44100 Hz  */
    static bool s_audio_active     = false;

    static const int   buf_sizes[]  = { 64, 128, 256, 512, 1024 };
    static const char *buf_labels[] = { "64", "128", "256", "512", "1024" };
    static const int   sr_values[]  = { 44100, 48000 };
    static const char *sr_labels[]  = { "44100 Hz", "48000 Hz" };

    /* Launch MUTED — don't auto-start audio. User picks devices. */
    FX_INFO("Launched muted. Select input+output devices to start audio.");

    /* Auto-load default preset for sensible starting tone */
    {
        bool loaded = fx_preset_load(engine, "presets/clean_sparkle.0xfx");
        if (!loaded) loaded = fx_preset_load(engine, "../presets/clean_sparkle.0xfx");
        if (loaded) {
            FX_INFO("Default preset loaded: Clean Sparkle");
        } else {
            FX_WARN("Could not load default preset, using engine defaults");
        }
    }

    /* Pre-pedal ID registry — tracks IDs returned by fx_chain_add_pedal */
    static fx_pedal_id s_pre_ids[32];
    static int         s_pre_id_count = 0;

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

            ImGui::SameLine(620);

            /* Audio status indicator */
            if (s_audio_active) {
                ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f), "LIVE");
            } else {
                ImGui::TextColored(ImVec4(0.6f, 0.3f, 0.1f, 1.0f), "MUTED");
            }
            ImGui::SameLine();

            if (ImGui::SmallButton("Audio...")) {
                ImGui::OpenPopup("audio_settings_popup");
            }

            /* Audio settings popup — input + output device selection */
            if (ImGui::BeginPopup("audio_settings_popup")) {
                ImGui::Text("Audio Settings");
                ImGui::Separator();

                /* Input device (guitar interface) */
                ImGui::TextDisabled("Input Device (Guitar):");
                struct InGetter {
                    static bool get(void *, int idx, const char **out) {
                        const char *n = fx_audio_get_device_name(idx);
                        if (!n) return false; *out = n; return true;
                    }
                };
                ImGui::SetNextItemWidth(300);
                if (ImGui::Combo("##input", &s_selected_input,
                                 InGetter::get, nullptr, num_input_devices)) {
                    /* Don't auto-start — user clicks Start */
                }

                ImGui::Spacing();

                /* Output device (speakers/headphones) */
                ImGui::TextDisabled("Output Device (Speakers/Headphones):");
                struct OutGetter {
                    static bool get(void *, int idx, const char **out) {
                        const char *n = fx_audio_get_output_name(idx);
                        if (!n) return false; *out = n; return true;
                    }
                };
                ImGui::SetNextItemWidth(300);
                if (ImGui::Combo("##output", &s_selected_output,
                                 OutGetter::get, nullptr, num_output_devices)) {
                    fx_audio_set_output(s_selected_output);
                }

                ImGui::Spacing();

                /* Buffer size + sample rate */
                ImGui::SetNextItemWidth(120);
                if (ImGui::Combo("Buffer", &s_selected_buf_idx, buf_labels, 5)) {
                    fx_audio_set_buffer_size(engine, buf_sizes[s_selected_buf_idx]);
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(120);
                if (ImGui::Combo("Rate", &s_selected_sr_idx, sr_labels, 2)) {
                    fx_audio_set_sample_rate(engine, (float)sr_values[s_selected_sr_idx]);
                }

                ImGui::Spacing();
                ImGui::Separator();

                /* Start / Stop audio button */
                if (!s_audio_active) {
                    if (s_selected_input >= 0) {
                        if (ImGui::Button("Start Audio", ImVec2(200, 30))) {
                            if (s_selected_output >= 0)
                                fx_audio_set_output(s_selected_output);
                            if (fx_audio_set_device(engine, s_selected_input)) {
                                s_audio_active = true;
                                FX_INFO("Audio started: in=%s out=%s",
                                    fx_audio_get_device_name(s_selected_input),
                                    s_selected_output >= 0 ? fx_audio_get_output_name(s_selected_output) : "(default)");
                            } else {
                                FX_ERROR("Failed to start audio");
                            }
                        }
                    } else {
                        ImGui::TextDisabled("Select an input device first");
                    }
                } else {
                    if (ImGui::Button("Stop Audio", ImVec2(200, 30))) {
                        fx_audio_shutdown();
                        fx_audio_init();
                        num_input_devices = fx_audio_get_device_count();
                        num_output_devices = fx_audio_get_output_count();
                        s_audio_active = false;
                        FX_INFO("Audio stopped");
                    }
                }

                ImGui::EndPopup();
            }

            ImGui::End();
        }

        /* ── Amp panel ────────────────────────────────────────── */
        {
            ImGui::SetNextWindowPos(ImVec2(10, 60));
            ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x - 20, 230));
            ImGui::Begin("Amp", NULL,
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoScrollbar);

            fx_amp_type_t amp_type = fx_amp_get_model(engine, FX_CHAIN_DEFAULT);

            /* ── Amp name — large centered title ──────────────── */
            {
                const char *amp_name = fx_amp_get_type_name(amp_type);
                float avail_w = ImGui::GetContentRegionAvail().x;
                ImVec2 text_size = ImGui::CalcTextSize(amp_name);
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_w - text_size.x) * 0.5f);
                ImGui::TextColored(ImVec4(0.90f, 0.65f, 0.20f, 1.0f), "%s", amp_name);
            }
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0.0f, 4.0f));

            /*
             * Knob sections: PREAMP | EQ | POWER
             * The engine exposes params in enum order; count tells us how many
             * are active for this model.  A param is present iff its enum index
             * is less than the count for this amp type.
             */
            int param_count = fx_amp_get_param_count(amp_type);
            auto has_param = [&](fx_amp_param_t p) -> bool {
                return (int)p < param_count;
            };

            /* Draw a single amp knob, preceded by a small spacer. */
            auto draw_amp_knob = [&](fx_amp_param_t p) {
                if (!has_param(p)) return;
                const char *name = fx_amp_get_param_name(amp_type, p);
                float val = fx_amp_get_param(engine, FX_CHAIN_DEFAULT, p);
                ImGui::Dummy(ImVec2(6.0f, 0.0f));
                ImGui::SameLine();
                if (knob_float(name, &val, 0.0f, 1.0f, 0.5f, 0.01f)) {
                    fx_amp_set_param(engine, FX_CHAIN_DEFAULT, p, val);
                }
                ImGui::SameLine();
            };

            /* Helper: draw a thin vertical separator line */
            auto draw_vsep = [&]() {
                ImGui::SameLine(0, 0);
                ImVec2 p0 = ImGui::GetCursorScreenPos();
                p0.x += 6.0f;
                ImVec2 p1 = ImVec2(p0.x, p0.y + 130.0f);
                ImGui::GetWindowDrawList()->AddLine(p0, p1,
                    IM_COL32(100, 88, 70, 180), 1.0f);
                ImGui::SameLine(0, 18);
            };

            /* ── PREAMP section ──── Gain ─────────────────────── */
            ImGui::BeginGroup();
            ImGui::TextDisabled("PREAMP");
            ImGui::Dummy(ImVec2(0.0f, 2.0f));
            draw_amp_knob(FX_AMP_PARAM_GAIN);
            ImGui::NewLine();
            ImGui::EndGroup();

            draw_vsep();

            /* ── EQ section ──── Bass / Mid / Treble / Cut ─────── */
            ImGui::BeginGroup();
            ImGui::TextDisabled("EQ");
            ImGui::Dummy(ImVec2(0.0f, 2.0f));
            draw_amp_knob(FX_AMP_PARAM_BASS);
            draw_amp_knob(FX_AMP_PARAM_MID);
            draw_amp_knob(FX_AMP_PARAM_TREBLE);
            draw_amp_knob(FX_AMP_PARAM_CUT);
            ImGui::NewLine();
            ImGui::EndGroup();

            draw_vsep();

            /* ── POWER section ── Presence / Sag / Master / Volume / Bright */
            ImGui::BeginGroup();
            ImGui::TextDisabled("POWER");
            ImGui::Dummy(ImVec2(0.0f, 2.0f));
            draw_amp_knob(FX_AMP_PARAM_PRESENCE);
            draw_amp_knob(FX_AMP_PARAM_SAG);
            draw_amp_knob(FX_AMP_PARAM_MASTER);
            draw_amp_knob(FX_AMP_PARAM_VOLUME);
            draw_amp_knob(FX_AMP_PARAM_BRIGHT);
            ImGui::NewLine();
            ImGui::EndGroup();

            ImGui::End();
        }

        /* ── Pedalboard ───────────────────────────────────────── */
        {
            ImGui::SetNextWindowPos(ImVec2(10, 300));
            ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x - 20,
                                            io.DisplaySize.y - 330));
            ImGui::Begin("Pedalboard", NULL,
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_HorizontalScrollbar);

            /* Shared pedal menu */
            static const struct { fx_pedal_type_t type; const char *name; } pedal_menu[] = {
                { FX_PEDAL_JADE_DRIVE,    "Jade Drive (OD)" },
                { FX_PEDAL_GOLD_DRIVE,    "Gold Drive (Transparent OD)" },
                { FX_PEDAL_RODENT,        "Rodent (Distortion)" },
                { FX_PEDAL_ECHO_DELAY,    "Echo Delay" },
                { FX_PEDAL_HALL_VERB,     "Hall Verb (Reverb)" },
                { FX_PEDAL_DRIP_VERB,     "Drip Verb (Spring Reverb)" },
                { FX_PEDAL_SQUEEZE_BOX,    "Squeeze Box (Compressor)" },
                { FX_PEDAL_NOISE_GATE,     "Noise Gate" },
                { FX_PEDAL_TONE_SCULPTOR,  "Tone Sculptor (Graphic EQ)" },
                { FX_PEDAL_MAMMOTH_FUZZ,   "Mammoth Fuzz (Big Muff)" },
                { FX_PEDAL_ROUND_FUZZ,     "Round Fuzz (Germanium)" },
                { FX_PEDAL_CHAOS_FUZZ,     "Chaos Fuzz (Gated)" },
                { FX_PEDAL_GRIT_CRUSH,     "Grit Crush (Bitcrusher)" },
                { FX_PEDAL_RING_TONE,      "Ring Tone (Ring Mod)" },
                { FX_PEDAL_WARM_TAPE,      "Warm Tape (Tape Sat)" },
                { FX_PEDAL_DRIFT_VIBRATO,  "Drift Vibrato (Pitch Vibrato)" },
                { FX_PEDAL_JET_FLANGER,    "Jet Flanger (Through-Zero)" },
                { FX_PEDAL_PLATE_VERB,     "Plate Verb (Plate Reverb)" },
                { FX_PEDAL_SHIMMER_VERB,   "Shimmer Verb (Octave Shimmer)" },
                { FX_PEDAL_CLOUD_VERB,     "Cloud Verb (Ambient/Freeze)" },
            };
            static const int pedal_menu_count = 20;

            /* Draw a pedal section (pre or post amp) */
            auto draw_pedal_section = [&](const char *label, fx_chain_pos_t pos,
                                          fx_pedal_id *ids, int *id_count,
                                          const char *popup_id, int section_idx) {
                ImGui::TextColored(ImVec4(0.90f, 0.65f, 0.20f, 1.0f), "%s", label);
                ImGui::Separator();
                ImGui::Dummy(ImVec2(0.0f, 4.0f));

                if (*id_count == 0) {
                    ImGui::TextDisabled("No pedals. Click [+] to add.");
                    ImGui::Dummy(ImVec2(0.0f, 4.0f));
                }

                int remove_idx = -1;

                for (int pi = 0; pi < *id_count; pi++) {
                    fx_pedal_id pid    = ids[pi];
                    fx_pedal_type_t pt = fx_pedal_get_type(engine, pid);
                    if (pt == FX_PEDAL_TYPE_COUNT) continue;

                    const char *pname = fx_pedal_get_type_name(pt);
                    int nparam        = fx_pedal_get_param_count(pt);
                    bool bypassed     = fx_pedal_get_bypass(engine, pid);

                    char child_id[64];
                    snprintf(child_id, sizeof(child_id), "pedal_%d_%d", section_idx, (int)pid);

                    float box_w = 30.0f + (nparam > 0 ? nparam * 68.0f : 68.0f);
                    if (box_w < 120.0f) box_w = 120.0f;

                    ImVec4 bg = bypassed ? ImVec4(0.10f, 0.09f, 0.08f, 1.0f)
                                        : ImVec4(0.14f, 0.13f, 0.11f, 1.0f);
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);
                    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);

                    if (ImGui::BeginChild(child_id, ImVec2(box_w, 160), true)) {
                        float avail_w = ImGui::GetContentRegionAvail().x;

                        /* [X] remove button (top-right) */
                        char x_id[32];
                        snprintf(x_id, sizeof(x_id), "X##rm_%d_%d", section_idx, (int)pid);
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail_w - 20.0f);
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.08f, 0.08f, 0.8f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.6f, 0.1f, 0.1f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.8f, 0.15f, 0.15f, 1.0f));
                        if (ImGui::SmallButton(x_id)) remove_idx = pi;
                        ImGui::PopStyleColor(3);

                        /* Pedal name */
                        ImVec2 tsize = ImGui::CalcTextSize(pname);
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_w - tsize.x) * 0.5f);
                        if (bypassed) ImGui::TextDisabled("%s", pname);
                        else ImGui::Text("%s", pname);

                        ImGui::Dummy(ImVec2(0.0f, 2.0f));

                        /* Knobs */
                        for (int k = 0; k < nparam; k++) {
                            const char *kname = fx_pedal_get_param_name(pt, k);
                            float kval = fx_pedal_get_param(engine, pid, k);
                            ImGui::Dummy(ImVec2(4.0f, 0.0f)); ImGui::SameLine();
                            if (knob_float(kname, &kval, 0.0f, 1.0f, 0.5f, 0.01f))
                                fx_pedal_set_param(engine, pid, k, kval);
                            if (k < nparam - 1) ImGui::SameLine();
                        }

                        ImGui::Dummy(ImVec2(0.0f, 4.0f));

                        /* Bottom row: bypass + reorder arrows */
                        {
                            char bp_id[48];
                            snprintf(bp_id, sizeof(bp_id), "%s##bp_%d_%d",
                                     bypassed ? "BYPASS" : "ACTIVE", section_idx, (int)pid);
                            ImGui::PushStyleColor(ImGuiCol_Button,
                                bypassed ? ImVec4(0.45f, 0.08f, 0.08f, 1.0f)
                                         : ImVec4(0.08f, 0.40f, 0.08f, 1.0f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                bypassed ? ImVec4(0.60f, 0.12f, 0.12f, 1.0f)
                                         : ImVec4(0.12f, 0.55f, 0.12f, 1.0f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                                bypassed ? ImVec4(0.75f, 0.18f, 0.18f, 1.0f)
                                         : ImVec4(0.18f, 0.70f, 0.18f, 1.0f));
                            float half_w = (avail_w - 16.0f) * 0.55f;
                            if (ImGui::Button(bp_id, ImVec2(half_w, 0)))
                                fx_pedal_set_bypass(engine, pid, !bypassed);
                            ImGui::PopStyleColor(3);

                            ImGui::SameLine();

                            /* Reorder arrows: < > */
                            char lid[32], rid[32];
                            snprintf(lid, sizeof(lid), "<##l_%d_%d", section_idx, (int)pid);
                            snprintf(rid, sizeof(rid), ">##r_%d_%d", section_idx, (int)pid);

                            if (pi > 0) {
                                if (ImGui::SmallButton(lid)) {
                                    fx_pedal_id tmp = ids[pi - 1];
                                    ids[pi - 1] = ids[pi]; ids[pi] = tmp;
                                    fx_chain_move_pedal(engine, pid, pos, pi - 1);
                                }
                            } else { ImGui::TextDisabled("<"); }

                            ImGui::SameLine();

                            if (pi < *id_count - 1) {
                                if (ImGui::SmallButton(rid)) {
                                    fx_pedal_id tmp = ids[pi + 1];
                                    ids[pi + 1] = ids[pi]; ids[pi] = tmp;
                                    fx_chain_move_pedal(engine, pid, pos, pi + 1);
                                }
                            } else { ImGui::TextDisabled(">"); }
                        }
                    }
                    ImGui::EndChild();
                    ImGui::PopStyleColor();
                    ImGui::PopStyleVar();
                    ImGui::SameLine(0, 8);
                }

                /* Deferred remove */
                if (remove_idx >= 0 && remove_idx < *id_count) {
                    fx_chain_remove_pedal(engine, ids[remove_idx]);
                    for (int j = remove_idx; j < *id_count - 1; j++) ids[j] = ids[j + 1];
                    (*id_count)--;
                }

                /* [+] Add button */
                if (*id_count > 0) ImGui::NewLine();
                char add_id[32];
                snprintf(add_id, sizeof(add_id), "[+] Add##%s", popup_id);
                if (ImGui::Button(add_id)) ImGui::OpenPopup(popup_id);
                if (ImGui::BeginPopup(popup_id)) {
                    for (int i = 0; i < pedal_menu_count; i++) {
                        if (ImGui::MenuItem(pedal_menu[i].name)) {
                            fx_pedal_id nid = fx_chain_add_pedal(engine, pedal_menu[i].type, pos);
                            if (nid >= 0 && *id_count < 32) ids[(*id_count)++] = nid;
                        }
                    }
                    ImGui::EndPopup();
                }
            };

            /* ── Pre-Amp Pedals ───────────────────────────────── */
            draw_pedal_section("Pre-Amp Pedals", FX_CHAIN_POS_PRE,
                               s_pre_ids, &s_pre_id_count, "add_pre_popup", 0);

            ImGui::Dummy(ImVec2(0.0f, 10.0f));
            ImGui::TextColored(ImVec4(0.60f, 0.50f, 0.30f, 1.0f), ">>> %s >>>",
                fx_amp_get_type_name(fx_amp_get_model(engine, FX_CHAIN_DEFAULT)));
            ImGui::Dummy(ImVec2(0.0f, 10.0f));

            /* ── Post-Amp Pedals ──────────────────────────────── */
            static fx_pedal_id s_post_ids[32];
            static int s_post_id_count = 0;

            draw_pedal_section("Post-Amp Pedals", FX_CHAIN_POS_POST,
                               s_post_ids, &s_post_id_count, "add_post_popup", 1);

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

    fx_log_shutdown();
    return 0;
}
