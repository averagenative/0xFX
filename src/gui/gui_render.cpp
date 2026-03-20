/*
 * gui_render.cpp — Shared GUI rendering for standalone + plugin
 *
 * This file contains the core per-frame ImGui rendering extracted from
 * gui_main.cpp so that both the standalone app and CLAP/VST3 plugins
 * can share the same visual interface.
 *
 * All mutable rendering state lives in the fx_gui_state struct. Static
 * data (pedal tables, amp knob maps, color tables, etc.) remain file-scope
 * const and are shared across instances.
 */
#include <SDL.h>
#include <SDL_opengl.h>

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"

#include "gui_render.h"

extern "C" {
#include "../engine/fx_engine.h"
#include "../core/log.h"
#include "knobs.h"
#include "texture.h"
}

#include <stdio.h>
#include <cmath>
#include <cstring>

/* ── Amp param tooltips ──────────────────────────────────────── */

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

/* ── Pedal tooltips ──────────────────────────────────────────── */

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

/* ── Signal chain node types ───────────────────────────────── */

enum NodeKind {
    NODE_INPUT = 0,
    NODE_PEDAL_PRE,
    NODE_SPLIT,
    NODE_AMP,
    NODE_CAB,
    NODE_MERGE,
    NODE_PEDAL_POST,
    NODE_STUDIO,
    NODE_OUTPUT
};

struct ChainNode {
    NodeKind    kind;
    int         slot;
    fx_pedal_id pedal_id;
    int         chain_id;
};

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

static const char *s_cab_type_names[] = {
    "1x12 Open", "2x12 Closed", "4x12 Straight", "4x12 Slant", "Direct"
};

/* ── Pedal gallery ──────────────────────────────────────────── */

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

/* ── Texture helpers ───────────────────────────────────────── */

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

static const char *s_cab_filenames[] = {
    "1x12_open", "2x12_closed", "4x12_straight", "4x12_slant", "direct_flat",
};

static uintptr_t load_pedal_texture(const char *type_name) {
    char fname[128];
    type_to_filename(type_name, fname, sizeof(fname));
    if (strcmp(fname, "orange_distortion") == 0) strcpy(fname, "orange_dist");
    char path[256];
    snprintf(path, sizeof(path), "resources/pedals/%s_body_nobg.png", fname);
    return fx_texture_load(path);
}

static void amp_name_to_filename(const char *type_name, char *out, int out_size) {
    type_to_filename(type_name, out, out_size);
    if (strcmp(out, "british_crunch") == 0) strcpy(out, "brit_crunch");
}

/* Currently unused — amp body textures are loaded via load_amp_face_texture.
 * Kept for future use (e.g., amp rack body behind faceplate). */
#if 0
static uintptr_t load_amp_body_texture(const char *type_name) {
    char fname[128];
    amp_name_to_filename(type_name, fname, sizeof(fname));
    char path[256];
    snprintf(path, sizeof(path), "resources/amps/%s_body_nobg.png", fname);
    return fx_texture_load(path);
}
#endif

static uintptr_t load_amp_face_texture(const char *type_name) {
    char fname[128];
    amp_name_to_filename(type_name, fname, sizeof(fname));
    char path[256];
    snprintf(path, sizeof(path), "resources/amps/%s_nobg.png", fname);
    return fx_texture_load(path);
}

static uintptr_t load_cab_texture(int cab_type_idx) {
    if (cab_type_idx < 0 || cab_type_idx >= FX_CAB_TYPE_COUNT) {
        return fx_texture_load("resources/cabs/4x12_straight_nobg.png");
    }
    char path[256];
    snprintf(path, sizeof(path), "resources/cabs/%s_nobg.png", s_cab_filenames[cab_type_idx]);
    return fx_texture_load(path);
}

/* ── GUI state struct ──────────────────────────────────────── */

struct fx_gui_state {
    fx_engine_t *engine;

    /* Pedal ID registries */
    fx_pedal_id  pre_ids[32];
    int          pre_id_count;
    fx_pedal_id  post_ids[32];
    int          post_id_count;

    /* Studio processor IDs */
    fx_studio_id studio_ids[8];
    int          studio_id_count;

    /* Signal chain selection */
    int          selected_node;
    int          cab_type;
    int          cab_type_b;

    /* Dual-chain (Y-split) */
    fx_chain_id  chain_b;

    /* Preset name */
    char         preset_name[128];

    /* Theme textures (loaded once) */
    uintptr_t    tex_pedalboard;
    uintptr_t    tex_tolex;
    bool         theme_tex_tried;

    /* Logo texture */
    uintptr_t    logo_tex;
    bool         logo_tried;
    float        logo_aspect;

    /* Save-As state */
    bool         save_as_open;
    char         save_as_name[128];
};

/* ── Theme setup ───────────────────────────────────────────── */

extern "C" void fx_gui_setup_theme(void) {
    ImGuiStyle &style = ImGui::GetStyle();
    ImVec4 *colors = style.Colors;

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

    style.WindowRounding    = 4.0f;
    style.FrameRounding     = 3.0f;
    style.GrabRounding      = 3.0f;
    style.TabRounding       = 3.0f;
    style.WindowPadding     = ImVec2(10, 10);
    style.FramePadding      = ImVec2(6, 4);
    style.ItemSpacing       = ImVec2(8, 6);
}

/* ── Create / Destroy ──────────────────────────────────────── */

extern "C" fx_gui_state_t *fx_gui_create(fx_engine_t *engine) {
    fx_gui_state_t *g = (fx_gui_state_t *)calloc(1, sizeof(fx_gui_state_t));
    if (!g) return NULL;
    g->engine = engine;
    g->selected_node = -1;
    g->chain_b = -1;
    g->cab_type = 0;
    g->cab_type_b = 0;
    g->logo_aspect = 1.96f;
    snprintf(g->preset_name, sizeof(g->preset_name), "Untitled");

    /* Sync pedal/studio IDs from engine */
    fx_gui_sync_from_engine(g);

    return g;
}

extern "C" void fx_gui_destroy(fx_gui_state_t *gui) {
    if (gui) free(gui);
}

extern "C" void fx_gui_sync_from_engine(fx_gui_state_t *gui) {
    if (!gui || !gui->engine) return;
    fx_engine_t *engine = gui->engine;

    gui->pre_id_count = fx_chain_get_pedal_count(engine, FX_CHAIN_POS_PRE);
    if (gui->pre_id_count > 32) gui->pre_id_count = 32;
    for (int i = 0; i < gui->pre_id_count; i++)
        gui->pre_ids[i] = fx_chain_get_pedal_at(engine, FX_CHAIN_POS_PRE, i);

    gui->post_id_count = fx_chain_get_pedal_count(engine, FX_CHAIN_POS_POST);
    if (gui->post_id_count > 32) gui->post_id_count = 32;
    for (int i = 0; i < gui->post_id_count; i++)
        gui->post_ids[i] = fx_chain_get_pedal_at(engine, FX_CHAIN_POS_POST, i);

    gui->studio_id_count = 0; /* studio IDs tracked separately */
    FX_INFO("GUI sync: %d pre-pedals, %d post-pedals", gui->pre_id_count, gui->post_id_count);
}

/* ── Render one frame ──────────────────────────────────────── */
/*
 * This is a forward-declaration; the actual massive render function is
 * defined below.  It contains all the rendering code that was previously
 * inline in gui_main.cpp's main loop (toolbar, signal chain, detail view,
 * status bar).
 *
 * When is_plugin is true, standalone-only elements are suppressed:
 *   - LIVE button (host controls audio)
 *   - Audio/MIDI settings popup
 *   - Debug record button
 *   - Window controls (_ [] X) and drag (host owns window chrome)
 *   - Keyboard shortcuts that close the window (Escape)
 *   - Window border / resize grip
 *   - Preset quick-save / Save-As (Ctrl+S / Ctrl+Shift+S)
 */

extern "C" void fx_gui_render_frame(fx_gui_state_t *gui, float win_w, float win_h,
                                     bool is_plugin) {
    if (!gui || !gui->engine) return;
    fx_engine_t *engine = gui->engine;

    /* Layout constants */
    const float TOOLBAR_H      = 64.0f;
    const float STATUS_H       = 50.0f;
    const float NODE_W         = 80.0f;
    const float NODE_H         = 60.0f;
    const float NODE_SPACING   = 56.0f;
    const float ADD_BTN_W      = 26.0f;
    const float CHAIN_PADDING  = 20.0f;

    /* Theme textures (loaded once) */
    if (!gui->theme_tex_tried) {
        gui->tex_pedalboard = fx_texture_load("resources/theme/pedalboard_surface_nobg.png");
        gui->tex_tolex      = fx_texture_load("resources/theme/tolex_surface_nobg.png");
        gui->theme_tex_tried = true;
    }

    /* ── Toolbar ───────────────────────────────────────────── */
    {
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(win_w, TOOLBAR_H));
        ImGui::Begin("##toolbar", NULL,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);

        /* Toolbar gradient */
        {
            ImDrawList *dl_tb = ImGui::GetWindowDrawList();
            ImVec2 tb_min = ImGui::GetWindowPos();
            ImVec2 tb_max = ImVec2(tb_min.x + win_w, tb_min.y + TOOLBAR_H);
            dl_tb->AddRectFilledMultiColor(
                tb_min, tb_max,
                IM_COL32(32, 28, 24, 255), IM_COL32(32, 28, 24, 255),
                IM_COL32(18, 16, 13, 255), IM_COL32(18, 16, 13, 255));
            dl_tb->AddLine(
                ImVec2(tb_min.x, tb_max.y - 1.0f),
                ImVec2(tb_max.x, tb_max.y - 1.0f),
                IM_COL32(60, 50, 38, 180), 1.0f);
        }

        /* Logo */
        {
            if (!gui->logo_tried) {
                gui->logo_tex = fx_texture_load("resources/logo/logo_neon_v4_red_trim_fade.png");
                gui->logo_tried = true;
                int lw = 0, lh = 0;
                if (gui->logo_tex && fx_texture_get_size(gui->logo_tex, &lw, &lh) && lh > 0)
                    gui->logo_aspect = (float)lw / (float)lh;
            }
            if (gui->logo_tex) {
                float logo_h = TOOLBAR_H - 8.0f;
                float logo_w = logo_h * gui->logo_aspect;
                ImGui::Image((ImTextureID)gui->logo_tex, ImVec2(logo_w, logo_h));
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
            }
        }

        ImGui::SameLine(380);

        /* Preset name display */
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.75f, 0.55f, 1.0f));
            ImGui::SetWindowFontScale(0.9f);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", gui->preset_name);
            ImGui::SetWindowFontScale(1.0f);
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Current preset");
        }

        ImGui::SameLine(0, 20);

        /* DUAL/SINGLE chain toggle (always shown — works in plugin too) */
        {
            bool is_dual = (gui->chain_b >= 0);
            if (is_dual) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.18f, 0.06f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.40f, 0.28f, 0.08f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.55f, 0.38f, 0.10f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.75f, 0.20f, 1.0f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.13f, 0.11f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.22f, 0.18f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.09f, 0.07f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.50f, 0.40f, 1.0f));
            }
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
            if (ImGui::Button(is_dual ? "DUAL##split" : "SINGLE##split",
                              ImVec2(72.0f, 32.0f))) {
                if (is_dual) {
                    if (gui->chain_b >= 0) {
                        fx_chain_destroy(engine, gui->chain_b);
                        gui->chain_b = -1;
                    }
                    gui->selected_node = -1;
                } else {
                    gui->chain_b = fx_chain_create(engine);
                    if (gui->chain_b >= 0) {
                        fx_chain_set_mix(engine, FX_CHAIN_DEFAULT, 0.5f);
                        fx_chain_set_mix(engine, gui->chain_b, 0.5f);
                    }
                }
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(4);

            if (is_dual) {
                ImDrawList *dl = ImGui::GetWindowDrawList();
                ImVec2 bmin = ImGui::GetItemRectMin(), bmax = ImGui::GetItemRectMax();
                dl->AddRect(ImVec2(bmin.x-2,bmin.y-2), ImVec2(bmax.x+2,bmax.y+2),
                            IM_COL32(200, 160, 30, 180), 6.0f, 0, 1.5f);
            }
        }

        /* NOTE: LIVE button, REC button, Settings, window controls, and window
         * drag are standalone-only. The plugin host manages audio and window
         * chrome. They are NOT rendered here — gui_main.cpp renders them after
         * calling us, or they could be added in future with an is_plugin guard. */

        ImGui::End();
    }

    /* ============================================================
     * BUILD THE FLATTENED SIGNAL CHAIN
     * ============================================================ */
    ChainNode chain[256];
    int chain_len = 0;
    bool is_dual = (gui->chain_b >= 0);

    chain[chain_len++] = { NODE_INPUT, -1, -1, 0 };

    for (int i = 0; i < gui->pre_id_count && chain_len < 250; i++)
        chain[chain_len++] = { NODE_PEDAL_PRE, i, gui->pre_ids[i], 0 };

    if (is_dual) {
        chain[chain_len++] = { NODE_SPLIT, -1, -1, 0 };
        chain[chain_len++] = { NODE_AMP, -1, -1, 0 };
        chain[chain_len++] = { NODE_CAB, -1, -1, 0 };
        chain[chain_len++] = { NODE_AMP, -1, -1, (int)gui->chain_b };
        chain[chain_len++] = { NODE_CAB, -1, -1, (int)gui->chain_b };
        chain[chain_len++] = { NODE_MERGE, -1, -1, 0 };
    } else {
        chain[chain_len++] = { NODE_AMP, -1, -1, 0 };
        chain[chain_len++] = { NODE_CAB, -1, -1, 0 };
    }

    for (int i = 0; i < gui->post_id_count && chain_len < 254; i++)
        chain[chain_len++] = { NODE_PEDAL_POST, i, gui->post_ids[i], 0 };

    for (int i = 0; i < gui->studio_id_count && chain_len < 254; i++)
        chain[chain_len++] = { NODE_STUDIO, i, gui->studio_ids[i], 0 };

    chain[chain_len++] = { NODE_OUTPUT, -1, -1, 0 };

    if (gui->selected_node >= chain_len) gui->selected_node = -1;

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

        float cy = win_pos.y + chain_area_h * 0.45f;
        const float LANE_OFFSET = NODE_H * 1.25f;
        float cy_a = is_dual ? cy - LANE_OFFSET * 0.5f : cy;
        float cy_b = is_dual ? cy + LANE_OFFSET * 0.5f : cy;

        /* Background texture tiling */
        {
            ImVec2 ch_min = ImGui::GetWindowPos();
            ImVec2 ch_max = ImVec2(ch_min.x + win_w, ch_min.y + chain_area_h);
            if (gui->tex_pedalboard) {
                const float TILE = 256.0f;
                dl->PushClipRect(ch_min, ch_max, true);
                for (float ty = ch_min.y; ty < ch_max.y; ty += TILE) {
                    for (float tx = ch_min.x; tx < ch_max.x; tx += TILE) {
                        float tx1 = (tx + TILE < ch_max.x) ? tx + TILE : ch_max.x;
                        float ty1 = (ty + TILE < ch_max.y) ? ty + TILE : ch_max.y;
                        float u1 = (tx1 - tx) / TILE;
                        float v1 = (ty1 - ty) / TILE;
                        dl->AddImage((ImTextureID)gui->tex_pedalboard,
                            ImVec2(tx, ty), ImVec2(tx1, ty1),
                            ImVec2(0.0f, 0.0f), ImVec2(u1, v1),
                            IM_COL32(255, 255, 255, 55));
                    }
                }
                dl->PopClipRect();
            }
        }

        /* Compute column positions for each node */
        int columns[256];
        int col = 0;
        int split_col = -1, merge_col = -1;
        for (int i = 0; i < chain_len; i++) {
            if (chain[i].kind == NODE_SPLIT) {
                columns[i] = col;
                split_col = col;
                col++;
            } else if (chain[i].kind == NODE_AMP && is_dual && split_col >= 0 && merge_col < 0) {
                /* Dual AMP/CAB nodes share columns */
                if (chain[i].chain_id == 0) {
                    columns[i] = col;
                } else {
                    columns[i] = col - 2; /* same column as chain A AMP */
                }
                col++;
            } else if (chain[i].kind == NODE_CAB && is_dual && split_col >= 0 && merge_col < 0) {
                if (chain[i].chain_id == 0) {
                    columns[i] = col;
                } else {
                    columns[i] = col - 2;
                }
                col++;
            } else if (chain[i].kind == NODE_MERGE) {
                columns[i] = col;
                merge_col = col;
                col++;
            } else {
                columns[i] = col;
                col++;
            }
        }

        float total_chain_w = col * (NODE_W + NODE_SPACING) + CHAIN_PADDING * 2;
        float start_x = content_min.x + CHAIN_PADDING;
        if (total_chain_w < win_w) {
            start_x = content_min.x + (win_w - total_chain_w) * 0.5f;
        }

        /* Helper lambda to get node center position */
        auto get_node_pos = [&](int idx) -> ImVec2 {
            float x = start_x + columns[idx] * (NODE_W + NODE_SPACING) + NODE_W * 0.5f;
            float y = cy;
            if (is_dual) {
                bool in_parallel = (split_col >= 0 && merge_col >= 0 &&
                                    columns[idx] > split_col && columns[idx] < merge_col);
                if (in_parallel) {
                    y = (chain[idx].chain_id != 0 && chain[idx].chain_id != FX_CHAIN_DEFAULT)
                        ? cy_b : cy_a;
                } else if (chain[idx].kind == NODE_SPLIT || chain[idx].kind == NODE_MERGE) {
                    y = cy;
                }
            }
            return ImVec2(x, y);
        };

        /* Draw bezier cables between SPLIT and MERGE */
        if (is_dual && split_col >= 0 && merge_col >= 0) {
            int split_idx = -1, merge_idx = -1;
            for (int i = 0; i < chain_len; i++) {
                if (chain[i].kind == NODE_SPLIT) split_idx = i;
                if (chain[i].kind == NODE_MERGE) merge_idx = i;
            }
            if (split_idx >= 0 && merge_idx >= 0) {
                ImVec2 sp = get_node_pos(split_idx);
                ImVec2 mp = get_node_pos(merge_idx);
                float sx = sp.x + NODE_W * 0.5f;
                float mx = mp.x - NODE_W * 0.5f;
                float tan_len = (mx - sx) * 0.4f;

                /* Top path (chain A) */
                dl->AddBezierCubic(
                    ImVec2(sx, sp.y), ImVec2(sx + tan_len, cy_a),
                    ImVec2(mx - tan_len, cy_a), ImVec2(mx, mp.y),
                    IM_COL32(200, 160, 30, 120), 2.5f);
                /* Bottom path (chain B) */
                dl->AddBezierCubic(
                    ImVec2(sx, sp.y), ImVec2(sx + tan_len, cy_b),
                    ImVec2(mx - tan_len, cy_b), ImVec2(mx, mp.y),
                    IM_COL32(80, 130, 200, 120), 2.5f);
            }
        }

        /* Draw all nodes */
        for (int i = 0; i < chain_len; i++) {
            ImVec2 center = get_node_pos(i);
            float nx = center.x - NODE_W * 0.5f;
            float ny = center.y - NODE_H * 0.5f;

            bool is_selected = (i == gui->selected_node);
            bool bypassed = false;
            if (chain[i].kind == NODE_PEDAL_PRE || chain[i].kind == NODE_PEDAL_POST)
                bypassed = fx_pedal_get_bypass(engine, chain[i].pedal_id);
            if (chain[i].kind == NODE_STUDIO)
                bypassed = fx_studio_get_bypass(engine, chain[i].pedal_id);

            const char *label = node_label(chain[i].kind, engine, chain[i].pedal_id, chain[i].chain_id);
            ImU32 col_fill = node_color(chain[i].kind, bypassed);

            if (chain[i].kind == NODE_SPLIT || chain[i].kind == NODE_MERGE) {
                /* Diamond shape */
                float dw = NODE_W * 0.4f;
                float dh = NODE_H * 0.4f;
                ImVec2 pts[4] = {
                    ImVec2(center.x, center.y - dh),
                    ImVec2(center.x + dw, center.y),
                    ImVec2(center.x, center.y + dh),
                    ImVec2(center.x - dw, center.y),
                };
                dl->AddConvexPolyFilled(pts, 4, col_fill);
                ImU32 border = is_selected ? IM_COL32(255, 200, 60, 255) : IM_COL32(80, 70, 55, 200);
                dl->AddPolyline(pts, 4, border, ImDrawFlags_Closed, is_selected ? 2.5f : 1.5f);

                /* Label below */
                ImVec2 ts = ImGui::CalcTextSize(label);
                dl->AddText(ImVec2(center.x - ts.x * 0.5f, center.y + dh + 4.0f),
                            IM_COL32(180, 170, 150, 200), label);

                /* Clickable area */
                ImGui::SetCursorScreenPos(ImVec2(center.x - dw, center.y - dh));
                char uid[32]; snprintf(uid, sizeof(uid), "##node_%d", i);
                if (ImGui::InvisibleButton(uid, ImVec2(dw * 2, dh * 2)))
                    gui->selected_node = i;
            } else {
                /* Regular rectangular node */
                ImVec2 tl = ImVec2(nx, ny);
                ImVec2 br = ImVec2(nx + NODE_W, ny + NODE_H);

                /* Node background with texture if available */
                dl->AddRectFilled(tl, br, col_fill, 6.0f);

                /* Selection highlight */
                if (is_selected)
                    dl->AddRect(ImVec2(tl.x-2,tl.y-2), ImVec2(br.x+2,br.y+2),
                                IM_COL32(255, 200, 60, 255), 8.0f, 0, 2.5f);
                else
                    dl->AddRect(tl, br, IM_COL32(80, 70, 55, 200), 6.0f, 0, 1.0f);

                /* LED indicator for bypass status */
                if (chain[i].kind == NODE_PEDAL_PRE || chain[i].kind == NODE_PEDAL_POST ||
                    chain[i].kind == NODE_STUDIO) {
                    float led_x = nx + NODE_W - 10.0f;
                    float led_y = ny + 8.0f;
                    ImU32 led_col = bypassed ? IM_COL32(80, 30, 30, 200)
                                             : IM_COL32(30, 200, 50, 255);
                    dl->AddCircleFilled(ImVec2(led_x, led_y), 4.0f, led_col);
                    if (!bypassed) {
                        dl->AddCircleFilled(ImVec2(led_x, led_y), 7.0f,
                                            IM_COL32(30, 200, 50, 40));
                    }
                }

                /* Label text — truncate if needed */
                {
                    ImGui::PushClipRect(tl, br, true);
                    ImVec2 ts = ImGui::CalcTextSize(label);
                    float tx = nx + (NODE_W - ts.x) * 0.5f;
                    float ty2 = ny + (NODE_H - ts.y) * 0.5f;
                    ImU32 text_col = bypassed ? IM_COL32(120, 110, 100, 180)
                                              : IM_COL32(230, 220, 200, 255);
                    dl->AddText(ImVec2(tx, ty2), text_col, label);
                    ImGui::PopClipRect();
                }

                /* Clickable area */
                ImGui::SetCursorScreenPos(tl);
                char uid[32]; snprintf(uid, sizeof(uid), "##node_%d", i);
                if (ImGui::InvisibleButton(uid, ImVec2(NODE_W, NODE_H)))
                    gui->selected_node = i;

                /* Tooltip */
                if (ImGui::IsItemHovered()) {
                    if (chain[i].kind == NODE_PEDAL_PRE || chain[i].kind == NODE_PEDAL_POST) {
                        fx_pedal_type_t pt = fx_pedal_get_type(engine, chain[i].pedal_id);
                        const char *tip = get_pedal_tooltip(pt);
                        if (tip) ImGui::SetTooltip("%s", tip);
                    }
                }
            }

            /* Draw cable to next node in single-lane sections */
            if (i < chain_len - 1) {
                bool in_parallel = is_dual && split_col >= 0 && merge_col >= 0 &&
                                   columns[i] >= split_col && columns[i] < merge_col;
                if (!in_parallel && chain[i+1].kind != NODE_SPLIT &&
                    chain[i].kind != NODE_MERGE) {
                    /* Simple to next: skip if next is in parallel zone */
                    bool next_parallel = is_dual && split_col >= 0 && merge_col >= 0 &&
                                         columns[i+1] > split_col && columns[i+1] < merge_col;
                    if (!next_parallel) {
                        ImVec2 p1 = ImVec2(center.x + NODE_W * 0.5f, center.y);
                        ImVec2 p2_center = get_node_pos(i + 1);
                        ImVec2 p2 = ImVec2(p2_center.x - NODE_W * 0.5f, p2_center.y);
                        dl->AddLine(p1, p2, IM_COL32(100, 90, 70, 180), 2.0f);
                    }
                }
            }

            /* [+] add button between pedals or at chain ends */
            if (chain[i].kind == NODE_INPUT ||
                chain[i].kind == NODE_PEDAL_PRE) {
                float btn_x = center.x + NODE_W * 0.5f + (NODE_SPACING - ADD_BTN_W) * 0.5f;
                float btn_y = center.y - ADD_BTN_W * 0.5f;
                ImGui::SetCursorScreenPos(ImVec2(btn_x, btn_y));
                char bid[32]; snprintf(bid, sizeof(bid), "##add_pre_%d", i);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.15f, 0.12f, 0.8f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.28f, 0.18f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.50f, 0.38f, 0.20f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.70f, 0.60f, 0.45f, 1.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
                if (ImGui::Button("+", ImVec2(ADD_BTN_W, ADD_BTN_W))) {
                    ImGui::OpenPopup("add_pedal_gallery_pre");
                }
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(4);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Add pre-amp pedal");
            }

            /* Post-amp [+] buttons */
            if (chain[i].kind == NODE_PEDAL_POST ||
                (chain[i].kind == NODE_MERGE && is_dual) ||
                (chain[i].kind == NODE_CAB && !is_dual)) {
                float btn_x = center.x + NODE_W * 0.5f + (NODE_SPACING - ADD_BTN_W) * 0.5f;
                float btn_y = center.y - ADD_BTN_W * 0.5f;
                ImGui::SetCursorScreenPos(ImVec2(btn_x, btn_y));
                char bid[32]; snprintf(bid, sizeof(bid), "##add_post_%d", i);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.15f, 0.12f, 0.8f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.18f, 0.30f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.35f, 0.25f, 0.45f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.60f, 0.50f, 0.70f, 1.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
                if (ImGui::Button("+", ImVec2(ADD_BTN_W, ADD_BTN_W))) {
                    ImGui::OpenPopup("add_pedal_gallery_post");
                }
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(4);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Add post-amp pedal or studio processor");
            }
        }

        /* Pedal gallery popup — pre-amp */
        if (ImGui::BeginPopup("add_pedal_gallery_pre")) {
            ImGui::Text("Add Pre-Amp Pedal");
            ImGui::Separator();
            for (int ci = 0; ci < s_pedal_category_count; ci++) {
                if (ImGui::TreeNode(s_pedal_categories[ci].label)) {
                    for (int pi = 0; pi < s_pedal_categories[ci].count; pi++) {
                        if (ImGui::Selectable(s_pedal_categories[ci].pedals[pi].name)) {
                            fx_pedal_id new_id = fx_chain_add_pedal(engine,
                                s_pedal_categories[ci].pedals[pi].type, FX_CHAIN_POS_PRE);
                            if (new_id >= 0 && gui->pre_id_count < 32) {
                                gui->pre_ids[gui->pre_id_count++] = new_id;
                            }
                            ImGui::CloseCurrentPopup();
                        }
                        /* Tooltip */
                        if (ImGui::IsItemHovered()) {
                            const char *tip = get_pedal_tooltip(s_pedal_categories[ci].pedals[pi].type);
                            if (tip) ImGui::SetTooltip("%s", tip);
                        }
                    }
                    ImGui::TreePop();
                }
            }
            ImGui::EndPopup();
        }

        /* Pedal gallery popup — post-amp */
        if (ImGui::BeginPopup("add_pedal_gallery_post")) {
            ImGui::Text("Add Post-Amp Effect");
            ImGui::Separator();
            /* Studio processors */
            if (ImGui::TreeNode("STUDIO")) {
                for (int t = 0; t < FX_STUDIO_COUNT; t++) {
                    const char *sname = fx_studio_get_type_name((fx_studio_type_t)t);
                    if (ImGui::Selectable(sname)) {
                        fx_studio_id sid = fx_studio_add(engine, (fx_studio_type_t)t);
                        if (sid >= 0 && gui->studio_id_count < 8) {
                            gui->studio_ids[gui->studio_id_count++] = sid;
                        }
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::TreePop();
            }
            /* Pedal categories */
            for (int ci = 0; ci < s_pedal_category_count; ci++) {
                if (ImGui::TreeNode(s_pedal_categories[ci].label)) {
                    for (int pi = 0; pi < s_pedal_categories[ci].count; pi++) {
                        if (ImGui::Selectable(s_pedal_categories[ci].pedals[pi].name)) {
                            fx_pedal_id new_id = fx_chain_add_pedal(engine,
                                s_pedal_categories[ci].pedals[pi].type, FX_CHAIN_POS_POST);
                            if (new_id >= 0 && gui->post_id_count < 32) {
                                gui->post_ids[gui->post_id_count++] = new_id;
                            }
                            ImGui::CloseCurrentPopup();
                        }
                        if (ImGui::IsItemHovered()) {
                            const char *tip = get_pedal_tooltip(s_pedal_categories[ci].pedals[pi].type);
                            if (tip) ImGui::SetTooltip("%s", tip);
                        }
                    }
                    ImGui::TreePop();
                }
            }
            ImGui::EndPopup();
        }

        ImGui::End();
    }

    /* ============================================================
     * DETAIL VIEW (~65% of window, below signal chain)
     * ============================================================ */
    {
        float chain_area_h = (win_h - TOOLBAR_H - STATUS_H) * 0.35f;
        float detail_h     = win_h - TOOLBAR_H - chain_area_h - STATUS_H;
        float detail_y     = TOOLBAR_H + chain_area_h;

        ImGui::SetNextWindowPos(ImVec2(0, detail_y));
        ImGui::SetNextWindowSize(ImVec2(win_w, detail_h));
        ImGui::Begin("##detail_view", NULL,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_HorizontalScrollbar);

        /* Detail view background */
        {
            ImDrawList *dl_dv = ImGui::GetWindowDrawList();
            ImVec2 dv_min = ImGui::GetWindowPos();
            ImVec2 dv_max = ImVec2(dv_min.x + win_w, dv_min.y + detail_h);

            if (gui->tex_tolex) {
                const float TILE = 256.0f;
                dl_dv->PushClipRect(dv_min, dv_max, true);
                for (float ty = dv_min.y; ty < dv_max.y; ty += TILE) {
                    for (float tx = dv_min.x; tx < dv_max.x; tx += TILE) {
                        float tx1 = (tx + TILE < dv_max.x) ? tx + TILE : dv_max.x;
                        float ty1 = (ty + TILE < dv_max.y) ? ty + TILE : dv_max.y;
                        float u1 = (tx1 - tx) / TILE;
                        float v1 = (ty1 - ty) / TILE;
                        dl_dv->AddImage((ImTextureID)gui->tex_tolex,
                            ImVec2(tx, ty), ImVec2(tx1, ty1),
                            ImVec2(0.0f, 0.0f), ImVec2(u1, v1),
                            IM_COL32(255, 255, 255, 40));
                    }
                }
                dl_dv->PopClipRect();
            } else {
                dl_dv->AddRectFilled(dv_min, dv_max, IM_COL32(18, 16, 13, 255));
            }

            /* Inner shadow + separator */
            dl_dv->AddRectFilledMultiColor(
                dv_min, ImVec2(dv_max.x, dv_min.y + 14.0f),
                IM_COL32(0, 0, 0, 100), IM_COL32(0, 0, 0, 100),
                IM_COL32(0, 0, 0,   0), IM_COL32(0, 0, 0,   0));
            dl_dv->AddLine(
                ImVec2(dv_min.x, dv_min.y), ImVec2(dv_max.x, dv_min.y),
                IM_COL32(45, 38, 28, 200), 1.0f);
        }

        if (gui->selected_node < 0 || gui->selected_node >= chain_len) {
            float avail_w = ImGui::GetContentRegionAvail().x;
            float avail_h = ImGui::GetContentRegionAvail().y;
            const char *msg = "Click a node in the signal chain to edit";
            ImVec2 ts = ImGui::CalcTextSize(msg);
            ImGui::SetCursorPos(ImVec2((avail_w - ts.x) * 0.5f, (avail_h - ts.y) * 0.5f));
            ImGui::TextDisabled("%s", msg);
        }
        else {
            ChainNode &sel = chain[gui->selected_node];

            if (sel.kind == NODE_SPLIT) {
                float avail_w = ImGui::GetContentRegionAvail().x;
                ImGui::SetWindowFontScale(1.35f);
                const char *title = "Y-Split";
                ImVec2 ts = ImGui::CalcTextSize(title);
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_w - ts.x) * 0.5f);
                ImGui::TextColored(ImVec4(0.92f, 0.68f, 0.22f, 1.0f), "%s", title);
                ImGui::SetWindowFontScale(1.0f);
                ImGui::TextDisabled("Signal splits into two parallel amp chains.");
            }
            else if (sel.kind == NODE_MERGE) {
                float avail_w = ImGui::GetContentRegionAvail().x;
                ImGui::SetWindowFontScale(1.35f);
                const char *title = "Mix / Blend";
                ImVec2 ts = ImGui::CalcTextSize(title);
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_w - ts.x) * 0.5f);
                ImGui::TextColored(ImVec4(0.92f, 0.68f, 0.22f, 1.0f), "%s", title);
                ImGui::SetWindowFontScale(1.0f);

                float slider_w = 280.0f;
                float slider_x = (avail_w - slider_w) * 0.5f;
                if (slider_x < 0.0f) slider_x = 0.0f;

                /* Chain A mix */
                float mix_a = fx_chain_get_mix(engine, FX_CHAIN_DEFAULT);
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + slider_x);
                ImGui::SetNextItemWidth(slider_w);
                if (ImGui::SliderFloat("Chain A Level", &mix_a, 0.0f, 1.0f, "%.2f"))
                    fx_chain_set_mix(engine, FX_CHAIN_DEFAULT, mix_a);

                if (gui->chain_b >= 0) {
                    float mix_b = fx_chain_get_mix(engine, gui->chain_b);
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + slider_x);
                    ImGui::SetNextItemWidth(slider_w);
                    if (ImGui::SliderFloat("Chain B Level", &mix_b, 0.0f, 1.0f, "%.2f"))
                        fx_chain_set_mix(engine, gui->chain_b, mix_b);
                }
            }
            else if (sel.kind == NODE_AMP) {
                fx_chain_id amp_chain = (fx_chain_id)sel.chain_id;
                fx_amp_type_t amp_type = fx_amp_get_model(engine, amp_chain);
                static const char *amp_names[] = {
                    "Fullerton Clean", "British Crunch", "Southwest Lead",
                    "Essex Chime", "Tweed Blues", "Meridian High Gain",
                    "Citrus Roar", "Citrus Terror", "Regent 800",
                    "Solar Monolith", "Eclipse Drone"
                };
                int current_amp = (int)amp_type;
                float avail_w = ImGui::GetContentRegionAvail().x;

                /* Title */
                {
                    const char *amp_name = fx_amp_get_type_name(amp_type);
                    ImGui::SetWindowFontScale(1.35f);
                    ImVec2 text_size = ImGui::CalcTextSize(amp_name);
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_w - text_size.x) * 0.5f);
                    ImGui::TextColored(ImVec4(0.92f, 0.68f, 0.22f, 1.0f), "%s", amp_name);
                    ImGui::SetWindowFontScale(1.0f);
                    char sub[64];
                    snprintf(sub, sizeof(sub), "Amp Model %s",
                             is_dual ? (amp_chain == 0 ? "-- Chain A" : "-- Chain B") : "");
                    ImVec2 sub_sz = ImGui::CalcTextSize(sub);
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_w - sub_sz.x) * 0.5f);
                    ImGui::TextDisabled("%s", sub);
                }

                ImGui::Dummy(ImVec2(0.0f, 4.0f));

                /* Model selector */
                {
                    float combo_w = 200.0f;
                    float combo_off = (avail_w - combo_w - 60.0f) * 0.5f;
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
                }

                ImGui::Dummy(ImVec2(0.0f, 8.0f));

                /* Amp face image with overlay knobs */
                {
                    const char *aname = fx_amp_get_type_name(amp_type);
                    uintptr_t face_tex = load_amp_face_texture(aname);
                    float img_w = 500.0f;
                    float img_h = img_w * 0.65f;
                    if (face_tex) {
                        int tw = 0, th = 0;
                        if (fx_texture_get_size(face_tex, &tw, &th) && th > 0)
                            img_h = img_w / ((float)tw / (float)th);
                    }
                    float img_x = (avail_w - img_w) * 0.5f;
                    if (img_x < 0.0f) img_x = 0.0f;
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + img_x);
                    ImVec2 img_pos = ImGui::GetCursorScreenPos();
                    (void)img_pos; /* used by overlay knob maps in gui_main.cpp */

                    if (face_tex)
                        ImGui::Image((ImTextureID)face_tex, ImVec2(img_w, img_h));
                    else
                        ImGui::Dummy(ImVec2(img_w, img_h));

                    /* Overlay knobs — simplified: show knobs using knob_float
                     * below the image. Full overlay map rendering is in gui_main.cpp
                     * and will be migrated incrementally. */
                    int param_count = fx_amp_get_param_count(amp_type);
                    ImGui::Dummy(ImVec2(0.0f, 8.0f));
                    for (int p = 0; p < param_count && p < 10; p++) {
                        fx_amp_param_t ap = (fx_amp_param_t)p;
                        const char *pname = fx_amp_get_param_name(amp_type, ap);
                        float val = fx_amp_get_param(engine, amp_chain, ap);
                        float pmax = (ap == FX_AMP_PARAM_SAG || ap == FX_AMP_PARAM_BRIGHT)
                                     ? 1.0f : 10.0f;
                        if (knob_float(pname, &val, 0.0f, pmax,
                                       pmax * 0.5f, 0.01f))
                            fx_amp_set_param(engine, amp_chain, ap, val);
                        if (p < param_count - 1) ImGui::SameLine();
                    }
                }
            }
            else if (sel.kind == NODE_CAB) {
                fx_chain_id cab_chain = (fx_chain_id)sel.chain_id;
                int &cab_type_ref = (sel.chain_id == 0) ? gui->cab_type : gui->cab_type_b;
                float avail_w = ImGui::GetContentRegionAvail().x;

                const char *title = is_dual ? (sel.chain_id == 0 ? "Cabinet A" : "Cabinet B") : "Cabinet";
                ImGui::SetWindowFontScale(1.35f);
                ImVec2 ts = ImGui::CalcTextSize(title);
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_w - ts.x) * 0.5f);
                ImGui::TextColored(ImVec4(0.92f, 0.68f, 0.22f, 1.0f), "%s", title);
                ImGui::SetWindowFontScale(1.0f);

                ImGui::Dummy(ImVec2(0.0f, 8.0f));

                /* Cab type selector */
                {
                    float combo_off = (avail_w - 280.0f) * 0.5f;
                    if (combo_off > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + combo_off);
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextDisabled("Cab Type");
                    ImGui::SameLine(0, 8);
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
                }

                ImGui::Dummy(ImVec2(0.0f, 8.0f));

                /* Cab image */
                {
                    uintptr_t cab_tex = load_cab_texture(cab_type_ref);
                    if (cab_tex) {
                        int cw = 0, ch = 0;
                        float imgh = 220.0f;
                        float imgw = imgh;
                        if (fx_texture_get_size(cab_tex, &cw, &ch) && ch > 0)
                            imgw = imgh * ((float)cw / (float)ch);
                        float imgx = (avail_w - imgw) * 0.5f;
                        if (imgx > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + imgx);
                        ImGui::Image((ImTextureID)cab_tex, ImVec2(imgw, imgh));
                    }
                }

                ImGui::Dummy(ImVec2(0.0f, 8.0f));

                /* Bypass toggle */
                bool cab_bypassed = fx_cab_get_bypass(engine, cab_chain);
                {
                    float btn_off = (avail_w - 120.0f) * 0.5f;
                    if (btn_off > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + btn_off);
                }
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
                ImGui::PushStyleColor(ImGuiCol_Button,
                    cab_bypassed ? ImVec4(0.30f, 0.10f, 0.08f, 0.9f) : ImVec4(0.10f, 0.28f, 0.10f, 0.9f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                    cab_bypassed ? ImVec4(0.42f, 0.14f, 0.10f, 1.0f) : ImVec4(0.14f, 0.40f, 0.14f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                    cab_bypassed ? ImVec4(0.55f, 0.18f, 0.12f, 1.0f) : ImVec4(0.18f, 0.52f, 0.18f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.78f, 0.65f, 1.0f));
                if (ImGui::Button(cab_bypassed ? "BYPASSED##cab" : "ACTIVE##cab", ImVec2(120, 28)))
                    fx_cab_set_bypass(engine, cab_chain, !cab_bypassed);
                ImGui::PopStyleColor(4);
                ImGui::PopStyleVar();
            }
            else if (sel.kind == NODE_PEDAL_PRE || sel.kind == NODE_PEDAL_POST) {
                fx_pedal_id pid = sel.pedal_id;
                fx_pedal_type_t pt = fx_pedal_get_type(engine, pid);
                if (pt < FX_PEDAL_TYPE_COUNT) {
                    const char *pname = fx_pedal_get_type_name(pt);
                    int nparam = fx_pedal_get_param_count(pt);
                    bool bypassed = fx_pedal_get_bypass(engine, pid);
                    float avail_w = ImGui::GetContentRegionAvail().x;

                    /* Title */
                    ImGui::SetWindowFontScale(1.35f);
                    ImVec2 ts = ImGui::CalcTextSize(pname);
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_w - ts.x) * 0.5f);
                    if (bypassed)
                        ImGui::TextDisabled("%s", pname);
                    else
                        ImGui::TextColored(ImVec4(0.92f, 0.68f, 0.22f, 1.0f), "%s", pname);
                    ImGui::SetWindowFontScale(1.0f);

                    ImGui::Dummy(ImVec2(0.0f, 4.0f));

                    /* Pedal image */
                    {
                        uintptr_t pedal_tex = load_pedal_texture(pname);
                        float imgh = 220.0f;
                        float imgw = imgh;
                        if (pedal_tex) {
                            int pw = 0, ph = 0;
                            if (fx_texture_get_size(pedal_tex, &pw, &ph) && ph > 0)
                                imgw = imgh * ((float)pw / (float)ph);
                        }
                        float imgx = (avail_w - imgw) * 0.5f;
                        if (imgx < 0.0f) imgx = 0.0f;
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + imgx);
                        if (pedal_tex) {
                            ImVec4 tint = bypassed ? ImVec4(0.6f,0.6f,0.6f,0.8f) : ImVec4(1,1,1,1);
                            ImGui::Image((ImTextureID)pedal_tex, ImVec2(imgw, imgh),
                                ImVec2(0,0), ImVec2(1,1), tint);
                        } else {
                            ImGui::Dummy(ImVec2(imgw, imgh));
                        }
                    }

                    ImGui::Dummy(ImVec2(0.0f, 8.0f));

                    /* Knobs */
                    {
                        const float KNOB_W = 66.0f;
                        const float KNOB_PAD = 8.0f;
                        float row_w = nparam * KNOB_W + (nparam > 1 ? (nparam-1)*KNOB_PAD : 0);
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

                    /* Bypass + Remove buttons */
                    {
                        float row_w = 120 + 16 + 80;
                        float row_off = (avail_w - row_w) * 0.5f;
                        if (row_off > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + row_off);

                        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
                        ImGui::PushStyleColor(ImGuiCol_Button,
                            bypassed ? ImVec4(0.30f,0.10f,0.08f,0.9f) : ImVec4(0.10f,0.28f,0.10f,0.9f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                            bypassed ? ImVec4(0.42f,0.14f,0.10f,1.0f) : ImVec4(0.14f,0.40f,0.14f,1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                            bypassed ? ImVec4(0.55f,0.18f,0.12f,1.0f) : ImVec4(0.18f,0.52f,0.18f,1.0f));
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f,0.78f,0.65f,1.0f));
                        if (ImGui::Button(bypassed ? "BYPASSED##ped" : "ACTIVE##ped", ImVec2(120, 28)))
                            fx_pedal_set_bypass(engine, pid, !bypassed);
                        ImGui::PopStyleColor(4);
                        ImGui::PopStyleVar();

                        ImGui::SameLine(0, 16);

                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f,0.10f,0.08f,0.8f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.50f,0.14f,0.10f,1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.65f,0.18f,0.12f,1.0f));
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f,0.70f,0.60f,1.0f));
                        if (ImGui::Button("Remove##rm_ped", ImVec2(80, 28))) {
                            fx_chain_remove_pedal(engine, pid);
                            fx_chain_pos_t pos = (sel.kind == NODE_PEDAL_PRE) ? FX_CHAIN_POS_PRE : FX_CHAIN_POS_POST;
                            fx_pedal_id *ids = (pos == FX_CHAIN_POS_PRE) ? gui->pre_ids : gui->post_ids;
                            int *id_count = (pos == FX_CHAIN_POS_PRE) ? &gui->pre_id_count : &gui->post_id_count;
                            int pi = sel.slot;
                            for (int j = pi; j < *id_count - 1; j++)
                                ids[j] = ids[j + 1];
                            (*id_count)--;
                            gui->selected_node = -1;
                        }
                        ImGui::PopStyleColor(4);
                    }
                }
            }
            else if (sel.kind == NODE_STUDIO) {
                fx_studio_id sid = sel.pedal_id;
                fx_studio_type_t st = fx_studio_get_type(engine, sid);
                if (st < FX_STUDIO_COUNT) {
                    const char *sname = fx_studio_get_type_name(st);
                    int nparam = fx_studio_get_param_count(st);
                    bool bypassed = fx_studio_get_bypass(engine, sid);
                    float avail_w = ImGui::GetContentRegionAvail().x;

                    ImGui::SetWindowFontScale(1.35f);
                    ImVec2 ts = ImGui::CalcTextSize(sname);
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_w - ts.x) * 0.5f);
                    if (bypassed)
                        ImGui::TextDisabled("%s", sname);
                    else
                        ImGui::TextColored(ImVec4(0.45f, 0.65f, 0.90f, 1.0f), "%s", sname);
                    ImGui::SetWindowFontScale(1.0f);

                    ImGui::Dummy(ImVec2(0.0f, 8.0f));

                    /* Knobs */
                    for (int k = 0; k < nparam; k++) {
                        const char *kname = fx_studio_get_param_name(st, k);
                        float kval = fx_studio_get_param(engine, sid, k);
                        if (knob_float(kname, &kval, 0.0f, 1.0f, 0.5f, 0.01f))
                            fx_studio_set_param(engine, sid, k, kval);
                        if (k < nparam - 1) ImGui::SameLine();
                    }

                    ImGui::Dummy(ImVec2(0.0f, 12.0f));

                    /* Bypass + Remove */
                    {
                        float row_off = (avail_w - 196.0f) * 0.5f;
                        if (row_off > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + row_off);

                        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
                        ImGui::PushStyleColor(ImGuiCol_Button,
                            bypassed ? ImVec4(0.30f,0.10f,0.08f,0.9f) : ImVec4(0.10f,0.20f,0.35f,0.9f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                            bypassed ? ImVec4(0.42f,0.14f,0.10f,1.0f) : ImVec4(0.14f,0.30f,0.50f,1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                            bypassed ? ImVec4(0.55f,0.18f,0.12f,1.0f) : ImVec4(0.18f,0.40f,0.65f,1.0f));
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f,0.80f,0.72f,1.0f));
                        if (ImGui::Button(bypassed ? "BYPASS##stb" : "ACTIVE##stb", ImVec2(100, 28)))
                            fx_studio_set_bypass(engine, sid, !bypassed);
                        ImGui::PopStyleColor(4);
                        ImGui::PopStyleVar();

                        ImGui::SameLine(0, 16);

                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f,0.10f,0.08f,0.8f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.50f,0.14f,0.10f,1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.65f,0.18f,0.12f,1.0f));
                        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f,0.70f,0.60f,1.0f));
                        if (ImGui::Button("Remove##rm_st", ImVec2(80, 28))) {
                            fx_studio_remove(engine, sid);
                            int pi = sel.slot;
                            for (int j = pi; j < gui->studio_id_count - 1; j++)
                                gui->studio_ids[j] = gui->studio_ids[j + 1];
                            gui->studio_id_count--;
                            gui->selected_node = -1;
                        }
                        ImGui::PopStyleColor(4);
                    }
                }
            }
            else {
                ImGui::TextDisabled("No editable parameters.");
            }
        }

        ImGui::End();
    }

    /* ── Status bar — level meters ───────────────────────────── */
    {
        ImGui::SetNextWindowPos(ImVec2(0, win_h - STATUS_H));
        ImGui::SetNextWindowSize(ImVec2(win_w, STATUS_H));
        ImGui::Begin("##status_bar", NULL,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);

        /* Dark background */
        {
            ImDrawList *dl_sb = ImGui::GetWindowDrawList();
            ImVec2 sb_min = ImGui::GetWindowPos();
            ImVec2 sb_max = ImVec2(sb_min.x + win_w, sb_min.y + STATUS_H);
            dl_sb->AddRectFilled(sb_min, sb_max, IM_COL32(16, 14, 11, 255));
            dl_sb->AddRectFilledMultiColor(
                sb_min, ImVec2(sb_max.x, sb_min.y + 8.0f),
                IM_COL32(0,0,0,80), IM_COL32(0,0,0,80),
                IM_COL32(0,0,0, 0), IM_COL32(0,0,0, 0));
            dl_sb->AddLine(sb_min, ImVec2(sb_max.x, sb_min.y),
                           IM_COL32(45, 38, 28, 200), 1.0f);
        }

        /* Simple level meter display */
        {
            float in_level  = fx_engine_get_input_level(engine);
            float out_level = fx_engine_get_output_level(engine);

            const float bar_w = 200.0f;
            const float bar_h = 12.0f;
            float bar_y = ImGui::GetCursorScreenPos().y + 12.0f;

            auto draw_meter = [&](float x, float level, bool clip) {
                ImDrawList *dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(ImVec2(x, bar_y), ImVec2(x + bar_w, bar_y + bar_h),
                                  IM_COL32(30, 27, 22, 255), 2.0f);
                float fill_w = bar_w * level;
                if (fill_w > bar_w) fill_w = bar_w;
                ImU32 fill_col = clip ? IM_COL32(220, 50, 30, 255)
                                      : (level > 0.8f ? IM_COL32(200, 160, 30, 255)
                                                       : IM_COL32(50, 170, 70, 255));
                if (fill_w > 0)
                    dl->AddRectFilled(ImVec2(x, bar_y), ImVec2(x + fill_w, bar_y + bar_h),
                                      fill_col, 2.0f);
            };

            ImGui::Spacing();
            ImGui::SetWindowFontScale(0.85f);
            ImGui::TextDisabled("IN");
            ImGui::SetWindowFontScale(1.0f);
            ImGui::SameLine(0, 6);
            ImVec2 in_meter_pos = ImGui::GetCursorScreenPos();
            in_meter_pos.y = bar_y;
            draw_meter(in_meter_pos.x, in_level, false);
            ImGui::Dummy(ImVec2(bar_w, bar_h));

            ImGui::SameLine(0, 30);

            ImGui::SetWindowFontScale(0.85f);
            ImGui::TextDisabled("OUT");
            ImGui::SetWindowFontScale(1.0f);
            ImGui::SameLine(0, 6);
            ImVec2 out_meter_pos = ImGui::GetCursorScreenPos();
            out_meter_pos.y = bar_y;
            draw_meter(out_meter_pos.x, out_level, false);
            ImGui::Dummy(ImVec2(bar_w, bar_h));
        }

        ImGui::End();
    }

    /* Window border (standalone only) */
    if (!is_plugin) {
        ImDrawList *fg = ImGui::GetForegroundDrawList();
        ImU32 border_col = IM_COL32(50, 45, 38, 200);
        fg->AddRect(ImVec2(0, 0), ImVec2(win_w, win_h), border_col, 0.0f, 0, 2.0f);
        float gs = 14.0f;
        ImU32 grip_col = IM_COL32(80, 72, 58, 180);
        fg->AddTriangleFilled(
            ImVec2(win_w - gs, win_h), ImVec2(win_w, win_h - gs),
            ImVec2(win_w, win_h), grip_col);
    }
}
