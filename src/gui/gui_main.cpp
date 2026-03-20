/*
 * 0xFX — GUI application entry point
 *
 * SDL2 + OpenGL 3.3 + Dear ImGui
 * Borderless window with custom title bar (no Windows chrome)
 *
 * Layout: BIAS FX-inspired signal chain flow
 *   Top:           Toolbar (logo, tuner, LIVE, _ [] X)
 *   Middle-top:    Signal chain — horizontal node flow
 *   Middle-bottom: Detail view — knobs for selected node
 *   Bottom:        Status bar with level meters
 */
#include <SDL.h>
#include <SDL_opengl.h>
#include <SDL_syswm.h>

#ifdef _WIN32
#include <windows.h>
#include <windowsx.h>  /* GET_X_LPARAM, GET_Y_LPARAM */
#endif

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"

extern "C" {
#include "../engine/fx_engine.h"
#include "../audio/audio_device.h"
#include "../core/log.h"
#include "../core/crash.h"
#include "knobs.h"
#include "texture.h"
}

#include <stdio.h>
#include <cmath>
#include <cstring>

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

/* ── Borderless window support (Win32) ─────────────────────────── */

#define RESIZE_BORDER 8

#ifdef _WIN32
static WNDPROC g_orig_wndproc = NULL;

static LRESULT CALLBACK borderless_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCALCSIZE && wp == TRUE) {
        if (IsZoomed(hwnd)) {
            NCCALCSIZE_PARAMS *params = (NCCALCSIZE_PARAMS *)lp;
            HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi; mi.cbSize = sizeof(mi);
            if (GetMonitorInfo(mon, &mi)) params->rgrc[0] = mi.rcWork;
        }
        return 0;
    }
    if (msg == WM_NCHITTEST) {
        LRESULT hit = DefWindowProcW(hwnd, msg, wp, lp);
        if (hit == HTCLIENT) {
            RECT rc; GetClientRect(hwnd, &rc);
            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            ScreenToClient(hwnd, &pt);
            int w = rc.right, h = rc.bottom;
            bool top = pt.y < RESIZE_BORDER, bottom = pt.y >= h - RESIZE_BORDER;
            bool left = pt.x < RESIZE_BORDER, right_ = pt.x >= w - RESIZE_BORDER;
            if (top && left)      return HTTOPLEFT;
            if (top && right_)    return HTTOPRIGHT;
            if (bottom && left)   return HTBOTTOMLEFT;
            if (bottom && right_) return HTBOTTOMRIGHT;
            if (top)              return HTTOP;
            if (bottom)           return HTBOTTOM;
            if (left)             return HTLEFT;
            if (right_)           return HTRIGHT;
        } else return hit;
    }
    if (msg == WM_GETMINMAXINFO) {
        MINMAXINFO *mmi = (MINMAXINFO *)lp;
        HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi; mi.cbSize = sizeof(mi);
        if (GetMonitorInfo(mon, &mi)) {
            mmi->ptMaxPosition.x = mi.rcWork.left - mi.rcMonitor.left;
            mmi->ptMaxPosition.y = mi.rcWork.top  - mi.rcMonitor.top;
            mmi->ptMaxSize.x     = mi.rcWork.right  - mi.rcWork.left;
            mmi->ptMaxSize.y     = mi.rcWork.bottom - mi.rcWork.top;
        }
        return 0;
    }
    return CallWindowProcW(g_orig_wndproc, hwnd, msg, wp, lp);
}

static void install_borderless_wndproc(SDL_Window *window) {
    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    if (SDL_GetWindowWMInfo(window, &wmInfo)) {
        HWND hwnd = wmInfo.info.win.window;
        g_orig_wndproc = (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC,
                                                      (LONG_PTR)borderless_wndproc);
        LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
        style |= WS_THICKFRAME | WS_CAPTION | WS_SYSMENU |
                 WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
        SetWindowLongPtrW(hwnd, GWL_STYLE, style);
        SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
    }
}
#endif /* _WIN32 */

/* ── Signal chain node types ─────────────────────────────────────── */

enum NodeKind {
    NODE_INPUT = 0,
    NODE_PEDAL_PRE,
    NODE_AMP,
    NODE_CAB,
    NODE_PEDAL_POST,
    NODE_OUTPUT
};

/* Signal chain node descriptor */
struct ChainNode {
    NodeKind    kind;
    int         slot;       /* index into pre/post pedal arrays, or -1 */
    fx_pedal_id pedal_id;   /* valid only for PEDAL nodes */
};

/* Node colors by kind */
static ImU32 node_color(NodeKind kind, bool bypassed) {
    if (bypassed) return IM_COL32(60, 55, 50, 255);
    switch (kind) {
        case NODE_INPUT:      return IM_COL32(50, 120, 80, 255);
        case NODE_PEDAL_PRE:  return IM_COL32(60, 100, 160, 255);
        case NODE_AMP:        return IM_COL32(180, 90, 30, 255);
        case NODE_CAB:        return IM_COL32(140, 80, 50, 255);
        case NODE_PEDAL_POST: return IM_COL32(100, 60, 160, 255);
        case NODE_OUTPUT:     return IM_COL32(50, 120, 80, 255);
        default:              return IM_COL32(80, 80, 80, 255);
    }
}

static const char *node_label(NodeKind kind, fx_engine_t *engine, fx_pedal_id pid) {
    switch (kind) {
        case NODE_INPUT:      return "INPUT";
        case NODE_AMP:        return fx_amp_get_type_name(fx_amp_get_model(engine, FX_CHAIN_DEFAULT));
        case NODE_CAB:        return "CAB";
        case NODE_OUTPUT:     return "OUTPUT";
        case NODE_PEDAL_PRE:
        case NODE_PEDAL_POST: {
            fx_pedal_type_t pt = fx_pedal_get_type(engine, pid);
            if (pt < FX_PEDAL_TYPE_COUNT) return fx_pedal_get_type_name(pt);
            return "???";
        }
        default: return "???";
    }
}

/* Cab type names (engine doesn't expose these) */
static const char *s_cab_type_names[] = {
    "1x12 Open", "2x12 Closed", "4x12 Straight", "4x12 Slant", "Direct"
};

/* ── Pedal gallery: category-organized pedal browser ──────────── */

struct PedalEntry {
    fx_pedal_type_t type;
    const char     *name;
};

struct PedalCategory {
    const char  *label;
    PedalEntry   pedals[8];
    int          count;
};

static const PedalCategory s_pedal_categories[] = {
    { "OVERDRIVE", {
        { FX_PEDAL_JADE_DRIVE,  "Jade Drive"  },
        { FX_PEDAL_GOLD_DRIVE,  "Gold Drive"  },
        { FX_PEDAL_BLUES_GRIT,  "Blues Grit"  },
    }, 3 },
    { "DISTORTION", {
        { FX_PEDAL_RODENT,      "Rodent"      },
        { FX_PEDAL_ORANGE_DIST, "Orange Dist" },
        { FX_PEDAL_METAL_ZONE,  "Metal Zone"  },
        { FX_PEDAL_AMP_BOX,     "Amp Box"     },
    }, 4 },
    { "FUZZ", {
        { FX_PEDAL_MAMMOTH_FUZZ, "Mammoth Fuzz" },
        { FX_PEDAL_ROUND_FUZZ,   "Round Fuzz"   },
        { FX_PEDAL_WRAITH_FUZZ,  "Wraith Fuzz"  },
        { FX_PEDAL_CHAOS_FUZZ,   "Chaos Fuzz"   },
    }, 4 },
    { "DELAY", {
        { FX_PEDAL_ECHO_DELAY,   "Echo Delay"   },
        { FX_PEDAL_CARBON_DELAY, "Carbon Delay" },
        { FX_PEDAL_TAPE_MACHINE, "Tape Machine" },
        { FX_PEDAL_MEMORY_ECHO,  "Memory Echo"  },
    }, 4 },
    { "REVERB", {
        { FX_PEDAL_DRIP_VERB,    "Drip Verb"    },
        { FX_PEDAL_HALL_VERB,    "Hall Verb"    },
        { FX_PEDAL_PLATE_VERB,   "Plate Verb"   },
        { FX_PEDAL_SHIMMER_VERB, "Shimmer Verb" },
        { FX_PEDAL_CLOUD_VERB,   "Cloud Verb"   },
    }, 5 },
    { "MODULATION", {
        { FX_PEDAL_LIQUID_CHORUS, "Liquid Chorus" },
        { FX_PEDAL_PHASE_SWEEP,   "Phase Sweep"   },
        { FX_PEDAL_JET_FLANGER,   "Jet Flanger"   },
        { FX_PEDAL_PULSE_TREM,    "Pulse Trem"    },
        { FX_PEDAL_DRIFT_VIBRATO, "Drift Vibrato" },
    }, 5 },
    { "DYNAMICS", {
        { FX_PEDAL_SQUEEZE_BOX, "Squeeze Box" },
        { FX_PEDAL_GLASS_COMP,  "Glass Comp"  },
        { FX_PEDAL_PUNCH_COMP,  "Punch Comp"  },
        { FX_PEDAL_NOISE_GATE,  "Noise Gate"  },
    }, 4 },
    { "FILTER/EQ", {
        { FX_PEDAL_HOWL_WAH,      "Howl Wah"      },
        { FX_PEDAL_QUACK_FILTER,  "Quack Filter"  },
        { FX_PEDAL_TONE_SCULPTOR, "Tone Sculptor" },
        { FX_PEDAL_PRECISION_EQ,  "Precision EQ"  },
    }, 4 },
    { "PITCH", {
        { FX_PEDAL_OCTAVE_ENGINE, "Octave Engine" },
        { FX_PEDAL_PITCH_WARP,    "Pitch Warp"    },
    }, 2 },
    { "UTILITY", {
        { FX_PEDAL_GRIT_CRUSH, "Grit Crush" },
        { FX_PEDAL_RING_TONE,  "Ring Tone"  },
        { FX_PEDAL_WARM_TAPE,  "Warm Tape"  },
    }, 3 },
    { "EXPERIMENTAL", {
        { FX_PEDAL_INFINITE_HOLD, "Infinite Hold" },
        { FX_PEDAL_GRAIN_CLOUD,   "Grain Cloud"   },
        { FX_PEDAL_LOOP_STATION,  "Loop Station"  },
    }, 3 },
};
static const int s_pedal_category_count = 11;

/* Shared pedal menu for add-pedal popups */
static const struct { fx_pedal_type_t type; const char *name; } s_pedal_menu[] = {
    { FX_PEDAL_JADE_DRIVE,    "Jade Drive (OD)" },
    { FX_PEDAL_GOLD_DRIVE,    "Gold Drive (Transparent OD)" },
    { FX_PEDAL_RODENT,        "Rodent (Distortion)" },
    { FX_PEDAL_ECHO_DELAY,    "Echo Delay" },
    { FX_PEDAL_HALL_VERB,     "Hall Verb (Reverb)" },
    { FX_PEDAL_DRIP_VERB,     "Drip Verb (Spring Reverb)" },
    { FX_PEDAL_SQUEEZE_BOX,   "Squeeze Box (Compressor)" },
    { FX_PEDAL_NOISE_GATE,    "Noise Gate" },
    { FX_PEDAL_TONE_SCULPTOR, "Tone Sculptor (Graphic EQ)" },
    { FX_PEDAL_MAMMOTH_FUZZ,  "Mammoth Fuzz (Big Muff)" },
    { FX_PEDAL_ROUND_FUZZ,    "Round Fuzz (Germanium)" },
    { FX_PEDAL_CHAOS_FUZZ,    "Chaos Fuzz (Gated)" },
    { FX_PEDAL_GRIT_CRUSH,    "Grit Crush (Bitcrusher)" },
    { FX_PEDAL_RING_TONE,     "Ring Tone (Ring Mod)" },
    { FX_PEDAL_WARM_TAPE,     "Warm Tape (Tape Sat)" },
    { FX_PEDAL_DRIFT_VIBRATO, "Drift Vibrato (Pitch Vibrato)" },
    { FX_PEDAL_JET_FLANGER,   "Jet Flanger (Through-Zero)" },
    { FX_PEDAL_PLATE_VERB,    "Plate Verb (Plate Reverb)" },
    { FX_PEDAL_SHIMMER_VERB,  "Shimmer Verb (Octave Shimmer)" },
    { FX_PEDAL_CLOUD_VERB,    "Cloud Verb (Ambient/Freeze)" },
    { FX_PEDAL_OCTAVE_ENGINE, "Octave Engine (Polyphonic Octave)" },
    { FX_PEDAL_LOOP_STATION,  "Loop Station (Looper)" },
    { FX_PEDAL_INFINITE_HOLD, "Infinite Hold (Freeze/Drone)" },
    { FX_PEDAL_GRAIN_CLOUD,   "Grain Cloud (Granular Delay)" },
};
static const int s_pedal_menu_count = 24;

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
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_BORDERLESS |
        SDL_WINDOW_ALLOW_HIGHDPI
    );
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_SetWindowMinimumSize(window, 800, 500);
#ifdef _WIN32
    install_borderless_wndproc(window);
#endif

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

    /* Texture loader smoke test */
    {
        uintptr_t test_tex = fx_texture_load("resources/knobs/knob_chicken_cream.png");
        if (test_tex) FX_INFO("Texture loaded: %lu", (unsigned long)test_tex);
    }

    /* Audio + engine init */
    fx_audio_init();
    fx_engine_t *engine = fx_engine_create(44100.0f);
    FX_INFO("Engine created");

    /* Audio device / settings state */
    int num_input_devices = fx_audio_get_device_count();
    int num_output_devices = fx_audio_get_output_count();
    static int  s_selected_input   = -1;
    static int  s_selected_output  = -1;
    static int  s_selected_buf_idx = 2;    /* default: 256 frames */
    static int  s_selected_sr_idx  = 0;    /* default: 44100 Hz  */
    static bool s_audio_active     = false;

    static const int   buf_sizes[]  = { 64, 128, 256, 512, 1024 };
    static const char *buf_labels[] = { "64", "128", "256", "512", "1024" };
    static const int   sr_values[]  = { 44100, 48000 };
    static const char *sr_labels[]  = { "44100 Hz", "48000 Hz" };

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

    /* Pedal ID registries — tracks IDs returned by fx_chain_add_pedal */
    static fx_pedal_id s_pre_ids[32];
    static int         s_pre_id_count = 0;
    static fx_pedal_id s_post_ids[32];
    static int         s_post_id_count = 0;

    /* Signal chain selection state */
    static int  s_selected_node = -1;  /* index into the flattened chain array */

    /* Layout constants */
    static const float TOOLBAR_H      = 50.0f;
    static const float STATUS_H       = 50.0f;
    static const float NODE_W         = 80.0f;
    static const float NODE_H         = 60.0f;
    static const float NODE_SPACING   = 56.0f;  /* wider to fit 40px [+] btn */
    static const float ADD_BTN_W      = 40.0f;
    static const float CHAIN_PADDING  = 20.0f;

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

        float win_w = io.DisplaySize.x;
        float win_h = io.DisplaySize.y;

        /* ── Toolbar ──────────────────────────────────────────── */
        {
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(win_w, TOOLBAR_H));
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

                ImVec4 note_color;
                if (!active) {
                    note_color = ImVec4(0.45f, 0.40f, 0.35f, 1.0f);
                } else if (cents < 0.0f ? -cents < 5.0f : cents < 5.0f) {
                    note_color = ImVec4(0.20f, 0.90f, 0.30f, 1.0f);
                } else if (cents < 0.0f ? -cents < 15.0f : cents < 15.0f) {
                    note_color = ImVec4(0.95f, 0.85f, 0.10f, 1.0f);
                } else {
                    note_color = ImVec4(0.95f, 0.25f, 0.20f, 1.0f);
                }

                ImGui::PushStyleColor(ImGuiCol_Text, note_color);
                ImGui::SetWindowFontScale(1.4f);
                ImGui::Text("%s", note);
                ImGui::SetWindowFontScale(1.0f);
                ImGui::PopStyleColor();
                ImGui::SameLine();

                /* Cents bar */
                {
                    const float bar_w    = 200.0f;
                    const float bar_h    = 10.0f;
                    const float dot_r    = 6.0f;
                    const float padding  = dot_r;

                    ImVec2 cursor = ImGui::GetCursorScreenPos();
                    float toolbar_top = ImGui::GetWindowPos().y;
                    float bar_cx_y   = toolbar_top + 25.0f;
                    float bar_top_y  = bar_cx_y - bar_h * 0.5f;
                    float bar_bot_y  = bar_cx_y + bar_h * 0.5f;

                    float bar_x0 = cursor.x + padding;
                    float bar_x1 = bar_x0 + bar_w;

                    ImDrawList *dl = ImGui::GetWindowDrawList();
                    dl->AddRectFilled(ImVec2(bar_x0, bar_top_y), ImVec2(bar_x1, bar_bot_y),
                                      IM_COL32(50, 45, 40, 255), 3.0f);

                    float mid_x = bar_x0 + bar_w * 0.5f;
                    dl->AddRectFilled(ImVec2(mid_x - 1.0f, bar_top_y - 2.0f),
                                      ImVec2(mid_x + 1.0f, bar_bot_y + 2.0f),
                                      IM_COL32(120, 110, 90, 255));

                    if (active) {
                        float t = (cents + 50.0f) / 100.0f;
                        if (t < 0.0f) t = 0.0f;
                        if (t > 1.0f) t = 1.0f;
                        float dot_x = bar_x0 + t * bar_w;
                        ImU32 dot_col = IM_COL32(
                            (int)(note_color.x * 255), (int)(note_color.y * 255),
                            (int)(note_color.z * 255), 255);
                        dl->AddCircleFilled(ImVec2(dot_x, bar_cx_y), dot_r, dot_col);
                    }

                    ImGui::Dummy(ImVec2(bar_w + padding * 2.0f, bar_h + dot_r * 2.0f));
                }
            }

            ImGui::SameLine(450);

            /* ── LIVE button — neon red-orange ─────────────────── */
            {
                ImVec2 live_sz(80.0f, 32.0f);
                if (s_audio_active) {
                    float t = (float)ImGui::GetTime();
                    float pulse = 0.85f + 0.15f * sinf(t * 3.0f);
                    ImGui::PushStyleColor(ImGuiCol_Button,
                        ImVec4(0.55f * pulse, 0.12f * pulse, 0.05f * pulse, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                        ImVec4(0.70f, 0.18f, 0.08f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                        ImVec4(0.40f, 0.10f, 0.04f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_Text,
                        ImVec4(1.0f * pulse, 0.45f * pulse, 0.15f * pulse, 1.0f));
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button,
                        ImVec4(0.12f, 0.10f, 0.08f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                        ImVec4(0.20f, 0.17f, 0.13f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                        ImVec4(0.08f, 0.07f, 0.05f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_Text,
                        ImVec4(0.40f, 0.35f, 0.28f, 1.0f));
                }
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
                if (ImGui::Button("LIVE", live_sz)) {
                    if (s_audio_active) {
                        fx_audio_shutdown(); fx_audio_init();
                        num_input_devices = fx_audio_get_device_count();
                        num_output_devices = fx_audio_get_output_count();
                        s_audio_active = false;
                        FX_INFO("Audio stopped (LIVE off)");
                    } else {
                        if (s_selected_input < 0 && num_input_devices > 0) s_selected_input = 0;
                        if (s_selected_output < 0 && num_output_devices > 0) s_selected_output = 0;
                        if (s_selected_input >= 0) {
                            if (s_selected_output >= 0) fx_audio_set_output(s_selected_output);
                            if (fx_audio_set_device(engine, s_selected_input)) {
                                s_audio_active = true;
                                FX_INFO("LIVE on: in=%s out=%s",
                                    fx_audio_get_device_name(s_selected_input),
                                    s_selected_output >= 0 ? fx_audio_get_output_name(s_selected_output) : "(default)");
                            }
                        }
                    }
                }
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(4);
                /* Glow ring when active */
                if (s_audio_active) {
                    ImDrawList *dl = ImGui::GetWindowDrawList();
                    ImVec2 bmin = ImGui::GetItemRectMin(), bmax = ImGui::GetItemRectMax();
                    float t = (float)ImGui::GetTime();
                    int g = (int)(120.0f * (0.3f + 0.2f * sinf(t * 3.0f)));
                    dl->AddRect(ImVec2(bmin.x-2,bmin.y-2), ImVec2(bmax.x+2,bmax.y+2),
                                IM_COL32(255, g, 30, g), 6.0f, 0, 2.0f);
                }
            }
            ImGui::SameLine();

            /* Audio settings gear */
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.13f, 0.11f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.22f, 0.18f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.09f, 0.07f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.70f, 0.65f, 0.55f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
            if (ImGui::Button("Settings", ImVec2(80.0f, 32.0f))) {
                ImGui::OpenPopup("audio_settings_popup");
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(4);

            /* Audio settings popup */
            if (ImGui::BeginPopup("audio_settings_popup")) {
                ImGui::Text("Audio Settings");
                ImGui::Separator();

                ImGui::TextDisabled("Input Device (Guitar):");
                struct InGetter {
                    static bool get(void *, int idx, const char **out) {
                        const char *n = fx_audio_get_device_name(idx);
                        if (!n) return false;
                        *out = n; return true;
                    }
                };
                ImGui::SetNextItemWidth(300);
                if (ImGui::Combo("##input", &s_selected_input,
                                 InGetter::get, nullptr, num_input_devices)) {
                }

                ImGui::Spacing();

                ImGui::TextDisabled("Output Device (Speakers/Headphones):");
                struct OutGetter {
                    static bool get(void *, int idx, const char **out) {
                        const char *n = fx_audio_get_output_name(idx);
                        if (!n) return false;
                        *out = n; return true;
                    }
                };
                ImGui::SetNextItemWidth(300);
                if (ImGui::Combo("##output", &s_selected_output,
                                 OutGetter::get, nullptr, num_output_devices)) {
                    fx_audio_set_output(s_selected_output);
                }

                ImGui::Spacing();

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

            /* ── Window controls: _ [] X (borderless) ─────────── */
            {
                ImVec2 wc_sz(35.0f, 28.0f);
                float controls_w = wc_sz.x * 3 + 4 + 8;
                ImGui::SameLine(ImGui::GetWindowWidth() - controls_w);

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.14f, 0.12f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.28f, 0.24f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.09f, 0.08f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.80f, 0.72f, 1.0f));
                if (ImGui::Button("_##wmin", wc_sz)) {
#ifdef _WIN32
                    SDL_SysWMinfo wmInfo; SDL_VERSION(&wmInfo.version);
                    if (SDL_GetWindowWMInfo(window, &wmInfo))
                        ShowWindow(wmInfo.info.win.window, SW_MINIMIZE);
#else
                    SDL_MinimizeWindow(window);
#endif
                }
                ImGui::PopStyleColor(4);
                ImGui::SameLine(0, 2);

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.14f, 0.12f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.28f, 0.24f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.09f, 0.08f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.80f, 0.72f, 1.0f));
                {
                    bool maximized = false;
#ifdef _WIN32
                    SDL_SysWMinfo wmI; SDL_VERSION(&wmI.version);
                    if (SDL_GetWindowWMInfo(window, &wmI))
                        maximized = IsZoomed(wmI.info.win.window) != 0;
#else
                    maximized = (SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) != 0;
#endif
                    if (ImGui::Button(maximized ? "[]##wmax" : "[ ]##wmax", wc_sz)) {
#ifdef _WIN32
                        SDL_SysWMinfo wmI2; SDL_VERSION(&wmI2.version);
                        if (SDL_GetWindowWMInfo(window, &wmI2))
                            ShowWindow(wmI2.info.win.window, maximized ? SW_RESTORE : SW_MAXIMIZE);
#else
                        if (maximized) SDL_RestoreWindow(window);
                        else SDL_MaximizeWindow(window);
#endif
                    }
                }
                ImGui::PopStyleColor(4);
                ImGui::SameLine(0, 2);

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.60f, 0.10f, 0.10f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.15f, 0.15f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.00f, 0.25f, 0.25f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
                if (ImGui::Button("X##wclose", wc_sz)) {
                    running = false;
                }
                ImGui::PopStyleColor(4);
            }

            /* ── Window drag (click empty toolbar area to move window) ── */
            {
                static bool dragging_window = false;
                static int drag_start_wx, drag_start_wy, drag_start_mx, drag_start_my;

                bool in_toolbar = (io.MousePos.y < TOOLBAR_H);
                bool over_widget = ImGui::IsAnyItemHovered() || ImGui::IsAnyItemActive();

                if (io.MouseDoubleClicked[0] && in_toolbar && !over_widget) {
#ifdef _WIN32
                    SDL_SysWMinfo wmI; SDL_VERSION(&wmI.version);
                    if (SDL_GetWindowWMInfo(window, &wmI)) {
                        bool is_max = IsZoomed(wmI.info.win.window) != 0;
                        ShowWindow(wmI.info.win.window, is_max ? SW_RESTORE : SW_MAXIMIZE);
                    }
#else
                    bool is_max = (SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) != 0;
                    if (is_max) SDL_RestoreWindow(window); else SDL_MaximizeWindow(window);
#endif
                }

                if (io.MouseClicked[0] && in_toolbar && !over_widget) {
                    dragging_window = true;
                    SDL_GetWindowPosition(window, &drag_start_wx, &drag_start_wy);
                    SDL_GetGlobalMouseState(&drag_start_mx, &drag_start_my);
                }
                if (dragging_window) {
                    if (io.MouseDown[0]) {
                        int mx, my;
                        SDL_GetGlobalMouseState(&mx, &my);
                        SDL_SetWindowPosition(window, drag_start_wx + mx - drag_start_mx,
                                                       drag_start_wy + my - drag_start_my);
                    } else {
                        dragging_window = false;
                    }
                }
            }

            ImGui::End();
        }

        /* ── Keyboard shortcuts ──────────────────────────────────── */
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            running = false;
        }

        /* ============================================================
         * BUILD THE FLATTENED SIGNAL CHAIN
         * INPUT -> [pre pedals] -> AMP -> CAB -> [post pedals] -> OUTPUT
         * ============================================================ */
        ChainNode chain[128];
        int chain_len = 0;

        /* INPUT */
        chain[chain_len++] = { NODE_INPUT, -1, -1 };

        /* Pre-amp pedals */
        for (int i = 0; i < s_pre_id_count && chain_len < 126; i++) {
            chain[chain_len++] = { NODE_PEDAL_PRE, i, s_pre_ids[i] };
        }

        /* AMP */
        chain[chain_len++] = { NODE_AMP, -1, -1 };

        /* CAB */
        chain[chain_len++] = { NODE_CAB, -1, -1 };

        /* Post-amp pedals */
        for (int i = 0; i < s_post_id_count && chain_len < 127; i++) {
            chain[chain_len++] = { NODE_PEDAL_POST, i, s_post_ids[i] };
        }

        /* OUTPUT */
        chain[chain_len++] = { NODE_OUTPUT, -1, -1 };

        /* Clamp selected_node */
        if (s_selected_node >= chain_len) s_selected_node = -1;

        /* ============================================================
         * SIGNAL CHAIN VIEW (~35% of window, below toolbar)
         * ============================================================ */
        {
            float chain_area_h = (win_h - TOOLBAR_H - STATUS_H) * 0.35f;
            ImGui::SetNextWindowPos(ImVec2(0, TOOLBAR_H));
            ImGui::SetNextWindowSize(ImVec2(win_w, chain_area_h));
            ImGui::Begin("##signal_chain", NULL,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_HorizontalScrollbar);

            ImDrawList *dl = ImGui::GetWindowDrawList();
            ImVec2 win_pos = ImGui::GetWindowPos();
            ImVec2 content_min = ImGui::GetCursorScreenPos();

            /* Center nodes vertically in the chain area */
            float cy = win_pos.y + chain_area_h * 0.45f;

            /* Calculate total width needed for scrolling */
            float total_w = chain_len * NODE_W + (chain_len - 1) * NODE_SPACING
                          + CHAIN_PADDING * 2.0f;

            /* Reserve scrollable content area */
            ImGui::Dummy(ImVec2(total_w, chain_area_h - 20.0f));

            /* Track where the [+] button popup should insert */
            static fx_chain_pos_t s_add_popup_pos = FX_CHAIN_POS_PRE;
            static int s_add_popup_insert_slot = -1;

            /* Center the chain horizontally: if chain fits, center it;
               otherwise start from CHAIN_PADDING (scrollable) */
            float chain_area_w = win_w;
            float chain_content_w = chain_len * NODE_W + (chain_len - 1) * NODE_SPACING;
            float center_offset = (chain_area_w > chain_content_w)
                ? (chain_area_w - chain_content_w) * 0.5f
                : CHAIN_PADDING;
            float x_cursor = content_min.x + center_offset - ImGui::GetScrollX();

            for (int ni = 0; ni < chain_len; ni++) {
                ChainNode &n = chain[ni];
                bool is_selected = (s_selected_node == ni);
                bool is_bypassed = false;

                if (n.kind == NODE_PEDAL_PRE || n.kind == NODE_PEDAL_POST) {
                    is_bypassed = fx_pedal_get_bypass(engine, n.pedal_id);
                } else if (n.kind == NODE_CAB) {
                    is_bypassed = fx_cab_get_bypass(engine, FX_CHAIN_DEFAULT);
                }

                float nx = x_cursor;
                float ny = cy - NODE_H * 0.5f;

                /* Draw node rectangle */
                ImU32 bg_col = node_color(n.kind, is_bypassed);
                dl->AddRectFilled(ImVec2(nx, ny), ImVec2(nx + NODE_W, ny + NODE_H),
                                  bg_col, 6.0f);

                /* Selection highlight */
                if (is_selected) {
                    dl->AddRect(ImVec2(nx - 2, ny - 2),
                                ImVec2(nx + NODE_W + 2, ny + NODE_H + 2),
                                IM_COL32(230, 180, 60, 255), 6.0f, 0, 2.5f);
                }

                /* Node label (centered below node) */
                const char *lbl = node_label(n.kind, engine, n.pedal_id);

                /* Truncate label if it's too long for the node */
                char short_lbl[16];
                ImVec2 lbl_size = ImGui::CalcTextSize(lbl);
                if (lbl_size.x > NODE_W - 4.0f) {
                    int copy_len = 9;
                    if (copy_len > (int)strlen(lbl)) copy_len = (int)strlen(lbl);
                    memcpy(short_lbl, lbl, copy_len);
                    short_lbl[copy_len] = '\0';
                    lbl = short_lbl;
                    lbl_size = ImGui::CalcTextSize(lbl);
                }

                float lbl_x = nx + (NODE_W - lbl_size.x) * 0.5f;
                float lbl_y = ny + NODE_H + 4.0f;
                dl->AddText(ImVec2(lbl_x, lbl_y),
                            is_bypassed ? IM_COL32(100, 90, 80, 200)
                                        : IM_COL32(210, 200, 180, 255),
                            lbl);

                /* Bypass indicator — small "X" on bypassed pedals */
                if (is_bypassed && (n.kind == NODE_PEDAL_PRE || n.kind == NODE_PEDAL_POST)) {
                    dl->AddLine(ImVec2(nx + 4, ny + 4), ImVec2(nx + 16, ny + 16),
                                IM_COL32(200, 60, 50, 200), 2.0f);
                    dl->AddLine(ImVec2(nx + 16, ny + 4), ImVec2(nx + 4, ny + 16),
                                IM_COL32(200, 60, 50, 200), 2.0f);
                }

                /* Invisible button for click detection */
                {
                    char btn_id[32];
                    snprintf(btn_id, sizeof(btn_id), "##node_%d", ni);
                    ImGui::SetCursorScreenPos(ImVec2(nx, ny));
                    if (ImGui::InvisibleButton(btn_id, ImVec2(NODE_W, NODE_H))) {
                        /* Don't select INPUT or OUTPUT */
                        if (n.kind != NODE_INPUT && n.kind != NODE_OUTPUT) {
                            s_selected_node = (s_selected_node == ni) ? -1 : ni;
                        }
                    }
                }

                x_cursor += NODE_W;

                /* Draw connecting line + [+] button to next node */
                if (ni < chain_len - 1) {
                    /* Determine if we should show [+] add button */
                    bool show_add = false;
                    fx_chain_pos_t add_pos = FX_CHAIN_POS_PRE;
                    int add_insert_slot = 0;

                    NodeKind cur_kind = chain[ni].kind;
                    NodeKind nxt_kind = chain[ni + 1].kind;

                    /* Pre-amp zone: INPUT..AMP */
                    if ((cur_kind == NODE_INPUT || cur_kind == NODE_PEDAL_PRE) &&
                        (nxt_kind == NODE_PEDAL_PRE || nxt_kind == NODE_AMP)) {
                        show_add = true;
                        add_pos = FX_CHAIN_POS_PRE;
                        add_insert_slot = 0;
                        for (int j = 0; j <= ni; j++) {
                            if (chain[j].kind == NODE_PEDAL_PRE) add_insert_slot++;
                        }
                    }
                    /* Post-amp zone: CAB..OUTPUT */
                    else if ((cur_kind == NODE_CAB || cur_kind == NODE_PEDAL_POST) &&
                             (nxt_kind == NODE_PEDAL_POST || nxt_kind == NODE_OUTPUT)) {
                        show_add = true;
                        add_pos = FX_CHAIN_POS_POST;
                        add_insert_slot = 0;
                        for (int j = 0; j <= ni; j++) {
                            if (chain[j].kind == NODE_PEDAL_POST) add_insert_slot++;
                        }
                    }

                    if (show_add) {
                        float add_x = x_cursor + (NODE_SPACING - ADD_BTN_W) * 0.5f;
                        float add_y = cy - ADD_BTN_W * 0.5f;

                        /* Invisible button for click detection */
                        char inv_id[32];
                        snprintf(inv_id, sizeof(inv_id), "##addinv_%d", ni);
                        ImGui::SetCursorScreenPos(ImVec2(add_x, add_y));
                        bool add_clicked = ImGui::InvisibleButton(inv_id, ImVec2(ADD_BTN_W, ADD_BTN_W));
                        bool add_hovered = ImGui::IsItemHovered();

                        /* Draw prominent [+] button with glowing amber border */
                        float t_pulse = (float)ImGui::GetTime();
                        float glow_a = add_hovered
                            ? 1.0f
                            : 0.55f + 0.25f * sinf(t_pulse * 2.5f);
                        ImU32 border_col = IM_COL32(
                            (int)(200 * glow_a),
                            (int)(140 * glow_a),
                            (int)(20  * glow_a),
                            (int)(220 * glow_a));
                        ImU32 bg_col = add_hovered
                            ? IM_COL32(80, 55, 12, 220)
                            : IM_COL32(35, 28, 8, 180);
                        float r = 6.0f;  /* corner radius */
                        dl->AddRectFilled(
                            ImVec2(add_x, add_y),
                            ImVec2(add_x + ADD_BTN_W, add_y + ADD_BTN_W),
                            bg_col, r);
                        dl->AddRect(
                            ImVec2(add_x, add_y),
                            ImVec2(add_x + ADD_BTN_W, add_y + ADD_BTN_W),
                            border_col, r, 0, 2.0f);
                        /* Draw + sign */
                        float cx2 = add_x + ADD_BTN_W * 0.5f;
                        float cy2 = add_y + ADD_BTN_W * 0.5f;
                        float arm = ADD_BTN_W * 0.28f;
                        ImU32 plus_col = IM_COL32(
                            (int)(230 * glow_a),
                            (int)(175 * glow_a),
                            (int)(40  * glow_a),
                            255);
                        dl->AddLine(ImVec2(cx2 - arm, cy2), ImVec2(cx2 + arm, cy2), plus_col, 2.5f);
                        dl->AddLine(ImVec2(cx2, cy2 - arm), ImVec2(cx2, cy2 + arm), plus_col, 2.5f);
                        /* Outer glow ring when hovered */
                        if (add_hovered) {
                            dl->AddRect(
                                ImVec2(add_x - 2, add_y - 2),
                                ImVec2(add_x + ADD_BTN_W + 2, add_y + ADD_BTN_W + 2),
                                IM_COL32(220, 160, 30, 100), r + 2, 0, 3.0f);
                        }

                        if (add_clicked) {
                            s_add_popup_pos = add_pos;
                            s_add_popup_insert_slot = add_insert_slot;
                            ImGui::OpenPopup("add_pedal_popup");
                        }
                    }

                    /* Draw connecting line */
                    float line_x0 = x_cursor;
                    float line_x1 = x_cursor + NODE_SPACING;
                    dl->AddLine(ImVec2(line_x0, cy), ImVec2(line_x1, cy),
                                IM_COL32(100, 90, 70, 200), 2.0f);

                    x_cursor += NODE_SPACING;
                }
            }

            /* ── Pedal gallery popup (500x400) ────────────────────── */
            /* Helper lambda to add a pedal from the gallery */
            auto gallery_add_pedal = [&](fx_pedal_type_t ptype) {
                fx_pedal_id nid = fx_chain_add_pedal(engine, ptype, s_add_popup_pos);
                if (nid >= 0) {
                    if (s_add_popup_pos == FX_CHAIN_POS_PRE && s_pre_id_count < 32) {
                        int slot = s_add_popup_insert_slot;
                        if (slot > s_pre_id_count) slot = s_pre_id_count;
                        for (int j = s_pre_id_count; j > slot; j--)
                            s_pre_ids[j] = s_pre_ids[j - 1];
                        s_pre_ids[slot] = nid;
                        s_pre_id_count++;
                        fx_chain_move_pedal(engine, nid, FX_CHAIN_POS_PRE, slot);
                    } else if (s_add_popup_pos == FX_CHAIN_POS_POST && s_post_id_count < 32) {
                        int slot = s_add_popup_insert_slot;
                        if (slot > s_post_id_count) slot = s_post_id_count;
                        for (int j = s_post_id_count; j > slot; j--)
                            s_post_ids[j] = s_post_ids[j - 1];
                        s_post_ids[slot] = nid;
                        s_post_id_count++;
                        fx_chain_move_pedal(engine, nid, FX_CHAIN_POS_POST, slot);
                    }
                }
            };

            ImGui::SetNextWindowSize(ImVec2(520, 420), ImGuiCond_Always);
            if (ImGui::BeginPopup("add_pedal_popup",
                                  ImGuiWindowFlags_NoResize)) {
                /* Header */
                {
                    const char *title = (s_add_popup_pos == FX_CHAIN_POS_PRE)
                        ? "Add Pedal  [PRE-AMP]"
                        : "Add Pedal  [POST-AMP]";
                    ImGui::PushStyleColor(ImGuiCol_Text,
                        ImVec4(0.90f, 0.65f, 0.20f, 1.0f));
                    ImGui::SetWindowFontScale(1.15f);
                    ImGui::Text("%s", title);
                    ImGui::SetWindowFontScale(1.0f);
                    ImGui::PopStyleColor();
                }
                ImGui::Separator();
                ImGui::Spacing();

                /* 3-column grid of categories */
                static const int COLS = 3;
                float col_w = (520.0f - 30.0f) / COLS;

                if (ImGui::BeginChild("##gallery_scroll", ImVec2(0, 0),
                                      false, ImGuiWindowFlags_HorizontalScrollbar)) {
                    if (ImGui::BeginTable("##gallery_table", COLS,
                                          ImGuiTableFlags_SizingFixedFit)) {
                        for (int ci = 0; ci < s_pedal_category_count; ci++) {
                            ImGui::TableNextColumn();
                            const PedalCategory &cat = s_pedal_categories[ci];

                            /* Category header */
                            ImGui::PushStyleColor(ImGuiCol_Text,
                                ImVec4(0.80f, 0.60f, 0.18f, 1.0f));
                            ImGui::TextUnformatted(cat.label);
                            ImGui::PopStyleColor();
                            ImGui::PushStyleColor(ImGuiCol_Separator,
                                ImVec4(0.50f, 0.35f, 0.10f, 0.6f));
                            ImGui::Separator();
                            ImGui::PopStyleColor();

                            /* Pedal rows */
                            for (int pi = 0; pi < cat.count; pi++) {
                                const PedalEntry &pe = cat.pedals[pi];

                                /* Build unique selectable ID */
                                char sel_id[64];
                                snprintf(sel_id, sizeof(sel_id),
                                         "%s##gal_%d_%d", pe.name, ci, pi);

                                ImGui::PushStyleColor(ImGuiCol_Header,
                                    ImVec4(0.25f, 0.18f, 0.06f, 1.0f));
                                ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
                                    ImVec4(0.60f, 0.42f, 0.10f, 1.0f));
                                ImGui::PushStyleColor(ImGuiCol_HeaderActive,
                                    ImVec4(0.80f, 0.55f, 0.12f, 1.0f));

                                if (ImGui::Selectable(sel_id, false,
                                                      ImGuiSelectableFlags_None,
                                                      ImVec2(col_w - 8.0f, 0))) {
                                    gallery_add_pedal(pe.type);
                                    ImGui::CloseCurrentPopup();
                                }
                                ImGui::PopStyleColor(3);
                            }
                            ImGui::Spacing();
                        }
                        ImGui::EndTable();
                    }
                    ImGui::EndChild();
                }
                ImGui::EndPopup();
            }

            ImGui::End();
        }

        /* ============================================================
         * DETAIL VIEW (~50% of window)
         * Shows knobs/controls for the selected node
         * ============================================================ */
        {
            float chain_area_h = (win_h - TOOLBAR_H - STATUS_H) * 0.35f;
            float detail_y = TOOLBAR_H + chain_area_h;
            float detail_h = win_h - detail_y - STATUS_H;

            ImGui::SetNextWindowPos(ImVec2(0, detail_y));
            ImGui::SetNextWindowSize(ImVec2(win_w, detail_h));
            ImGui::Begin("##detail_view", NULL,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_HorizontalScrollbar);

            if (s_selected_node < 0 || s_selected_node >= chain_len) {
                /* Nothing selected */
                float avail_w = ImGui::GetContentRegionAvail().x;
                float avail_h = ImGui::GetContentRegionAvail().y;
                const char *msg = "Click a node in the signal chain to edit";
                ImVec2 ts = ImGui::CalcTextSize(msg);
                ImGui::SetCursorPos(ImVec2((avail_w - ts.x) * 0.5f, (avail_h - ts.y) * 0.5f));
                ImGui::TextDisabled("%s", msg);
            }
            else {
                ChainNode &sel = chain[s_selected_node];

                /* ── AMP detail view ──────────────────────────── */
                if (sel.kind == NODE_AMP) {
                    fx_amp_type_t amp_type = fx_amp_get_model(engine, FX_CHAIN_DEFAULT);

                    static const char *amp_names[] = {
                        "Fullerton Clean", "British Crunch", "Southwest Lead",
                        "Essex Chime", "Tweed Blues"
                    };
                    int current_amp = (int)amp_type;
                    float avail_w = ImGui::GetContentRegionAvail().x;

                    /* Title */
                    {
                        const char *amp_name = fx_amp_get_type_name(amp_type);
                        ImVec2 text_size = ImGui::CalcTextSize(amp_name);
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_w - text_size.x) * 0.5f);
                        ImGui::TextColored(ImVec4(0.90f, 0.65f, 0.20f, 1.0f), "%s", amp_name);
                    }

                    ImGui::Separator();
                    ImGui::Dummy(ImVec2(0.0f, 4.0f));

                    /* Model selector */
                    ImGui::SetNextItemWidth(200);
                    if (ImGui::Combo("Model", &current_amp, amp_names, FX_AMP_COUNT)) {
                        fx_amp_set_model(engine, FX_CHAIN_DEFAULT, (fx_amp_type_t)current_amp);
                    }

                    ImGui::Dummy(ImVec2(0.0f, 8.0f));

                    /* Knob layout: PREAMP | EQ | POWER */
                    int param_count = fx_amp_get_param_count(amp_type);
                    auto has_param = [&](fx_amp_param_t p) -> bool {
                        return (int)p < param_count;
                    };

                    /* Estimate knob width (knob_float uses ~60px per knob including label) */
                    const float KNOB_W   = 66.0f;  /* approx width per knob */
                    const float SEP_W    = 20.0f;  /* vsep width */
                    const float PAD_W    =  6.0f;  /* per-knob leading dummy */

                    int n_preamp = has_param(FX_AMP_PARAM_GAIN) ? 1 : 0;
                    int n_eq     = (has_param(FX_AMP_PARAM_BASS)   ? 1 : 0)
                                 + (has_param(FX_AMP_PARAM_MID)    ? 1 : 0)
                                 + (has_param(FX_AMP_PARAM_TREBLE) ? 1 : 0)
                                 + (has_param(FX_AMP_PARAM_CUT)    ? 1 : 0);
                    int n_power  = (has_param(FX_AMP_PARAM_PRESENCE) ? 1 : 0)
                                 + (has_param(FX_AMP_PARAM_SAG)      ? 1 : 0)
                                 + (has_param(FX_AMP_PARAM_MASTER)   ? 1 : 0)
                                 + (has_param(FX_AMP_PARAM_VOLUME)   ? 1 : 0)
                                 + (has_param(FX_AMP_PARAM_BRIGHT)   ? 1 : 0);

                    float total_knob_w = (n_preamp + n_eq + n_power) * (KNOB_W + PAD_W)
                                       + 2.0f * SEP_W; /* two separators */
                    float knob_start_x = (avail_w - total_knob_w) * 0.5f;
                    if (knob_start_x < 0.0f) knob_start_x = 0.0f;
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + knob_start_x);

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

                    auto draw_vsep = [&]() {
                        ImGui::SameLine(0, 0);
                        ImVec2 p0 = ImGui::GetCursorScreenPos();
                        p0.x += 6.0f;
                        ImVec2 p1 = ImVec2(p0.x, p0.y + 130.0f);
                        ImGui::GetWindowDrawList()->AddLine(p0, p1,
                            IM_COL32(100, 88, 70, 180), 1.0f);
                        ImGui::SameLine(0, 18);
                    };

                    /* PREAMP section */
                    ImGui::BeginGroup();
                    ImGui::TextDisabled("PREAMP");
                    ImGui::Dummy(ImVec2(0.0f, 2.0f));
                    draw_amp_knob(FX_AMP_PARAM_GAIN);
                    ImGui::NewLine();
                    ImGui::EndGroup();

                    draw_vsep();

                    /* EQ section */
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

                    /* POWER section */
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
                }

                /* ── CAB detail view ──────────────────────────── */
                else if (sel.kind == NODE_CAB) {
                    float avail_w = ImGui::GetContentRegionAvail().x;

                    const char *title = "Cabinet";
                    ImVec2 ts = ImGui::CalcTextSize(title);
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_w - ts.x) * 0.5f);
                    ImGui::TextColored(ImVec4(0.90f, 0.65f, 0.20f, 1.0f), "%s", title);
                    ImGui::Separator();
                    ImGui::Dummy(ImVec2(0.0f, 8.0f));

                    /* Cab type selector */
                    static int s_cab_type = 0;
                    ImGui::SetNextItemWidth(200);
                    if (ImGui::Combo("Cab Type", &s_cab_type, s_cab_type_names, FX_CAB_TYPE_COUNT)) {
                        fx_cab_params_t params;
                        params.cab_type = (fx_cab_type_t)s_cab_type;
                        params.mic_pos  = FX_MIC_ON_AXIS;
                        params.speaker_fs = 80.0f;
                        params.brightness = 0.5f;
                        params.resonance  = 0.5f;
                        fx_cab_generate_ir(engine, FX_CHAIN_DEFAULT, &params);
                    }

                    ImGui::Dummy(ImVec2(0.0f, 8.0f));

                    /* Bypass toggle */
                    bool cab_bypassed = fx_cab_get_bypass(engine, FX_CHAIN_DEFAULT);
                    ImGui::PushStyleColor(ImGuiCol_Button,
                        cab_bypassed ? ImVec4(0.45f, 0.08f, 0.08f, 1.0f)
                                     : ImVec4(0.08f, 0.40f, 0.08f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                        cab_bypassed ? ImVec4(0.60f, 0.12f, 0.12f, 1.0f)
                                     : ImVec4(0.12f, 0.55f, 0.12f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                        cab_bypassed ? ImVec4(0.75f, 0.18f, 0.18f, 1.0f)
                                     : ImVec4(0.18f, 0.70f, 0.18f, 1.0f));
                    if (ImGui::Button(cab_bypassed ? "BYPASSED##cab" : "ACTIVE##cab",
                                      ImVec2(120, 30))) {
                        fx_cab_set_bypass(engine, FX_CHAIN_DEFAULT, !cab_bypassed);
                    }
                    ImGui::PopStyleColor(3);
                }

                /* ── PEDAL detail view ────────────────────────── */
                else if (sel.kind == NODE_PEDAL_PRE || sel.kind == NODE_PEDAL_POST) {
                    fx_pedal_id pid = sel.pedal_id;
                    fx_pedal_type_t pt = fx_pedal_get_type(engine, pid);
                    if (pt < FX_PEDAL_TYPE_COUNT) {
                        const char *pname = fx_pedal_get_type_name(pt);
                        int nparam = fx_pedal_get_param_count(pt);
                        bool bypassed = fx_pedal_get_bypass(engine, pid);
                        float avail_w = ImGui::GetContentRegionAvail().x;

                        /* Title */
                        {
                            ImVec2 ts = ImGui::CalcTextSize(pname);
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_w - ts.x) * 0.5f);
                            if (bypassed) ImGui::TextDisabled("%s", pname);
                            else ImGui::TextColored(ImVec4(0.90f, 0.65f, 0.20f, 1.0f), "%s", pname);
                        }
                        ImGui::Separator();
                        ImGui::Dummy(ImVec2(0.0f, 8.0f));

                        /* Knobs — centered horizontally */
                        {
                            const float KNOB_W  = 66.0f;
                            const float KNOB_PAD = 8.0f;
                            float row_w = nparam * KNOB_W + (nparam > 1 ? (nparam - 1) * KNOB_PAD : 0.0f);
                            float knob_off = (avail_w - row_w) * 0.5f;
                            if (knob_off < 0.0f) knob_off = 0.0f;
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + knob_off);
                        }
                        for (int k = 0; k < nparam; k++) {
                            const char *kname = fx_pedal_get_param_name(pt, k);
                            float kval = fx_pedal_get_param(engine, pid, k);
                            ImGui::Dummy(ImVec2(8.0f, 0.0f)); ImGui::SameLine();
                            if (knob_float(kname, &kval, 0.0f, 1.0f, 0.5f, 0.01f))
                                fx_pedal_set_param(engine, pid, k, kval);
                            if (k < nparam - 1) ImGui::SameLine();
                        }

                        ImGui::Dummy(ImVec2(0.0f, 12.0f));

                        /* Bottom row: bypass + reorder + remove */
                        {
                            char bp_id[48];
                            snprintf(bp_id, sizeof(bp_id), "%s##bp_detail",
                                     bypassed ? "BYPASS" : "ACTIVE");
                            ImGui::PushStyleColor(ImGuiCol_Button,
                                bypassed ? ImVec4(0.45f, 0.08f, 0.08f, 1.0f)
                                         : ImVec4(0.08f, 0.40f, 0.08f, 1.0f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                bypassed ? ImVec4(0.60f, 0.12f, 0.12f, 1.0f)
                                         : ImVec4(0.12f, 0.55f, 0.12f, 1.0f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                                bypassed ? ImVec4(0.75f, 0.18f, 0.18f, 1.0f)
                                         : ImVec4(0.18f, 0.70f, 0.18f, 1.0f));
                            if (ImGui::Button(bp_id, ImVec2(120, 30)))
                                fx_pedal_set_bypass(engine, pid, !bypassed);
                            ImGui::PopStyleColor(3);

                            ImGui::SameLine(0, 12);

                            /* Reorder arrows */
                            fx_chain_pos_t pos = (sel.kind == NODE_PEDAL_PRE)
                                                  ? FX_CHAIN_POS_PRE : FX_CHAIN_POS_POST;
                            fx_pedal_id *ids = (pos == FX_CHAIN_POS_PRE) ? s_pre_ids : s_post_ids;
                            int *id_count = (pos == FX_CHAIN_POS_PRE) ? &s_pre_id_count : &s_post_id_count;
                            int pi = sel.slot;

                            if (pi > 0) {
                                if (ImGui::Button("<##reorder_l")) {
                                    fx_pedal_id tmp = ids[pi - 1];
                                    ids[pi - 1] = ids[pi]; ids[pi] = tmp;
                                    fx_chain_move_pedal(engine, pid, pos, pi - 1);
                                    s_selected_node--;
                                }
                            } else {
                                ImGui::TextDisabled("<");
                            }
                            ImGui::SameLine();

                            if (pi < *id_count - 1) {
                                if (ImGui::Button(">##reorder_r")) {
                                    fx_pedal_id tmp = ids[pi + 1];
                                    ids[pi + 1] = ids[pi]; ids[pi] = tmp;
                                    fx_chain_move_pedal(engine, pid, pos, pi + 1);
                                    s_selected_node++;
                                }
                            } else {
                                ImGui::TextDisabled(">");
                            }

                            ImGui::SameLine(0, 20);

                            /* Remove button */
                            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.08f, 0.08f, 1.0f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.12f, 0.12f, 1.0f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.9f, 0.15f, 0.15f, 1.0f));
                            if (ImGui::Button("Remove##rm_detail", ImVec2(90, 30))) {
                                fx_chain_remove_pedal(engine, pid);
                                for (int j = pi; j < *id_count - 1; j++)
                                    ids[j] = ids[j + 1];
                                (*id_count)--;
                                s_selected_node = -1;
                            }
                            ImGui::PopStyleColor(3);
                        }
                    }
                }

                /* ── INPUT / OUTPUT — nothing to show ─────────── */
                else {
                    ImGui::TextDisabled("No editable parameters.");
                }
            }

            ImGui::End();
        }

        /* ── Status bar — level meters ────────────────────────── */
        {
            ImGui::SetNextWindowPos(ImVec2(0, win_h - STATUS_H));
            ImGui::SetNextWindowSize(ImVec2(win_w, STATUS_H));
            ImGui::Begin("##status", NULL,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);

            float in_level  = fx_engine_get_input_level(engine);
            float out_level = fx_engine_get_output_level(engine);

            static int s_no_signal_frames = 0;
            const float NO_SIGNAL_THRESHOLD = 0.001f;
            const int   NO_SIGNAL_FRAME_COUNT = 120;
            if (in_level < NO_SIGNAL_THRESHOLD) {
                s_no_signal_frames++;
            } else {
                s_no_signal_frames = 0;
            }
            bool no_signal = (s_no_signal_frames >= NO_SIGNAL_FRAME_COUNT);

            static float s_in_clip_timer  = 0.0f;
            static float s_out_clip_timer = 0.0f;
            float dt = io.DeltaTime;
            if (in_level  > 0.95f) s_in_clip_timer  = 0.5f;
            if (out_level > 0.95f) s_out_clip_timer = 0.5f;
            s_in_clip_timer  = s_in_clip_timer  > 0.0f ? s_in_clip_timer  - dt : 0.0f;
            s_out_clip_timer = s_out_clip_timer > 0.0f ? s_out_clip_timer - dt : 0.0f;

            ImDrawList *dl  = ImGui::GetWindowDrawList();
            ImVec2      win = ImGui::GetWindowPos();
            float bar_h     = 20.0f;
            float bar_w     = 300.0f;
            float bar_y     = win.y + (STATUS_H - bar_h) * 0.5f;

            int num_segs = 20;

            auto draw_meter = [&](float x0, float level, bool clip_flash) {
                /* Dark background panel for contrast */
                float panel_pad = 4.0f;
                dl->AddRectFilled(
                    ImVec2(x0 - panel_pad, bar_y - panel_pad),
                    ImVec2(x0 + bar_w + 11.0f + panel_pad, bar_y + bar_h + panel_pad),
                    IM_COL32(18, 16, 14, 255), 4.0f);

                /* Meter background */
                dl->AddRectFilled(ImVec2(x0, bar_y), ImVec2(x0 + bar_w, bar_y + bar_h),
                                  IM_COL32(30, 28, 24, 255), 3.0f);

                float t = level < 0.0f ? 0.0f : (level > 1.0f ? 1.0f : level);
                float seg_w     = (bar_w - (num_segs - 1) * 1.5f) / (float)num_segs;
                int   lit_segs  = (int)(t * num_segs + 0.5f);
                for (int s = 0; s < num_segs; s++) {
                    float sx0 = x0 + s * (seg_w + 1.5f);
                    float sx1 = sx0 + seg_w;
                    if (s < lit_segs) {
                        ImU32 col;
                        if (s < 14) {
                            col = IM_COL32(40, 200, 60, 255);
                        } else if (s < 18) {
                            int r = 180 + (s - 14) * 15;
                            col = IM_COL32(r, 200, 20, 255);
                        } else {
                            col = IM_COL32(230, 40, 30, 255);
                        }
                        dl->AddRectFilled(ImVec2(sx0, bar_y), ImVec2(sx1, bar_y + bar_h),
                                          col, 1.5f);
                    } else {
                        dl->AddRectFilled(ImVec2(sx0, bar_y), ImVec2(sx1, bar_y + bar_h),
                                          IM_COL32(40, 40, 36, 255), 1.5f);
                    }
                }

                /* 3dB tick marks every 3 segments */
                for (int s = 3; s < num_segs; s += 3) {
                    float tick_x = x0 + s * (seg_w + 1.5f) - 0.75f;
                    dl->AddLine(ImVec2(tick_x, bar_y),
                                ImVec2(tick_x, bar_y + 5.0f),
                                IM_COL32(120, 110, 90, 160), 1.0f);
                    dl->AddLine(ImVec2(tick_x, bar_y + bar_h - 5.0f),
                                ImVec2(tick_x, bar_y + bar_h),
                                IM_COL32(120, 110, 90, 160), 1.0f);
                }

                /* Clip indicator */
                float cx0 = x0 + bar_w + 3.0f;
                float cx1 = cx0 + 8.0f;
                ImU32 clip_col = clip_flash
                    ? IM_COL32(255, 30, 20, 255)
                    : IM_COL32(60, 20, 18, 255);
                dl->AddRectFilled(ImVec2(cx0, bar_y), ImVec2(cx1, bar_y + bar_h),
                                  clip_col, 2.0f);
            };

            /* Label font scale for larger IN/OUT text */
            float label_scale = 1.3f;

            /* Left: input meter */
            float left_x = 10.0f;
            ImGui::SetCursorPosX(left_x);
            ImGui::SetCursorPosY((STATUS_H - ImGui::GetTextLineHeight() * label_scale) * 0.5f);
            ImGui::SetWindowFontScale(label_scale);
            ImGui::TextDisabled("IN");
            ImGui::SetWindowFontScale(1.0f);
            ImGui::SameLine(0, 6);

            ImVec2 in_meter_pos = ImGui::GetCursorScreenPos();
            in_meter_pos.y = bar_y;
            draw_meter(in_meter_pos.x, in_level, s_in_clip_timer > 0.0f);
            ImGui::Dummy(ImVec2(bar_w + 14.0f, bar_h));

            /* Center: NO SIGNAL text */
            if (no_signal) {
                const char *ns_text = "NO SIGNAL";
                float text_w = ImGui::CalcTextSize(ns_text).x;
                float cx = (win_w - text_w) * 0.5f;
                float cy = (STATUS_H - ImGui::GetTextLineHeight()) * 0.5f;
                ImGui::SetCursorPos(ImVec2(cx, cy));
                ImGui::TextColored(ImVec4(0.55f, 0.50f, 0.40f, 0.7f), "%s", ns_text);
            }

            /* Right: output meter */
            {
                float right_margin  = 10.0f;
                float clip_w        = 8.0f + 3.0f;
                float label_txt_w   = ImGui::CalcTextSize("OUT").x * label_scale + 6.0f;
                float out_x         = win_w - right_margin - clip_w - bar_w - label_txt_w;

                ImGui::SetCursorPos(ImVec2(out_x, (STATUS_H - ImGui::GetTextLineHeight() * label_scale) * 0.5f));
                ImGui::SetWindowFontScale(label_scale);
                ImGui::TextDisabled("OUT");
                ImGui::SetWindowFontScale(1.0f);
                ImGui::SameLine(0, 6);

                ImVec2 out_meter_pos = ImGui::GetCursorScreenPos();
                out_meter_pos.y = bar_y;
                draw_meter(out_meter_pos.x, out_level, s_out_clip_timer > 0.0f);
                ImGui::Dummy(ImVec2(bar_w + clip_w, bar_h));
            }

            ImGui::End();
        }

        /* ── Window border + resize grip (borderless) ──────── */
        {
            ImDrawList *fg = ImGui::GetForegroundDrawList();
            ImU32 border_col = IM_COL32(50, 45, 38, 200);
            fg->AddRect(ImVec2(0, 0),
                        ImVec2(win_w, win_h),
                        border_col, 0.0f, 0, 2.0f);
            float gs = 14.0f;
            float bx = win_w, by = win_h;
            ImU32 grip_col = IM_COL32(80, 72, 58, 180);
            fg->AddTriangleFilled(
                ImVec2(bx - gs, by), ImVec2(bx, by - gs), ImVec2(bx, by), grip_col);
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
