/*
 * 0xFX — CPLUG plugin configuration header
 *
 * This file is force-included into all CPLUG plugin translation units via
 * the compiler -include flag.  All CPLUG_* macros that configure the
 * format wrappers must be defined here.
 */
#ifndef PLUGIN_CONFIG_H
#define PLUGIN_CONFIG_H

/* ── Plugin identity ───────────────────────────────────────────── */
#define CPLUG_COMPANY_NAME   "averagenative"
#define CPLUG_COMPANY_EMAIL  ""
#define CPLUG_PLUGIN_NAME    "0xFX"
#define CPLUG_PLUGIN_URI     "com.averagenative.0xfx"
#define CPLUG_PLUGIN_VERSION "0.1.0"

/* ── Plugin type ───────────────────────────────────────────────── */
/* 0xFX is an audio effect (guitar amp simulator), not an instrument */
#define CPLUG_IS_INSTRUMENT 0

/*
 * CPLUG_WANT_GUI must be 1 even though we stub out the GUI callbacks.
 * The CLAP wrapper unconditionally references the host_gui field in
 * CLAPPlugin_init() regardless of this guard, so the struct member must
 * exist for compilation to succeed.  Setting this to 1 and returning
 * NULL / false from all cplug_create/destroy/setParent etc. callbacks
 * is the correct no-op-GUI approach until TASK-070+.
 */
#define CPLUG_WANT_GUI         1
#define CPLUG_GUI_RESIZABLE    0

/* MIDI CC input for parameter control via hardware controllers */
#define CPLUG_WANT_MIDI_INPUT  1
#define CPLUG_WANT_MIDI_OUTPUT 0

/* ── VST3 identifiers ──────────────────────────────────────────── */
/* Categories: https://steinbergmedia.github.io/vst3_doc/vstinterfaces/namespaceSteinberg_1_1Vst_1_1PlugType.html */
#define CPLUG_VST3_CATEGORIES "Fx|Guitar"

/* TUIDs must be unique 4-byte-tuple literals — keep stable across releases */
#define CPLUG_VST3_TUID_COMPONENT  '0xFX', 'comp', '0001', 0
#define CPLUG_VST3_TUID_CONTROLLER '0xFX', 'edit', '0001', 0

/* ── CLAP identifiers ──────────────────────────────────────────── */
#define CPLUG_CLAP_ID          "com.averagenative.0xfx"
#define CPLUG_CLAP_DESCRIPTION "Guitar amp simulator — preamp, tone stack, power amp and cabinet IR"
/* CLAP_PLUGIN_FEATURE_* values from <clap/plugin-features.h> */
#define CPLUG_CLAP_FEATURES    CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_DISTORTION

#endif /* PLUGIN_CONFIG_H */
