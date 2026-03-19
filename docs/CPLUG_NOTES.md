# CPLUG Integration Notes

CPLUG is vendored at `deps/cplug/` (source: https://github.com/Tremus/CPLUG).
It is a thin C99 wrapper over VST3, CLAP, and Audio Unit v2 plugin formats with no
external dependencies beyond the bundled CLAP and VST3 C-API headers.

---

## How CPLUG Works

CPLUG uses a **callback / forward-declaration model**: `deps/cplug/src/cplug.h`
declares a fixed set of functions that *you* implement in your own source file.
CPLUG's format-specific source files (`cplug_clap.c`, `cplug_vst3.c`, etc.) call
those functions and handle all of the host-facing boilerplate.

### Lifecycle callbacks you implement

| Function | When called |
|---|---|
| `cplug_libraryLoad()` | Shared-library `DllMain` / `__attribute__((constructor))` |
| `cplug_libraryUnload()` | Shared-library unload |
| `cplug_createPlugin(CplugHostContext*)` | Host instantiates the plugin; return your state pointer |
| `cplug_destroyPlugin(void*)` | Host destroys the instance |
| `cplug_setSampleRateAndBlockSize(void*, double, uint32_t)` | Before first process call |
| `cplug_process(void*, CplugProcessContext*)` | Audio thread process block |
| `cplug_saveState(void*, const void*, cplug_writeProc)` | Host requests preset save |
| `cplug_loadState(void*, const void*, cplug_readProc)` | Host restores preset |

### Parameter callbacks

`cplug_getNumParameters`, `cplug_getParameterID`, `cplug_getParameterFlags`,
`cplug_getParameterRange`, `cplug_getParameterName`, `cplug_getParameterValue`,
`cplug_getDefaultParameterValue`, `cplug_setParameterValue`,
`cplug_normaliseParameterValue`, `cplug_denormaliseParameterValue`,
`cplug_parameterStringToValue`, `cplug_parameterValueToString`

### GUI callbacks (only if `CPLUG_WANT_GUI 1`)

`cplug_createGUI`, `cplug_destroyGUI`, `cplug_setParent`, `cplug_setVisible`,
`cplug_setScaleFactor`, `cplug_getSize`, `cplug_checkSize`, `cplug_setSize`

### Audio processing model

`cplug_process` receives a `CplugProcessContext*` with function pointers:

- `ctx->dequeueEvent(ctx, &event, frame)` — pop the next scheduled event
  (parameter change or MIDI) at or after `frame`; returns false when the queue
  is empty for this block.
- `ctx->getAudioOutput(ctx, busIdx)` — returns `float**` (array of channel
  pointers) for the output bus.
- `ctx->getAudioInput(ctx, busIdx)` — same for input.

The canonical inner loop is a **sample-accurate event loop**:
```c
CplugEvent event;
uint32_t frame = 0;
while (ctx->dequeueEvent(ctx, &event, frame)) {
    switch (event.type) {
    case CPLUG_EVENT_PROCESS_AUDIO:
        /* render from frame to event.processAudio.endFrame */
        frame = event.processAudio.endFrame;
        break;
    case CPLUG_EVENT_PARAM_CHANGE_UPDATE:
        cplug_setParameterValue(plugin, event.parameter.id, event.parameter.value);
        break;
    /* MIDI, note expression, ... */
    }
}
```

### Thread-safe GUI <-> audio communication

CPLUG provides lock-free atomic helpers (`cplug_atomic_i32`, `cplug_atomic_*`)
but leaves queue implementation to the user.  The example uses two single-producer
/ single-consumer ring buffers of `CplugEvent` (one main→audio, one audio→main),
sized to `CPLUG_EVENT_QUEUE_SIZE` (default 256).

---

## Build Setup: CLAP Target

To build a CLAP plugin on Linux/Windows:

1. Compile your plugin source together with `deps/cplug/src/cplug_clap.c` into a
   shared library.
2. Force-include your `config.h` (defines `CPLUG_COMPANY_NAME`, `CPLUG_PLUGIN_NAME`,
   `CPLUG_CLAP_ID`, `CPLUG_CLAP_FEATURES`, etc.) using `-include` / `/FI`.
3. Add `deps/cplug/src` to include paths (for `cplug.h` and the bundled
   `clap/clap.h`).
4. Define `CPLUG_SHARED` so `CPLUG_API` expands to the correct dllexport /
   visibility attribute.
5. On Linux the output is a `.so` renamed to `.clap`; on Windows a `.dll` renamed
   to `.clap`.

Minimal CMake sketch (Linux):
```cmake
add_library(0xfx_clap MODULE
    src/plugin/plugin_clap.c          # our implementation of all cplug_* callbacks
    deps/cplug/src/cplug_clap.c       # CPLUG CLAP wrapper
)
target_include_directories(0xfx_clap PRIVATE deps/cplug/src src/engine)
target_compile_definitions(0xfx_clap PRIVATE CPLUG_SHARED)
target_compile_options(0xfx_clap PRIVATE -include ${CMAKE_SOURCE_DIR}/src/plugin/plugin_config.h)
set_target_properties(0xfx_clap PROPERTIES
    PREFIX ""
    SUFFIX ".clap"
)
target_link_libraries(0xfx_clap PRIVATE 0xfx_engine)
```

---

## Build Setup: VST3 Target

VST3 is identical in structure; swap `cplug_clap.c` for `cplug_vst3.c` and add
the two TUID macros to `config.h`:

```c
#define CPLUG_VST3_TUID_COMPONENT  '0xFX', 'comp', '0001', 0
#define CPLUG_VST3_TUID_CONTROLLER '0xFX', 'edit', '0001', 0
#define CPLUG_VST3_CATEGORIES      "Fx|Guitar"
```

The output `.so` / `.dll` must be placed inside the standard VST3 bundle directory
structure:
```
0xFX.vst3/
  Contents/
    x86_64-linux/   (or x86_64-win/ on Windows)
      0xFX.so
```

CPLUG's example CMakeLists.txt shows the post-build commands needed to build this
tree and copy it to the system plugin directory.

**Licensing note**: `cplug_vst3.c` and `vst3_c_api.h` are covered by the
Steinberg VST3 SDK License (GPL v3 / Steinberg dual-license). The rest of CPLUG is
public domain / MIT.

---

## Key Files to Implement for 0xFX

| File | Purpose |
|---|---|
| `src/plugin/plugin_config.h` | `CPLUG_*` macros: name, IDs, format features |
| `src/plugin/plugin_clap.c` | All `cplug_*` callback implementations for CLAP |
| `src/plugin/plugin_vst3.c` | Same callbacks, compiled against `cplug_vst3.c` (or share one impl file) |

The plugin implementation file needs to:

1. **Create/destroy**: allocate an `fx_engine_t` via `fx_engine_create()` in
   `cplug_createPlugin`, destroy it in `cplug_destroyPlugin`.
2. **Configure**: call `fx_engine_set_sample_rate()` / block-size equivalent from
   `cplug_setSampleRateAndBlockSize`.
3. **Process**: in `cplug_process`, iterate the event queue, then call
   `fx_engine_process(engine, input[0], output[0], output[1], numFrames)` for the
   audio segment.  0xFX is mono-in / stereo-out: 1 input bus (1 ch), 1 output bus
   (2 ch).
4. **Parameters**: expose 0xFX engine parameters (amp model, gain, EQ bands, pedal
   params) as CPLUG parameters with stable 4-byte IDs.
5. **State**: `cplug_saveState` / `cplug_loadState` can delegate to
   `fx_preset_save` / `fx_preset_load` or serialize raw parameter values.
6. **No GUI** (initially): set `CPLUG_WANT_GUI 0` in config; the host will use its
   own generic parameter UI.  GUI integration via our existing ImGui layer is
   TASK-070+.

### Bus layout for 0xFX

```c
uint32_t cplug_getNumInputBusses(void* p)            { return 1; }
uint32_t cplug_getNumOutputBusses(void* p)           { return 1; }
uint32_t cplug_getInputBusChannelCount(void* p, uint32_t i)  { return 1; } // mono guitar in
uint32_t cplug_getOutputBusChannelCount(void* p, uint32_t i) { return 2; } // stereo out
```

---

## Source Files in deps/cplug/src/

| File | Description |
|---|---|
| `cplug.h` | The full public API — all forward declarations |
| `cplug_clap.c` | CLAP host entry point and dispatch (~900 LOC) |
| `cplug_vst3.c` | VST3 host entry point and dispatch (~2700 LOC) |
| `cplug_auv2.c` | Audio Unit v2 (macOS only, ~1700 LOC) |
| `cplug_standalone_win.c` | Windows standalone host with hotreload (~1900 LOC) |
| `cplug_standalone_osx.m` | macOS standalone host (~1500 LOC, ObjC) |
| `clap/clap.h` | Bundled CLAP API (MIT) |
| `vst3_c_api.h` | Bundled VST3 C API (Steinberg license) |
| `cplug_extensions/window.h` | Optional cross-platform window helper |
