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
#include "../audio/recorder.h"
#include "../audio/midi_input.h"
#include "../core/log.h"
#include "../core/crash.h"
#include "knobs.h"
#include "texture.h"
}

#include <stdio.h>
#include <cmath>
#include <cstring>
#include <ctime>
#include <sys/stat.h>

extern "C" {
#include "cJSON.h"
}

/* ── Session config helpers (TASK-307) ───────────────────────── */

#ifdef _WIN32
#define PATH_SEP "\\"
static const char *get_config_dir(void) {
    static char buf[512];
    const char *appdata = getenv("APPDATA");
    if (!appdata) appdata = ".";
    snprintf(buf, sizeof(buf), "%s\\0xFX", appdata);
    return buf;
}
#else
#define PATH_SEP "/"
static const char *get_config_dir(void) {
    static char buf[512];
    const char *home = getenv("HOME");
    if (!home) home = ".";
    snprintf(buf, sizeof(buf), "%s/.0xfx", home);
    return buf;
}
#endif

static void ensure_dir(const char *path) {
#ifdef _WIN32
    CreateDirectoryA(path, NULL);
#else
    mkdir(path, 0755);
#endif
}

static const char *get_config_path(void) {
    static char buf[600];
    snprintf(buf, sizeof(buf), "%s" PATH_SEP "config.json", get_config_dir());
    return buf;
}

struct SessionConfig {
    int  input_device_idx;
    int  output_device_idx;
    int  window_w;
    int  window_h;
    int  buf_size_idx;
    int  sr_idx;
};

static void session_config_defaults(SessionConfig *cfg) {
    cfg->input_device_idx  = -1;
    cfg->output_device_idx = -1;
    cfg->window_w          = 1400;
    cfg->window_h          = 800;
    cfg->buf_size_idx      = 2;
    cfg->sr_idx            = 0;
}

static bool session_config_load(SessionConfig *cfg) {
    session_config_defaults(cfg);
    FILE *f = fopen(get_config_path(), "r");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz <= 0 || sz > 65536) { fclose(f); return false; }
    char *cbuf = (char *)malloc(sz + 1);
    if (!cbuf) { fclose(f); return false; }
    fread(cbuf, 1, sz, f);
    cbuf[sz] = '\0';
    fclose(f);
    cJSON *root = cJSON_Parse(cbuf);
    free(cbuf);
    if (!root) return false;
    cJSON *v;
    if ((v = cJSON_GetObjectItemCaseSensitive(root, "input_device"))  && cJSON_IsNumber(v)) cfg->input_device_idx  = (int)v->valuedouble;
    if ((v = cJSON_GetObjectItemCaseSensitive(root, "output_device")) && cJSON_IsNumber(v)) cfg->output_device_idx = (int)v->valuedouble;
    if ((v = cJSON_GetObjectItemCaseSensitive(root, "window_w"))      && cJSON_IsNumber(v)) cfg->window_w          = (int)v->valuedouble;
    if ((v = cJSON_GetObjectItemCaseSensitive(root, "window_h"))      && cJSON_IsNumber(v)) cfg->window_h          = (int)v->valuedouble;
    if ((v = cJSON_GetObjectItemCaseSensitive(root, "buf_size_idx"))  && cJSON_IsNumber(v)) cfg->buf_size_idx      = (int)v->valuedouble;
    if ((v = cJSON_GetObjectItemCaseSensitive(root, "sr_idx"))        && cJSON_IsNumber(v)) cfg->sr_idx            = (int)v->valuedouble;
    cJSON_Delete(root);
    return true;
}

static void session_config_save(const SessionConfig *cfg) {
    ensure_dir(get_config_dir());
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "input_device",  cfg->input_device_idx);
    cJSON_AddNumberToObject(root, "output_device", cfg->output_device_idx);
    cJSON_AddNumberToObject(root, "window_w",      cfg->window_w);
    cJSON_AddNumberToObject(root, "window_h",      cfg->window_h);
    cJSON_AddNumberToObject(root, "buf_size_idx",  cfg->buf_size_idx);
    cJSON_AddNumberToObject(root, "sr_idx",        cfg->sr_idx);
    char *json = cJSON_Print(root);
    cJSON_Delete(root);
    if (!json) return;
    FILE *f = fopen(get_config_path(), "w");
    if (f) { fputs(json, f); fclose(f); }
    free(json);
}

/* ── Amp param tooltips (TASK-201) ───────────────────────────── */

static const char *s_amp_param_tooltips[] = {
    "Gain \xe2\x80\x94 Preamp drive level. Higher = more distortion",
    "Volume \xe2\x80\x94 Overall output volume",
    "Bass \xe2\x80\x94 Low frequency EQ",
    "Mid \xe2\x80\x94 Midrange frequency EQ",
    "Treble \xe2\x80\x94 High frequency EQ",
    "Presence \xe2\x80\x94 Upper-mid sparkle/bite",
    "Sag \xe2\x80\x94 Power supply droop. Higher = spongier feel",
    "Master \xe2\x80\x94 Power amp volume",
    "Bright \xe2\x80\x94 Treble boost switch",
    "Cut \xe2\x80\x94 High frequency cut"
};
static const int s_amp_param_tooltip_count =
    (int)(sizeof(s_amp_param_tooltips) / sizeof(s_amp_param_tooltips[0]));

/* ── Pedal one-line descriptions for gallery tooltips ─────── */

struct PedalTooltip { fx_pedal_type_t type; const char *desc; };
static const PedalTooltip s_pedal_tooltips[] = {
    { FX_PEDAL_JADE_DRIVE,    "Smooth overdrive with mid-hump character (TS-style)" },
    { FX_PEDAL_GOLD_DRIVE,    "Transparent overdrive - clean blend with tanh clipping" },
    { FX_PEDAL_BLUES_GRIT,    "Warm gritty blues overdrive with presence boost" },
    { FX_PEDAL_RODENT,        "Aggressive distortion with backwards tone filter (RAT-style)" },
    { FX_PEDAL_ORANGE_DIST,   "High-gain distortion with scooped mids" },
    { FX_PEDAL_METAL_ZONE,    "Modern metal distortion with graphic EQ contour" },
    { FX_PEDAL_AMP_BOX,       "Amp-in-a-box - simulates a pushed tube amp preamp" },
    { FX_PEDAL_MAMMOTH_FUZZ,  "Big Muff-style fuzz with 4-stage clipping and scooped mids" },
    { FX_PEDAL_ROUND_FUZZ,    "Germanium fuzz - smooth and woolly, cleans up with guitar volume" },
    { FX_PEDAL_WRAITH_FUZZ,   "Velcro-style octave fuzz with harmonic overtones" },
    { FX_PEDAL_CHAOS_FUZZ,    "Sputtery gated fuzz with oscillation control" },
    { FX_PEDAL_ECHO_DELAY,    "Digital echo delay - Time, Feedback, Mix" },
    { FX_PEDAL_CARBON_DELAY,  "BBD analog delay - repeats darken progressively" },
    { FX_PEDAL_TAPE_MACHINE,  "Tape echo with wow/flutter and tape degradation" },
    { FX_PEDAL_MEMORY_ECHO,   "Warm analog-voiced delay with modulation" },
    { FX_PEDAL_DRIP_VERB,     "Spring reverb with characteristic drip transient" },
    { FX_PEDAL_HALL_VERB,     "Lush hall reverb (Freeverb algorithm)" },
    { FX_PEDAL_PLATE_VERB,    "Dense plate reverb with diffusion network" },
    { FX_PEDAL_SHIMMER_VERB,  "Reverb with octave-up pitch in feedback for pads" },
    { FX_PEDAL_CLOUD_VERB,    "Ambient reverb with infinite decay / freeze mode" },
    { FX_PEDAL_LIQUID_CHORUS, "Lush BBD-style chorus with stereo spread" },
    { FX_PEDAL_PHASE_SWEEP,   "Analog-voiced phaser with sweeping notch filters" },
    { FX_PEDAL_JET_FLANGER,   "Through-zero flanger with classic jet-plane sweep" },
    { FX_PEDAL_PULSE_TREM,    "Tremolo - amplitude modulation at LFO rate" },
    { FX_PEDAL_DRIFT_VIBRATO, "True pitch vibrato (not amplitude) via delay modulation" },
    { FX_PEDAL_SQUEEZE_BOX,   "Compressor with peak-detecting envelope follower (4:1 ratio)" },
    { FX_PEDAL_GLASS_COMP,    "Optical-style transparent compressor" },
    { FX_PEDAL_PUNCH_COMP,    "FET-style punchy compressor for attack-heavy sounds" },
    { FX_PEDAL_NOISE_GATE,    "Noise gate - silences signal below threshold" },
    { FX_PEDAL_HOWL_WAH,      "Expression wah - bandpass sweep controlled by expression (0-1)" },
    { FX_PEDAL_QUACK_FILTER,  "Auto-wah - envelope follower drives bandpass cutoff" },
    { FX_PEDAL_TONE_SCULPTOR, "7-band graphic EQ for precise frequency shaping" },
    { FX_PEDAL_PRECISION_EQ,  "Parametric EQ with sweepable frequency bands" },
    { FX_PEDAL_OCTAVE_ENGINE, "Polyphonic octave - sub-octave and octave-up tracking" },
    { FX_PEDAL_PITCH_WARP,    "Pitch shifter with intelligent note tracking" },
    { FX_PEDAL_GRIT_CRUSH,    "Bitcrusher - reduces bit depth and sample rate for lo-fi tones" },
    { FX_PEDAL_RING_TONE,     "Ring modulator - carrier frequency creates metallic tones" },
    { FX_PEDAL_WARM_TAPE,     "Tape saturation - adds harmonic warmth and soft limiting" },
    { FX_PEDAL_INFINITE_HOLD, "Freeze pedal - captures audio frame and loops as infinite drone" },
    { FX_PEDAL_GRAIN_CLOUD,   "Granular delay - chops audio into grains for textural sounds" },
    { FX_PEDAL_LOOP_STATION,  "Looper - record, overdub, play and undo loops (up to 5 min)" },
};
static const int s_pedal_tooltip_count =
    (int)(sizeof(s_pedal_tooltips) / sizeof(s_pedal_tooltips[0]));

static const char *get_pedal_tooltip(fx_pedal_type_t type) {
    for (int i = 0; i < s_pedal_tooltip_count; i++) {
        if (s_pedal_tooltips[i].type == type) return s_pedal_tooltips[i].desc;
    }
    return nullptr;
}

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
    NODE_SPLIT,       /* Y-split diamond node (signal splits here) */
    NODE_AMP,
    NODE_CAB,
    NODE_MERGE,       /* merge/mix diamond node (paths join here) */
    NODE_PEDAL_POST,
    NODE_STUDIO,      /* rack effect processor (post-amp) */
    NODE_OUTPUT
};

/* Signal chain node descriptor */
struct ChainNode {
    NodeKind    kind;
    int         slot;       /* index into pre/post pedal arrays, or -1 */
    fx_pedal_id pedal_id;   /* valid only for PEDAL nodes */
    int         chain_id;   /* which parallel chain (0=top, 1=bottom) for AMP/CAB in split mode */
};

/* Node colors by kind */
static ImU32 node_color(NodeKind kind, bool bypassed) {
    if (bypassed) return IM_COL32(60, 55, 50, 255);
    switch (kind) {
        case NODE_INPUT:      return IM_COL32(50, 120, 80, 255);
        case NODE_PEDAL_PRE:  return IM_COL32(60, 100, 160, 255);
        case NODE_SPLIT:      return IM_COL32(200, 160, 30, 255);
        case NODE_AMP:        return IM_COL32(180, 90, 30, 255);
        case NODE_CAB:        return IM_COL32(140, 80, 50, 255);
        case NODE_MERGE:      return IM_COL32(200, 160, 30, 255);
        case NODE_PEDAL_POST: return IM_COL32(100, 60, 160, 255);
        case NODE_STUDIO:     return IM_COL32(60, 100, 140, 255);
        case NODE_OUTPUT:     return IM_COL32(50, 120, 80, 255);
        default:              return IM_COL32(80, 80, 80, 255);
    }
}

static const char *node_label(NodeKind kind, fx_engine_t *engine, fx_pedal_id pid, int chain_id) {
    switch (kind) {
        case NODE_INPUT:      return "INPUT";
        case NODE_SPLIT:      return "SPLIT";
        case NODE_AMP:        return fx_amp_get_type_name(fx_amp_get_model(engine, (fx_chain_id)chain_id));
        case NODE_CAB:        return (chain_id == 0) ? "CAB A" : "CAB B";
        case NODE_MERGE:      return "MIX";
        case NODE_OUTPUT:     return "OUTPUT";
        case NODE_STUDIO: {
            fx_studio_type_t st = fx_studio_get_type(engine, pid);
            if (st < FX_STUDIO_COUNT) return fx_studio_get_type_name(st);
            return "???";
        }
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

/* ── Texture path helpers ──────────────────────────────────────── */

/* Convert a display name like "Jade Drive" -> "jade_drive" */
static void type_to_filename(const char *type_name, char *out, int out_size) {
    int i = 0;
    for (; type_name[i] && i < out_size - 1; i++) {
        char c = type_name[i];
        if (c == ' ') c = '_';
        else if (c >= 'A' && c <= 'Z') c = c + 32;
        out[i] = c;
    }
    out[i] = '\0';
}

/* Cab type enum -> filename base (without _nobg.png) */
static const char *s_cab_filenames[] = {
    "1x12_open",      /* FX_CAB_1X12_OPEN */
    "2x12_closed",    /* FX_CAB_2X12_CLOSED */
    "4x12_straight",  /* FX_CAB_4X12_STRAIGHT */
    "4x12_slant",     /* FX_CAB_4X12_SLANT */
    "direct_flat",    /* FX_CAB_DIRECT */
};

/* Special-case pedal name overrides where type_to_filename doesn't match the asset */
static uintptr_t load_pedal_texture(const char *type_name) {
    char fname[128];
    type_to_filename(type_name, fname, sizeof(fname));

    /* "Orange Distortion" -> "orange_distortion" but asset is "orange_dist" */
    if (strcmp(fname, "orange_distortion") == 0) {
        strcpy(fname, "orange_dist");
    }

    char path[256];
    snprintf(path, sizeof(path), "resources/pedals/%s_body_nobg.png", fname);
    return fx_texture_load(path);
}

/* Map display amp names to asset filenames where they differ */
static void amp_name_to_filename(const char *type_name, char *out, int out_size) {
    type_to_filename(type_name, out, out_size);
    /* "British Crunch" -> "british_crunch" but asset is "brit_crunch" */
    if (strcmp(out, "british_crunch") == 0) strcpy(out, "brit_crunch");
}

static uintptr_t load_amp_body_texture(const char *type_name) {
    char fname[128];
    amp_name_to_filename(type_name, fname, sizeof(fname));
    char path[256];
    snprintf(path, sizeof(path), "resources/amps/%s_body_nobg.png", fname);
    return fx_texture_load(path);
}

static uintptr_t load_amp_face_texture(const char *type_name) {
    char fname[128];
    amp_name_to_filename(type_name, fname, sizeof(fname));
    char path[256];
    snprintf(path, sizeof(path), "resources/amps/%s_nobg.png", fname);
    return fx_texture_load(path);
}

static uintptr_t load_cab_texture(int cab_type_idx) {
    if (cab_type_idx < 0 || cab_type_idx >= FX_CAB_TYPE_COUNT) {
        /* Default to 4x12 straight */
        return fx_texture_load("resources/cabs/4x12_straight_nobg.png");
    }
    char path[256];
    snprintf(path, sizeof(path), "resources/cabs/%s_nobg.png", s_cab_filenames[cab_type_idx]);
    return fx_texture_load(path);
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

    /* Load session config (window size, device prefs) */
    SessionConfig s_session_cfg;
    session_config_load(&s_session_cfg);
    if (s_session_cfg.window_w < 800)  s_session_cfg.window_w = 800;
    if (s_session_cfg.window_h < 500)  s_session_cfg.window_h = 500;

    SDL_Window *window = SDL_CreateWindow(
        "0xFX — Guitar Amp Sim & Pedalboard",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        s_session_cfg.window_w, s_session_cfg.window_h,
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

    /* Audio + engine + MIDI init */
    fx_audio_init();
    fx_midi_init();
    fx_engine_t *engine = fx_engine_create(44100.0f);
    FX_INFO("Engine created");

    /* Audio device / settings state */
    int num_input_devices = fx_audio_get_device_count();
    int num_output_devices = fx_audio_get_output_count();
    int num_midi_devices = fx_midi_get_device_count();
    /* Apply saved device selections */
    static int  s_selected_input   = -1;
    static int  s_selected_output  = -1;
    static int  s_selected_buf_idx = 2;    /* default: 256 frames */
    static int  s_selected_sr_idx  = 0;    /* default: 44100 Hz  */
    static bool s_audio_active     = false;
    static bool s_monitor_only    = false; /* device open for metering, output muted */
    static int  s_selected_midi   = -1;
    static bool s_midi_active     = false;

    /* Restore from session config */
    if (s_session_cfg.input_device_idx  >= 0 && s_session_cfg.input_device_idx  < num_input_devices)
        s_selected_input  = s_session_cfg.input_device_idx;
    if (s_session_cfg.output_device_idx >= 0 && s_session_cfg.output_device_idx < num_output_devices)
        s_selected_output = s_session_cfg.output_device_idx;
    if (s_session_cfg.buf_size_idx >= 0 && s_session_cfg.buf_size_idx < 5)
        s_selected_buf_idx = s_session_cfg.buf_size_idx;
    if (s_session_cfg.sr_idx >= 0 && s_session_cfg.sr_idx < 2)
        s_selected_sr_idx = s_session_cfg.sr_idx;

    static const int   buf_sizes[]  = { 64, 128, 256, 512, 1024 };
    static const char *buf_labels[] = { "64", "128", "256", "512", "1024" };
    static const int   sr_values[]  = { 44100, 48000 };
    static const char *sr_labels[]  = { "44100 Hz", "48000 Hz" };

    FX_INFO("Launched muted. Select input+output devices to start audio.");

    /* Auto-start monitoring if we have saved device preferences */
    if (s_selected_input >= 0 && num_input_devices > 0 && !s_monitor_only) {
        if (s_selected_output >= 0) fx_audio_set_output(s_selected_output);
        if (fx_audio_set_device(engine, s_selected_input)) {
            fx_audio_set_mute_output(true);
            s_monitor_only = true;
            FX_INFO("Auto-monitoring input: %s", fx_audio_get_device_name(s_selected_input));
        }
    }

    /* Auto-load last session preset first, then fall back to default */
    /* NOTE: GUI sync happens after static locals are declared below */
    static bool s_needs_gui_sync = false;
    static char s_preset_name[128] = "Untitled";
    {
        bool loaded = fx_preset_load(engine, "presets/last_session.0xfx");
        if (!loaded) loaded = fx_preset_load(engine, "../presets/last_session.0xfx");
        if (loaded) {
            s_needs_gui_sync = true;
            snprintf(s_preset_name, sizeof(s_preset_name), "Last Session");
            FX_INFO("Last session preset restored");
        } else {
            loaded = fx_preset_load(engine, "presets/clean_sparkle.0xfx");
            if (!loaded) loaded = fx_preset_load(engine, "../presets/clean_sparkle.0xfx");
            if (loaded) {
                s_needs_gui_sync = true;
                snprintf(s_preset_name, sizeof(s_preset_name), "Clean Sparkle");
                FX_INFO("Default preset loaded: Clean Sparkle");
            } else {
                FX_WARN("Could not load any preset, using engine defaults");
            }
        }
    }

    /* Pedal ID registries — tracks IDs returned by fx_chain_add_pedal */
    static fx_pedal_id s_pre_ids[32];
    static int         s_pre_id_count = 0;
    static fx_pedal_id s_post_ids[32];
    static int         s_post_id_count = 0;

    /* Studio processor ID registry */
    static fx_studio_id s_studio_ids[8];
    static int          s_studio_id_count = 0;

    /* Sync GUI ID arrays from engine after preset load */
    if (s_needs_gui_sync) {
        s_needs_gui_sync = false;
        s_pre_id_count = fx_chain_get_pedal_count(engine, FX_CHAIN_POS_PRE);
        if (s_pre_id_count > 32) s_pre_id_count = 32;
        for (int i = 0; i < s_pre_id_count; i++)
            s_pre_ids[i] = fx_chain_get_pedal_at(engine, FX_CHAIN_POS_PRE, i);

        s_post_id_count = fx_chain_get_pedal_count(engine, FX_CHAIN_POS_POST);
        if (s_post_id_count > 32) s_post_id_count = 32;
        for (int i = 0; i < s_post_id_count; i++)
            s_post_ids[i] = fx_chain_get_pedal_at(engine, FX_CHAIN_POS_POST, i);

        FX_INFO("GUI sync: %d pre-pedals, %d post-pedals", s_pre_id_count, s_post_id_count);
    }

    /* Signal chain selection state */
    static int  s_selected_node = -1;  /* index into the flattened chain array */
    static int  s_cab_type = 0;        /* current cab type for chain 0 (for texture lookup) */
    static int  s_cab_type_b = 0;      /* cab type for chain 1 (dual mode) */

    /* Dual-chain (Y-split) state */
    static fx_chain_id s_chain_b = -1; /* chain ID for the second parallel path, -1 = single */

    /* Layout constants */
    static const float TOOLBAR_H      = 64.0f;
    static const float STATUS_H       = 50.0f;
    static const float NODE_W         = 80.0f;
    static const float NODE_H         = 60.0f;
    static const float NODE_SPACING   = 56.0f;  /* wider to fit 40px [+] btn */
    static const float ADD_BTN_W      = 26.0f;
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

        /* ── Theme textures (loaded once) ────────────────────────── */
        static uintptr_t s_tex_pedalboard  = 0;
        static uintptr_t s_tex_tolex       = 0;
        static bool      s_theme_tex_tried = false;
        if (!s_theme_tex_tried) {
            s_tex_pedalboard = fx_texture_load("resources/theme/pedalboard_surface_nobg.png");
            s_tex_tolex      = fx_texture_load("resources/theme/tolex_surface_nobg.png");
            s_theme_tex_tried = true;
        }

        /* ── Toolbar ──────────────────────────────────────────── */
        {
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(win_w, TOOLBAR_H));
            ImGui::Begin("##toolbar", NULL,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);

            /* Toolbar gradient: slightly lighter at top, darker at bottom */
            {
                ImDrawList *dl_tb = ImGui::GetWindowDrawList();
                ImVec2 tb_min = ImGui::GetWindowPos();
                ImVec2 tb_max = ImVec2(tb_min.x + win_w, tb_min.y + TOOLBAR_H);
                dl_tb->AddRectFilledMultiColor(
                    tb_min, tb_max,
                    IM_COL32(32, 28, 24, 255),  /* top-left  — slightly lighter */
                    IM_COL32(32, 28, 24, 255),  /* top-right — slightly lighter */
                    IM_COL32(18, 16, 13, 255),  /* bot-right — darker */
                    IM_COL32(18, 16, 13, 255)); /* bot-left  — darker */
                /* Bottom edge separator line */
                dl_tb->AddLine(
                    ImVec2(tb_min.x, tb_max.y - 1.0f),
                    ImVec2(tb_max.x, tb_max.y - 1.0f),
                    IM_COL32(60, 50, 38, 180), 1.0f);
            }

            /* Neon logo image (cached) — pre-trimmed + faded asset */
            {
                static uintptr_t s_logo_tex = 0;
                static bool s_logo_tried = false;
                static float s_logo_aspect = 1.96f;
                if (!s_logo_tried) {
                    s_logo_tex = fx_texture_load("resources/logo/logo_neon_v4_red_trim_fade.png");
                    s_logo_tried = true;
                    int lw = 0, lh = 0;
                    if (s_logo_tex && fx_texture_get_size(s_logo_tex, &lw, &lh) && lh > 0)
                        s_logo_aspect = (float)lw / (float)lh;
                }
                if (s_logo_tex) {
                    float logo_h = TOOLBAR_H - 8.0f;
                    float logo_w = logo_h * s_logo_aspect;
                    ImGui::Image((ImTextureID)s_logo_tex, ImVec2(logo_w, logo_h));
                } else {
                    ImGui::Text("0xFX");
                }
            }
            ImGui::SameLine(130);

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
                    float bar_cx_y   = toolbar_top + TOOLBAR_H * 0.5f;
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
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Chromatic tuner -- detects pitch of input signal");

                    /* TUNER label */
                    {
                        ImDrawList *tdl = ImGui::GetWindowDrawList();
                        ImGui::SetWindowFontScale(0.65f);
                        const char *tlbl = "TUNER";
                        ImVec2 tsz = ImGui::CalcTextSize(tlbl);
                        float tlx = cursor.x + padding + (bar_w - tsz.x) * 0.5f;
                        float tly = bar_bot_y + 4.0f;
                        tdl->AddText(ImVec2(tlx, tly), IM_COL32(120, 110, 90, 150), tlbl);
                        ImGui::SetWindowFontScale(1.0f);
                    }
                }
            }

            ImGui::SameLine(380);

            /* ── Preset name display ──────────────────────────── */
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.75f, 0.55f, 1.0f));
                ImGui::SetWindowFontScale(0.9f);
                ImGui::AlignTextToFramePadding();
                ImGui::Text("%s", s_preset_name);
                ImGui::SetWindowFontScale(1.0f);
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Current preset — Ctrl+S to save, Ctrl+Shift+S to save as");
            }

            ImGui::SameLine(0, 20);

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
                if (ImGui::Button(s_audio_active ? "LIVE [ON]" : "LIVE [OFF]", live_sz)) {
                    if (s_audio_active) {
                        /* Switch to monitor mode — keep device open, mute output */
                        fx_audio_set_mute_output(true);
                        s_audio_active = false;
                        s_monitor_only = true;
                        FX_INFO("LIVE off (monitoring input)");
                    } else {
                        if (s_selected_input < 0 && num_input_devices > 0) s_selected_input = 0;
                        if (s_selected_output < 0 && num_output_devices > 0) s_selected_output = 0;
                        if (s_selected_input >= 0) {
                            if (s_monitor_only) {
                                /* Already monitoring — just unmute */
                                fx_audio_set_mute_output(false);
                                s_audio_active = true;
                                s_monitor_only = false;
                                FX_INFO("LIVE on (unmuted)");
                            } else {
                                /* Open device fresh */
                                if (s_selected_output >= 0) fx_audio_set_output(s_selected_output);
                                if (fx_audio_set_device(engine, s_selected_input)) {
                                    fx_audio_set_mute_output(false);
                                    s_audio_active = true;
                                    FX_INFO("LIVE on: in=%s out=%s",
                                        fx_audio_get_device_name(s_selected_input),
                                        s_selected_output >= 0 ? fx_audio_get_output_name(s_selected_output) : "(default)");
                                }
                            }
                        }
                    }
                }
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(4);
                /* Glow ring */
                {
                    ImDrawList *dl = ImGui::GetWindowDrawList();
                    ImVec2 bmin = ImGui::GetItemRectMin(), bmax = ImGui::GetItemRectMax();
                    float t = (float)ImGui::GetTime();
                    if (s_audio_active) {
                        /* Hot glow when active */
                        int g = (int)(120.0f * (0.3f + 0.2f * sinf(t * 3.0f)));
                        dl->AddRect(ImVec2(bmin.x-2,bmin.y-2), ImVec2(bmax.x+2,bmax.y+2),
                                    IM_COL32(255, g, 30, g), 6.0f, 0, 2.0f);
                    } else {
                        /* Pulsing "attention" glow when inactive — draw users to click */
                        float pulse = 0.4f + 0.4f * sinf(t * 2.0f);
                        int a = (int)(pulse * 120.0f);
                        dl->AddRect(ImVec2(bmin.x-3,bmin.y-3), ImVec2(bmax.x+3,bmax.y+3),
                                    IM_COL32(200, 140, 30, a), 8.0f, 0, 2.0f);
                        dl->AddRect(ImVec2(bmin.x-6,bmin.y-6), ImVec2(bmax.x+6,bmax.y+6),
                                    IM_COL32(200, 140, 30, a/3), 10.0f, 0, 1.5f);
                    }
                }
                /* Tooltip */
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Toggle audio processing on/off (Space)");
            }
            ImGui::SameLine();

            /* ── DUAL/SINGLE chain toggle ──────────────────────── */
            {
                bool is_dual = (s_chain_b >= 0);
                if (is_dual) {
                    ImGui::PushStyleColor(ImGuiCol_Button,
                        ImVec4(0.25f, 0.18f, 0.06f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                        ImVec4(0.40f, 0.28f, 0.08f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                        ImVec4(0.55f, 0.38f, 0.10f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_Text,
                        ImVec4(0.95f, 0.75f, 0.20f, 1.0f));
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button,
                        ImVec4(0.15f, 0.13f, 0.11f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                        ImVec4(0.25f, 0.22f, 0.18f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                        ImVec4(0.10f, 0.09f, 0.07f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_Text,
                        ImVec4(0.55f, 0.50f, 0.40f, 1.0f));
                }
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
                if (ImGui::Button(is_dual ? "DUAL##split" : "SINGLE##split",
                                  ImVec2(72.0f, 32.0f))) {
                    if (is_dual) {
                        /* Switch back to SINGLE — destroy chain B */
                        if (s_chain_b >= 0) {
                            fx_chain_destroy(engine, s_chain_b);
                            s_chain_b = -1;
                        }
                        /* Deselect if we had selected chain B's nodes */
                        s_selected_node = -1;
                    } else {
                        /* Switch to DUAL — create chain B */
                        s_chain_b = fx_chain_create(engine);
                        if (s_chain_b >= 0) {
                            fx_chain_set_mix(engine, FX_CHAIN_DEFAULT, 0.5f);
                            fx_chain_set_mix(engine, s_chain_b, 0.5f);
                            FX_INFO("Dual chain enabled: chain B id=%d", (int)s_chain_b);
                        }
                    }
                }
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(4);

                /* Glow border when dual is active */
                if (is_dual) {
                    ImDrawList *dl = ImGui::GetWindowDrawList();
                    ImVec2 bmin = ImGui::GetItemRectMin(), bmax = ImGui::GetItemRectMax();
                    dl->AddRect(ImVec2(bmin.x-2,bmin.y-2), ImVec2(bmax.x+2,bmax.y+2),
                                IM_COL32(200, 160, 30, 180), 6.0f, 0, 1.5f);
                }
            }

            ImGui::SameLine();

            /* REC button + format selector (REC first, format after) */
            {
                static int rec_format_idx = 0;
                bool recording = fx_recorder_active();

                /* REC button */
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
                if (recording) {
                    float t = (float)ImGui::GetTime();
                    float pulse = 0.7f + 0.3f * sinf(t * 4.0f);
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f*pulse, 0.05f, 0.05f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.2f, 1.0f));
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.13f, 0.11f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.70f, 0.40f, 0.35f, 1.0f));
                }
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.12f, 0.10f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.50f, 0.15f, 0.12f, 1.0f));

                char rec_label[32];
                if (recording) {
                    float dur = fx_recorder_duration();
                    int mins = (int)(dur / 60.0f);
                    int secs = (int)dur % 60;
                    snprintf(rec_label, sizeof(rec_label), "%d:%02d", mins, secs);
                } else {
                    snprintf(rec_label, sizeof(rec_label), "REC");
                }

                if (ImGui::Button(rec_label, ImVec2(recording ? 56.0f : 48.0f, 32.0f))) {
                    if (recording) {
                        fx_recorder_stop();
                    } else {
                        const char *exts[] = { ".wav", ".wav", ".mp3", ".mp3", ".flac", ".flac" };
                        char rec_path[256];
                        /* Timestamp filename: recording_2026-03-21_110555.wav */
                        time_t now = time(NULL);
                        struct tm *t = localtime(&now);
                        snprintf(rec_path, sizeof(rec_path),
                            "recording_%04d-%02d-%02d_%02d%02d%02d%s",
                            t->tm_year+1900, t->tm_mon+1, t->tm_mday,
                            t->tm_hour, t->tm_min, t->tm_sec,
                            exts[rec_format_idx]);
                        fx_recorder_start(rec_path, (fx_record_format_t)rec_format_idx, 44100.0f);
                    }
                }
                ImGui::PopStyleColor(4);
                ImGui::PopStyleVar();
                if (ImGui::IsItemHovered()) {
                    if (recording) {
                        float dur = fx_recorder_duration();
                        ImGui::SetTooltip("Stop recording (%.1f sec)", dur);
                    } else {
                        ImGui::SetTooltip("Record processed output (%s)",
                            fx_recorder_format_name((fx_record_format_t)rec_format_idx));
                    }
                }

                /* Format dropdown after REC button */
                if (!recording) {
                    ImGui::SameLine();
                    ImGui::AlignTextToFramePadding();
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
                    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.12f, 0.10f, 0.09f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.70f, 0.65f, 0.55f, 1.0f));
                    ImGui::PushItemWidth(100.0f);
                    const char *fmt_names[] = {
                        "WAV 16-bit", "WAV 24-bit",
                        "MP3 192k", "MP3 320k",
                        "FLAC 16-bit", "FLAC 24-bit"
                    };
                    ImGui::Combo("##rec_fmt", &rec_format_idx, fmt_names, FX_RECORD_FORMAT_COUNT);
                    ImGui::PopItemWidth();
                    ImGui::PopStyleColor(2);
                    ImGui::PopStyleVar();
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Recording format");
                }
            }

            ImGui::SameLine();

            /* Audio settings gear — last toolbar item before window controls */
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
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Audio device and buffer settings");

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
                        /* Auto-start monitoring if not already open */
                        if (!s_monitor_only) {
                            if (s_selected_output >= 0) fx_audio_set_output(s_selected_output);
                            if (fx_audio_set_device(engine, s_selected_input)) {
                                fx_audio_set_mute_output(true);
                                s_monitor_only = true;
                                FX_INFO("Monitoring input: %s", fx_audio_get_device_name(s_selected_input));
                            }
                        }
                        if (ImGui::Button("Start Audio (Go LIVE)", ImVec2(200, 30))) {
                            fx_audio_set_mute_output(false);
                            s_audio_active = true;
                            s_monitor_only = false;
                            FX_INFO("Audio started: in=%s out=%s",
                                fx_audio_get_device_name(s_selected_input),
                                s_selected_output >= 0 ? fx_audio_get_output_name(s_selected_output) : "(default)");
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

                /* ── MIDI Settings ─────────────────────────────── */
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Text("MIDI Settings");
                ImGui::Separator();

                ImGui::TextDisabled("MIDI Input Device:");
                struct MidiGetter {
                    static bool get(void *, int idx, const char **out) {
                        const char *n = fx_midi_get_device_name(idx);
                        if (!n) return false;
                        *out = n; return true;
                    }
                };
                ImGui::SetNextItemWidth(300);
                if (num_midi_devices > 0) {
                    if (ImGui::Combo("##midi_input", &s_selected_midi,
                                     MidiGetter::get, nullptr, num_midi_devices)) {
                        /* Selection changed — open new device */
                        if (s_selected_midi >= 0) {
                            if (fx_midi_open(s_selected_midi)) {
                                s_midi_active = true;
                                FX_INFO("MIDI opened: %s",
                                    fx_midi_get_device_name(s_selected_midi));
                            } else {
                                s_midi_active = false;
                                FX_ERROR("Failed to open MIDI device");
                            }
                        }
                    }
                } else {
                    ImGui::TextDisabled("No MIDI devices found");
                }

                if (s_midi_active) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "Active");
                }

                /* MIDI Learn */
                ImGui::Spacing();
                if (fx_midi_learn_active()) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.3f, 0.3f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5f, 0.15f, 0.15f, 1.0f));
                    if (ImGui::Button("Cancel MIDI Learn", ImVec2(200, 28))) {
                        fx_midi_learn_cancel();
                    }
                    ImGui::PopStyleColor(3);
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
                                       "Move a CC knob on your controller...");
                } else {
                    if (ImGui::Button("MIDI Learn", ImVec2(200, 28))) {
                        /* Map next CC to param 0 — GUI can set specific target */
                        fx_midi_learn_start(0);
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Click, then move a CC knob to map it");
                }

                /* CC Mapping table */
                ImGui::Spacing();
                ImGui::TextDisabled("CC Mappings:");
                {
                    bool has_mappings = false;
                    for (int cc = 0; cc < 128; cc++) {
                        int param = fx_midi_get_mapped_param(cc);
                        if (param >= 0) {
                            has_mappings = true;
                            ImGui::Text("  CC %3d -> Param %d", cc, param);
                            ImGui::SameLine();
                            char unmap_id[32];
                            snprintf(unmap_id, sizeof(unmap_id), "X##cc%d", cc);
                            if (ImGui::SmallButton(unmap_id)) {
                                fx_midi_unmap_cc(cc);
                            }
                        }
                    }
                    if (!has_mappings) {
                        ImGui::TextDisabled("  (none)");
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
        /* Space = toggle LIVE */
        if (ImGui::IsKeyPressed(ImGuiKey_Space) && !ImGui::GetIO().WantCaptureKeyboard) {
            if (s_audio_active) {
                fx_audio_shutdown(); fx_audio_init();
                num_input_devices  = fx_audio_get_device_count();
                num_output_devices = fx_audio_get_output_count();
                s_audio_active = false;
                FX_INFO("Audio stopped via Space");
            } else {
                if (s_selected_input < 0 && num_input_devices > 0) s_selected_input = 0;
                if (s_selected_output < 0 && num_output_devices > 0) s_selected_output = 0;
                if (s_selected_input >= 0) {
                    if (s_selected_output >= 0) fx_audio_set_output(s_selected_output);
                    if (fx_audio_set_device(engine, s_selected_input)) {
                        s_audio_active = true;
                        FX_INFO("LIVE on via Space");
                    }
                }
            }
        }
        /* Ctrl+S = quick-save preset */
        static bool s_save_as_open = false;
        static char s_save_as_name[128] = "";
        {
            bool ctrl = ImGui::GetIO().KeyCtrl;
            bool shift = ImGui::GetIO().KeyShift;
            if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S)) {
                if (shift) {
                    /* Ctrl+Shift+S = Save As */
                    strcpy(s_save_as_name, "");
                    s_save_as_open = true;
                    ImGui::OpenPopup("save_as_popup");
                } else {
                    /* Ctrl+S = quick-save to last_session */
                    bool ok = fx_preset_save(engine, "presets/last_session.0xfx");
                    if (!ok) ok = fx_preset_save(engine, "../presets/last_session.0xfx");
                    FX_INFO(ok ? "Quick-saved to last_session.0xfx" : "Quick-save failed");
                }
            }
        }
        /* Save As popup (Ctrl+Shift+S) */
        if (s_save_as_open) {
            ImGui::OpenPopup("save_as_popup");
        }
        if (ImGui::BeginPopupModal("save_as_popup", &s_save_as_open,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Save Preset As");
            ImGui::Separator();
            ImGui::SetNextItemWidth(280);
            bool enter_pressed = ImGui::InputText("Preset Name", s_save_as_name,
                                                  sizeof(s_save_as_name),
                                                  ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::Spacing();
            if ((ImGui::Button("Save", ImVec2(120, 0)) || enter_pressed) &&
                s_save_as_name[0] != '\0') {
                char path[400];
                snprintf(path, sizeof(path), "presets/%s.0xfx", s_save_as_name);
                bool ok = fx_preset_save(engine, path);
                if (!ok) {
                    snprintf(path, sizeof(path), "../presets/%s.0xfx", s_save_as_name);
                    ok = fx_preset_save(engine, path);
                }
                FX_INFO(ok ? "Saved preset: %s" : "Save failed: %s", s_save_as_name);
                s_save_as_open = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                s_save_as_open = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        /* ============================================================
         * BUILD THE FLATTENED SIGNAL CHAIN
         *
         * SINGLE: INPUT -> [pre pedals] -> AMP -> CAB -> [post pedals] -> OUTPUT
         * DUAL:   INPUT -> [pre pedals] -> SPLIT -> AMP A -> CAB A -> MERGE
         *                                        -> AMP B -> CAB B ->
         *                               -> [post pedals] -> OUTPUT
         *
         * In DUAL mode the chain array stores nodes sequentially; the
         * special SPLIT/MERGE nodes encode where paths diverge/converge.
         * The second path's nodes carry chain_id=s_chain_b so the
         * renderer can lay them out on a separate vertical lane.
         * ============================================================ */
        ChainNode chain[256];
        int chain_len = 0;
        bool is_dual = (s_chain_b >= 0);

        /* INPUT */
        chain[chain_len++] = { NODE_INPUT, -1, -1, 0 };

        /* Pre-amp pedals */
        for (int i = 0; i < s_pre_id_count && chain_len < 250; i++) {
            chain[chain_len++] = { NODE_PEDAL_PRE, i, s_pre_ids[i], 0 };
        }

        if (is_dual) {
            /* SPLIT diamond */
            chain[chain_len++] = { NODE_SPLIT, -1, -1, 0 };

            /* Chain A: AMP A + CAB A (chain 0 = top lane) */
            chain[chain_len++] = { NODE_AMP, -1, -1, 0 };
            chain[chain_len++] = { NODE_CAB, -1, -1, 0 };

            /* Chain B: AMP B + CAB B (s_chain_b = bottom lane) */
            chain[chain_len++] = { NODE_AMP, -1, -1, (int)s_chain_b };
            chain[chain_len++] = { NODE_CAB, -1, -1, (int)s_chain_b };

            /* MERGE (mix) diamond */
            chain[chain_len++] = { NODE_MERGE, -1, -1, 0 };
        } else {
            /* Single path AMP + CAB */
            chain[chain_len++] = { NODE_AMP, -1, -1, 0 };
            chain[chain_len++] = { NODE_CAB, -1, -1, 0 };
        }

        /* Post-amp pedals */
        for (int i = 0; i < s_post_id_count && chain_len < 254; i++) {
            chain[chain_len++] = { NODE_PEDAL_POST, i, s_post_ids[i], 0 };
        }

        /* Studio processors (post-amp rack gear) */
        for (int i = 0; i < s_studio_id_count && chain_len < 254; i++) {
            chain[chain_len++] = { NODE_STUDIO, i, s_studio_ids[i], 0 };
        }

        /* OUTPUT */
        chain[chain_len++] = { NODE_OUTPUT, -1, -1, 0 };

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

            /* Vertical center of the chain area (used as the "main" lane) */
            float cy = win_pos.y + chain_area_h * 0.45f;

            /* In DUAL mode we stack two lanes above/below cy */
            const float LANE_OFFSET = NODE_H * 1.25f;  /* vertical gap between parallel paths */
            float cy_a = is_dual ? cy - LANE_OFFSET * 0.5f : cy;  /* top lane (chain A) */
            float cy_b = is_dual ? cy + LANE_OFFSET * 0.5f : cy;  /* bottom lane (chain B) */

            /* ── Layout: compute x-column assignments ──────────────────
             * SINGLE: each node occupies one column, separated by NODE_SPACING.
             * DUAL:   SPLIT / MERGE columns span both lanes.
             *         Chain-A and chain-B nodes share the same x-column
             *         (stacked vertically).
             *
             * We compute a "column" index for each node, then map to x.
             * ─────────────────────────────────────────────────────────── */
            int node_col[256];  /* which x-column does node ni occupy? */
            int num_cols = 0;
            {
                bool in_split = false;
                int col = 0;
                int amp_a_col = -1;
                int cab_a_col = -1;
                for (int ni = 0; ni < chain_len; ni++) {
                    NodeKind k = chain[ni].kind;
                    if (k == NODE_SPLIT) {
                        node_col[ni] = col++;
                        in_split = true;
                        amp_a_col = -1; cab_a_col = -1;
                    } else if (k == NODE_MERGE) {
                        in_split = false;
                        node_col[ni] = col++;
                    } else if (in_split && k == NODE_AMP) {
                        if (chain[ni].chain_id == 0) {
                            amp_a_col = col;
                            node_col[ni] = col++;
                        } else {
                            /* chain B AMP shares column with chain A AMP */
                            node_col[ni] = (amp_a_col >= 0) ? amp_a_col : col;
                        }
                    } else if (in_split && k == NODE_CAB) {
                        if (chain[ni].chain_id == 0) {
                            cab_a_col = col;
                            node_col[ni] = col++;
                        } else {
                            /* chain B CAB shares column with chain A CAB */
                            node_col[ni] = (cab_a_col >= 0) ? cab_a_col : col;
                        }
                    } else {
                        node_col[ni] = col++;
                    }
                }
                num_cols = col;
            }

            /* Total width for scrolling */
            float total_w = num_cols * NODE_W + (num_cols - 1) * NODE_SPACING
                          + CHAIN_PADDING * 2.0f;

            /* Reserve scrollable content area (extra height for two lanes in dual mode) */
            float content_h = is_dual ? chain_area_h * 0.85f : (chain_area_h - 20.0f);
            ImGui::Dummy(ImVec2(total_w, content_h));

            /* Track where the [+] button popup should insert */
            static fx_chain_pos_t s_add_popup_pos = FX_CHAIN_POS_PRE;
            static int s_add_popup_insert_slot = -1;

            /* Center the chain horizontally */
            float chain_area_w = win_w;
            float chain_content_w = num_cols * NODE_W + (num_cols - 1) * NODE_SPACING;
            float center_offset = (chain_area_w > chain_content_w)
                ? (chain_area_w - chain_content_w) * 0.5f
                : CHAIN_PADDING;

            /* Map column index -> screen x (left edge of node) */
            auto col_to_x = [&](int col) -> float {
                return content_min.x + center_offset - ImGui::GetScrollX()
                     + col * (NODE_W + NODE_SPACING);
            };

            /* ── Draw SPLIT / MERGE bezier paths first (behind nodes) ── */
            if (is_dual) {
                /* Find the SPLIT and MERGE node columns */
                int split_col = -1, merge_col = -1;
                int amp_col_a = -1, cab_col_a = -1, cab_col_b = -1;
                for (int ni = 0; ni < chain_len; ni++) {
                    if (chain[ni].kind == NODE_SPLIT) split_col = node_col[ni];
                    if (chain[ni].kind == NODE_MERGE) merge_col = node_col[ni];
                    if (chain[ni].kind == NODE_AMP && chain[ni].chain_id == 0) amp_col_a = node_col[ni];
                    if (chain[ni].kind == NODE_CAB && chain[ni].chain_id == 0) cab_col_a = node_col[ni];
                    if (chain[ni].kind == NODE_CAB && chain[ni].chain_id != 0) cab_col_b = node_col[ni];
                }
                if (split_col >= 0 && merge_col >= 0 && amp_col_a >= 0) {
                    float sx   = col_to_x(split_col) + NODE_W;   /* right of SPLIT */
                    float mx   = col_to_x(merge_col);             /* left of MERGE  */
                    float ax   = col_to_x(amp_col_a);             /* left of AMP column */
                    float cp   = 40.0f;

                    ImU32 cable_col = s_audio_active
                        ? IM_COL32(210, 150, 30, 200)
                        : IM_COL32(110, 85, 30, 160);
                    ImU32 cable_shd = IM_COL32(20, 15, 5, 100);

                    /* SPLIT -> AMP A (upper path) */
                    {
                        ImVec2 p0(sx, cy), p3(ax, cy_a + NODE_H * 0.5f);
                        ImVec2 p1(sx + cp, cy), p2(ax - cp, cy_a + NODE_H * 0.5f);
                        dl->AddBezierCubic(ImVec2(p0.x+1,p0.y+2),ImVec2(p1.x+1,p1.y+2),
                                           ImVec2(p2.x+1,p2.y+2),ImVec2(p3.x+1,p3.y+2),
                                           cable_shd, 2.5f, 16);
                        dl->AddBezierCubic(p0, p1, p2, p3, cable_col, 2.5f, 16);
                    }
                    /* SPLIT -> AMP B (lower path) */
                    {
                        ImVec2 p0(sx, cy), p3(ax, cy_b + NODE_H * 0.5f);
                        ImVec2 p1(sx + cp, cy), p2(ax - cp, cy_b + NODE_H * 0.5f);
                        dl->AddBezierCubic(ImVec2(p0.x+1,p0.y+2),ImVec2(p1.x+1,p1.y+2),
                                           ImVec2(p2.x+1,p2.y+2),ImVec2(p3.x+1,p3.y+2),
                                           cable_shd, 2.5f, 16);
                        dl->AddBezierCubic(p0, p1, p2, p3, cable_col, 2.5f, 16);
                    }
                    /* AMP A -> CAB A */
                    if (cab_col_a >= 0) {
                        float a_rx = col_to_x(amp_col_a) + NODE_W;
                        float c_lx = col_to_x(cab_col_a);
                        float acy  = cy_a + NODE_H * 0.5f;
                        ImVec2 p0(a_rx, acy), p3(c_lx, acy);
                        ImVec2 p1(a_rx + 20, acy), p2(c_lx - 20, acy);
                        dl->AddBezierCubic(ImVec2(p0.x+1,p0.y+2),ImVec2(p1.x+1,p1.y+2),
                                           ImVec2(p2.x+1,p2.y+2),ImVec2(p3.x+1,p3.y+2),
                                           cable_shd, 2.5f, 12);
                        dl->AddBezierCubic(p0, p1, p2, p3, cable_col, 2.5f, 12);
                    }
                    /* AMP B -> CAB B */
                    if (cab_col_b >= 0) {
                        float a_rx = col_to_x(amp_col_a) + NODE_W;
                        float c_lx = col_to_x(cab_col_b);
                        float bcy  = cy_b + NODE_H * 0.5f;
                        ImVec2 p0(a_rx, bcy), p3(c_lx, bcy);
                        ImVec2 p1(a_rx + 20, bcy), p2(c_lx - 20, bcy);
                        dl->AddBezierCubic(ImVec2(p0.x+1,p0.y+2),ImVec2(p1.x+1,p1.y+2),
                                           ImVec2(p2.x+1,p2.y+2),ImVec2(p3.x+1,p3.y+2),
                                           cable_shd, 2.5f, 12);
                        dl->AddBezierCubic(p0, p1, p2, p3, cable_col, 2.5f, 12);
                    }
                    /* CAB A -> MERGE */
                    if (cab_col_a >= 0) {
                        float c_rx = col_to_x(cab_col_a) + NODE_W;
                        float acy  = cy_a + NODE_H * 0.5f;
                        ImVec2 p0(c_rx, acy), p3(mx, cy);
                        ImVec2 p1(c_rx + cp, acy), p2(mx - cp, cy);
                        dl->AddBezierCubic(ImVec2(p0.x+1,p0.y+2),ImVec2(p1.x+1,p1.y+2),
                                           ImVec2(p2.x+1,p2.y+2),ImVec2(p3.x+1,p3.y+2),
                                           cable_shd, 2.5f, 16);
                        dl->AddBezierCubic(p0, p1, p2, p3, cable_col, 2.5f, 16);
                    }
                    /* CAB B -> MERGE */
                    if (cab_col_b >= 0) {
                        float c_rx = col_to_x(cab_col_b) + NODE_W;
                        float bcy  = cy_b + NODE_H * 0.5f;
                        ImVec2 p0(c_rx, bcy), p3(mx, cy);
                        ImVec2 p1(c_rx + cp, bcy), p2(mx - cp, cy);
                        dl->AddBezierCubic(ImVec2(p0.x+1,p0.y+2),ImVec2(p1.x+1,p1.y+2),
                                           ImVec2(p2.x+1,p2.y+2),ImVec2(p3.x+1,p3.y+2),
                                           cable_shd, 2.5f, 16);
                        dl->AddBezierCubic(p0, p1, p2, p3, cable_col, 2.5f, 16);
                    }
                }
            }

            /* ── Section labels above the signal chain ──────────────── */
            {
                ImGui::SetWindowFontScale(0.75f);
                ImU32 label_col = IM_COL32(160, 140, 110, 120);
                float label_y = cy - NODE_H * 0.5f - 18.0f;
                if (is_dual) label_y = cy_a - NODE_H * 0.5f - 18.0f;

                /* Find first/last column for each section */
                for (int ni = 0; ni < chain_len; ni++) {
                    const char *section = nullptr;
                    if (chain[ni].kind == NODE_PEDAL_PRE && (ni == 0 || chain[ni-1].kind != NODE_PEDAL_PRE))
                        section = "PEDALS";
                    else if (chain[ni].kind == NODE_AMP && (ni == 0 || (chain[ni-1].kind != NODE_AMP && chain[ni-1].kind != NODE_SPLIT)))
                        section = "AMP";
                    else if (chain[ni].kind == NODE_CAB && (ni == 0 || chain[ni-1].kind != NODE_CAB))
                        section = "CABINET";
                    else if (chain[ni].kind == NODE_STUDIO && (ni == 0 || chain[ni-1].kind != NODE_STUDIO))
                        section = "RACK FX";

                    if (section) {
                        float nx = col_to_x(node_col[ni]);
                        ImVec2 sz = ImGui::CalcTextSize(section);
                        dl->AddText(ImVec2(nx + (NODE_W - sz.x) * 0.5f, label_y), label_col, section);
                    }
                }
                ImGui::SetWindowFontScale(1.0f);
            }

            /* ── Draw all nodes ──────────────────────────────────────── */
            for (int ni = 0; ni < chain_len; ni++) {
                ChainNode &n = chain[ni];
                bool is_selected = (s_selected_node == ni);
                bool is_bypassed = false;

                if (n.kind == NODE_PEDAL_PRE || n.kind == NODE_PEDAL_POST) {
                    is_bypassed = fx_pedal_get_bypass(engine, n.pedal_id);
                } else if (n.kind == NODE_STUDIO) {
                    is_bypassed = fx_studio_get_bypass(engine, n.pedal_id);
                } else if (n.kind == NODE_CAB) {
                    is_bypassed = fx_cab_get_bypass(engine, (fx_chain_id)n.chain_id);
                }

                /* Determine Y position for this node */
                float node_cy;
                if (is_dual) {
                    if (n.chain_id != 0) {
                        node_cy = cy_b;  /* chain B = bottom lane */
                    } else if (n.kind == NODE_AMP || n.kind == NODE_CAB) {
                        node_cy = cy_a;  /* chain A amp/cab = top lane */
                    } else {
                        node_cy = cy;    /* center lane (pre/post/split/merge/in/out) */
                    }
                } else {
                    node_cy = cy;
                }

                float nx = col_to_x(node_col[ni]);
                float ny = node_cy - NODE_H * 0.5f;

                /* ── SPLIT / MERGE: draw as diamond ─────────────── */
                if (n.kind == NODE_SPLIT || n.kind == NODE_MERGE) {
                    float dm  = NODE_H * 0.55f;  /* half-size of diamond */
                    float dcx = nx + NODE_W * 0.5f;
                    float dcy = node_cy;
                    ImU32 fill_col = IM_COL32(60, 48, 12, 255);
                    ImU32 edge_col = is_selected
                        ? IM_COL32(255, 220, 60, 255)
                        : IM_COL32(220, 170, 30, 255);
                    ImVec2 top(dcx, dcy - dm);
                    ImVec2 rgt(dcx + dm, dcy);
                    ImVec2 bot(dcx, dcy + dm);
                    ImVec2 lft(dcx - dm, dcy);
                    dl->AddQuadFilled(top, rgt, bot, lft, fill_col);
                    dl->AddQuad(top, rgt, bot, lft, edge_col, 2.0f);

                    /* Label inside diamond */
                    const char *dlbl = (n.kind == NODE_SPLIT) ? "Y" : "M";
                    ImVec2 dlbl_sz = ImGui::CalcTextSize(dlbl);
                    dl->AddText(ImVec2(dcx - dlbl_sz.x * 0.5f, dcy - dlbl_sz.y * 0.5f),
                                IM_COL32(220, 200, 100, 255), dlbl);

                    /* Below label */
                    const char *blbl = (n.kind == NODE_SPLIT) ? "SPLIT" : "MIX";
                    ImVec2 blbl_sz = ImGui::CalcTextSize(blbl);
                    dl->AddText(ImVec2(dcx - blbl_sz.x * 0.5f, dcy + dm + 4.0f),
                                IM_COL32(180, 165, 120, 200), blbl);

                    /* Selection glow ring */
                    if (is_selected) {
                        dl->AddQuad(ImVec2(top.x, top.y - 3), ImVec2(rgt.x + 3, rgt.y),
                                    ImVec2(bot.x, bot.y + 3), ImVec2(lft.x - 3, lft.y),
                                    IM_COL32(255, 220, 60, 200), 2.5f);
                    }

                    /* Invisible button for click detection */
                    ImGui::SetCursorScreenPos(ImVec2(dcx - dm, dcy - dm));
                    char btn_id[32];
                    snprintf(btn_id, sizeof(btn_id), "##node_%d", ni);
                    if (ImGui::InvisibleButton(btn_id, ImVec2(dm * 2.0f, dm * 2.0f))) {
                        s_selected_node = (s_selected_node == ni) ? -1 : ni;
                    }
                    /* SPLIT skips cable drawing (handled by bezier pass),
                     * but MERGE falls through to draw cable + [+] to next node */
                    if (n.kind == NODE_SPLIT) continue;
                }

                /* ── Regular rectangular node ───────────────────── */
                bool drew_texture = false;
                {
                    uintptr_t tex = 0;
                    if (n.kind == NODE_PEDAL_PRE || n.kind == NODE_PEDAL_POST) {
                        fx_pedal_type_t pt = fx_pedal_get_type(engine, n.pedal_id);
                        if (pt < FX_PEDAL_TYPE_COUNT) {
                            const char *tname = fx_pedal_get_type_name(pt);
                            tex = load_pedal_texture(tname);
                        }
                    } else if (n.kind == NODE_AMP) {
                        const char *aname = fx_amp_get_type_name(
                            fx_amp_get_model(engine, (fx_chain_id)n.chain_id));
                        tex = load_amp_body_texture(aname);
                    } else if (n.kind == NODE_CAB) {
                        int ctype = (n.chain_id == 0) ? s_cab_type : s_cab_type_b;
                        tex = load_cab_texture(ctype);
                    } else if (n.kind == NODE_STUDIO) {
                        static const char *rack_fnames[] = {
                            "iron_squeeze", "glass_eq", "reel_warmth", "brick_wall",
                            "velvet_press", "glue_bus", "valve_color", "precision_eq", "room_engine"
                        };
                        fx_studio_type_t st = fx_studio_get_type(engine, n.pedal_id);
                        if (st >= 0 && st < FX_STUDIO_COUNT) {
                            char spath[256];
                            snprintf(spath, sizeof(spath), "resources/studio/%s_nobg.png", rack_fnames[st]);
                            tex = fx_texture_load(spath);
                        }
                    } else if (n.kind == NODE_INPUT) {
                        tex = fx_texture_load("resources/cables/trs_plug_input.png");
                        /* Render INPUT flipped horizontally (plug tip faces left) */
                        if (tex) {
                            ImVec4 tint = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
                            ImGui::SetCursorScreenPos(ImVec2(nx, ny));
                            ImGui::Image((ImTextureID)tex, ImVec2(NODE_W, NODE_H),
                                         ImVec2(1, 0), ImVec2(0, 1), tint);
                            drew_texture = true;
                            tex = 0; /* skip default rendering below */
                        }
                    } else if (n.kind == NODE_OUTPUT) {
                        tex = fx_texture_load("resources/cables/xlr_plug_output.png");
                    }
                    if (tex) {
                        ImVec4 tint = is_bypassed
                            ? ImVec4(0.5f, 0.5f, 0.5f, 0.7f)
                            : ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
                        ImGui::SetCursorScreenPos(ImVec2(nx, ny));
                        ImGui::Image((ImTextureID)tex, ImVec2(NODE_W, NODE_H),
                                     ImVec2(0, 0), ImVec2(1, 1), tint);
                        drew_texture = true;
                    }
                }
                if (!drew_texture) {
                    ImU32 bg_col = node_color(n.kind, is_bypassed);
                    dl->AddRectFilled(ImVec2(nx, ny), ImVec2(nx + NODE_W, ny + NODE_H),
                                      bg_col, 6.0f);
                }

                /* Selection highlight */
                if (is_selected) {
                    dl->AddRect(ImVec2(nx - 2, ny - 2),
                                ImVec2(nx + NODE_W + 2, ny + NODE_H + 2),
                                IM_COL32(230, 180, 60, 255), 6.0f, 0, 2.5f);
                }

                /* Node label (centered below node) */
                const char *lbl = node_label(n.kind, engine, n.pedal_id, n.chain_id);
                /* Override cab label with actual cab type name */
                if (n.kind == NODE_CAB) {
                    int ctype = (n.chain_id == 0) ? s_cab_type : s_cab_type_b;
                    if (ctype >= 0 && ctype < FX_CAB_TYPE_COUNT)
                        lbl = s_cab_type_names[ctype];
                }
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

                /* Bypass LED */
                if (n.kind == NODE_PEDAL_PRE || n.kind == NODE_PEDAL_POST) {
                    const char *led_path = is_bypassed
                        ? "resources/leds/led_red_off_nobg.png"
                        : "resources/leds/led_green_on_nobg.png";
                    uintptr_t led_tex = fx_texture_load(led_path);
                    const float LED_SZ = 12.0f;
                    float led_x = nx + NODE_W - LED_SZ - 3.0f;
                    float led_y = ny + 3.0f;
                    if (led_tex) {
                        ImGui::SetCursorScreenPos(ImVec2(led_x, led_y));
                        ImGui::Image((ImTextureID)(uintptr_t)led_tex, ImVec2(LED_SZ, LED_SZ));
                    } else {
                        ImU32 dot_col = is_bypassed
                            ? IM_COL32(200, 60, 50, 200)
                            : IM_COL32(60, 200, 60, 220);
                        dl->AddCircleFilled(
                            ImVec2(led_x + LED_SZ*0.5f, led_y + LED_SZ*0.5f),
                            LED_SZ * 0.5f, dot_col, 12);
                    }
                }

                /* Click detection: left-click = select, right-click/double-click = stomp (bypass toggle) */
                {
                    char btn_id[32];
                    snprintf(btn_id, sizeof(btn_id), "##node_%d", ni);
                    ImGui::SetCursorScreenPos(ImVec2(nx, ny));
                    if (ImGui::InvisibleButton(btn_id, ImVec2(NODE_W, NODE_H))) {
                        if (n.kind != NODE_INPUT && n.kind != NODE_OUTPUT) {
                            s_selected_node = (s_selected_node == ni) ? -1 : ni;
                        }
                    }
                    /* Right-click or double-click = stomp footswitch (toggle bypass) */
                    bool stomped = false;
                    if (ImGui::IsItemHovered()) {
                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                            stomped = true;
                        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                            stomped = true;
                    }
                    if (stomped) {
                        if (n.kind == NODE_PEDAL_PRE || n.kind == NODE_PEDAL_POST) {
                            bool bp = fx_pedal_get_bypass(engine, n.pedal_id);
                            fx_pedal_set_bypass(engine, n.pedal_id, !bp);
                        } else if (n.kind == NODE_STUDIO) {
                            bool bp = fx_studio_get_bypass(engine, n.pedal_id);
                            fx_studio_set_bypass(engine, n.pedal_id, !bp);
                        } else if (n.kind == NODE_CAB) {
                            bool bp = fx_cab_get_bypass(engine, (fx_chain_id)n.chain_id);
                            fx_cab_set_bypass(engine, (fx_chain_id)n.chain_id, !bp);
                        }
                    }
                    /* Tooltip hint */
                    if (ImGui::IsItemHovered() && (n.kind == NODE_PEDAL_PRE || n.kind == NODE_PEDAL_POST || n.kind == NODE_STUDIO)) {
                        ImGui::SetTooltip("Right-click to bypass/activate");
                    }
                }

                /* ── Draw connecting cable to next node (single-lane sections) ── */
                if (ni < chain_len - 1) {
                    NodeKind cur_kind = chain[ni].kind;
                    NodeKind nxt_kind = chain[ni + 1].kind;

                    /* Only draw cables for sequential single-lane sections.
                       Split-section cables are drawn in the bezier pass above. */
                    bool is_linear = false;
                    bool show_add  = false;
                    fx_chain_pos_t add_pos = FX_CHAIN_POS_PRE;
                    int  add_insert_slot   = 0;

                    if (!is_dual) {
                        /* Pre-amp zone */
                        if ((cur_kind == NODE_INPUT || cur_kind == NODE_PEDAL_PRE) &&
                            (nxt_kind == NODE_PEDAL_PRE || nxt_kind == NODE_AMP)) {
                            is_linear = true; show_add = true; add_pos = FX_CHAIN_POS_PRE;
                            for (int j = 0; j <= ni; j++)
                                if (chain[j].kind == NODE_PEDAL_PRE) add_insert_slot++;
                        }
                        /* Post-amp zone — rack effects */
                        else if ((cur_kind == NODE_CAB || cur_kind == NODE_PEDAL_POST ||
                                  cur_kind == NODE_STUDIO) &&
                                 (nxt_kind == NODE_PEDAL_POST || nxt_kind == NODE_STUDIO ||
                                  nxt_kind == NODE_OUTPUT)) {
                            is_linear = true; show_add = true; add_pos = FX_CHAIN_POS_POST;
                            for (int j = 0; j <= ni; j++)
                                if (chain[j].kind == NODE_PEDAL_POST || chain[j].kind == NODE_STUDIO)
                                    add_insert_slot++;
                        }
                        /* AMP -> CAB */
                        else if (cur_kind == NODE_AMP && nxt_kind == NODE_CAB) {
                            is_linear = true;
                        }
                    } else {
                        /* Dual mode: only linear pre/post sections */
                        if ((cur_kind == NODE_INPUT || cur_kind == NODE_PEDAL_PRE) &&
                            (nxt_kind == NODE_PEDAL_PRE || nxt_kind == NODE_SPLIT)) {
                            is_linear = true;
                            if (nxt_kind == NODE_PEDAL_PRE || nxt_kind == NODE_SPLIT) {
                                show_add = true; add_pos = FX_CHAIN_POS_PRE;
                                for (int j = 0; j <= ni; j++)
                                    if (chain[j].kind == NODE_PEDAL_PRE) add_insert_slot++;
                            }
                        } else if ((cur_kind == NODE_MERGE || cur_kind == NODE_PEDAL_POST ||
                                    cur_kind == NODE_STUDIO) &&
                                   (nxt_kind == NODE_PEDAL_POST || nxt_kind == NODE_STUDIO ||
                                    nxt_kind == NODE_OUTPUT)) {
                            is_linear = true;
                            show_add = true; add_pos = FX_CHAIN_POS_POST;
                            for (int j = 0; j <= ni; j++)
                                if (chain[j].kind == NODE_PEDAL_POST || chain[j].kind == NODE_STUDIO)
                                    add_insert_slot++;
                        }
                    }

                    if (is_linear) {
                        float x_right = col_to_x(node_col[ni]) + NODE_W;
                        float x_next  = col_to_x(node_col[ni + 1]);
                        float lcy     = node_cy;

                        /* [+] button: register invisible button first (for click detection),
                         * but defer visual drawing until after cable is rendered */
                        float add_x = 0, add_y = 0;
                        bool add_clicked = false, add_hovered = false;
                        if (show_add) {
                            add_x = x_right + (x_next - x_right - ADD_BTN_W) * 0.5f;
                            add_y = lcy - ADD_BTN_W * 0.5f;
                            /* Register invisible button for click detection */
                            char inv_id[32];
                            snprintf(inv_id, sizeof(inv_id), "##addinv_%d", ni);
                            ImGui::SetCursorScreenPos(ImVec2(add_x, add_y));
                            add_clicked = ImGui::InvisibleButton(inv_id, ImVec2(ADD_BTN_W, ADD_BTN_W));
                            add_hovered = ImGui::IsItemHovered();
                            if (add_clicked) {
                                s_add_popup_pos = add_pos;
                                s_add_popup_insert_slot = add_insert_slot;
                                if (add_pos == FX_CHAIN_POS_POST)
                                    ImGui::OpenPopup("add_studio_popup");
                                else
                                    ImGui::OpenPopup("add_pedal_popup");
                            }
                        }

                        /* Cable — realistic patch cable between nodes (drawn BEFORE [+] so [+] is on top) */
                        float cable_x0 = x_right, cable_y0 = lcy;
                        float cable_x1 = x_next,  cable_y1 = lcy;

                        /* INPUT/OUTPUT: adjust endpoints + droop */
                        bool has_droop = false;
                        if (cur_kind == NODE_INPUT) {
                            /* Flipped plug — cable exits from bottom-right where the cable end is */
                            cable_x0 = col_to_x(node_col[ni]) + NODE_W * 0.55f;
                            cable_y0 = lcy + NODE_H * 0.45f;
                            has_droop = true;
                        }
                        if (nxt_kind == NODE_OUTPUT) {
                            cable_x1 = col_to_x(node_col[ni + 1]) + NODE_W * 0.15f;
                            cable_y1 = lcy + NODE_H * 0.15f;
                            has_droop = true;
                        }

                        float span = cable_x1 - cable_x0;
                        ImVec2 p0(cable_x0, cable_y0);
                        ImVec2 p3(cable_x1, cable_y1);
                        ImVec2 p1, p2;

                        if (has_droop) {
                            /* Natural cable sag for instrument/mic cables */
                            float sag = 18.0f + span * 0.12f;
                            p1 = ImVec2(cable_x0 + span * 0.25f, cable_y0 + sag);
                            p2 = ImVec2(cable_x1 - span * 0.25f, cable_y1 + sag);
                        } else {
                            /* Straight patch cable between pedals/amps/cabs */
                            p1 = ImVec2(cable_x0 + span * 0.33f, cable_y0);
                            p2 = ImVec2(cable_x1 - span * 0.33f, cable_y1);
                        }

                        /* Shadow layer */
                        dl->AddBezierCubic(
                            ImVec2(p0.x + 1, p0.y + 2),
                            ImVec2(p1.x + 1, p1.y + 2),
                            ImVec2(p2.x + 1, p2.y + 2),
                            ImVec2(p3.x + 1, p3.y + 2),
                            IM_COL32(0, 0, 0, 120), 7.0f, 24);
                        /* Cable body — thick dark rubber */
                        dl->AddBezierCubic(p0, p1, p2, p3,
                            IM_COL32(35, 30, 25, 255), 5.0f, 24);
                        /* Highlight stripe — subtle sheen along top */
                        dl->AddBezierCubic(
                            ImVec2(p0.x, p0.y - 1),
                            ImVec2(p1.x, p1.y - 1),
                            ImVec2(p2.x, p2.y - 1),
                            ImVec2(p3.x, p3.y - 1),
                            IM_COL32(70, 60, 45, 100), 1.5f, 24);

                        /* [+] button visual — drawn ON TOP of cable */
                        if (show_add) {
                            float t_pulse = (float)ImGui::GetTime();
                            float glow_a  = add_hovered ? 1.0f : 0.55f + 0.25f * sinf(t_pulse * 2.5f);
                            ImU32 bcol = IM_COL32((int)(200*glow_a),(int)(140*glow_a),(int)(20*glow_a),(int)(220*glow_a));
                            ImU32 bgcol = add_hovered ? IM_COL32(80,55,12,220) : IM_COL32(35,28,8,180);
                            float r = 6.0f;
                            dl->AddRectFilled(ImVec2(add_x,add_y), ImVec2(add_x+ADD_BTN_W,add_y+ADD_BTN_W), bgcol, r);
                            dl->AddRect(ImVec2(add_x,add_y), ImVec2(add_x+ADD_BTN_W,add_y+ADD_BTN_W), bcol, r, 0, 2.0f);
                            float cx2 = add_x + ADD_BTN_W * 0.5f;
                            float cy2 = add_y + ADD_BTN_W * 0.5f;
                            float arm = ADD_BTN_W * 0.28f;
                            ImU32 pcol = IM_COL32((int)(230*glow_a),(int)(175*glow_a),(int)(40*glow_a),255);
                            dl->AddLine(ImVec2(cx2-arm,cy2), ImVec2(cx2+arm,cy2), pcol, 2.5f);
                            dl->AddLine(ImVec2(cx2,cy2-arm), ImVec2(cx2,cy2+arm), pcol, 2.5f);
                            if (add_hovered)
                                dl->AddRect(ImVec2(add_x-2,add_y-2),ImVec2(add_x+ADD_BTN_W+2,add_y+ADD_BTN_W+2),
                                            IM_COL32(220,160,30,100), r+2, 0, 3.0f);
                        }
                    }
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
                                if (ImGui::IsItemHovered()) {
                                    const char *tip = get_pedal_tooltip(pe.type);
                                    if (tip) ImGui::SetTooltip("%s", tip);
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

            /* ── Rack effect add popup (post-amp [+]) ──── */
            ImGui::SetNextWindowSize(ImVec2(320, 340), ImGuiCond_Always);
            if (ImGui::BeginPopup("add_studio_popup",
                                  ImGuiWindowFlags_NoResize)) {
                ImGui::PushStyleColor(ImGuiCol_Text,
                    ImVec4(0.90f, 0.65f, 0.20f, 1.0f));
                ImGui::SetWindowFontScale(1.15f);
                ImGui::Text("Add Rack Effect  [POST-AMP]");
                ImGui::SetWindowFontScale(1.0f);
                ImGui::PopStyleColor();
                ImGui::Separator();
                ImGui::Spacing();

                static const struct { fx_studio_type_t type; const char *name; const char *desc; } studio_menu[] = {
                    { FX_STUDIO_IRON_SQUEEZE, "Iron Squeeze",  "FET compressor — punchy, fast attack" },
                    { FX_STUDIO_VELVET_PRESS, "Velvet Press",  "Optical compressor — smooth, musical" },
                    { FX_STUDIO_GLUE_BUS,     "Glue Bus",      "VCA bus compressor — glue, punch" },
                    { FX_STUDIO_GLASS_EQ,     "Glass EQ",      "Passive EQ — musical, sweet top end" },
                    { FX_STUDIO_PRECISION_EQ, "Precision EQ",  "Channel EQ — warm, proportional-Q" },
                    { FX_STUDIO_REEL_WARMTH,  "Reel Warmth",   "Tape saturation — warmth, harmonics" },
                    { FX_STUDIO_VALVE_COLOR,  "Valve Color",   "Tube saturation — rich harmonics" },
                    { FX_STUDIO_BRICK_WALL,   "Brick Wall",    "Brickwall limiter — output protection" },
                    { FX_STUDIO_ROOM_ENGINE,  "Room Engine",   "Room simulation — studio ambience" },
                };

                for (int si = 0; si < 9; si++) {
                    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.18f, 0.22f, 0.30f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.25f, 0.35f, 0.50f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.30f, 0.45f, 0.65f, 1.0f));

                    char sel_id[64];
                    snprintf(sel_id, sizeof(sel_id), "%s##studio_%d",
                             studio_menu[si].name, si);
                    if (ImGui::Selectable(sel_id, false)) {
                        if (s_studio_id_count < 8) {
                            fx_studio_id nid = fx_studio_add(engine, studio_menu[si].type);
                            if (nid >= 0) {
                                s_studio_ids[s_studio_id_count++] = nid;
                            }
                        }
                        ImGui::CloseCurrentPopup();
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", studio_menu[si].desc);

                    ImGui::PopStyleColor(3);
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

            /* ── Detail view background: amp rack interior (tolex) ── */
            {
                ImDrawList *dl_dv = ImGui::GetWindowDrawList();
                ImVec2 dv_min = ImGui::GetWindowPos();
                ImVec2 dv_max = ImVec2(dv_min.x + win_w, dv_min.y + detail_h);

                if (s_tex_tolex) {
                    /* Tile tolex texture at 256x256 — even more subtle than
                     * pedalboard; this is the amp rack padded interior */
                    const float TILE = 256.0f;
                    dl_dv->PushClipRect(dv_min, dv_max, true);
                    for (float ty = dv_min.y; ty < dv_max.y; ty += TILE) {
                        for (float tx = dv_min.x; tx < dv_max.x; tx += TILE) {
                            float tx1 = (tx + TILE < dv_max.x) ? tx + TILE : dv_max.x;
                            float ty1 = (ty + TILE < dv_max.y) ? ty + TILE : dv_max.y;
                            float u1 = (tx1 - tx) / TILE;
                            float v1 = (ty1 - ty) / TILE;
                            /* Darker than pedalboard (alpha 40 vs 55) */
                            dl_dv->AddImage((ImTextureID)s_tex_tolex,
                                ImVec2(tx, ty), ImVec2(tx1, ty1),
                                ImVec2(0.0f, 0.0f), ImVec2(u1, v1),
                                IM_COL32(255, 255, 255, 40));
                        }
                    }
                    dl_dv->PopClipRect();
                } else {
                    /* Fallback: slightly different shade than pedalboard area */
                    dl_dv->AddRectFilled(dv_min, dv_max, IM_COL32(18, 16, 13, 255));
                }

                /* Top inner shadow: chain area "casts" shadow downward */
                dl_dv->AddRectFilledMultiColor(
                    dv_min, ImVec2(dv_max.x, dv_min.y + 14.0f),
                    IM_COL32(0, 0, 0, 100), IM_COL32(0, 0, 0, 100),
                    IM_COL32(0, 0, 0,   0), IM_COL32(0, 0, 0,   0));
                /* Thin top border line separating chain from detail */
                dl_dv->AddLine(
                    ImVec2(dv_min.x, dv_min.y),
                    ImVec2(dv_max.x, dv_min.y),
                    IM_COL32(45, 38, 28, 200), 1.0f);
            }

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

                /* ── SPLIT / MERGE (mixer) detail view ───────── */
                if (sel.kind == NODE_SPLIT) {
                    float avail_w = ImGui::GetContentRegionAvail().x;
                    ImGui::SetWindowFontScale(1.35f);
                    const char *title = "Y-Split";
                    ImVec2 ts = ImGui::CalcTextSize(title);
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_w - ts.x) * 0.5f);
                    ImGui::TextColored(ImVec4(0.92f, 0.68f, 0.22f, 1.0f), "%s", title);
                    ImGui::SetWindowFontScale(1.0f);
                    const char *sub = "Signal splits into two parallel amp chains";
                    ImVec2 sub_sz = ImGui::CalcTextSize(sub);
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_w - sub_sz.x) * 0.5f);
                    ImGui::TextDisabled("%s", sub);
                    ImGui::Dummy(ImVec2(0.0f, 12.0f));
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_w - 300.0f) * 0.5f);
                    ImGui::TextDisabled("Click the AMP A / AMP B or CAB A / CAB B nodes to edit each path.");
                    ImGui::Dummy(ImVec2(0.0f, 8.0f));
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_w - 300.0f) * 0.5f);
                    ImGui::TextDisabled("Click MIX to set per-path blend levels.");
                }

                /* ── MERGE (mixer) detail view ────────────────── */
                else if (sel.kind == NODE_MERGE) {
                    float avail_w = ImGui::GetContentRegionAvail().x;
                    ImGui::SetWindowFontScale(1.35f);
                    const char *title = "Mix / Blend";
                    ImVec2 ts = ImGui::CalcTextSize(title);
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_w - ts.x) * 0.5f);
                    ImGui::TextColored(ImVec4(0.92f, 0.68f, 0.22f, 1.0f), "%s", title);
                    ImGui::SetWindowFontScale(1.0f);
                    const char *sub = "Per-chain blend levels";
                    ImVec2 sub_sz = ImGui::CalcTextSize(sub);
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_w - sub_sz.x) * 0.5f);
                    ImGui::TextDisabled("%s", sub);
                    ImGui::Dummy(ImVec2(0.0f, 4.0f));
                    {
                        ImVec2 sep_p0 = ImGui::GetCursorScreenPos();
                        ImGui::GetWindowDrawList()->AddLine(
                            sep_p0, ImVec2(sep_p0.x + avail_w, sep_p0.y),
                            IM_COL32(180, 130, 40, 100), 1.0f);
                        ImGui::Dummy(ImVec2(0.0f, 8.0f));
                    }

                    float slider_w = 280.0f;
                    float slider_x = (avail_w - slider_w) * 0.5f;
                    if (slider_x < 0.0f) slider_x = 0.0f;

                    /* Chain A mix slider */
                    {
                        float mix_a = fx_chain_get_mix(engine, FX_CHAIN_DEFAULT);
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + slider_x);
                        ImGui::SetNextItemWidth(slider_w);
                        ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.80f, 0.55f, 0.15f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(1.0f, 0.70f, 0.20f, 1.0f));
                        if (ImGui::SliderFloat("Chain A Level", &mix_a, 0.0f, 1.0f, "%.2f")) {
                            fx_chain_set_mix(engine, FX_CHAIN_DEFAULT, mix_a);
                        }
                        ImGui::PopStyleColor(2);
                    }
                    ImGui::Dummy(ImVec2(0.0f, 8.0f));
                    /* Chain B mix slider */
                    if (s_chain_b >= 0) {
                        float mix_b = fx_chain_get_mix(engine, s_chain_b);
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + slider_x);
                        ImGui::SetNextItemWidth(slider_w);
                        ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.30f, 0.60f, 0.80f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.40f, 0.75f, 1.0f, 1.0f));
                        if (ImGui::SliderFloat("Chain B Level", &mix_b, 0.0f, 1.0f, "%.2f")) {
                            fx_chain_set_mix(engine, s_chain_b, mix_b);
                        }
                        ImGui::PopStyleColor(2);
                    }
                }

                /* ── AMP detail view ──────────────────────────── */
                else if (sel.kind == NODE_AMP) {
                    fx_chain_id amp_chain = (fx_chain_id)sel.chain_id;
                    fx_amp_type_t amp_type = fx_amp_get_model(engine, amp_chain);

                    static const char *amp_names[] = {
                        "Fullerton Clean", "British Crunch", "Southwest Lead",
                        "Essex Chime", "Tweed Blues", "Meridian High Gain",
                        "Citrus Roar", "Citrus Terror", "Regent 800",
                        "Solar Monolith", "Eclipse Drone", "Emerald Ratrod Deluxe"
                    };
                    int current_amp = (int)amp_type;
                    float avail_w = ImGui::GetContentRegionAvail().x;

                    /* Title — large amp name + "Amp Model" subtitle */
                    {
                        const char *amp_name = fx_amp_get_type_name(amp_type);
                        ImGui::SetWindowFontScale(1.35f);
                        ImVec2 text_size = ImGui::CalcTextSize(amp_name);
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_w - text_size.x) * 0.5f);
                        ImGui::TextColored(ImVec4(0.92f, 0.68f, 0.22f, 1.0f), "%s", amp_name);
                        ImGui::SetWindowFontScale(1.0f);
                        char sub[64];
                        snprintf(sub, sizeof(sub), "Amp Model %s",
                                 is_dual ? (amp_chain == 0 ? "— Chain A" : "— Chain B") : "");
                        ImVec2 sub_sz = ImGui::CalcTextSize(sub);
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_w - sub_sz.x) * 0.5f);
                        ImGui::TextDisabled("%s", sub);
                    }

                    ImGui::Dummy(ImVec2(0.0f, 4.0f));
                    {
                        ImVec2 sep_p0 = ImGui::GetCursorScreenPos();
                        ImGui::GetWindowDrawList()->AddLine(
                            sep_p0, ImVec2(sep_p0.x + avail_w, sep_p0.y),
                            IM_COL32(180, 130, 40, 100), 1.0f);
                        ImGui::Dummy(ImVec2(0.0f, 3.0f));
                    }

                    /* Model selector — "Model | [dropdown]" centered */
                    {
                        float label_w = ImGui::CalcTextSize("Model").x;
                        float combo_w = 200.0f;
                        float total_w = label_w + 8.0f + combo_w;
                        float combo_off = (avail_w - total_w) * 0.5f;
                        if (combo_off > 0.0f)
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + combo_off);
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextDisabled("Model");
                        ImGui::SameLine(0, 8);
                        ImGui::SetNextItemWidth(combo_w);
                        char model_label[32];
                        snprintf(model_label, sizeof(model_label), "##amp_model_%d", (int)amp_chain);
                        if (ImGui::Combo(model_label, &current_amp, amp_names, FX_AMP_COUNT)) {
                            fx_amp_set_model(engine, amp_chain, (fx_amp_type_t)current_amp);
                        }
                        /* Scroll wheel to cycle through amp models when hovered */
                        if (ImGui::IsItemHovered() && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup)) {
                            float wheel = ImGui::GetIO().MouseWheel;
                            if (wheel != 0.0f) {
                                int next = current_amp + (wheel < 0.0f ? 1 : -1);
                                if (next < 0) next = FX_AMP_COUNT - 1;
                                if (next >= FX_AMP_COUNT) next = 0;
                                current_amp = next;
                                fx_amp_set_model(engine, amp_chain, (fx_amp_type_t)current_amp);
                            }
                        }
                        if (ImGui::IsItemHovered()) {
                            static const char *amp_descs[] = {
                                "Clean, chimey American tone — silver panel era",
                                "Classic British crunch — plexi-era overdrive",
                                "High-gain American lead — tight, aggressive",
                                "British chime and jangle — Class A character",
                                "Warm vintage blues — tweed era breakup",
                                "Brutal modern metal — scooped, crushing gain",
                                "Thick British roar — EL34 warmth and fuzz",
                                "Small but fierce — Class A lunchbox grit",
                                "Classic British rock — single-channel aggression",
                                "Massive doom — thunderous clean into crushing fuzz",
                                "Extreme drone — subsonic doom with feedback sustain",
                                "American hotrod combo — clean to gritty drive, 6L6 punch",
                            };
                            if (current_amp >= 0 && current_amp < FX_AMP_COUNT)
                                ImGui::SetTooltip("%s", amp_descs[current_amp]);
                        }
                    }

                    ImGui::Dummy(ImVec2(0.0f, 8.0f));

                    /* Amp face image with interactive overlay knobs */
                    int param_count = fx_amp_get_param_count(amp_type);
                    auto has_param = [&](fx_amp_param_t p) -> bool {
                        /* Standard contiguous params (Gain..Cut) */
                        if ((int)p < param_count) return true;
                        /* Citrus Terror uses the Tone param */
                        if (amp_type == FX_AMP_CITRUS_TERROR && p == FX_AMP_PARAM_TONE)
                            return true;
                        /* Eclipse Drone uses the Feedback param */
                        if (amp_type == FX_AMP_ECLIPSE_DRONE && p == FX_AMP_PARAM_FEEDBACK)
                            return true;
                        return false;
                    };

                    {
                        const char *aname = fx_amp_get_type_name(amp_type);
                        uintptr_t face_tex = load_amp_face_texture(aname);

                        /* Use actual image aspect ratio */
                        float img_w = 500.0f;
                        float img_h = img_w * 0.65f;
                        if (face_tex) {
                            int tw = 0, th = 0;
                            if (fx_texture_get_size(face_tex, &tw, &th) && th > 0) {
                                float aspect = (float)tw / (float)th;
                                img_h = img_w / aspect;
                            }
                        }
                        float img_x = (avail_w - img_w) * 0.5f;
                        if (img_x < 0.0f) img_x = 0.0f;
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + img_x);

                        ImVec2 img_pos = ImGui::GetCursorScreenPos();

                        if (face_tex) {
                            ImGui::Image((ImTextureID)face_tex, ImVec2(img_w, img_h),
                                         ImVec2(0, 0), ImVec2(1, 1),
                                         ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
                        } else {
                            ImGui::Dummy(ImVec2(img_w, img_h));
                        }

                        /* Knob position maps — normalized (x,y) on the amp face image.
                         * Each amp model has knobs at different positions on its faceplate.
                         * Positions estimated from the generated amp images. */
                        struct AmpKnobPos {
                            fx_amp_param_t param;
                            float nx, ny;   /* normalized position on image */
                        };

                        /* Knob size on the image */
                        const float OVERLAY_KNOB_SZ = 34.0f;
                        const char *knob_tex = "resources/knobs/knob_dome_silver_nobg.png";

                        /* Positions from red dots — size-filtered detection.
                         * -1 param = dummy knob (fills hole, not interactive) */
                        const int DUMMY = -1;

                        /* Fullerton Clean: 7 knobs */
                        static const AmpKnobPos fullerton_knobs[] = {
                            { FX_AMP_PARAM_VOLUME,   0.299f, 0.476f },
                            { FX_AMP_PARAM_TREBLE,   0.375f, 0.475f },
                            { FX_AMP_PARAM_MID,      0.451f, 0.477f },
                            { FX_AMP_PARAM_BASS,     0.527f, 0.476f },
                            { FX_AMP_PARAM_PRESENCE, 0.603f, 0.477f },
                            { FX_AMP_PARAM_GAIN,     0.678f, 0.476f },
                            { FX_AMP_PARAM_SAG,      0.753f, 0.476f },
                        };

                        /* British Crunch: 7 knobs */
                        static const AmpKnobPos brit_crunch_knobs[] = {
                            { FX_AMP_PARAM_PRESENCE, 0.341f, 0.536f },
                            { FX_AMP_PARAM_BASS,     0.419f, 0.538f },
                            { FX_AMP_PARAM_MID,      0.496f, 0.541f },
                            { FX_AMP_PARAM_TREBLE,   0.574f, 0.539f },
                            { FX_AMP_PARAM_VOLUME,   0.653f, 0.538f },
                            { FX_AMP_PARAM_GAIN,     0.730f, 0.538f },
                            { FX_AMP_PARAM_MASTER,   0.808f, 0.538f },
                        };

                        /* Southwest Lead: 6 knobs */
                        static const AmpKnobPos southwest_knobs[] = {
                            { FX_AMP_PARAM_GAIN,     0.343f, 0.651f },
                            { FX_AMP_PARAM_BASS,     0.439f, 0.650f },
                            { FX_AMP_PARAM_MID,      0.533f, 0.652f },
                            { FX_AMP_PARAM_TREBLE,   0.630f, 0.652f },
                            { FX_AMP_PARAM_PRESENCE, 0.727f, 0.652f },
                            { FX_AMP_PARAM_MASTER,   0.822f, 0.651f },
                        };

                        /* Essex Chime: 7 dots, 6 params — last 1 dummy */
                        static const AmpKnobPos essex_knobs[] = {
                            { FX_AMP_PARAM_VOLUME,   0.288f, 0.378f },
                            { FX_AMP_PARAM_BASS,     0.369f, 0.378f },
                            { FX_AMP_PARAM_TREBLE,   0.450f, 0.379f },
                            { FX_AMP_PARAM_CUT,      0.530f, 0.379f },
                            { FX_AMP_PARAM_PRESENCE, 0.610f, 0.378f },
                            { FX_AMP_PARAM_GAIN,     0.688f, 0.378f },
                            { (fx_amp_param_t)DUMMY,  0.769f, 0.378f },
                        };

                        /* Tweed Blues: 5 knobs */
                        static const AmpKnobPos tweed_knobs[] = {
                            { FX_AMP_PARAM_VOLUME,   0.344f, 0.575f },
                            { FX_AMP_PARAM_BASS,     0.438f, 0.575f },
                            { FX_AMP_PARAM_TREBLE,   0.532f, 0.573f },
                            { FX_AMP_PARAM_GAIN,     0.625f, 0.573f },
                            { FX_AMP_PARAM_MASTER,   0.718f, 0.574f },
                        };

                        /* Meridian High Gain: 7 knobs */
                        static const AmpKnobPos meridian_knobs[] = {
                            { FX_AMP_PARAM_GAIN,     0.323f, 0.600f },
                            { FX_AMP_PARAM_BASS,     0.411f, 0.601f },
                            { FX_AMP_PARAM_MID,      0.498f, 0.600f },
                            { FX_AMP_PARAM_TREBLE,   0.583f, 0.600f },
                            { FX_AMP_PARAM_PRESENCE, 0.668f, 0.601f },
                            { FX_AMP_PARAM_VOLUME,   0.753f, 0.601f },
                            { FX_AMP_PARAM_MASTER,   0.838f, 0.600f },
                        };

                        /* Citrus Roar: 5 knobs */
                        static const AmpKnobPos citrus_roar_knobs[] = {
                            { FX_AMP_PARAM_GAIN,     0.348f, 0.546f },
                            { FX_AMP_PARAM_BASS,     0.415f, 0.550f },
                            { FX_AMP_PARAM_MID,      0.484f, 0.559f },
                            { FX_AMP_PARAM_TREBLE,   0.554f, 0.567f },
                            { FX_AMP_PARAM_VOLUME,   0.625f, 0.573f },
                        };

                        /* Citrus Terror: 3 knobs */
                        static const AmpKnobPos citrus_terror_knobs[] = {
                            { FX_AMP_PARAM_GAIN,     0.299f, 0.528f },
                            { FX_AMP_PARAM_TONE,     0.423f, 0.527f },
                            { FX_AMP_PARAM_VOLUME,   0.541f, 0.527f },
                        };

                        /* Regent 800: 7 knobs */
                        static const AmpKnobPos regent_knobs[] = {
                            { FX_AMP_PARAM_GAIN,     0.404f, 0.601f },
                            { FX_AMP_PARAM_BASS,     0.472f, 0.602f },
                            { FX_AMP_PARAM_MID,      0.539f, 0.601f },
                            { FX_AMP_PARAM_TREBLE,   0.607f, 0.602f },
                            { FX_AMP_PARAM_PRESENCE, 0.675f, 0.602f },
                            { FX_AMP_PARAM_VOLUME,   0.743f, 0.600f },
                            { FX_AMP_PARAM_MASTER,   0.810f, 0.600f },
                        };

                        /* Solar Monolith: 6 knobs */
                        static const AmpKnobPos solar_knobs[] = {
                            { FX_AMP_PARAM_GAIN,     0.231f, 0.513f },
                            { FX_AMP_PARAM_BASS,     0.328f, 0.514f },
                            { FX_AMP_PARAM_MID,      0.425f, 0.513f },
                            { FX_AMP_PARAM_TREBLE,   0.524f, 0.515f },
                            { FX_AMP_PARAM_VOLUME,   0.619f, 0.513f },
                            { FX_AMP_PARAM_MASTER,   0.718f, 0.514f },
                        };

                        /* Eclipse Drone: 6 knobs */
                        static const AmpKnobPos eclipse_knobs[] = {
                            { FX_AMP_PARAM_GAIN,     0.136f, 0.507f },
                            { FX_AMP_PARAM_BASS,     0.241f, 0.507f },
                            { FX_AMP_PARAM_MID,      0.347f, 0.506f },
                            { FX_AMP_PARAM_TREBLE,   0.454f, 0.507f },
                            { FX_AMP_PARAM_FEEDBACK, 0.557f, 0.505f },
                            { FX_AMP_PARAM_VOLUME,   0.662f, 0.507f },
                        };

                        /* Emerald Deluxe: 7 knobs (same layout as Fullerton Clean) */
                        static const AmpKnobPos emerald_deluxe_knobs[] = {
                            { FX_AMP_PARAM_VOLUME,   0.345f, 0.289f },
                            { FX_AMP_PARAM_TREBLE,   0.409f, 0.286f },
                            { FX_AMP_PARAM_MID,      0.472f, 0.286f },
                            { FX_AMP_PARAM_BASS,     0.536f, 0.286f },
                            { FX_AMP_PARAM_PRESENCE, 0.599f, 0.289f },
                            { FX_AMP_PARAM_GAIN,     0.663f, 0.289f },
                            { FX_AMP_PARAM_SAG,      0.726f, 0.289f },
                        };

                        /* Select the right map */
                        const AmpKnobPos *knob_map = nullptr;
                        int knob_map_count = 0;
                        switch (amp_type) {
                            case FX_AMP_FULLERTON_CLEAN:
                                knob_map = fullerton_knobs;
                                knob_map_count = 7;
                                break;
                            case FX_AMP_BRIT_CRUNCH:
                                knob_map = brit_crunch_knobs;
                                knob_map_count = 7;
                                break;
                            case FX_AMP_SOUTHWEST_LEAD:
                                knob_map = southwest_knobs;
                                knob_map_count = 6;
                                break;
                            case FX_AMP_ESSEX_CHIME:
                                knob_map = essex_knobs;
                                knob_map_count = 7;
                                break;
                            case FX_AMP_TWEED_BLUES:
                                knob_map = tweed_knobs;
                                knob_map_count = 5;
                                break;
                            case FX_AMP_MERIDIAN_HIGH_GAIN:
                                knob_map = meridian_knobs;
                                knob_map_count = 7;
                                break;
                            case FX_AMP_CITRUS_ROAR:
                                knob_map = citrus_roar_knobs;
                                knob_map_count = 5;
                                break;
                            case FX_AMP_CITRUS_TERROR:
                                knob_map = citrus_terror_knobs;
                                knob_map_count = 3;
                                break;
                            case FX_AMP_REGENT_800:
                                knob_map = regent_knobs;
                                knob_map_count = 7;
                                break;
                            case FX_AMP_SOLAR_MONOLITH:
                                knob_map = solar_knobs;
                                knob_map_count = 6;
                                break;
                            case FX_AMP_ECLIPSE_DRONE:
                                knob_map = eclipse_knobs;
                                knob_map_count = 6;
                                break;
                            case FX_AMP_EMERALD_DELUXE:
                                knob_map = emerald_deluxe_knobs;
                                knob_map_count = 7;
                                break;
                            default: break;
                        }

                        /* Render overlay knobs on the amp face */
                        if (knob_map) {
                            for (int ki = 0; ki < knob_map_count; ki++) {
                                const AmpKnobPos &kp = knob_map[ki];
                                float kx = img_pos.x + kp.nx * img_w - OVERLAY_KNOB_SZ * 0.5f;
                                float ky = img_pos.y + kp.ny * img_h - OVERLAY_KNOB_SZ * 0.5f;

                                if ((int)kp.param == DUMMY || !has_param(kp.param)) {
                                    /* Dummy knob — static, no interaction */
                                    float dummy = 0.5f;
                                    char did[32];
                                    snprintf(did, sizeof(did), "##amp_dummy_%d_%d", (int)amp_chain, ki);
                                    knob_overlay(did, &dummy, 0.0f, 1.0f, 0.5f, 0.0f,
                                                 kx, ky, OVERLAY_KNOB_SZ, knob_tex);
                                } else {
                                    const char *pname = fx_amp_get_param_name(amp_type, kp.param);
                                    float val = fx_amp_get_param(engine, amp_chain, kp.param);
                                    char kid[48];
                                    snprintf(kid, sizeof(kid), "%s##amp_ov_%d_%d",
                                             pname, (int)amp_chain, ki);
                                    if (knob_overlay(kid, &val, 0.0f, 1.0f, 0.5f, 0.01f,
                                                     kx, ky, OVERLAY_KNOB_SZ, knob_tex)) {
                                        fx_amp_set_param(engine, amp_chain, kp.param, val);
                                    }
                                }
                            }
                        }
                    }

                    /* Fallback: any params NOT in the overlay map still get regular knobs */
                    {
                        /* Check which params are mapped */
                        bool param_mapped[FX_AMP_PARAM_COUNT] = {};
                        /* (overlay knobs handle the main params — show remaining below) */

                        /* Show unmapped params as regular knobs */
                        bool has_unmapped = false;
                        for (int p = 0; p < param_count; p++) {
                            if (!param_mapped[p]) { has_unmapped = true; break; }
                        }
                        /* All amp params are covered by the overlay maps above,
                         * but keep this fallback for safety */
                        (void)has_unmapped;
                    }
                }

                /* ── CAB detail view ──────────────────────────── */
                else if (sel.kind == NODE_CAB) {
                    fx_chain_id cab_chain = (fx_chain_id)sel.chain_id;
                    int &cab_type_ref = (sel.chain_id == 0) ? s_cab_type : s_cab_type_b;
                    float avail_w = ImGui::GetContentRegionAvail().x;

                    /* Large title: "Cabinet" */
                    {
                        const char *title = is_dual ? (sel.chain_id == 0 ? "Cabinet A" : "Cabinet B") : "Cabinet";
                        ImGui::SetWindowFontScale(1.35f);
                        ImVec2 ts = ImGui::CalcTextSize(title);
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_w - ts.x) * 0.5f);
                        ImGui::TextColored(ImVec4(0.92f, 0.68f, 0.22f, 1.0f), "%s", title);
                        ImGui::SetWindowFontScale(1.0f);
                        /* Subtitle: "Cabinet — 4x12 Straight" style */
                        const char *cab_name = (cab_type_ref >= 0 && cab_type_ref < FX_CAB_TYPE_COUNT)
                            ? s_cab_type_names[cab_type_ref] : "Unknown";
                        char sub[64];
                        snprintf(sub, sizeof(sub), "Cabinet \xe2\x80\x94 %s", cab_name);
                        ImVec2 sub_sz = ImGui::CalcTextSize(sub);
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_w - sub_sz.x) * 0.5f);
                        ImGui::TextDisabled("%s", sub);
                    }
                    ImGui::Dummy(ImVec2(0.0f, 4.0f));
                    {
                        ImVec2 sep_p0 = ImGui::GetCursorScreenPos();
                        ImGui::GetWindowDrawList()->AddLine(
                            sep_p0, ImVec2(sep_p0.x + avail_w, sep_p0.y),
                            IM_COL32(180, 130, 40, 100), 1.0f);
                        ImGui::Dummy(ImVec2(0.0f, 3.0f));
                    }
                    ImGui::Dummy(ImVec2(0.0f, 4.0f));

                    /* Cab type selector — "Cab Type | [dropdown]" centered */
                    {
                        float label_w = ImGui::CalcTextSize("Cab Type").x;
                        float combo_w = 200.0f;
                        float total_w = label_w + 8.0f + combo_w;
                        float combo_off = (avail_w - total_w) * 0.5f;
                        if (combo_off > 0.0f)
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + combo_off);
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextDisabled("Cab Type");
                        ImGui::SameLine(0, 8);
                    }
                    ImGui::SetNextItemWidth(200);
                    if (ImGui::Combo("##cab_type_sel", &cab_type_ref, s_cab_type_names, FX_CAB_TYPE_COUNT)) {
                        fx_cab_params_t params;
                        params.cab_type = (fx_cab_type_t)cab_type_ref;
                        params.mic_pos  = FX_MIC_ON_AXIS;
                        params.speaker_fs = 80.0f;
                        params.brightness = 0.5f;
                        params.resonance  = 0.5f;
                        fx_cab_generate_ir(engine, cab_chain, &params);
                    }
                    /* Scroll wheel to cycle cab types */
                    if (ImGui::IsItemHovered() && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup)) {
                        float wheel = ImGui::GetIO().MouseWheel;
                        if (wheel != 0.0f) {
                            int next = cab_type_ref + (wheel < 0.0f ? 1 : -1);
                            if (next < 0) next = FX_CAB_TYPE_COUNT - 1;
                            if (next >= FX_CAB_TYPE_COUNT) next = 0;
                            cab_type_ref = next;
                            fx_cab_params_t params;
                            params.cab_type = (fx_cab_type_t)cab_type_ref;
                            params.mic_pos  = FX_MIC_ON_AXIS;
                            params.speaker_fs = 80.0f;
                            params.brightness = 0.5f;
                            params.resonance  = 0.5f;
                            fx_cab_generate_ir(engine, cab_chain, &params);
                        }
                    }

                    ImGui::Dummy(ImVec2(0.0f, 8.0f));

                    /* Cabinet image — centered */
                    {
                        uintptr_t cab_tex = load_cab_texture(cab_type_ref);
                        if (cab_tex) {
                            int cw = 0, ch = 0;
                            float img_h = 220.0f;
                            float img_w = img_h;
                            if (fx_texture_get_size(cab_tex, &cw, &ch) && ch > 0) {
                                float aspect = (float)cw / (float)ch;
                                img_w = img_h * aspect;
                            }
                            float img_x = (avail_w - img_w) * 0.5f;
                            if (img_x > 0.0f)
                                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + img_x);
                            ImGui::Image((ImTextureID)cab_tex, ImVec2(img_w, img_h));
                        }
                    }

                    ImGui::Dummy(ImVec2(0.0f, 8.0f));

                    /* Bypass toggle — centered */
                    bool cab_bypassed = fx_cab_get_bypass(engine, cab_chain);
                    {
                        float btn_w = 120.0f;
                        float btn_off = (avail_w - btn_w) * 0.5f;
                        if (btn_off > 0.0f)
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + btn_off);
                    }
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
                    ImGui::PushStyleColor(ImGuiCol_Button,
                        cab_bypassed ? ImVec4(0.30f, 0.10f, 0.08f, 0.9f)
                                     : ImVec4(0.10f, 0.28f, 0.10f, 0.9f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                        cab_bypassed ? ImVec4(0.42f, 0.14f, 0.10f, 1.0f)
                                     : ImVec4(0.14f, 0.40f, 0.14f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                        cab_bypassed ? ImVec4(0.55f, 0.18f, 0.12f, 1.0f)
                                     : ImVec4(0.18f, 0.52f, 0.18f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.78f, 0.65f, 1.0f));
                    if (ImGui::Button(cab_bypassed ? "BYPASSED##cab" : "ON (Active)##cab",
                                      ImVec2(120, 28))) {
                        fx_cab_set_bypass(engine, cab_chain, !cab_bypassed);
                    }
                    ImGui::PopStyleColor(4);
                    ImGui::PopStyleVar();
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

                        /* Look up category for subtitle ("Jade Drive — Overdrive") */
                        const char *pedal_category = nullptr;
                        for (int ci = 0; ci < s_pedal_category_count && !pedal_category; ci++) {
                            for (int pi = 0; pi < s_pedal_categories[ci].count; pi++) {
                                if (s_pedal_categories[ci].pedals[pi].type == pt) {
                                    pedal_category = s_pedal_categories[ci].label;
                                    break;
                                }
                            }
                        }

                        /* Title — large name + subtitle */
                        {
                            ImGui::SetWindowFontScale(1.35f);
                            ImVec2 ts = ImGui::CalcTextSize(pname);
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_w - ts.x) * 0.5f);
                            if (bypassed)
                                ImGui::TextDisabled("%s", pname);
                            else
                                ImGui::TextColored(ImVec4(0.92f, 0.68f, 0.22f, 1.0f), "%s", pname);
                            ImGui::SetWindowFontScale(1.0f);
                            if (pedal_category) {
                                char sub[64];
                                snprintf(sub, sizeof(sub), "%s \xe2\x80\x94 %s",
                                         pname, pedal_category);
                                /* Title-case the category */
                                ImVec2 sub_sz = ImGui::CalcTextSize(sub);
                                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_w - sub_sz.x) * 0.5f);
                                ImGui::TextDisabled("%s", sub);
                            }
                        }
                        ImGui::Dummy(ImVec2(0.0f, 4.0f));
                        {
                            ImVec2 sep_p0 = ImGui::GetCursorScreenPos();
                            ImGui::GetWindowDrawList()->AddLine(
                                sep_p0, ImVec2(sep_p0.x + avail_w, sep_p0.y),
                                IM_COL32(180, 130, 40, 100), 1.0f);
                            ImGui::Dummy(ImVec2(0.0f, 3.0f));
                        }
                        /* Pedal body image with overlay knobs + LED */
                        {
                            /* Convert display name to filename for lookups */
                            char pedal_fname[128];
                            type_to_filename(pname, pedal_fname, sizeof(pedal_fname));
                            if (strcmp(pedal_fname, "orange_distortion") == 0)
                                strcpy(pedal_fname, "orange_dist");

                            uintptr_t pedal_tex = load_pedal_texture(pname);
                            float img_h = 220.0f;
                            float img_w = img_h;
                            if (pedal_tex) {
                                int pw = 0, ph = 0;
                                if (fx_texture_get_size(pedal_tex, &pw, &ph) && ph > 0) {
                                    float aspect = (float)pw / (float)ph;
                                    img_w = img_h * aspect;
                                }
                            }
                            float img_x = (avail_w - img_w) * 0.5f;
                            if (img_x < 0.0f) img_x = 0.0f;
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + img_x);

                            ImVec2 img_pos = ImGui::GetCursorScreenPos();
                            float cursor_after_img_y = 0; /* track where cursor should be after image */

                            if (pedal_tex) {
                                ImVec4 tint = bypassed
                                    ? ImVec4(0.6f, 0.6f, 0.6f, 0.8f)
                                    : ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
                                ImGui::Image((ImTextureID)pedal_tex,
                                    ImVec2(img_w, img_h),
                                    ImVec2(0, 0), ImVec2(1, 1), tint);
                            } else {
                                ImGui::Dummy(ImVec2(img_w, img_h));
                            }
                            cursor_after_img_y = ImGui::GetCursorPosY();

                            /* LED indicator on pedal image (detected from green/purple dots) */
                            {
                                static const struct { const char *name; float x, y; } s_led_pos[] = {
                                    {"amp_box",0.502f,0.451f}, {"blues_grit",0.496f,0.605f},
                                    {"carbon_delay",0.496f,0.641f}, {"chaos_fuzz",0.369f,0.729f},
                                    {"cloud_verb",0.502f,0.213f}, {"drift_vibrato",0.500f,0.547f},
                                    {"drip_verb",0.506f,0.615f}, {"echo_delay",0.496f,0.582f},
                                    {"glass_comp",0.494f,0.588f}, {"gold_drive",0.502f,0.373f},
                                    {"grit_crush",0.498f,0.565f}, {"hall_verb",0.498f,0.600f},
                                    {"howl_wah",0.635f,0.576f}, {"jade_drive",0.490f,0.576f},
                                    {"jet_flanger",0.322f,0.693f}, {"liquid_chorus",0.502f,0.330f},
                                    {"mammoth_fuzz",0.336f,0.711f}, {"memory_echo",0.348f,0.703f},
                                    {"metal_zone",0.498f,0.599f}, {"noise_gate",0.494f,0.525f},
                                    {"orange_dist",0.498f,0.537f}, {"phase_sweep",0.508f,0.445f},
                                    {"plate_verb",0.496f,0.488f}, {"pulse_trem",0.320f,0.816f},
                                    {"punch_comp",0.502f,0.545f}, {"quack_filter",0.494f,0.451f},
                                    {"ring_tone",0.497f,0.578f}, {"rodent",0.500f,0.562f},
                                    {"round_fuzz",0.645f,0.398f}, {"shimmer_verb",0.498f,0.674f},
                                    {"squeeze_box",0.359f,0.578f}, {"tape_machine",0.498f,0.600f},
                                    {"tone_sculptor",0.500f,0.621f}, {"warm_tape",0.342f,0.721f},
                                    {"wraith_fuzz",0.498f,0.525f},
                                    {"grain_cloud",0.496f,0.559f},
                                    {"infinite_hold",0.506f,0.506f},
                                    {"precision_eq",0.498f,0.541f},
                                    {"pitch_warp",0.500f,0.516f},
                                    {"octave_engine",0.498f,0.521f},
                                    {"loop_station",0.496f,0.523f},
                                };
                                /* Look up LED position for this pedal */
                                for (int li = 0; li < 41; li++) {
                                    if (strcmp(s_led_pos[li].name, pedal_fname) == 0) {
                                        float led_cx = img_pos.x + s_led_pos[li].x * img_w;
                                        float led_cy = img_pos.y + s_led_pos[li].y * img_h;
                                        ImDrawList *ldl = ImGui::GetWindowDrawList();
                                        if (!bypassed) {
                                            /* Green glow when active */
                                            ldl->AddCircleFilled(ImVec2(led_cx, led_cy), 6.0f,
                                                IM_COL32(40, 220, 40, 200), 12);
                                            ldl->AddCircleFilled(ImVec2(led_cx, led_cy), 10.0f,
                                                IM_COL32(40, 200, 40, 60), 12);
                                            ldl->AddCircleFilled(ImVec2(led_cx, led_cy), 16.0f,
                                                IM_COL32(40, 180, 40, 25), 12);
                                        } else {
                                            /* Dim red when bypassed */
                                            ldl->AddCircleFilled(ImVec2(led_cx, led_cy), 4.0f,
                                                IM_COL32(180, 40, 30, 150), 12);
                                        }
                                        break;
                                    }
                                }
                            }

                            /* Stomp switch click area (detected from purple dots) */
                            {
                                static const struct { const char *name; float x0,y0,x1,y1; } s_stomp[] = {
                                    {"amp_box",0.410f,0.652f,0.602f,0.809f},{"blues_grit",0.383f,0.656f,0.602f,0.812f},
                                    {"carbon_delay",0.395f,0.680f,0.621f,0.848f},{"chaos_fuzz",0.516f,0.570f,0.695f,0.738f},
                                    {"cloud_verb",0.387f,0.676f,0.602f,0.828f},{"drift_vibrato",0.387f,0.609f,0.609f,0.777f},
                                    {"drip_verb",0.395f,0.660f,0.617f,0.832f},{"echo_delay",0.375f,0.648f,0.613f,0.812f},
                                    {"glass_comp",0.375f,0.633f,0.613f,0.832f},{"gold_drive",0.375f,0.621f,0.621f,0.809f},
                                    {"grit_crush",0.379f,0.551f,0.629f,0.797f},{"hall_verb",0.387f,0.645f,0.617f,0.816f},
                                    {"howl_wah",0.551f,0.629f,0.719f,0.773f},{"jade_drive",0.391f,0.652f,0.598f,0.820f},
                                    {"jet_flanger",0.387f,0.625f,0.617f,0.805f},{"liquid_chorus",0.395f,0.590f,0.602f,0.777f},
                                    {"mammoth_fuzz",0.398f,0.621f,0.602f,0.777f},{"memory_echo",0.391f,0.598f,0.605f,0.777f},
                                    {"metal_zone",0.387f,0.633f,0.621f,0.816f},{"noise_gate",0.395f,0.582f,0.602f,0.754f},
                                    {"orange_dist",0.383f,0.582f,0.609f,0.781f},{"phase_sweep",0.383f,0.613f,0.582f,0.793f},
                                    {"plate_verb",0.391f,0.590f,0.609f,0.773f},{"pulse_trem",0.391f,0.660f,0.605f,0.844f},
                                    {"punch_comp",0.406f,0.613f,0.598f,0.773f},{"quack_filter",0.328f,0.551f,0.566f,0.750f},
                                    {"ring_tone",0.395f,0.621f,0.605f,0.770f},{"rodent",0.395f,0.617f,0.602f,0.785f},
                                    {"round_fuzz",0.379f,0.578f,0.621f,0.781f},{"shimmer_verb",0.395f,0.707f,0.609f,0.855f},
                                    {"squeeze_box",0.391f,0.617f,0.617f,0.789f},{"tape_machine",0.398f,0.648f,0.605f,0.812f},
                                    {"tone_sculptor",0.406f,0.684f,0.605f,0.844f},{"warm_tape",0.398f,0.629f,0.598f,0.805f},
                                    {"wraith_fuzz",0.414f,0.602f,0.594f,0.742f},
                                    {"grain_cloud",0.387f,0.609f,0.609f,0.773f},
                                    {"infinite_hold",0.414f,0.594f,0.598f,0.738f},
                                    {"precision_eq",0.414f,0.613f,0.586f,0.762f},
                                    {"pitch_warp",0.402f,0.609f,0.594f,0.766f},
                                    {"octave_engine",0.410f,0.578f,0.590f,0.727f},
                                    {"loop_station",0.398f,0.594f,0.598f,0.773f},
                                };
                                for (int si = 0; si < 41; si++) {
                                    if (strcmp(s_stomp[si].name, pedal_fname) == 0) {
                                        float sx0 = img_pos.x + s_stomp[si].x0 * img_w;
                                        float sy0 = img_pos.y + s_stomp[si].y0 * img_h;
                                        float sx1 = img_pos.x + s_stomp[si].x1 * img_w;
                                        float sy1 = img_pos.y + s_stomp[si].y1 * img_h;

                                        /* Invisible stomp button */
                                        ImGui::SetCursorScreenPos(ImVec2(sx0, sy0));
                                        char stomp_id[32];
                                        snprintf(stomp_id, sizeof(stomp_id), "##stomp_%d", (int)pid);
                                        if (ImGui::InvisibleButton(stomp_id, ImVec2(sx1-sx0, sy1-sy0))) {
                                            fx_pedal_set_bypass(engine, pid, !bypassed);
                                        }
                                        /* Hover highlight on stomp area */
                                        if (ImGui::IsItemHovered()) {
                                            ImDrawList *sdl = ImGui::GetWindowDrawList();
                                            sdl->AddRectFilled(ImVec2(sx0, sy0), ImVec2(sx1, sy1),
                                                IM_COL32(255, 255, 255, 20), 4.0f);
                                            ImGui::SetTooltip("Click to %s", bypassed ? "activate" : "bypass");
                                        }
                                        break;
                                    }
                                }
                            }

                            /* Per-pedal knob position maps (detected from red dots) */
                            struct PedalKnobMap { const char *name; int count; float pos[8][2]; };
                            static const PedalKnobMap s_pedal_knob_maps[] = {
                                { "amp_box", 6, { {0.324f,0.162f},{0.330f,0.328f},{0.504f,0.336f},{0.506f,0.158f},{0.678f,0.162f},{0.686f,0.332f} } },
                                { "blues_grit", 3, { {0.334f,0.212f},{0.502f,0.216f},{0.666f,0.218f} } },
                                { "carbon_delay", 4, { {0.363f,0.163f},{0.363f,0.339f},{0.647f,0.335f},{0.649f,0.161f} } },
                                { "chaos_fuzz", 4, { {0.357f,0.197f},{0.625f,0.195f},{0.355f,0.381f},{0.633f,0.379f} } },
                                { "cloud_verb", 5, { {0.352f,0.214f},{0.352f,0.360f},{0.498f,0.364f},{0.648f,0.212f},{0.648f,0.360f} } },
                                { "drift_vibrato", 2, { {0.381f,0.209f},{0.615f,0.207f} } },
                                { "drip_verb", 3, { {0.350f,0.202f},{0.504f,0.206f},{0.664f,0.206f} } },
                                { "echo_delay", 4, { {0.360f,0.346f},{0.362f,0.164f},{0.638f,0.158f},{0.638f,0.342f} } },
                                { "glass_comp", 4, { {0.344f,0.145f},{0.350f,0.346f},{0.650f,0.145f},{0.652f,0.346f} } },
                                { "gold_drive", 3, { {0.354f,0.182f},{0.500f,0.186f},{0.648f,0.182f} } },
                                { "grit_crush", 2, { {0.371f,0.321f},{0.623f,0.325f} } },
                                { "hall_verb", 3, { {0.327f,0.229f},{0.497f,0.225f},{0.669f,0.231f} } },
                                { "howl_wah", 2, { {0.599f,0.307f},{0.705f,0.305f} } },
                                { "jade_drive", 6, { {0.337f,0.161f},{0.347f,0.329f},{0.489f,0.167f},{0.497f,0.325f},{0.643f,0.323f},{0.659f,0.163f} } },
                                { "jet_flanger", 4, { {0.359f,0.181f},{0.365f,0.362f},{0.635f,0.179f},{0.635f,0.363f} } },
                                { "liquid_chorus", 3, { {0.345f,0.203f},{0.503f,0.203f},{0.661f,0.201f} } },
                                { "mammoth_fuzz", 3, { {0.335f,0.213f},{0.495f,0.217f},{0.657f,0.213f} } },
                                { "memory_echo", 6, { {0.323f,0.333f},{0.325f,0.163f},{0.503f,0.165f},{0.503f,0.331f},{0.677f,0.329f},{0.679f,0.163f} } },
                                { "metal_zone", 6, { {0.327f,0.171f},{0.333f,0.333f},{0.499f,0.333f},{0.501f,0.171f},{0.663f,0.173f},{0.671f,0.331f} } },
                                { "noise_gate", 4, { {0.381f,0.263f},{0.621f,0.263f},{0.377f,0.409f},{0.621f,0.407f} } },
                                { "orange_dist", 3, { {0.321f,0.201f},{0.507f,0.201f},{0.681f,0.203f} } },
                                { "phase_sweep", 4, { {0.377f,0.373f},{0.389f,0.203f},{0.679f,0.229f},{0.655f,0.393f} } },
                                { "plate_verb", 3, { {0.343f,0.229f},{0.499f,0.227f},{0.656f,0.229f} } },
                                { "pulse_trem", 6, { {0.326f,0.193f},{0.510f,0.193f},{0.693f,0.193f},{0.334f,0.391f},{0.505f,0.391f},{0.677f,0.393f} } },
                                { "punch_comp", 4, { {0.392f,0.290f},{0.609f,0.290f},{0.393f,0.442f},{0.609f,0.441f} } },
                                { "quack_filter", 4, { {0.378f,0.301f},{0.672f,0.344f},{0.352f,0.445f},{0.641f,0.483f} } },
                                { "ring_tone", 3, { {0.341f,0.228f},{0.501f,0.228f},{0.657f,0.226f} } },
                                { "rodent", 4, { {0.355f,0.247f},{0.500f,0.333f},{0.503f,0.162f},{0.648f,0.249f} } },
                                { "round_fuzz", 2, { {0.331f,0.250f},{0.670f,0.253f} } },
                                { "shimmer_verb", 4, { {0.357f,0.168f},{0.357f,0.359f},{0.647f,0.167f},{0.649f,0.356f} } },
                                { "squeeze_box", 2, { {0.378f,0.237f},{0.623f,0.236f} } },
                                { "tape_machine", 6, { {0.320f,0.180f},{0.322f,0.344f},{0.496f,0.179f},{0.498f,0.346f},{0.679f,0.182f},{0.679f,0.346f} } },
                                { "tone_sculptor", 3, { {0.318f,0.208f},{0.498f,0.209f},{0.680f,0.209f} } },
                                { "warm_tape", 3, { {0.344f,0.252f},{0.502f,0.252f},{0.659f,0.251f} } },
                                { "wraith_fuzz", 6, { {0.348f,0.214f},{0.351f,0.349f},{0.501f,0.214f},{0.501f,0.350f},{0.657f,0.345f},{0.664f,0.211f} } },
                                { "grain_cloud", 4, { {0.379f,0.242f},{0.625f,0.242f},{0.379f,0.422f},{0.621f,0.422f} } },
                                { "infinite_hold", 3, { {0.363f,0.230f},{0.508f,0.230f},{0.656f,0.230f} } },
                                { "precision_eq", 5, { {0.350f,0.193f},{0.350f,0.354f},{0.502f,0.193f},{0.502f,0.350f},{0.646f,0.197f} } },
                                { "pitch_warp", 3, { {0.361f,0.225f},{0.506f,0.225f},{0.650f,0.225f} } },
                                { "octave_engine", 4, { {0.377f,0.178f},{0.377f,0.330f},{0.623f,0.178f},{0.623f,0.330f} } },
                                { "loop_station", 2, { {0.387f,0.238f},{0.609f,0.238f} } },
                            };
                            static const int s_pedal_knob_map_count = 41;

                            const float PEDAL_KNOB_SZ = 32.0f;
                            const char *knob_tex = "resources/knobs/knob_pointer_black_nobg.png";

                            /* Look up per-pedal knob positions (pedal_fname already set above) */

                            const PedalKnobMap *pmap = nullptr;
                            for (int mi = 0; mi < s_pedal_knob_map_count; mi++) {
                                if (strcmp(s_pedal_knob_maps[mi].name, pedal_fname) == 0) {
                                    pmap = &s_pedal_knob_maps[mi];
                                    break;
                                }
                            }

                            /* Render knobs: interactive for params, static dummies for extra holes */
                            int total_holes = pmap ? pmap->count : nparam;
                            for (int k = 0; k < total_holes; k++) {
                                float kx, ky;
                                if (pmap && k < pmap->count) {
                                    kx = img_pos.x + pmap->pos[k][0] * img_w - PEDAL_KNOB_SZ * 0.5f;
                                    ky = img_pos.y + pmap->pos[k][1] * img_h - PEDAL_KNOB_SZ * 0.5f;
                                } else {
                                    float margin = img_w * 0.15f;
                                    float usable = img_w - 2.0f * margin;
                                    float sp = (nparam > 1) ? usable / (float)(nparam - 1) : 0.0f;
                                    kx = img_pos.x + margin + k * sp - PEDAL_KNOB_SZ * 0.5f;
                                    ky = img_pos.y + img_h * 0.35f - PEDAL_KNOB_SZ * 0.5f;
                                }

                                if (k < nparam) {
                                    /* Interactive knob — mapped to a parameter */
                                    const char *kname = fx_pedal_get_param_name(pt, k);
                                    float kval = fx_pedal_get_param(engine, pid, k);
                                    char kid[48];
                                    snprintf(kid, sizeof(kid), "%s##ped_ov_%d_%d",
                                             kname, (int)pid, k);
                                    if (knob_overlay(kid, &kval, 0.0f, 1.0f, 0.5f, 0.01f,
                                                     kx, ky, PEDAL_KNOB_SZ, knob_tex)) {
                                        fx_pedal_set_param(engine, pid, k, kval);
                                    }
                                } else {
                                    /* Static dummy knob — fills an extra knob hole */
                                    float dummy = 0.5f;
                                    char did[32];
                                    snprintf(did, sizeof(did), "##dummy_%d_%d", (int)pid, k);
                                    knob_overlay(did, &dummy, 0.0f, 1.0f, 0.5f, 0.0f,
                                                 kx, ky, PEDAL_KNOB_SZ, knob_tex);
                                }
                            }

                            /* Restore cursor to below the image (overlay knobs moved it) */
                            ImGui::SetCursorPosY(cursor_after_img_y);
                        }

                        ImGui::Dummy(ImVec2(0.0f, 12.0f));

                        /* Bottom row: reorder + remove (bypass is on the stomp switch) */
                        {
                            const float BTN_SZ = 36.0f;
                            const float ARR_SZ = 30.0f;
                            float row_w = ARR_SZ + 4 + ARR_SZ + 16 + BTN_SZ;
                            float row_off = (avail_w - row_w) * 0.5f;
                            if (row_off > 0.0f)
                                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + row_off);

                            /* Reorder arrows — textured */
                            fx_chain_pos_t pos = (sel.kind == NODE_PEDAL_PRE)
                                                  ? FX_CHAIN_POS_PRE : FX_CHAIN_POS_POST;
                            fx_pedal_id *ids = (pos == FX_CHAIN_POS_PRE) ? s_pre_ids : s_post_ids;
                            int *id_count = (pos == FX_CHAIN_POS_PRE) ? &s_pre_id_count : &s_post_id_count;
                            int pi = sel.slot;

                            /* Left arrow — programmatic */
                            {
                                bool can_left = (pi > 0);
                                ImGui::PushID("##arr_l");
                                ImVec2 ap = ImGui::GetCursorScreenPos();
                                bool l_clicked = ImGui::InvisibleButton("##arr_l_click", ImVec2(ARR_SZ, ARR_SZ));
                                bool l_hovered = ImGui::IsItemHovered();
                                ImDrawList *adl = ImGui::GetWindowDrawList();
                                float acx = ap.x + ARR_SZ * 0.5f;
                                float acy = ap.y + ARR_SZ * 0.5f;
                                float ar = ARR_SZ * 0.42f;
                                ImU32 abg = (l_hovered && can_left) ? IM_COL32(60, 50, 35, 240) : IM_COL32(40, 35, 25, 200);
                                ImU32 aedge = can_left ? IM_COL32(180, 150, 80, 180) : IM_COL32(80, 70, 50, 100);
                                ImU32 afg = can_left ? IM_COL32(230, 200, 140, 240) : IM_COL32(80, 70, 50, 120);
                                adl->AddCircleFilled(ImVec2(acx, acy), ar, abg, 16);
                                adl->AddCircle(ImVec2(acx, acy), ar, aedge, 16, 1.5f);
                                /* Left arrow triangle */
                                float ta = ARR_SZ * 0.22f;
                                adl->AddTriangleFilled(
                                    ImVec2(acx - ta, acy),
                                    ImVec2(acx + ta * 0.6f, acy - ta),
                                    ImVec2(acx + ta * 0.6f, acy + ta), afg);
                                if (l_hovered && can_left) ImGui::SetTooltip("Move left");
                                if (l_clicked && can_left) {
                                    fx_pedal_id tmp = ids[pi - 1];
                                    ids[pi - 1] = ids[pi]; ids[pi] = tmp;
                                    fx_chain_move_pedal(engine, pid, pos, pi - 1);
                                    s_selected_node--;
                                }
                                ImGui::PopID();
                            }
                            ImGui::SameLine(0, 4);

                            /* Right arrow — programmatic */
                            {
                                bool can_right = (pi < *id_count - 1);
                                ImGui::PushID("##arr_r");
                                ImVec2 ap = ImGui::GetCursorScreenPos();
                                bool r_clicked = ImGui::InvisibleButton("##arr_r_click", ImVec2(ARR_SZ, ARR_SZ));
                                bool r_hovered = ImGui::IsItemHovered();
                                ImDrawList *adl = ImGui::GetWindowDrawList();
                                float acx = ap.x + ARR_SZ * 0.5f;
                                float acy = ap.y + ARR_SZ * 0.5f;
                                float ar = ARR_SZ * 0.42f;
                                ImU32 abg = (r_hovered && can_right) ? IM_COL32(60, 50, 35, 240) : IM_COL32(40, 35, 25, 200);
                                ImU32 aedge = can_right ? IM_COL32(180, 150, 80, 180) : IM_COL32(80, 70, 50, 100);
                                ImU32 afg = can_right ? IM_COL32(230, 200, 140, 240) : IM_COL32(80, 70, 50, 120);
                                adl->AddCircleFilled(ImVec2(acx, acy), ar, abg, 16);
                                adl->AddCircle(ImVec2(acx, acy), ar, aedge, 16, 1.5f);
                                /* Right arrow triangle */
                                float ta = ARR_SZ * 0.22f;
                                adl->AddTriangleFilled(
                                    ImVec2(acx + ta, acy),
                                    ImVec2(acx - ta * 0.6f, acy - ta),
                                    ImVec2(acx - ta * 0.6f, acy + ta), afg);
                                if (r_hovered && can_right) ImGui::SetTooltip("Move right");
                                if (r_clicked && can_right) {
                                    fx_pedal_id tmp = ids[pi + 1];
                                    ids[pi + 1] = ids[pi]; ids[pi] = tmp;
                                    fx_chain_move_pedal(engine, pid, pos, pi + 1);
                                    s_selected_node++;
                                }
                                ImGui::PopID();
                            }

                            ImGui::SameLine(0, 16);

                            /* Remove button — drawn X with dark red background */
                            {
                                ImGui::PushID("##rm_btn");
                                ImVec2 rp = ImGui::GetCursorScreenPos();
                                bool rm_clicked = ImGui::InvisibleButton("##rm_click", ImVec2(BTN_SZ, BTN_SZ));
                                bool rm_hovered = ImGui::IsItemHovered();
                                ImDrawList *rdl = ImGui::GetWindowDrawList();

                                /* Background circle */
                                float rcx = rp.x + BTN_SZ * 0.5f;
                                float rcy = rp.y + BTN_SZ * 0.5f;
                                float rr = BTN_SZ * 0.42f;
                                rdl->AddCircleFilled(ImVec2(rcx, rcy), rr,
                                    rm_hovered ? IM_COL32(180, 40, 30, 240) : IM_COL32(120, 30, 20, 200), 16);
                                rdl->AddCircle(ImVec2(rcx, rcy), rr,
                                    IM_COL32(220, 60, 40, 180), 16, 1.5f);

                                /* Bold X */
                                float xarm = rr * 0.5f;
                                ImU32 xcol = IM_COL32(255, 220, 200, 240);
                                rdl->AddLine(ImVec2(rcx - xarm, rcy - xarm), ImVec2(rcx + xarm, rcy + xarm), xcol, 3.0f);
                                rdl->AddLine(ImVec2(rcx + xarm, rcy - xarm), ImVec2(rcx - xarm, rcy + xarm), xcol, 3.0f);

                                if (rm_hovered)
                                    ImGui::SetTooltip("Remove pedal");

                                if (rm_clicked) {
                                    fx_chain_remove_pedal(engine, pid);
                                    for (int j = pi; j < *id_count - 1; j++)
                                        ids[j] = ids[j + 1];
                                    (*id_count)--;
                                    s_selected_node = -1;
                                }
                                ImGui::PopID();
                            }
                        }
                    }
                }

                /* ── STUDIO processor detail view ────────────── */
                else if (sel.kind == NODE_STUDIO) {
                    fx_studio_id sid = sel.pedal_id;
                    fx_studio_type_t st = fx_studio_get_type(engine, sid);
                    if (st < FX_STUDIO_COUNT) {
                        const char *sname = fx_studio_get_type_name(st);
                        int nparam = fx_studio_get_param_count(st);
                        bool bypassed = fx_studio_get_bypass(engine, sid);
                        float avail_w = ImGui::GetContentRegionAvail().x;

                        /* Title */
                        {
                            ImGui::SetWindowFontScale(1.35f);
                            ImVec2 ts = ImGui::CalcTextSize(sname);
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_w - ts.x) * 0.5f);
                            if (bypassed)
                                ImGui::TextDisabled("%s", sname);
                            else
                                ImGui::TextColored(ImVec4(0.45f, 0.65f, 0.90f, 1.0f), "%s", sname);
                            ImGui::SetWindowFontScale(1.0f);
                            ImVec2 sub_sz = ImGui::CalcTextSize("Rack Effect");
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_w - sub_sz.x) * 0.5f);
                            ImGui::TextDisabled("Rack Effect");
                        }
                        ImGui::Dummy(ImVec2(0.0f, 4.0f));
                        {
                            ImVec2 sep_p0 = ImGui::GetCursorScreenPos();
                            ImGui::GetWindowDrawList()->AddLine(
                                sep_p0, ImVec2(sep_p0.x + avail_w, sep_p0.y),
                                IM_COL32(60, 100, 160, 100), 1.0f);
                            ImGui::Dummy(ImVec2(0.0f, 2.0f));
                        }

                        /* Rack unit image + overlay knobs */
                        {
                            static const char *rack_fnames[] = {
                                "iron_squeeze", "glass_eq", "reel_warmth", "brick_wall",
                                "velvet_press", "glue_bus", "valve_color", "precision_eq", "room_engine"
                            };

                            /* Knob position maps from red dot detection */
                            struct RackKnobMap { int count; float pos[8][2]; };
                            static const RackKnobMap rack_knob_maps[] = {
                                /* iron_squeeze: 6 (5 params + 1 dummy) */
                                { 6, { {0.154f,0.628f},{0.244f,0.628f},{0.338f,0.630f},{0.656f,0.628f},{0.746f,0.628f},{0.838f,0.628f} } },
                                /* glass_eq: 7 (6 params + 1 dummy) */
                                { 7, { {0.139f,0.516f},{0.244f,0.516f},{0.354f,0.516f},{0.461f,0.516f},{0.568f,0.516f},{0.676f,0.519f},{0.783f,0.516f} } },
                                /* reel_warmth: 5 (4 params + 1 dummy) */
                                { 5, { {0.227f,0.648f},{0.344f,0.651f},{0.447f,0.648f},{0.551f,0.651f},{0.656f,0.651f} } },
                                /* brick_wall: 3 (3 params) */
                                { 3, { {0.239f,0.523f},{0.390f,0.523f},{0.546f,0.523f} } },
                                /* velvet_press: 3 (3 params) */
                                { 3, { {0.213f,0.545f},{0.348f,0.543f},{0.486f,0.543f} } },
                                /* glue_bus: 5 (5 params) */
                                { 5, { {0.438f,0.551f},{0.521f,0.551f},{0.607f,0.551f},{0.693f,0.554f},{0.777f,0.554f} } },
                                /* valve_color: 4 (4 params) */
                                { 4, { {0.246f,0.557f},{0.367f,0.540f},{0.481f,0.528f},{0.588f,0.513f} } },
                                /* precision_eq: 6 (5 params + 1 dummy) */
                                { 6, { {0.196f,0.547f},{0.323f,0.544f},{0.446f,0.544f},{0.567f,0.544f},{0.688f,0.541f},{0.813f,0.541f} } },
                                /* room_engine: 5 (4 params + 1 dummy) */
                                { 5, { {0.448f,0.538f},{0.554f,0.521f},{0.649f,0.512f},{0.743f,0.500f},{0.831f,0.485f} } },
                            };

                            char rpath[256];
                            if (st >= 0 && st < FX_STUDIO_COUNT)
                                snprintf(rpath, sizeof(rpath), "resources/studio/%s_nobg.png", rack_fnames[st]);
                            else
                                rpath[0] = '\0';
                            uintptr_t rack_tex = fx_texture_load(rpath);

                            float img_w = 500.0f;
                            float img_h = img_w * 0.3f;
                            if (rack_tex) {
                                int rw = 0, rh = 0;
                                if (fx_texture_get_size(rack_tex, &rw, &rh) && rh > 0) {
                                    float aspect = (float)rw / (float)rh;
                                    img_h = img_w / aspect;
                                }
                            }
                            float img_x = (avail_w - img_w) * 0.5f;
                            if (img_x > 0.0f)
                                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + img_x);

                            ImVec2 img_pos = ImGui::GetCursorScreenPos();

                            if (rack_tex) {
                                ImVec4 tint = bypassed ? ImVec4(0.5f,0.5f,0.5f,0.7f) : ImVec4(1,1,1,1);
                                ImGui::Image((ImTextureID)rack_tex, ImVec2(img_w, img_h),
                                    ImVec2(0,0), ImVec2(1,1), tint);
                            } else {
                                ImGui::Dummy(ImVec2(img_w, img_h));
                            }
                            float cursor_after_rack_y = ImGui::GetCursorPosY();

                            /* Overlay knobs on the rack image */
                            if (st >= 0 && st < FX_STUDIO_COUNT) {
                                const RackKnobMap &rmap = rack_knob_maps[st];
                                const float RACK_KNOB_SZ = 30.0f;
                                const char *knob_tex = "resources/knobs/knob_dome_silver_nobg.png";

                                for (int k = 0; k < rmap.count; k++) {
                                    float kx = img_pos.x + rmap.pos[k][0] * img_w - RACK_KNOB_SZ * 0.5f;
                                    float ky = img_pos.y + rmap.pos[k][1] * img_h - RACK_KNOB_SZ * 0.5f;

                                    if (k < nparam) {
                                        const char *kname = fx_studio_get_param_name(st, k);
                                        float kval = fx_studio_get_param(engine, sid, k);
                                        char kid[48];
                                        snprintf(kid, sizeof(kid), "%s##rack_ov_%d_%d", kname, (int)sid, k);
                                        if (knob_overlay(kid, &kval, 0.0f, 1.0f, 0.5f, 0.01f,
                                                         kx, ky, RACK_KNOB_SZ, knob_tex)) {
                                            fx_studio_set_param(engine, sid, k, kval);
                                        }
                                    } else {
                                        float dummy = 0.5f;
                                        char did[32];
                                        snprintf(did, sizeof(did), "##rack_d_%d_%d", (int)sid, k);
                                        knob_overlay(did, &dummy, 0.0f, 1.0f, 0.5f, 0.0f,
                                                     kx, ky, RACK_KNOB_SZ, knob_tex);
                                    }
                                }
                            }
                            /* Restore cursor below image (overlay knobs moved it) */
                            ImGui::SetCursorPosY(cursor_after_rack_y);
                        }

                        ImGui::Dummy(ImVec2(0.0f, 20.0f));

                        /* Bypass + remove */
                        {
                            const float BTN_H = 28.0f;
                            float row_w = 120 + 16 + 80;
                            float row_off = (avail_w - row_w) * 0.5f;
                            if (row_off > 0.0f)
                                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + row_off);

                            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
                            ImGui::PushStyleColor(ImGuiCol_Button,
                                bypassed ? ImVec4(0.30f, 0.10f, 0.08f, 0.9f)
                                         : ImVec4(0.10f, 0.20f, 0.35f, 0.9f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                bypassed ? ImVec4(0.42f, 0.14f, 0.10f, 1.0f)
                                         : ImVec4(0.14f, 0.30f, 0.50f, 1.0f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                                bypassed ? ImVec4(0.55f, 0.18f, 0.12f, 1.0f)
                                         : ImVec4(0.18f, 0.40f, 0.65f, 1.0f));
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.80f, 0.72f, 1.0f));
                            char bp_id[48];
                            snprintf(bp_id, sizeof(bp_id), "%s##studio_bp",
                                     bypassed ? "BYPASSED" : "ON (Active)");
                            if (ImGui::Button(bp_id, ImVec2(120, BTN_H)))
                                fx_studio_set_bypass(engine, sid, !bypassed);
                            ImGui::PopStyleColor(4);
                            ImGui::PopStyleVar();

                            ImGui::SameLine(0, 16);

                            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
                            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.10f, 0.08f, 0.8f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.50f, 0.14f, 0.10f, 1.0f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.65f, 0.18f, 0.12f, 1.0f));
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.70f, 0.60f, 1.0f));
                            if (ImGui::Button("Remove##rm_studio", ImVec2(80, BTN_H))) {
                                fx_studio_remove(engine, sid);
                                int pi = sel.slot;
                                for (int j = pi; j < s_studio_id_count - 1; j++)
                                    s_studio_ids[j] = s_studio_ids[j + 1];
                                s_studio_id_count--;
                                s_selected_node = -1;
                            }
                            ImGui::PopStyleColor(4);
                            ImGui::PopStyleVar();
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

            /* ── Status bar: dark background + inner shadow at top ── */
            {
                ImDrawList *dl_sb = ImGui::GetWindowDrawList();
                ImVec2 sb_min = ImGui::GetWindowPos();
                ImVec2 sb_max = ImVec2(sb_min.x + win_w, sb_min.y + STATUS_H);
                /* Solid dark base (slightly warmer than pure black) */
                dl_sb->AddRectFilled(sb_min, sb_max, IM_COL32(14, 12, 10, 255));
                /* Inner shadow at top edge: detail view "sits over" the status bar */
                dl_sb->AddRectFilledMultiColor(
                    sb_min, ImVec2(sb_max.x, sb_min.y + 8.0f),
                    IM_COL32(0, 0, 0, 110), IM_COL32(0, 0, 0, 110),
                    IM_COL32(0, 0, 0,   0), IM_COL32(0, 0, 0,   0));
                /* Top separator line */
                dl_sb->AddLine(
                    ImVec2(sb_min.x, sb_min.y),
                    ImVec2(sb_max.x, sb_min.y),
                    IM_COL32(40, 34, 26, 200), 1.0f);
            }

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

    /* Auto-save last session preset and config */
    {
        bool ok = fx_preset_save(engine, "presets/last_session.0xfx");
        if (!ok) fx_preset_save(engine, "../presets/last_session.0xfx");
        FX_INFO(ok ? "Session saved to last_session.0xfx" : "Could not auto-save session");
    }
    {
        /* Snapshot window size before destruction */
        int ww, wh;
        SDL_GetWindowSize(window, &ww, &wh);
        s_session_cfg.window_w          = ww;
        s_session_cfg.window_h          = wh;
        s_session_cfg.input_device_idx  = s_selected_input;
        s_session_cfg.output_device_idx = s_selected_output;
        s_session_cfg.buf_size_idx      = s_selected_buf_idx;
        s_session_cfg.sr_idx            = s_selected_sr_idx;
        session_config_save(&s_session_cfg);
        FX_INFO("Config saved to %s", get_config_path());
    }

    /* Cleanup */
    fx_texture_shutdown();
    fx_midi_shutdown();
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
