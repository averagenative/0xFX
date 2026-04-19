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
#include "imgui.h"
#include "imgui_impl_opengl3.h"

#include "gui_render.h"
#include "fx_theme.h"
#include "custom_cabs.h"

extern "C" {
#include "../engine/fx_engine.h"
#include "../core/log.h"
#include "knobs.h"
#include "texture.h"
}

#include <stdio.h>
#include <cmath>
#include <cstring>
#include <ctime>
#include <cstdlib>
#include <cfloat>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <dirent.h>
#endif

extern "C" {
#include "cJSON.h"
}

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
    "1x12 Open", "2x12 Closed", "4x12 Straight", "4x12 Slant"
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
    "1x12_open", "2x12_closed", "4x12_straight", "4x12_slant",
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

/* ── Preset Browser ─────────────────────────────────────────── */

struct PresetEntry {
    char name[128];
    char description[256];
    char category[32];
    char path[512];
    bool is_factory;
};

#define MAX_BROWSER_PRESETS 128
/* Mutable preset browser state moved into fx_gui_state struct.
 * The helper functions below take explicit pointers instead of
 * using file-scope statics, making them safe for multi-instance use. */

/* ── GUI state struct ──────────────────────────────────────── */
/* All mutable per-instance rendering state lives here. Static const data
 * (knob maps, pedal tables, etc.) stays file-scope — it's read-only and
 * safe to share across instances. */

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
    bool         preset_modified;

    /* Theme textures (loaded once per instance) */
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

    /* Preset browser state (per-instance, was file-scope static) */
    PresetEntry  browser_presets[MAX_BROWSER_PRESETS];
    int          browser_preset_count;
    bool         browser_needs_scan;
    int          selected_cat;

    /* Tuner EMA smoothing (per-instance, no statics) */
    float        tuner_smoothed_cents;

    /* Per-instance theme selection. -1 = unset (standalone manages its own
     * theme); 0..FX_THEME_COUNT-1 = palette applied every frame. */
    int          current_theme;

    /* Looper docked-strip panel visibility (per-instance). */
    bool         looper_panel_open;
};

static const char *s_preset_categories[] = {
    "Classic", "80s", "90s", "Modern", "Heavy", "Experimental", "User"
};
static const int s_preset_category_count = 7;

static bool preset_scan_file(const char *path, PresetEntry *entry, bool is_factory) {
    FILE *f = fopen(path, "r");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz <= 0 || sz > 65536) { fclose(f); return false; }
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc(sz + 1);
    if (!buf) { fclose(f); return false; }
    size_t rd = fread(buf, 1, sz, f);
    fclose(f);
    buf[rd] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) return false;

    cJSON *name_j = cJSON_GetObjectItemCaseSensitive(root, "name");
    cJSON *desc_j = cJSON_GetObjectItemCaseSensitive(root, "description");
    cJSON *cat_j  = cJSON_GetObjectItemCaseSensitive(root, "category");

    if (name_j && cJSON_IsString(name_j))
        snprintf(entry->name, sizeof(entry->name), "%s", name_j->valuestring);
    else
        snprintf(entry->name, sizeof(entry->name), "Untitled");

    if (desc_j && cJSON_IsString(desc_j))
        snprintf(entry->description, sizeof(entry->description), "%s", desc_j->valuestring);
    else
        entry->description[0] = '\0';

    if (cat_j && cJSON_IsString(cat_j))
        snprintf(entry->category, sizeof(entry->category), "%s", cat_j->valuestring);
    else
        snprintf(entry->category, sizeof(entry->category), "User");

    snprintf(entry->path, sizeof(entry->path), "%s", path);
    entry->is_factory = is_factory;

    cJSON_Delete(root);
    return true;
}

static void preset_scan_dir(const char *dirpath, bool is_factory, const char *category_override,
                            PresetEntry *browser_presets, int *browser_preset_count) {
#ifdef _WIN32
    char pattern[600];
    snprintf(pattern, sizeof(pattern), "%s\\*.0xfx", dirpath);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (*browser_preset_count >= MAX_BROWSER_PRESETS) break;
        char fullpath[600];
        snprintf(fullpath, sizeof(fullpath), "%s\\%s", dirpath, fd.cFileName);
        PresetEntry *e = &browser_presets[*browser_preset_count];
        if (preset_scan_file(fullpath, e, is_factory)) {
            if (category_override && category_override[0])
                snprintf(e->category, sizeof(e->category), "%s", category_override);
            (*browser_preset_count)++;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(dirpath);
    if (!d) return;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (*browser_preset_count >= MAX_BROWSER_PRESETS) break;
        const char *name = ent->d_name;
        size_t len = strlen(name);
        if (len < 5 || strcmp(name + len - 5, ".0xfx") != 0) continue;
        char fullpath[600];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", dirpath, name);
        PresetEntry *e = &browser_presets[*browser_preset_count];
        if (preset_scan_file(fullpath, e, is_factory)) {
            if (category_override && category_override[0])
                snprintf(e->category, sizeof(e->category), "%s", category_override);
            (*browser_preset_count)++;
        }
    }
    closedir(d);
#endif
}

static void preset_browser_scan(fx_gui_state_t *gui) {
    gui->browser_preset_count = 0;

    const char *factory_cats[] = { "classic", "80s", "90s", "modern", "heavy", "experimental" };
    const char *cat_labels[]   = { "Classic", "80s", "90s", "Modern", "Heavy", "Experimental" };

    /* Search multiple base paths — CWD varies between standalone and plugin */
    const char *base_paths[] = {
        "presets/factory",
        "../presets/factory",
#ifdef _WIN32
        "C:/Users/Dan Michael/Desktop/0xFX-test/presets/factory",
#endif
    };
    int n_bases = sizeof(base_paths) / sizeof(base_paths[0]);

    for (int i = 0; i < 6; i++) {
        for (int b = 0; b < n_bases; b++) {
            char dirpath[600];
            snprintf(dirpath, sizeof(dirpath), "%s/%s", base_paths[b], factory_cats[i]);
            preset_scan_dir(dirpath, true, cat_labels[i],
                            gui->browser_presets, &gui->browser_preset_count);
        }
    }

    const char *user_dirs[] = { "presets", "../presets" };
    for (int d = 0; d < 2; d++) {
#ifdef _WIN32
        char pattern[600];
        snprintf(pattern, sizeof(pattern), "%s\\*.0xfx", user_dirs[d]);
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA(pattern, &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            if (gui->browser_preset_count >= MAX_BROWSER_PRESETS) break;
            if (strstr(fd.cFileName, "last_session") != NULL) continue;
            char fullpath[600];
            snprintf(fullpath, sizeof(fullpath), "%s\\%s", user_dirs[d], fd.cFileName);
            bool dup = false;
            for (int k = 0; k < gui->browser_preset_count; k++) {
                const char *existing = strrchr(gui->browser_presets[k].path, '\\');
                if (!existing) existing = strrchr(gui->browser_presets[k].path, '/');
                if (!existing) existing = gui->browser_presets[k].path;
                else existing++;
                if (strcmp(existing, fd.cFileName) == 0) { dup = true; break; }
            }
            if (dup) continue;
            PresetEntry *e = &gui->browser_presets[gui->browser_preset_count];
            if (preset_scan_file(fullpath, e, false)) {
                if (e->category[0] == '\0' || strcmp(e->category, "User") == 0)
                    snprintf(e->category, sizeof(e->category), "User");
                gui->browser_preset_count++;
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
#else
        DIR *dd = opendir(user_dirs[d]);
        if (!dd) continue;
        struct dirent *ent;
        while ((ent = readdir(dd)) != NULL) {
            if (gui->browser_preset_count >= MAX_BROWSER_PRESETS) break;
            const char *name = ent->d_name;
            size_t len = strlen(name);
            if (len < 5 || strcmp(name + len - 5, ".0xfx") != 0) continue;
            if (strstr(name, "last_session") != NULL) continue;
            char fullpath[600];
            snprintf(fullpath, sizeof(fullpath), "%s/%s", user_dirs[d], name);
            bool dup = false;
            for (int k = 0; k < gui->browser_preset_count; k++) {
                const char *existing = strrchr(gui->browser_presets[k].path, '/');
                if (!existing) existing = gui->browser_presets[k].path;
                else existing++;
                if (strcmp(existing, name) == 0) { dup = true; break; }
            }
            if (dup) continue;
            PresetEntry *e = &gui->browser_presets[gui->browser_preset_count];
            if (preset_scan_file(fullpath, e, false)) {
                if (e->category[0] == '\0' || strcmp(e->category, "User") == 0)
                    snprintf(e->category, sizeof(e->category), "User");
                gui->browser_preset_count++;
            }
        }
        closedir(dd);
#endif
    }

    gui->browser_needs_scan = false;
    FX_INFO("Preset browser: scanned %d presets", gui->browser_preset_count);
}

/* ── Surprise Me — random preset generator ──────────────────── */

static float randf(float lo, float hi) {
    return lo + (float)rand() / (float)RAND_MAX * (hi - lo);
}

static void surprise_me_generate(fx_engine_t *engine, char *preset_name, int name_sz) {
    srand((unsigned)time(NULL));

    /* Clear existing chain */
    int pre_count = fx_chain_get_pedal_count(engine, FX_CHAIN_POS_PRE);
    for (int i = pre_count - 1; i >= 0; i--) {
        fx_pedal_id pid = fx_chain_get_pedal_at(engine, FX_CHAIN_POS_PRE, i);
        fx_chain_remove_pedal(engine, pid);
    }
    int post_count = fx_chain_get_pedal_count(engine, FX_CHAIN_POS_POST);
    for (int i = post_count - 1; i >= 0; i--) {
        fx_pedal_id pid = fx_chain_get_pedal_at(engine, FX_CHAIN_POS_POST, i);
        fx_chain_remove_pedal(engine, pid);
    }

    /* Random amp */
    fx_amp_type_t amp = (fx_amp_type_t)(rand() % FX_AMP_COUNT);
    fx_amp_set_model(engine, FX_CHAIN_DEFAULT, amp);
    fx_amp_set_param(engine, FX_CHAIN_DEFAULT, FX_AMP_PARAM_GAIN,     randf(0.2f, 0.7f));
    fx_amp_set_param(engine, FX_CHAIN_DEFAULT, FX_AMP_PARAM_VOLUME,   randf(0.5f, 0.7f));
    fx_amp_set_param(engine, FX_CHAIN_DEFAULT, FX_AMP_PARAM_BASS,     randf(0.2f, 0.8f));
    fx_amp_set_param(engine, FX_CHAIN_DEFAULT, FX_AMP_PARAM_MID,      randf(0.2f, 0.8f));
    fx_amp_set_param(engine, FX_CHAIN_DEFAULT, FX_AMP_PARAM_TREBLE,   randf(0.2f, 0.8f));
    fx_amp_set_param(engine, FX_CHAIN_DEFAULT, FX_AMP_PARAM_PRESENCE, randf(0.3f, 0.7f));

    /* Random cab */
    fx_cab_type_t cab = (fx_cab_type_t)(rand() % FX_CAB_TYPE_COUNT);
    fx_cab_params_t cab_params = {};
    cab_params.cab_type = cab;
    cab_params.mic_pos = FX_MIC_ON_AXIS;
    cab_params.speaker_fs = 80.0f;
    cab_params.brightness = randf(0.3f, 0.7f);
    cab_params.resonance = randf(0.2f, 0.6f);
    fx_cab_generate_ir(engine, FX_CHAIN_DEFAULT, &cab_params);

    /* Noise gate defaults */
    fx_gate_set_threshold(engine, randf(-55.0f, -42.0f));
    fx_gate_set_attack(engine, 1.0f);
    fx_gate_set_release(engine, 40.0f);
    fx_gate_set_hold(engine, 12.0f);

    /* Pre-pedal types that make sense before amp */
    fx_pedal_type_t pre_types[] = {
        FX_PEDAL_JADE_DRIVE, FX_PEDAL_GOLD_DRIVE, FX_PEDAL_BLUES_GRIT,
        FX_PEDAL_RODENT, FX_PEDAL_ORANGE_DIST, FX_PEDAL_METAL_ZONE,
        FX_PEDAL_AMP_BOX, FX_PEDAL_MAMMOTH_FUZZ, FX_PEDAL_ROUND_FUZZ,
        FX_PEDAL_WRAITH_FUZZ, FX_PEDAL_CHAOS_FUZZ,
        FX_PEDAL_SQUEEZE_BOX, FX_PEDAL_GLASS_COMP, FX_PEDAL_PUNCH_COMP,
        FX_PEDAL_NOISE_GATE, FX_PEDAL_HOWL_WAH, FX_PEDAL_QUACK_FILTER,
        FX_PEDAL_WARM_TAPE, FX_PEDAL_GRIT_CRUSH, FX_PEDAL_OCTAVE_ENGINE,
    };
    int pre_type_count = (int)(sizeof(pre_types) / sizeof(pre_types[0]));

    /* Post-pedal types */
    fx_pedal_type_t post_types[] = {
        FX_PEDAL_ECHO_DELAY, FX_PEDAL_CARBON_DELAY, FX_PEDAL_TAPE_MACHINE,
        FX_PEDAL_MEMORY_ECHO, FX_PEDAL_DRIP_VERB, FX_PEDAL_HALL_VERB,
        FX_PEDAL_PLATE_VERB, FX_PEDAL_SHIMMER_VERB, FX_PEDAL_CLOUD_VERB,
        FX_PEDAL_LIQUID_CHORUS, FX_PEDAL_PHASE_SWEEP, FX_PEDAL_JET_FLANGER,
        FX_PEDAL_PULSE_TREM, FX_PEDAL_DRIFT_VIBRATO,
    };
    int post_type_count = (int)(sizeof(post_types) / sizeof(post_types[0]));

    /* Add 1-2 pre pedals */
    int n_pre = 1 + (rand() % 2);
    for (int i = 0; i < n_pre; i++) {
        fx_pedal_type_t pt = pre_types[rand() % pre_type_count];
        fx_pedal_id pid = fx_chain_add_pedal(engine, pt, FX_CHAIN_POS_PRE);
        if (pid >= 0) {
            int pc = fx_pedal_get_param_count(pt);
            for (int p = 0; p < pc; p++)
                fx_pedal_set_param(engine, pid, p, randf(0.2f, 0.8f));
        }
    }

    /* Add 1-2 post pedals */
    int n_post = 1 + (rand() % 2);
    for (int i = 0; i < n_post; i++) {
        fx_pedal_type_t pt = post_types[rand() % post_type_count];
        fx_pedal_id pid = fx_chain_add_pedal(engine, pt, FX_CHAIN_POS_POST);
        if (pid >= 0) {
            int pc = fx_pedal_get_param_count(pt);
            for (int p = 0; p < pc; p++)
                fx_pedal_set_param(engine, pid, p, randf(0.2f, 0.8f));
        }
    }

    /* Safety limiter — always at end of rack chain to prevent hearing damage */
    {
        fx_studio_id lid = fx_studio_add(engine, FX_STUDIO_BRICK_WALL);
        fx_studio_set_param(engine, lid, 0, 0.4f);  /* threshold ~-7dB (conservative) */
        fx_studio_set_param(engine, lid, 1, 0.85f);  /* ceiling ~-0.15dB */
        fx_studio_set_param(engine, lid, 2, 0.5f);  /* medium release */
    }

    /* Generate a fun name */
    static const char *adjectives[] = {
        "Cosmic", "Brutal", "Velvet", "Neon", "Rusty", "Haunted", "Molten",
        "Crystal", "Shadow", "Atomic", "Vintage", "Frosty", "Electric", "Midnight",
    };
    static const char *nouns[] = {
        "Thunder", "Voodoo", "Horizon", "Blaze", "Storm", "Whisper", "Fury",
        "Dream", "Howl", "Phantom", "Siren", "Cascade", "Tremor", "Inferno",
    };
    int n_adj = (int)(sizeof(adjectives) / sizeof(adjectives[0]));
    int n_noun = (int)(sizeof(nouns) / sizeof(nouns[0]));
    snprintf(preset_name, name_sz, "%s %s",
             adjectives[rand() % n_adj], nouns[rand() % n_noun]);

    FX_INFO("Surprise Me: generated '%s'", preset_name);
}

/* HSV to RGB for rainbow button effect */
static ImVec4 hsv_to_rgb(float h, float s, float v) {
    float r, g, b;
    ImGui::ColorConvertHSVtoRGB(h, s, v, r, g, b);
    return ImVec4(r, g, b, 1.0f);
}

/* ── Theme setup ───────────────────────────────────────────── */

extern "C" void fx_gui_setup_theme(void) {
    ImGuiStyle &style = ImGui::GetStyle();
    fx_theme_apply(style, FX_THEME_GRIME_DARK);

    /* Shape / spacing settings are theme-agnostic. */
    style.WindowRounding    = 4.0f;
    style.FrameRounding     = 3.0f;
    style.GrabRounding      = 3.0f;
    style.TabRounding       = 3.0f;
    style.WindowPadding     = ImVec2(10, 10);
    style.FramePadding      = ImVec2(6, 4);
    style.ItemSpacing       = ImVec2(8, 6);
}

extern "C" int fx_gui_get_theme(const fx_gui_state_t *gui) {
    return gui ? gui->current_theme : -1;
}

extern "C" void fx_gui_set_theme(fx_gui_state_t *gui, int theme_id) {
    if (!gui) return;
    if (theme_id < 0 || theme_id >= (int)FX_THEME_COUNT) {
        gui->current_theme = -1;
        return;
    }
    gui->current_theme = theme_id;
    fx_theme_apply(ImGui::GetStyle(), (fx_theme_id_t)theme_id);
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
    g->browser_needs_scan = true;
    g->browser_preset_count = 0;
    g->selected_cat = 0;
    g->current_theme = -1;
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

    gui->studio_id_count = 0; /* studio IDs tracked separately — reset on preset load */
    gui->selected_node = -1;

    /* Sync dual chain state from engine */
    int chain_count = fx_chain_get_count(engine);
    if (chain_count > 1) {
        gui->chain_b = 1; /* chain 0 is default, chain 1 is the dual */
    } else {
        gui->chain_b = -1;
    }

    FX_INFO("GUI sync: %d pre-pedals, %d post-pedals, chains=%d",
            gui->pre_id_count, gui->post_id_count, chain_count);
}

/* ── Looper panel (ported from gui_main.cpp, per-instance) ──── */

static ImVec4 looper_state_color_th(fx_loop_state_t s, bool muted,
                                    const fx_theme_t *th) {
    if (muted) return ImVec4(0.35f, 0.35f, 0.35f, 1.0f);
    switch (s) {
    case FX_LOOP_EMPTY:       return th->frame;
    case FX_LOOP_ARMED:       return ImVec4(0.80f, 0.60f, 0.15f, 1.0f);
    case FX_LOOP_RECORDING:   return ImVec4(0.85f, 0.15f, 0.15f, 1.0f);
    case FX_LOOP_PLAYING:     return ImVec4(0.20f, 0.70f, 0.30f, 1.0f);
    case FX_LOOP_OVERDUBBING: return ImVec4(0.85f, 0.55f, 0.20f, 1.0f);
    }
    return th->frame;
}

static const char *looper_state_label_str(fx_loop_state_t s) {
    switch (s) {
    case FX_LOOP_EMPTY:       return "empty";
    case FX_LOOP_ARMED:       return "armed";
    case FX_LOOP_RECORDING:   return "REC";
    case FX_LOOP_PLAYING:     return "play";
    case FX_LOOP_OVERDUBBING: return "DUB";
    }
    return "?";
}

static ImVec4 scale_rgb_local(ImVec4 c, float s) {
    return ImVec4(c.x * s, c.y * s, c.z * s, c.w);
}

static void looper_render_panel(fx_gui_state_t *gui, fx_engine_t *engine,
                                float x, float y, float w, float h,
                                const fx_theme_t *th) {
    if (!gui->looper_panel_open) return;
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::Begin("##looper_panel", NULL,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings);

    /* Background gradient + bottom separator */
    {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        ImVec2 p0 = ImGui::GetWindowPos();
        ImVec2 p1 = ImVec2(p0.x + w, p0.y + h);
        ImU32 c_top = ImGui::ColorConvertFloat4ToU32(
            ImVec4(th->panel.x * 1.15f, th->panel.y * 1.15f, th->panel.z * 1.15f, 1.0f));
        ImU32 c_bot = ImGui::ColorConvertFloat4ToU32(th->bg);
        dl->AddRectFilledMultiColor(p0, p1, c_top, c_top, c_bot, c_bot);
        ImVec4 sep = th->border; sep.w = 0.8f;
        dl->AddLine(ImVec2(p0.x, p1.y - 1.0f), ImVec2(p1.x, p1.y - 1.0f),
                    ImGui::ColorConvertFloat4ToU32(sep), 1.0f);
    }

    ImGui::PushStyleColor(ImGuiCol_Text, th->accent_glow);
    ImGui::SetCursorPos(ImVec2(10, 8));
    ImGui::Text("LOOPER");
    ImGui::PopStyleColor();

    ImGui::SetCursorPos(ImVec2(w - 56, 6));
    if (ImGui::SmallButton("?##looper_help"))
        ImGui::OpenPopup("looper_keybinds");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show looper keybinds");

    ImGui::SetCursorPos(ImVec2(w - 28, 6));
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.60f, 0.20f, 0.20f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.45f, 0.12f, 0.12f, 1.0f));
    if (ImGui::SmallButton("X##looper_close"))
        gui->looper_panel_open = false;
    ImGui::PopStyleColor(3);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Close looper panel");

    if (ImGui::BeginPopup("looper_keybinds")) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.78f, 0.6f, 1.0f));
        ImGui::Text("Looper keybinds");
        ImGui::PopStyleColor();
        ImGui::Separator();
        ImGui::BulletText("1 - 9       tap slot (rec -> play -> overdub)");
        ImGui::BulletText("Shift + 1-9 mute / unmute slot");
        ImGui::BulletText("Alt + 1-9   clear slot");
        ImGui::BulletText("Space       tap the FOCUSED slot");
        ImGui::BulletText("R           arm next empty slot");
        ImGui::BulletText("Tab         cycle focused slot");
        ImGui::BulletText("Ctrl + Z    undo last overdub on focused");
        ImGui::Separator();
        ImGui::TextDisabled("Clicking a pad does the same as pressing 1-9.");
        ImGui::TextDisabled("Click again while RECORDING to stop and play.");
        ImGui::EndPopup();
    }

    const int focused = fx_looper_focused(engine);
    bool playing = fx_looper_master_is_playing(engine);

    ImGui::SetCursorPos(ImVec2(10, 28));
    {
        ImVec4 btn = playing ? ImVec4(0.20f, 0.55f, 0.25f, 1.0f)
                             : scale_rgb_local(th->accent, 0.9f);
        ImGui::PushStyleColor(ImGuiCol_Button, btn);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
            playing ? ImVec4(0.28f, 0.70f, 0.33f, 1.0f) : th->accent_hover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,
            playing ? ImVec4(0.16f, 0.48f, 0.22f, 1.0f) : th->accent_active);
    }
    if (ImGui::Button(playing ? "Pause" : "Play", ImVec2(72, 26))) {
        fx_looper_master_toggle(engine);
    }
    ImGui::PopStyleColor(3);

    ImGui::SameLine();
    float level = fx_looper_get_master_level(engine);
    ImGui::SetNextItemWidth(130);
    if (ImGui::SliderFloat("##master", &level, 0.0f, 1.0f, "Vol %.2f")) {
        fx_looper_set_master_level(engine, level);
    }

    ImGui::SetCursorPos(ImVec2(10, 62));
    bool sync = fx_looper_get_sync(engine);
    if (ImGui::Checkbox("Sync", &sync)) fx_looper_set_sync(engine, sync);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Quantize new slots to the first loop's length");
    ImGui::SameLine();
    bool pre = fx_looper_get_tap_pre_chain(engine);
    if (ImGui::Checkbox("Pre-chain", &pre))
        fx_looper_set_tap_pre_chain(engine, pre);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("ON: record raw input. OFF: record post-chain (processed tone).");

    ImGui::SetCursorPos(ImVec2(10, 92));
    ImGui::Text("Focus:%d", focused + 1);
    ImGui::SameLine();
    if (ImGui::SmallButton("R arm"))    fx_looper_arm_next(engine);
    ImGui::SameLine();
    if (ImGui::SmallButton("Tab"))      fx_looper_focus_next(engine);
    ImGui::SameLine();
    if (ImGui::SmallButton("Undo"))     fx_looper_slot_undo(engine, focused);
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear"))    fx_looper_slot_clear(engine, focused);

    ImGui::SetCursorPos(ImVec2(10, 116));
    if (ImGui::SmallButton("Export slot")) {
        char path[512];
        snprintf(path, sizeof(path), "loop_slot_%d.wav", focused + 1);
        if (fx_looper_export_slot_wav(engine, focused, path))
            FX_INFO("Looper: exported slot %d to %s", focused + 1, path);
        else FX_WARN("Looper: export failed (slot empty?)");
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Export mix")) {
        if (fx_looper_export_mix_wav(engine, "loop_mix.wav"))
            FX_INFO("Looper: exported mix to loop_mix.wav");
        else FX_WARN("Looper: mix export failed");
    }

    /* 9 pads in a horizontal row on the right side of the strip. */
    const float pads_left = 260.0f;
    const float pad_gap   = 6.0f;
    const float pads_avail = w - pads_left - 12.0f;
    float pad_w = (pads_avail - pad_gap * 8) / 9.0f;
    if (pad_w < 56.0f) pad_w = 56.0f;
    if (pad_w > 110.0f) pad_w = 110.0f;
    const float pad_h = h - 20.0f;

    ImDrawList *dl = ImGui::GetWindowDrawList();

    for (int slot = 0; slot < 9; slot++) {
        float px = pads_left + slot * (pad_w + pad_gap);
        ImGui::SetCursorPos(ImVec2(px, 10));

        fx_loop_state_t st = fx_looper_get_slot_state(engine, slot);
        bool muted = fx_looper_get_slot_muted(engine, slot);
        int len    = fx_looper_get_slot_length_frames(engine, slot);
        int pos    = fx_looper_get_slot_play_pos(engine, slot);
        int layers = fx_looper_get_slot_layers(engine, slot);

        ImVec4 c = looper_state_color_th(st, muted, th);
        ImGui::PushStyleColor(ImGuiCol_Button, c);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
            ImVec4(c.x * 1.25f + 0.05f, c.y * 1.25f + 0.05f, c.z * 1.25f + 0.05f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,
            ImVec4(c.x * 0.8f, c.y * 0.8f, c.z * 0.8f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));

        char label[64];
        snprintf(label, sizeof(label), "%d\n%s%s\n%.1fs L%d###looper_pad_%d",
                 slot + 1, looper_state_label_str(st),
                 muted ? " m" : "",
                 len > 0 ? (float)len / 48000.0f : 0.0f, layers,
                 slot);

        ImGui::PushID(slot);
        if (ImGui::Button(label, ImVec2(pad_w, pad_h))) {
            fx_looper_set_focus(engine, slot);
            fx_looper_slot_tap(engine, slot);
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            fx_looper_set_focus(engine, slot);
            fx_looper_slot_clear(engine, slot);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Left-click: tap (rec/play/dub)\nRight-click: clear slot");
        ImGui::PopID();
        ImGui::PopStyleColor(4);

        ImVec2 p_min = ImGui::GetItemRectMin();
        ImVec2 p_max = ImGui::GetItemRectMax();
        if (len > 0 && (st == FX_LOOP_PLAYING || st == FX_LOOP_OVERDUBBING)) {
            float t = (float)pos / (float)len;
            float bar_x = p_min.x + 3 + t * (p_max.x - p_min.x - 6);
            dl->AddLine(ImVec2(bar_x, p_max.y - 6),
                        ImVec2(bar_x, p_max.y - 2),
                        IM_COL32(255, 255, 255, 220), 2.0f);
        }
        if (slot == focused) {
            dl->AddRect(p_min, p_max,
                        ImGui::ColorConvertFloat4ToU32(th->accent_glow),
                        3.0f, 0, 2.0f);
        }
    }

    ImGui::End();
}

static void looper_handle_keys(fx_engine_t *engine) {
    ImGuiIO &io = ImGui::GetIO();
    bool ctrl  = io.KeyCtrl;
    bool shift = io.KeyShift;
    bool alt   = io.KeyAlt;

    static const ImGuiKey num_keys[9] = {
        ImGuiKey_1, ImGuiKey_2, ImGuiKey_3,
        ImGuiKey_4, ImGuiKey_5, ImGuiKey_6,
        ImGuiKey_7, ImGuiKey_8, ImGuiKey_9,
    };
    for (int i = 0; i < 9; i++) {
        if (ImGui::IsKeyPressed(num_keys[i], false)) {
            if (alt)        fx_looper_slot_clear(engine, i);
            else if (shift) fx_looper_slot_mute(engine, i);
            else {
                fx_looper_set_focus(engine, i);
                fx_looper_slot_tap(engine, i);
            }
        }
    }

    if (ImGui::IsKeyPressed(ImGuiKey_R, false) && !ctrl && !shift && !alt)
        fx_looper_arm_next(engine);
    if (ImGui::IsKeyPressed(ImGuiKey_Tab, false) && !ctrl && !shift && !alt)
        fx_looper_focus_next(engine);
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z, false))
        fx_looper_slot_undo(engine, fx_looper_focused(engine));
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

    /* Per-frame theme apply — only when an explicit theme is selected.
     * Plugins use this for per-instance palettes; each plugin has its own
     * ImGui context so this never cross-contaminates. Standalone leaves
     * current_theme at -1 and manages its own theme in gui_main.cpp. */
    if (gui->current_theme >= 0 && gui->current_theme < (int)FX_THEME_COUNT) {
        fx_theme_apply(ImGui::GetStyle(), (fx_theme_id_t)gui->current_theme);
    }

    /* Layout constants */
    const float TOOLBAR_H      = 64.0f;
    const float LOOPER_H       = 136.0f;
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
            float raw_cents = active ? fx_tuner_get_cents(engine) : 0.0f;
            /* EMA smoothing for smoother animation (alpha=0.15) */
            if (!active) { gui->tuner_smoothed_cents = 0.0f; }
            else { gui->tuner_smoothed_cents += 0.15f * (raw_cents - gui->tuner_smoothed_cents); }
            float cents = gui->tuner_smoothed_cents;

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

        /* Preset name display */
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.75f, 0.55f, 1.0f));
            ImGui::SetWindowFontScale(0.9f);
            ImGui::AlignTextToFramePadding();
            if (gui->preset_modified)
                ImGui::Text("%s (unsaved)", gui->preset_name);
            else
                ImGui::Text("%s", gui->preset_name);
            ImGui::SetWindowFontScale(1.0f);
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Current preset");
        }

        ImGui::SameLine(0, 10);

        /* Presets browser button */
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.13f, 0.11f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.22f, 0.18f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.09f, 0.07f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 0.65f, 0.45f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
            if (ImGui::Button("Presets", ImVec2(70.0f, 32.0f))) {
                if (gui->browser_needs_scan) preset_browser_scan(gui);
                ImGui::OpenPopup("preset_browser_popup");
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(4);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Browse factory and user presets");
        }

        ImGui::SameLine(0, 6);

        /* Looper panel toggle */
        {
            int theme_id = (gui->current_theme >= 0 &&
                            gui->current_theme < (int)FX_THEME_COUNT)
                           ? gui->current_theme : (int)FX_THEME_GRIME_DARK;
            const fx_theme_t *th = fx_theme_get((fx_theme_id_t)theme_id);
            ImVec4 btn_bg = gui->looper_panel_open
                ? scale_rgb_local(th->accent, 0.55f)
                : th->frame;
            ImGui::PushStyleColor(ImGuiCol_Button,        btn_bg);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, th->frame_hover);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  th->frame_active);
            ImGui::PushStyleColor(ImGuiCol_Text,          th->text);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
            if (ImGui::Button("Looper", ImVec2(70.0f, 32.0f)))
                gui->looper_panel_open = !gui->looper_panel_open;
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(4);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("9-slot looper (1-9 tap, R arm, Tab focus, Ctrl+Z undo)");
        }

        /* Preset browser popup */
        ImGui::SetNextWindowSizeConstraints(ImVec2(640.0f, 0.0f),
                                            ImVec2(640.0f, FLT_MAX));
        if (ImGui::BeginPopup("preset_browser_popup")) {
            if (gui->browser_needs_scan) preset_browser_scan(gui);

            const float pb_popup_w = 640.0f;
            const float pb_btn_w   = 22.0f;
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.78f, 0.6f, 1.0f));
            ImGui::Text("Preset Library");
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::SetCursorPosX(pb_popup_w - pb_btn_w - ImGui::GetStyle().WindowPadding.x);
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.60f, 0.20f, 0.20f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.45f, 0.12f, 0.12f, 1.0f));
            if (ImGui::Button("X##preset_close", ImVec2(pb_btn_w, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopStyleColor(3);
            ImGui::Separator();

            /* Mystery Rig button with rainbow border */
            {
                float t = (float)ImGui::GetTime();
                float hue = fmodf(t * 0.3f, 1.0f);
                ImVec4 rainbow = hsv_to_rgb(hue, 0.8f, 0.9f);
                (void)hsv_to_rgb(hue, 0.5f, 0.6f);

                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.10f, 0.08f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.15f, 0.12f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.08f, 0.07f, 0.05f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, rainbow);
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);

                if (ImGui::Button(is_plugin ? "Mystery Rig (standalone only)" : "Mystery Rig",
                                  ImVec2(ImGui::GetContentRegionAvail().x, 36.0f))) {
                    if (!is_plugin) {
                        surprise_me_generate(engine, gui->preset_name, sizeof(gui->preset_name));
                        gui->preset_modified = false;
                        fx_gui_sync_from_engine(gui);
                        gui->selected_node = -1;
                    }
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopStyleVar();
                ImGui::PopStyleColor(4);

                /* Draw rainbow border around the button */
                {
                    ImDrawList *dl_rb = ImGui::GetWindowDrawList();
                    ImVec2 bmin = ImGui::GetItemRectMin();
                    ImVec2 bmax = ImGui::GetItemRectMax();
                    float pulse = 0.7f + 0.3f * sinf(t * 4.0f);
                    ImU32 border_col = ImGui::ColorConvertFloat4ToU32(
                        ImVec4(rainbow.x * pulse, rainbow.y * pulse, rainbow.z * pulse, 0.9f));
                    dl_rb->AddRect(bmin, bmax, border_col, 6.0f, 0, 2.0f);
                    ImU32 glow_col = ImGui::ColorConvertFloat4ToU32(
                        ImVec4(rainbow.x * 0.4f, rainbow.y * 0.4f, rainbow.z * 0.4f, 0.3f * pulse));
                    dl_rb->AddRect(ImVec2(bmin.x - 1, bmin.y - 1),
                                   ImVec2(bmax.x + 1, bmax.y + 1), glow_col, 7.0f, 0, 2.0f);
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Generate a random rig -- surprise yourself!");

                ImGui::Spacing();
            }

            /* Category tabs */
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2, 2));
            for (int c = 0; c < s_preset_category_count; c++) {
                int cat_count = 0;
                for (int p = 0; p < gui->browser_preset_count; p++) {
                    if (strcmp(gui->browser_presets[p].category, s_preset_categories[c]) == 0)
                        cat_count++;
                }
                if (cat_count == 0) continue;

                if (c > 0) ImGui::SameLine();
                bool selected = (gui->selected_cat == c);
                if (selected) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.25f, 0.15f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.85f, 0.6f, 1.0f));
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.10f, 0.08f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.55f, 0.45f, 1.0f));
                }
                char tab_label[64];
                snprintf(tab_label, sizeof(tab_label), "%s (%d)", s_preset_categories[c], cat_count);
                if (ImGui::Button(tab_label)) {
                    gui->selected_cat = c;
                }
                ImGui::PopStyleColor(2);
            }
            ImGui::PopStyleVar();

            ImGui::Separator();

            /* Preset list for selected category */
            ImGui::BeginChild("preset_list", ImVec2(480, 320), true);

            const char *sel_cat = s_preset_categories[gui->selected_cat];
            for (int p = 0; p < gui->browser_preset_count; p++) {
                PresetEntry *pe = &gui->browser_presets[p];
                if (strcmp(pe->category, sel_cat) != 0) continue;

                ImGui::PushID(p);

                if (pe->is_factory) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.7f, 0.3f, 1.0f));
                    ImGui::Text("[F]");
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.6f, 0.85f, 1.0f));
                    ImGui::Text("[U]");
                }
                ImGui::PopStyleColor();
                ImGui::SameLine();

                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.82f, 0.65f, 1.0f));
                if (ImGui::Selectable(pe->name, false, ImGuiSelectableFlags_None, ImVec2(440, 0))) {
                    bool ok = fx_preset_load(engine, pe->path);
                    if (ok) {
                        snprintf(gui->preset_name, sizeof(gui->preset_name), "%s", pe->name);
                        gui->preset_modified = false;
                        fx_gui_sync_from_engine(gui);
                        gui->selected_node = -1;
                        FX_INFO("Loaded preset: %s", pe->name);
                    } else {
                        FX_ERROR("Failed to load preset: %s", pe->path);
                    }
                    ImGui::CloseCurrentPopup();
                }
                ImGui::PopStyleColor();

                if (ImGui::IsItemHovered() && pe->description[0]) {
                    ImGui::SetTooltip("%s", pe->description);
                }

                /* Right-click context menu — delete for user presets only */
                if (!pe->is_factory && ImGui::BeginPopupContextItem()) {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.35f, 0.30f, 1.0f));
                    if (ImGui::Selectable("Delete")) {
                        remove(pe->path);
                        FX_INFO("Deleted preset: %s", pe->name);
                        gui->browser_needs_scan = true;
                    }
                    ImGui::PopStyleColor();
                    ImGui::EndPopup();
                }

                ImGui::PopID();
            }

            ImGui::EndChild();

            ImGui::Separator();

            /* Save / Save As / Refresh at bottom */
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.15f, 0.12f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.24f, 0.18f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 0.68f, 0.5f, 1.0f));

                if (ImGui::Button("Save", ImVec2(100, 0))) {
                    bool ok = fx_preset_save(engine, "presets/last_session.0xfx");
                    if (!ok) ok = fx_preset_save(engine, "../presets/last_session.0xfx");
                    FX_INFO(ok ? "Quick-saved" : "Quick-save failed");
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Save As...", ImVec2(100, 0))) {
                    gui->save_as_open = true;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Refresh", ImVec2(80, 0))) {
                    gui->browser_needs_scan = true;
                    preset_browser_scan(gui);
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Rescan preset directories");

                ImGui::PopStyleColor(3);
            }

            ImGui::EndPopup();
        }

        /* Save As popup modal */
        if (gui->save_as_open) {
            ImGui::OpenPopup("save_as_popup_r");
        }
        if (ImGui::BeginPopupModal("save_as_popup_r", &gui->save_as_open,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Save Preset As");
            ImGui::Separator();
            ImGui::SetNextItemWidth(280);
            bool enter_pressed = ImGui::InputText("Preset Name", gui->save_as_name,
                                                  sizeof(gui->save_as_name),
                                                  ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::Spacing();
            if ((ImGui::Button("Save", ImVec2(120, 0)) || enter_pressed) &&
                gui->save_as_name[0] != '\0') {
                char path[400];
                snprintf(path, sizeof(path), "presets/%s.0xfx", gui->save_as_name);
                bool ok = fx_preset_save(engine, path);
                if (!ok) {
                    snprintf(path, sizeof(path), "../presets/%s.0xfx", gui->save_as_name);
                    ok = fx_preset_save(engine, path);
                }
                FX_INFO(ok ? "Saved preset: %s" : "Save failed: %s", gui->save_as_name);
                if (ok) gui->browser_needs_scan = true;
                gui->save_as_open = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                gui->save_as_open = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::SameLine(0, 10);

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
            if (ImGui::Button(is_dual ? "DUAL##chain_toggle" : "SINGLE##chain_toggle",
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

        /* Plugin-only theme picker — standalone has its own in gui_main.cpp. */
        if (is_plugin) {
            int theme_id = (gui->current_theme >= 0 &&
                            gui->current_theme < (int)FX_THEME_COUNT)
                           ? gui->current_theme : (int)FX_THEME_GRIME_DARK;
            const fx_theme_t *th = fx_theme_get((fx_theme_id_t)theme_id);

            /* Right-align: pull to win_w - button_w - margin. */
            const float btn_w  = 150.0f;
            const float btn_h  = 32.0f;
            const float margin = 12.0f;
            ImGui::SameLine(win_w - btn_w - margin);

            ImGui::PushStyleColor(ImGuiCol_Button,        th->frame);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, th->frame_hover);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  th->frame_active);
            ImGui::PushStyleColor(ImGuiCol_Text,          th->text);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

            char tlabel[64];
            snprintf(tlabel, sizeof(tlabel), "Theme: %s", th->name);
            if (ImGui::Button(tlabel, ImVec2(btn_w, btn_h))) {
                ImGui::OpenPopup("theme_picker_popup");
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(4);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Cycle color theme — persists per plugin instance");

            if (ImGui::BeginPopup("theme_picker_popup")) {
                ImGui::PushStyleColor(ImGuiCol_Text, th->text);
                ImGui::Text("Color Theme");
                ImGui::PopStyleColor();
                ImGui::Separator();
                for (int i = 0; i < (int)FX_THEME_COUNT; i++) {
                    const fx_theme_t *ti = fx_theme_get((fx_theme_id_t)i);
                    bool selected = (i == theme_id);
                    if (ImGui::Selectable(ti->name, selected)) {
                        fx_gui_set_theme(gui, i);
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", ti->description);
                }
                ImGui::EndPopup();
            }
        }

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

    /* Looper docked strip between toolbar and signal chain. */
    if (gui->looper_panel_open) {
        int theme_id = (gui->current_theme >= 0 &&
                        gui->current_theme < (int)FX_THEME_COUNT)
                       ? gui->current_theme : (int)FX_THEME_GRIME_DARK;
        const fx_theme_t *lth = fx_theme_get((fx_theme_id_t)theme_id);
        looper_render_panel(gui, engine, 0.0f, TOOLBAR_H, win_w, LOOPER_H, lth);
        if (!ImGui::GetIO().WantTextInput)
            looper_handle_keys(engine);
    }

    /* ============================================================
     * SIGNAL CHAIN VIEW (~35% of window, below toolbar + looper)
     * ============================================================ */
    {
        float looper_h = gui->looper_panel_open ? LOOPER_H : 0.0f;
        float chain_top = TOOLBAR_H + looper_h;
        float chain_area_h = (win_h - chain_top - STATUS_H) * 0.35f;
        ImGui::SetNextWindowPos(ImVec2(0, chain_top));
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

        /* Section labels above the signal chain */
        {
            ImGui::SetWindowFontScale(0.75f);
            ImU32 label_col = IM_COL32(160, 140, 110, 120);
            float label_y = cy - NODE_H * 0.5f - 18.0f;
            if (is_dual) label_y = cy_a - NODE_H * 0.5f - 18.0f;

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
                    float nx_lbl = start_x + columns[ni] * (NODE_W + NODE_SPACING);
                    ImVec2 sz = ImGui::CalcTextSize(section);
                    dl->AddText(ImVec2(nx_lbl + (NODE_W - sz.x) * 0.5f, label_y), label_col, section);
                }
            }
            ImGui::SetWindowFontScale(1.0f);
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

                /* Node background — try texture first, then solid fill */
                bool drew_texture = false;
                {
                    uintptr_t tex = 0;
                    if (chain[i].kind == NODE_PEDAL_PRE || chain[i].kind == NODE_PEDAL_POST) {
                        fx_pedal_type_t pt = fx_pedal_get_type(engine, chain[i].pedal_id);
                        if (pt < FX_PEDAL_TYPE_COUNT) {
                            const char *tname = fx_pedal_get_type_name(pt);
                            tex = load_pedal_texture(tname);
                        }
                    } else if (chain[i].kind == NODE_AMP) {
                        const char *aname = fx_amp_get_type_name(
                            fx_amp_get_model(engine, (fx_chain_id)chain[i].chain_id));
                        char fname[128];
                        amp_name_to_filename(aname, fname, sizeof(fname));
                        char path[256];
                        snprintf(path, sizeof(path), "resources/amps/%s_body_nobg.png", fname);
                        tex = fx_texture_load(path);
                    } else if (chain[i].kind == NODE_CAB) {
                        int ctype = (chain[i].chain_id == 0) ? gui->cab_type : gui->cab_type_b;
                        tex = load_cab_texture(ctype);
                    } else if (chain[i].kind == NODE_STUDIO) {
                        static const char *rack_fnames[] = {
                            "iron_squeeze", "glass_eq", "reel_warmth", "brick_wall",
                            "velvet_press", "glue_bus", "valve_color", "precision_eq", "room_engine"
                        };
                        fx_studio_type_t st = fx_studio_get_type(engine, chain[i].pedal_id);
                        if (st >= 0 && st < FX_STUDIO_COUNT) {
                            char spath[256];
                            snprintf(spath, sizeof(spath), "resources/studio/%s_nobg.png", rack_fnames[st]);
                            tex = fx_texture_load(spath);
                        }
                    } else if (chain[i].kind == NODE_INPUT) {
                        /* TRS plug input — rendered flipped horizontally */
                        tex = fx_texture_load("resources/cables/trs_plug_input.png");
                        if (tex) {
                            ImGui::SetCursorScreenPos(ImVec2(nx, ny));
                            ImGui::PushID(i);
                            ImGui::Image((ImTextureID)tex, ImVec2(NODE_W, NODE_H),
                                         ImVec2(1, 0), ImVec2(0, 1),
                                         ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
                            ImGui::PopID();
                            drew_texture = true;
                            tex = 0;
                        }
                    } else if (chain[i].kind == NODE_OUTPUT) {
                        tex = fx_texture_load("resources/cables/xlr_plug_output.png");
                    }
                    if (tex) {
                        ImVec4 tint = bypassed
                            ? ImVec4(0.5f, 0.5f, 0.5f, 0.7f)
                            : ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
                        ImGui::SetCursorScreenPos(ImVec2(nx, ny));
                        ImGui::PushID(i);
                        ImGui::Image((ImTextureID)tex, ImVec2(NODE_W, NODE_H),
                                     ImVec2(0, 0), ImVec2(1, 1), tint);
                        ImGui::PopID();
                        drew_texture = true;
                    }
                }
                if (!drew_texture) {
                    dl->AddRectFilled(tl, br, col_fill, 6.0f);
                }

                /* Selection highlight */
                if (is_selected)
                    dl->AddRect(ImVec2(tl.x-2,tl.y-2), ImVec2(br.x+2,br.y+2),
                                IM_COL32(255, 200, 60, 255), 8.0f, 0, 2.5f);
                else if (!drew_texture)
                    dl->AddRect(tl, br, IM_COL32(80, 70, 55, 200), 6.0f, 0, 1.0f);

                /* LED indicator for bypass status */
                if (chain[i].kind == NODE_PEDAL_PRE || chain[i].kind == NODE_PEDAL_POST ||
                    chain[i].kind == NODE_STUDIO) {
                    const char *led_path = bypassed
                        ? "resources/leds/led_red_off_nobg.png"
                        : "resources/leds/led_green_on_nobg.png";
                    uintptr_t led_tex = fx_texture_load(led_path);
                    const float LED_SZ = 12.0f;
                    float led_x = nx + NODE_W - LED_SZ - 3.0f;
                    float led_y = ny + 3.0f;
                    if (led_tex) {
                        ImGui::SetCursorScreenPos(ImVec2(led_x, led_y));
                        ImGui::PushID(i + 1000);
                        ImGui::Image((ImTextureID)(uintptr_t)led_tex, ImVec2(LED_SZ, LED_SZ));
                        ImGui::PopID();
                    } else {
                        ImU32 led_col = bypassed ? IM_COL32(200, 60, 50, 200)
                                                 : IM_COL32(60, 200, 60, 220);
                        dl->AddCircleFilled(
                            ImVec2(led_x + LED_SZ*0.5f, led_y + LED_SZ*0.5f),
                            LED_SZ * 0.5f, led_col, 12);
                    }
                }

                /* Label text below node */
                {
                    /* Override cab label with actual cab type name */
                    if (chain[i].kind == NODE_CAB) {
                        int ctype = (chain[i].chain_id == 0) ? gui->cab_type : gui->cab_type_b;
                        if (ctype >= 0 && ctype < FX_CAB_TYPE_COUNT)
                            label = s_cab_type_names[ctype];
                    }
                    char short_lbl[16];
                    ImVec2 ts = ImGui::CalcTextSize(label);
                    if (ts.x > NODE_W - 4.0f) {
                        int copy_len = 9;
                        if (copy_len > (int)strlen(label)) copy_len = (int)strlen(label);
                        memcpy(short_lbl, label, copy_len);
                        short_lbl[copy_len] = '\0';
                        label = short_lbl;
                        ts = ImGui::CalcTextSize(label);
                    }
                    float lbl_x = nx + (NODE_W - ts.x) * 0.5f;
                    float lbl_y = ny + NODE_H + 4.0f;
                    dl->AddText(ImVec2(lbl_x, lbl_y),
                                bypassed ? IM_COL32(100, 90, 80, 200)
                                         : IM_COL32(210, 200, 180, 255),
                                label);
                }

                /* Clickable area */
                ImGui::SetCursorScreenPos(tl);
                char uid[32]; snprintf(uid, sizeof(uid), "##node_%d", i);
                if (ImGui::InvisibleButton(uid, ImVec2(NODE_W, NODE_H))) {
                    if (chain[i].kind != NODE_INPUT && chain[i].kind != NODE_OUTPUT)
                        gui->selected_node = (gui->selected_node == i) ? -1 : i;
                }

                /* Right-click or double-click = stomp (toggle bypass) */
                {
                    bool stomped = false;
                    if (ImGui::IsItemHovered()) {
                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                            stomped = true;
                        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                            stomped = true;
                    }
                    if (stomped) {
                        if (chain[i].kind == NODE_PEDAL_PRE || chain[i].kind == NODE_PEDAL_POST) {
                            bool bp = fx_pedal_get_bypass(engine, chain[i].pedal_id);
                            fx_pedal_set_bypass(engine, chain[i].pedal_id, !bp);
                        } else if (chain[i].kind == NODE_STUDIO) {
                            bool bp = fx_studio_get_bypass(engine, chain[i].pedal_id);
                            fx_studio_set_bypass(engine, chain[i].pedal_id, !bp);
                        } else if (chain[i].kind == NODE_CAB) {
                            bool bp = fx_cab_get_bypass(engine, (fx_chain_id)chain[i].chain_id);
                            fx_cab_set_bypass(engine, (fx_chain_id)chain[i].chain_id, !bp);
                        }
                    }
                }

                /* Tooltip */
                if (ImGui::IsItemHovered()) {
                    if (chain[i].kind == NODE_PEDAL_PRE || chain[i].kind == NODE_PEDAL_POST) {
                        fx_pedal_type_t pt = fx_pedal_get_type(engine, chain[i].pedal_id);
                        const char *tip = get_pedal_tooltip(pt);
                        if (tip) ImGui::SetTooltip("%s", tip);
                    } else if (chain[i].kind == NODE_PEDAL_PRE || chain[i].kind == NODE_PEDAL_POST ||
                               chain[i].kind == NODE_STUDIO) {
                        ImGui::SetTooltip("Right-click to bypass/activate");
                    }
                }
            }

            /* Draw cable to next node in single-lane sections */
            if (i < chain_len - 1) {
                bool in_parallel = is_dual && split_col >= 0 && merge_col >= 0 &&
                                   columns[i] >= split_col && columns[i] < merge_col;
                if (!in_parallel && chain[i+1].kind != NODE_SPLIT &&
                    chain[i].kind != NODE_MERGE) {
                    bool next_parallel = is_dual && split_col >= 0 && merge_col >= 0 &&
                                         columns[i+1] > split_col && columns[i+1] < merge_col;
                    if (!next_parallel) {
                        /* Cable endpoints */
                        float cable_x0 = center.x + NODE_W * 0.5f;
                        float cable_y0 = center.y;
                        ImVec2 p2_center = get_node_pos(i + 1);
                        float cable_x1 = p2_center.x - NODE_W * 0.5f;
                        float cable_y1 = p2_center.y;

                        /* INPUT/OUTPUT: adjust endpoints + droop */
                        bool has_droop = false;
                        if (chain[i].kind == NODE_INPUT) {
                            cable_x0 = center.x + NODE_W * 0.05f;
                            cable_y0 = center.y + NODE_H * 0.45f;
                            has_droop = true;
                        }
                        if (i + 1 < chain_len && chain[i+1].kind == NODE_OUTPUT) {
                            cable_x1 = p2_center.x - NODE_W * 0.35f;
                            cable_y1 = p2_center.y + NODE_H * 0.15f;
                            has_droop = true;
                        }

                        float span = cable_x1 - cable_x0;
                        ImVec2 p0(cable_x0, cable_y0);
                        ImVec2 p3(cable_x1, cable_y1);
                        ImVec2 cp1, cp2;

                        if (has_droop) {
                            /* Natural cable sag for instrument/mic cables */
                            float sag = 18.0f + span * 0.12f;
                            cp1 = ImVec2(cable_x0 + span * 0.25f, cable_y0 + sag);
                            cp2 = ImVec2(cable_x1 - span * 0.25f, cable_y1 + sag);
                        } else {
                            /* Straight patch cable between pedals/amps/cabs */
                            cp1 = ImVec2(cable_x0 + span * 0.33f, cable_y0);
                            cp2 = ImVec2(cable_x1 - span * 0.33f, cable_y1);
                        }

                        /* Shadow layer */
                        dl->AddBezierCubic(
                            ImVec2(p0.x + 1, p0.y + 2),
                            ImVec2(cp1.x + 1, cp1.y + 2),
                            ImVec2(cp2.x + 1, cp2.y + 2),
                            ImVec2(p3.x + 1, p3.y + 2),
                            IM_COL32(0, 0, 0, 120), 7.0f, 24);
                        /* Cable body */
                        dl->AddBezierCubic(p0, cp1, cp2, p3,
                            IM_COL32(35, 30, 25, 255), 5.0f, 24);
                        /* Highlight stripe */
                        dl->AddBezierCubic(
                            ImVec2(p0.x, p0.y - 1),
                            ImVec2(cp1.x, cp1.y - 1),
                            ImVec2(cp2.x, cp2.y - 1),
                            ImVec2(p3.x, p3.y - 1),
                            IM_COL32(70, 60, 45, 100), 1.5f, 24);
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

            /* Post-amp [+] buttons — opens combined pedal + studio popup */
            if (chain[i].kind == NODE_PEDAL_POST ||
                chain[i].kind == NODE_STUDIO ||
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
                    ImGui::SetTooltip("Add rack effect");
            }
        }

        /* Pedal gallery popup — pre-amp */
        ImGui::SetNextWindowSizeConstraints(ImVec2(320.0f, 0.0f),
                                            ImVec2(320.0f, FLT_MAX));
        if (ImGui::BeginPopup("add_pedal_gallery_pre")) {
            const float pg_popup_w = 320.0f;
            const float pg_btn_w   = 22.0f;
            ImGui::Text("Add Pre-Amp Pedal");
            ImGui::SameLine();
            ImGui::SetCursorPosX(pg_popup_w - pg_btn_w - ImGui::GetStyle().WindowPadding.x);
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.60f, 0.20f, 0.20f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.45f, 0.12f, 0.12f, 1.0f));
            if (ImGui::Button("X##pedal_pre_close", ImVec2(pg_btn_w, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopStyleColor(3);
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
        ImGui::SetNextWindowSizeConstraints(ImVec2(320.0f, 0.0f),
                                            ImVec2(320.0f, FLT_MAX));
        if (ImGui::BeginPopup("add_pedal_gallery_post")) {
            const float pg_popup_w = 320.0f;
            const float pg_btn_w   = 22.0f;
            ImGui::Text("Add Post-Amp Effect");
            ImGui::SameLine();
            ImGui::SetCursorPosX(pg_popup_w - pg_btn_w - ImGui::GetStyle().WindowPadding.x);
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.60f, 0.20f, 0.20f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.45f, 0.12f, 0.12f, 1.0f));
            if (ImGui::Button("X##pedal_post_close", ImVec2(pg_btn_w, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopStyleColor(3);
            ImGui::Separator();
            /* Rack effects with descriptions */
            if (ImGui::TreeNode("RACK EFFECTS")) {
                static const struct { fx_studio_type_t type; const char *name; const char *desc; } studio_menu[] = {
                    { FX_STUDIO_IRON_SQUEEZE, "Iron Squeeze",  "FET compressor \xe2\x80\x94 punchy, fast attack" },
                    { FX_STUDIO_VELVET_PRESS, "Velvet Press",  "Optical compressor \xe2\x80\x94 smooth, musical" },
                    { FX_STUDIO_GLUE_BUS,     "Glue Bus",      "VCA bus compressor \xe2\x80\x94 glue, punch" },
                    { FX_STUDIO_GLASS_EQ,     "Glass EQ",      "Passive EQ \xe2\x80\x94 musical, sweet top end" },
                    { FX_STUDIO_PRECISION_EQ, "Precision EQ",  "Channel EQ \xe2\x80\x94 warm, proportional-Q" },
                    { FX_STUDIO_REEL_WARMTH,  "Reel Warmth",   "Tape saturation \xe2\x80\x94 warmth, harmonics" },
                    { FX_STUDIO_VALVE_COLOR,  "Valve Color",   "Tube saturation \xe2\x80\x94 rich harmonics" },
                    { FX_STUDIO_BRICK_WALL,   "Brick Wall",    "Brickwall limiter \xe2\x80\x94 output protection" },
                    { FX_STUDIO_ROOM_ENGINE,  "Room Engine",   "Room simulation \xe2\x80\x94 studio ambience" },
                };
                for (int si = 0; si < 9; si++) {
                    if (ImGui::Selectable(studio_menu[si].name)) {
                        if (gui->studio_id_count < 8) {
                            fx_studio_id sid = fx_studio_add(engine, studio_menu[si].type);
                            if (sid >= 0)
                                gui->studio_ids[gui->studio_id_count++] = sid;
                        }
                        ImGui::CloseCurrentPopup();
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", studio_menu[si].desc);
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
        float looper_h = gui->looper_panel_open ? LOOPER_H : 0.0f;
        float chain_top = TOOLBAR_H + looper_h;
        float chain_area_h = (win_h - chain_top - STATUS_H) * 0.35f;
        float detail_h     = win_h - chain_top - chain_area_h - STATUS_H;
        float detail_y     = chain_top + chain_area_h;

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
                    "Solar Monolith", "Eclipse Drone", "Emerald Ratrod Deluxe"
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
                    /* Amp model tooltips/descriptions */
                    if (ImGui::IsItemHovered()) {
                        static const char *amp_descs[] = {
                            "Clean, chimey American tone \xe2\x80\x94 silver panel era",
                            "Classic British crunch \xe2\x80\x94 plexi-era overdrive",
                            "High-gain American lead \xe2\x80\x94 tight, aggressive",
                            "British chime and jangle \xe2\x80\x94 Class A character",
                            "Warm vintage blues \xe2\x80\x94 tweed era breakup",
                            "Brutal modern metal \xe2\x80\x94 scooped, crushing gain",
                            "Thick British roar \xe2\x80\x94 EL34 warmth and fuzz",
                            "Small but fierce \xe2\x80\x94 Class A lunchbox grit",
                            "Classic British rock \xe2\x80\x94 single-channel aggression",
                            "Massive doom \xe2\x80\x94 thunderous clean into crushing fuzz",
                            "Extreme drone \xe2\x80\x94 subsonic doom with feedback sustain",
                            "American hotrod combo \xe2\x80\x94 clean to gritty drive, 6L6 punch",
                        };
                        if (current_amp >= 0 && current_amp < FX_AMP_COUNT)
                            ImGui::SetTooltip("%s", amp_descs[current_amp]);
                    }
                }

                ImGui::Dummy(ImVec2(0.0f, 8.0f));

                /* Amp face image with interactive overlay knobs */
                int param_count = fx_amp_get_param_count(amp_type);
                auto has_param = [&](fx_amp_param_t p) -> bool {
                    if ((int)p < param_count) return true;
                    if (amp_type == FX_AMP_CITRUS_TERROR && p == FX_AMP_PARAM_TONE)
                        return true;
                    if (amp_type == FX_AMP_ECLIPSE_DRONE && p == FX_AMP_PARAM_FEEDBACK)
                        return true;
                    return false;
                };

                {
                    const char *aname = fx_amp_get_type_name(amp_type);
                    uintptr_t face_tex = load_amp_face_texture(aname);

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

                    /* Per-amp knob position maps — normalized (x,y) on the amp face image */
                    struct AmpKnobPos {
                        fx_amp_param_t param;
                        float nx, ny;
                    };

                    const float OVERLAY_KNOB_SZ = 34.0f;
                    const char *knob_tex = "resources/knobs/knob_dome_silver_nobg.png";
                    const int DUMMY = -1;

                    static const AmpKnobPos fullerton_knobs[] = {
                        { FX_AMP_PARAM_VOLUME,   0.299f, 0.476f },
                        { FX_AMP_PARAM_TREBLE,   0.375f, 0.475f },
                        { FX_AMP_PARAM_MID,      0.451f, 0.477f },
                        { FX_AMP_PARAM_BASS,     0.527f, 0.476f },
                        { FX_AMP_PARAM_PRESENCE, 0.603f, 0.477f },
                        { FX_AMP_PARAM_GAIN,     0.678f, 0.476f },
                        { FX_AMP_PARAM_SAG,      0.753f, 0.476f },
                    };
                    static const AmpKnobPos brit_crunch_knobs[] = {
                        { FX_AMP_PARAM_PRESENCE, 0.341f, 0.536f },
                        { FX_AMP_PARAM_BASS,     0.419f, 0.538f },
                        { FX_AMP_PARAM_MID,      0.496f, 0.541f },
                        { FX_AMP_PARAM_TREBLE,   0.574f, 0.539f },
                        { FX_AMP_PARAM_VOLUME,   0.653f, 0.538f },
                        { FX_AMP_PARAM_GAIN,     0.730f, 0.538f },
                        { FX_AMP_PARAM_MASTER,   0.808f, 0.538f },
                    };
                    static const AmpKnobPos southwest_knobs[] = {
                        { FX_AMP_PARAM_GAIN,     0.343f, 0.651f },
                        { FX_AMP_PARAM_BASS,     0.439f, 0.650f },
                        { FX_AMP_PARAM_MID,      0.533f, 0.652f },
                        { FX_AMP_PARAM_TREBLE,   0.630f, 0.652f },
                        { FX_AMP_PARAM_PRESENCE, 0.727f, 0.652f },
                        { FX_AMP_PARAM_MASTER,   0.822f, 0.651f },
                    };
                    static const AmpKnobPos essex_knobs[] = {
                        { FX_AMP_PARAM_VOLUME,   0.288f, 0.378f },
                        { FX_AMP_PARAM_BASS,     0.369f, 0.378f },
                        { FX_AMP_PARAM_TREBLE,   0.450f, 0.379f },
                        { FX_AMP_PARAM_CUT,      0.530f, 0.379f },
                        { FX_AMP_PARAM_PRESENCE, 0.610f, 0.378f },
                        { FX_AMP_PARAM_GAIN,     0.688f, 0.378f },
                        { (fx_amp_param_t)DUMMY,  0.769f, 0.378f },
                    };
                    static const AmpKnobPos tweed_knobs[] = {
                        { FX_AMP_PARAM_VOLUME,   0.344f, 0.575f },
                        { FX_AMP_PARAM_BASS,     0.438f, 0.575f },
                        { FX_AMP_PARAM_TREBLE,   0.532f, 0.573f },
                        { FX_AMP_PARAM_GAIN,     0.625f, 0.573f },
                        { FX_AMP_PARAM_MASTER,   0.718f, 0.574f },
                    };
                    static const AmpKnobPos meridian_knobs[] = {
                        { FX_AMP_PARAM_GAIN,     0.323f, 0.600f },
                        { FX_AMP_PARAM_BASS,     0.411f, 0.601f },
                        { FX_AMP_PARAM_MID,      0.498f, 0.600f },
                        { FX_AMP_PARAM_TREBLE,   0.583f, 0.600f },
                        { FX_AMP_PARAM_PRESENCE, 0.668f, 0.601f },
                        { FX_AMP_PARAM_VOLUME,   0.753f, 0.601f },
                        { FX_AMP_PARAM_MASTER,   0.838f, 0.600f },
                    };
                    static const AmpKnobPos citrus_roar_knobs[] = {
                        { FX_AMP_PARAM_GAIN,     0.348f, 0.546f },
                        { FX_AMP_PARAM_BASS,     0.415f, 0.550f },
                        { FX_AMP_PARAM_MID,      0.484f, 0.559f },
                        { FX_AMP_PARAM_TREBLE,   0.554f, 0.567f },
                        { FX_AMP_PARAM_VOLUME,   0.625f, 0.573f },
                    };
                    static const AmpKnobPos citrus_terror_knobs[] = {
                        { FX_AMP_PARAM_GAIN,     0.299f, 0.528f },
                        { FX_AMP_PARAM_TONE,     0.423f, 0.527f },
                        { FX_AMP_PARAM_VOLUME,   0.541f, 0.527f },
                    };
                    static const AmpKnobPos regent_knobs[] = {
                        { FX_AMP_PARAM_GAIN,     0.404f, 0.601f },
                        { FX_AMP_PARAM_BASS,     0.472f, 0.602f },
                        { FX_AMP_PARAM_MID,      0.539f, 0.601f },
                        { FX_AMP_PARAM_TREBLE,   0.607f, 0.602f },
                        { FX_AMP_PARAM_PRESENCE, 0.675f, 0.602f },
                        { FX_AMP_PARAM_VOLUME,   0.743f, 0.600f },
                        { FX_AMP_PARAM_MASTER,   0.810f, 0.600f },
                    };
                    static const AmpKnobPos solar_knobs[] = {
                        { FX_AMP_PARAM_GAIN,     0.231f, 0.513f },
                        { FX_AMP_PARAM_BASS,     0.328f, 0.514f },
                        { FX_AMP_PARAM_MID,      0.425f, 0.513f },
                        { FX_AMP_PARAM_TREBLE,   0.524f, 0.515f },
                        { FX_AMP_PARAM_VOLUME,   0.619f, 0.513f },
                        { FX_AMP_PARAM_MASTER,   0.718f, 0.514f },
                    };
                    static const AmpKnobPos eclipse_knobs[] = {
                        { FX_AMP_PARAM_GAIN,     0.136f, 0.507f },
                        { FX_AMP_PARAM_BASS,     0.241f, 0.507f },
                        { FX_AMP_PARAM_MID,      0.347f, 0.506f },
                        { FX_AMP_PARAM_TREBLE,   0.454f, 0.507f },
                        { FX_AMP_PARAM_FEEDBACK, 0.557f, 0.505f },
                        { FX_AMP_PARAM_VOLUME,   0.662f, 0.507f },
                    };
                    static const AmpKnobPos emerald_deluxe_knobs[] = {
                        { FX_AMP_PARAM_VOLUME,   0.299f, 0.476f },
                        { FX_AMP_PARAM_TREBLE,   0.375f, 0.475f },
                        { FX_AMP_PARAM_MID,      0.451f, 0.477f },
                        { FX_AMP_PARAM_BASS,     0.527f, 0.476f },
                        { FX_AMP_PARAM_PRESENCE, 0.603f, 0.477f },
                        { FX_AMP_PARAM_GAIN,     0.678f, 0.476f },
                        { FX_AMP_PARAM_SAG,      0.753f, 0.476f },
                    };

                    const AmpKnobPos *knob_map = nullptr;
                    int knob_map_count = 0;
                    switch (amp_type) {
                        case FX_AMP_FULLERTON_CLEAN:    knob_map = fullerton_knobs;     knob_map_count = 7; break;
                        case FX_AMP_BRIT_CRUNCH:        knob_map = brit_crunch_knobs;    knob_map_count = 7; break;
                        case FX_AMP_SOUTHWEST_LEAD:     knob_map = southwest_knobs;     knob_map_count = 6; break;
                        case FX_AMP_ESSEX_CHIME:        knob_map = essex_knobs;         knob_map_count = 7; break;
                        case FX_AMP_TWEED_BLUES:        knob_map = tweed_knobs;         knob_map_count = 5; break;
                        case FX_AMP_MERIDIAN_HIGH_GAIN: knob_map = meridian_knobs;      knob_map_count = 7; break;
                        case FX_AMP_CITRUS_ROAR:        knob_map = citrus_roar_knobs;   knob_map_count = 5; break;
                        case FX_AMP_CITRUS_TERROR:      knob_map = citrus_terror_knobs; knob_map_count = 3; break;
                        case FX_AMP_REGENT_800:         knob_map = regent_knobs;        knob_map_count = 7; break;
                        case FX_AMP_SOLAR_MONOLITH:     knob_map = solar_knobs;         knob_map_count = 6; break;
                        case FX_AMP_ECLIPSE_DRONE:      knob_map = eclipse_knobs;       knob_map_count = 6; break;
                        case FX_AMP_EMERALD_DELUXE:     knob_map = emerald_deluxe_knobs; knob_map_count = 7; break;
                        default: break;
                    }

                    /* Render overlay knobs on the amp face */
                    if (knob_map) {
                        for (int ki = 0; ki < knob_map_count; ki++) {
                            const AmpKnobPos &kp = knob_map[ki];
                            float kx = img_pos.x + kp.nx * img_w - OVERLAY_KNOB_SZ * 0.5f;
                            float ky = img_pos.y + kp.ny * img_h - OVERLAY_KNOB_SZ * 0.5f;

                            if ((int)kp.param == DUMMY || !has_param(kp.param)) {
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
            }
            else if (sel.kind == NODE_CAB) {
                fx_chain_id cab_chain = (fx_chain_id)sel.chain_id;
                int &cab_type_ref = (sel.chain_id == 0) ? gui->cab_type : gui->cab_type_b;
                float avail_w = ImGui::GetContentRegionAvail().x;

                /* Title with cab type name subtitle */
                {
                    const char *title = is_dual ? (sel.chain_id == 0 ? "Cabinet A" : "Cabinet B") : "Cabinet";
                    ImGui::SetWindowFontScale(1.35f);
                    ImVec2 ts = ImGui::CalcTextSize(title);
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_w - ts.x) * 0.5f);
                    ImGui::TextColored(ImVec4(0.92f, 0.68f, 0.22f, 1.0f), "%s", title);
                    ImGui::SetWindowFontScale(1.0f);
                    /* Subtitle: "Cabinet -- 4x12 Straight" style */
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
                char cab_combo_id[32];
                snprintf(cab_combo_id, sizeof(cab_combo_id), "##cab_sel_%d", sel.chain_id);

                /* Is an imported IR currently active on this chain? */
                const char *active_ir = fx_cab_get_custom_ir_path(engine, cab_chain);
                bool is_custom_active = (active_ir && *active_ir);
                const char *preview = is_custom_active
                    ? fx_cab_get_custom_name(engine, cab_chain)
                    : s_cab_type_names[cab_type_ref];
                if (!preview || !*preview) preview = "(unnamed)";

                /* Snapshot the shared library once per frame for this combo. */
                static const int MAX_CC_PER_FRAME = 64;
                fx_custom_cab_t cc_snap[MAX_CC_PER_FRAME];
                int cc_count = fx_custom_cabs_snapshot(cc_snap, MAX_CC_PER_FRAME);

                if (ImGui::BeginCombo(cab_combo_id, preview)) {
                    for (int i = 0; i < FX_CAB_TYPE_COUNT; i++) {
                        bool sel_i = !is_custom_active && (cab_type_ref == i);
                        if (ImGui::Selectable(s_cab_type_names[i], sel_i)) {
                            cab_type_ref = i;
                            fx_cab_params_t params;
                            params.cab_type = (fx_cab_type_t)cab_type_ref;
                            params.mic_pos  = FX_MIC_ON_AXIS;
                            params.speaker_fs = 80.0f;
                            params.brightness = 0.5f;
                            params.resonance  = 0.5f;
                            fx_cab_generate_ir(engine, cab_chain, &params);
                        }
                    }
                    if (cc_count > 0) {
                        ImGui::Separator();
                        for (int i = 0; i < cc_count; i++) {
                            bool sel_i = is_custom_active &&
                                         strcmp(active_ir, cc_snap[i].ir_path) == 0;
                            char label[80];
                            snprintf(label, sizeof(label), "%s##cc%d",
                                     cc_snap[i].name, i);
                            if (ImGui::Selectable(label, sel_i)) {
                                if (fx_cab_load_ir(engine, cab_chain,
                                                   cc_snap[i].ir_path)) {
                                    fx_cab_set_custom_name(engine, cab_chain,
                                                           cc_snap[i].name);
                                    fx_cab_set_custom_image_path(engine, cab_chain,
                                                                 cc_snap[i].image_path);
                                } else {
                                    FX_WARN("Custom IR load failed: %s",
                                            cc_snap[i].ir_path);
                                }
                            }
                        }
                    }
                    ImGui::EndCombo();
                }

                /* Scroll wheel to cycle cab types (stock only for the plugin
                 * to keep wheel UX predictable — custom entries stay pick-only). */
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
                        float imgh = 220.0f;
                        float imgw = imgh;
                        if (fx_texture_get_size(cab_tex, &cw, &ch) && ch > 0) {
                            float aspect = (float)cw / (float)ch;
                            imgw = imgh * aspect;
                        }
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
                if (ImGui::Button(cab_bypassed ? "BYPASSED##cab" : "ON (Active)##cab", ImVec2(120, 28)))
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

                    /* Look up category for subtitle */
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
                        float imgx = (avail_w - img_w) * 0.5f;
                        if (imgx < 0.0f) imgx = 0.0f;
                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + imgx);

                        ImVec2 img_pos = ImGui::GetCursorScreenPos();
                        float cursor_after_img_y = 0;

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

                        /* LED indicator on pedal image */
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
                            for (int li = 0; li < 41; li++) {
                                if (strcmp(s_led_pos[li].name, pedal_fname) == 0) {
                                    float led_cx = img_pos.x + s_led_pos[li].x * img_w;
                                    float led_cy = img_pos.y + s_led_pos[li].y * img_h;
                                    ImDrawList *ldl = ImGui::GetWindowDrawList();
                                    if (!bypassed) {
                                        ldl->AddCircleFilled(ImVec2(led_cx, led_cy), 6.0f,
                                            IM_COL32(40, 220, 40, 200), 12);
                                        ldl->AddCircleFilled(ImVec2(led_cx, led_cy), 10.0f,
                                            IM_COL32(40, 200, 40, 60), 12);
                                        ldl->AddCircleFilled(ImVec2(led_cx, led_cy), 16.0f,
                                            IM_COL32(40, 180, 40, 25), 12);
                                    } else {
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

                        /* Per-pedal knob position maps */
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
                                float dummy = 0.5f;
                                char did[32];
                                snprintf(did, sizeof(did), "##dummy_%d_%d", (int)pid, k);
                                knob_overlay(did, &dummy, 0.0f, 1.0f, 0.5f, 0.0f,
                                             kx, ky, PEDAL_KNOB_SZ, knob_tex);
                            }
                        }

                        /* Restore cursor to below the image */
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

                        /* Reorder arrows */
                        fx_chain_pos_t pos = (sel.kind == NODE_PEDAL_PRE)
                                              ? FX_CHAIN_POS_PRE : FX_CHAIN_POS_POST;
                        fx_pedal_id *ids = (pos == FX_CHAIN_POS_PRE) ? gui->pre_ids : gui->post_ids;
                        int *id_count = (pos == FX_CHAIN_POS_PRE) ? &gui->pre_id_count : &gui->post_id_count;
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
                                gui->selected_node--;
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
                                gui->selected_node++;
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
                                gui->selected_node = -1;
                            }
                            ImGui::PopID();
                        }
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
                        ImGui::Dummy(ImVec2(0.0f, 3.0f));
                    }
                    ImGui::Dummy(ImVec2(0.0f, 2.0f));

                    /* Rack unit image + overlay knobs */
                    {
                        static const char *rack_fnames[] = {
                            "iron_squeeze", "glass_eq", "reel_warmth", "brick_wall",
                            "velvet_press", "glue_bus", "valve_color", "precision_eq", "room_engine"
                        };

                        /* Knob position maps */
                        struct RackKnobMap { int count; float pos[8][2]; };
                        static const RackKnobMap rack_knob_maps[] = {
                            /* iron_squeeze: 6 (5 params + 1 dummy) */
                            { 6, { {0.150f,0.500f},{0.290f,0.500f},{0.430f,0.500f},{0.570f,0.500f},{0.710f,0.500f},{0.850f,0.500f} } },
                            /* glass_eq: 7 (6 params + 1 dummy) */
                            { 7, { {0.150f,0.500f},{0.267f,0.500f},{0.383f,0.500f},{0.500f,0.500f},{0.617f,0.500f},{0.733f,0.500f},{0.850f,0.500f} } },
                            /* reel_warmth: 5 (4 params + 1 dummy) */
                            { 5, { {0.150f,0.500f},{0.325f,0.500f},{0.500f,0.500f},{0.675f,0.500f},{0.850f,0.500f} } },
                            /* brick_wall: 3 (3 params) */
                            { 3, { {0.150f,0.500f},{0.500f,0.500f},{0.850f,0.500f} } },
                            /* velvet_press: 3 (3 params) */
                            { 3, { {0.150f,0.500f},{0.500f,0.500f},{0.850f,0.500f} } },
                            /* glue_bus: 5 (5 params) */
                            { 5, { {0.150f,0.500f},{0.325f,0.500f},{0.500f,0.500f},{0.675f,0.500f},{0.850f,0.500f} } },
                            /* valve_color: 4 (4 params) */
                            { 4, { {0.150f,0.500f},{0.383f,0.500f},{0.617f,0.500f},{0.850f,0.500f} } },
                            /* precision_eq: 6 (5 params + 1 dummy) */
                            { 6, { {0.150f,0.500f},{0.290f,0.500f},{0.430f,0.500f},{0.570f,0.500f},{0.710f,0.500f},{0.850f,0.500f} } },
                            /* room_engine: 5 (4 params + 1 dummy) */
                            { 5, { {0.150f,0.500f},{0.325f,0.500f},{0.500f,0.500f},{0.675f,0.500f},{0.850f,0.500f} } },
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

                    /* Bypass + Remove */
                    {
                        const float BTN_H = 28.0f;
                        float row_w = 120 + 16 + 80;
                        float row_off = (avail_w - row_w) * 0.5f;
                        if (row_off > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + row_off);

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
                            for (int j = pi; j < gui->studio_id_count - 1; j++)
                                gui->studio_ids[j] = gui->studio_ids[j + 1];
                            gui->studio_id_count--;
                            gui->selected_node = -1;
                        }
                        ImGui::PopStyleColor(4);
                        ImGui::PopStyleVar();
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

            ImGui::SameLine(0, 15);

            /* Master volume slider */
            {
                float mv = fx_engine_get_master_volume(engine);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.58f, 0.45f, 1.0f));
                ImGui::SetWindowFontScale(0.7f);
                ImGui::Text("MASTER");
                ImGui::SetWindowFontScale(1.0f);
                ImGui::PopStyleColor();
                ImGui::SameLine(0, 4);
                ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.80f, 0.58f, 0.18f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.95f, 0.70f, 0.20f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.10f, 0.09f, 0.07f, 1.0f));
                ImGui::SetNextItemWidth(60.0f);
                if (ImGui::SliderFloat("##master_vol", &mv, 0.0f, 1.0f, "")) {
                    fx_engine_set_master_volume(engine, mv);
                }
                ImGui::PopStyleColor(3);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Master Volume: %.0f%%", mv * 100.0f);
            }

            ImGui::SameLine(0, 15);

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
