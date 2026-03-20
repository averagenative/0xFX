# Frontend Tasks (Layer 3)

> GUI, ImGui, SDL2/OpenGL, knob widgets, pedalboard, amp panels, visual assets.
> All code under `src/gui/`.

---

## Phase 3B: GUI — Pedalboard + Amp Panel + Knobs [MVP]

### TASK-025: Vendor ImGui + SDL2+OpenGL backend
- **Status**: done
- **Phase**: 3
- **Priority**: P0
- **Depends**: TASK-003 (infra - CMakeLists)
- **Notes**: ImGui vendored in deps/imgui/. SDL2+OpenGL3 backends. CMake imgui target.

### TASK-026: GUI main window + render loop
- **Status**: done
- **Phase**: 3
- **Priority**: P0
- **Depends**: TASK-025
- **Notes**: gui_main.cpp — SDL2 window, OpenGL 3.3, ImGui render loop, "worn grime" dark theme.

### TASK-027: Rotary knob widget
- **Status**: done
- **Phase**: 3
- **Priority**: P0
- **Depends**: TASK-026
- **Notes**: knobs.cpp/h — arc rendering, vertical drag, shift+fine, double-click reset. knob_float, knob_mini, knob_inline, knob_core_ext.

### TASK-028: Amp panel (skeuomorphic)
- **Status**: queued
- **Phase**: 3
- **Priority**: P0
- **Depends**: TASK-027, TASK-015 (engine - param introspection)
- **Notes**: Turnable knobs per amp type. Calls fx_amp_set_param(). Layout varies per model.

### TASK-029: Pedalboard view
- **Status**: queued
- **Phase**: 3
- **Priority**: P0
- **Depends**: TASK-027, TASK-017 (engine - pedal interface)
- **Notes**: Horizontal pedal slots (pre/post amp). Stomp box rendering with knobs + bypass LED. "+" to add.

### TASK-030: Drag-and-drop pedal reordering
- **Status**: queued
- **Phase**: 3
- **Priority**: P0
- **Depends**: TASK-029
- **Notes**: Click-hold, drag, ghost insertion point. Calls fx_chain_move_pedal(). No audio glitch.

### TASK-031: Audio device selector
- **Status**: queued
- **Phase**: 3
- **Priority**: P0
- **Depends**: TASK-026, TASK-006 (engine - device manager)
- **Notes**: Settings dropdown with detected interfaces. Buffer size + sample rate selectors.

### TASK-032: Tuner display
- **Status**: queued
- **Phase**: 3
- **Priority**: P0
- **Depends**: TASK-026
- **Notes**: fx_tuner_get_frequency(), note name + cents bar. Always-on, ~30Hz update.

---

## Phase 4C: Preset UI

### TASK-045: Preset browser
- **Status**: queued
- **Phase**: 4
- **Priority**: P1
- **Depends**: TASK-042 (infra - preset save), TASK-043 (infra - preset load)
- **Notes**: Preset list panel, click to load, right-click save-as. Name + tags.

---

## Phase 4B: Multi-Chain GUI

### TASK-041: Multi-chain routing view
- **Status**: queued
- **Phase**: 4
- **Priority**: P1
- **Depends**: TASK-039 (engine - parallel chains)
- **Notes**: Visual splitter node. Per-chain amp+cab column. Mix slider. Add/Remove chain buttons.

---

## Phase 4A: Cab IR Panel

### TASK-038: Cabinet IR selector GUI
- **Status**: queued
- **Phase**: 4
- **Priority**: P1
- **Depends**: TASK-035 (engine - cab load API)
- **Notes**: Cab dropdown (bundled + user), "Load IR..." file browser, IR waveform display, bypass.

---

## Phase 7: Visual Assets + Theme

### TASK-081: Themed GUI (worn grime aesthetic)
- **Status**: queued
- **Phase**: 7
- **Priority**: P2
- **Depends**: TASK-080 (infra - theme textures)
- **Notes**: Dark color scheme, warm accents (amber LEDs, rust). Apply generated textures as backgrounds.

### TASK-082: Integrate pedal/amp/cab graphics
- **Status**: queued
- **Phase**: 7
- **Priority**: P2
- **Depends**: TASK-077, TASK-078, TASK-079 (infra - generated assets)
- **Notes**: Render actual pedal images, not colored rectangles. Amp face graphics. Cab images.

---

## Phase 7: Pedal Skin System (Composite Rendering)

### TASK-209: OpenGL texture loader for asset PNGs
- **Status**: queued
- **Phase**: 7
- **Priority**: P2
- **Depends**: TASK-026
- **Notes**: Load PNGs as GL textures. Cache by path. Support RGBA transparency. Draw textured quad with rotation + alpha. See docs/PEDAL_SKIN_SYSTEM.md.

### TASK-202: Pedal layout descriptor system
- **Status**: queued
- **Phase**: 7
- **Priority**: P2
- **Depends**: TASK-200, TASK-027
- **Notes**: fx_pedal_layout_t struct: body_image, knob slots (x,y,size,param_index,style), LED position (on/off images), stomp position. Static table for all pedal types. Normalized coords (0-1).

### TASK-203: Texture-mapped knob rendering (rotatable sprite)
- **Status**: queued
- **Phase**: 7
- **Priority**: P2
- **Depends**: TASK-027, TASK-202
- **Notes**: Load knob PNG as texture, render rotated to match param value (0-1 → 135° to -135°). Mouse drag to adjust via fx_pedal_set_param(). Shift+drag fine control. Multiple knob styles.

### TASK-204: LED on/off rendering tied to bypass state
- **Status**: queued
- **Phase**: 7
- **Priority**: P2
- **Depends**: TASK-202
- **Notes**: LED sprite at layout position. Active (bypass=false) → on image with glow. Bypassed → off image. Optional additive blend for bloom.

### TASK-205: Stomp footswitch toggle (click to bypass/activate)
- **Status**: queued
- **Phase**: 7
- **Priority**: P2
- **Depends**: TASK-202, TASK-204
- **Notes**: Click footswitch area → fx_pedal_set_bypass(). Visual: depress on click, LED updates. Bypassed pedal body dims (alpha ~0.5). Active = full brightness.

### TASK-206: Composite pedal renderer (body + knobs + LED + stomp)
- **Status**: queued
- **Phase**: 7
- **Priority**: P2
- **Depends**: TASK-202, TASK-203, TASK-204, TASK-205
- **Notes**: Single function renders complete pedal. Knob rotation reflects live params. LED reflects bypass. Dimmed when bypassed but still in chain. Works for all types via layout table.

### TASK-207: Amp panel layout descriptors + composite renderer
- **Status**: queued
- **Phase**: 7
- **Priority**: P2
- **Depends**: TASK-201, TASK-203
- **Notes**: fx_amp_layout_t: panel_image, knob slots, pilot light. 5 amp models mapped. Composite: panel + rotated knobs + pilot light.

### TASK-208: Signal chain cable rendering between components
- **Status**: queued
- **Phase**: 7
- **Priority**: P2
- **Depends**: TASK-206
- **Notes**: Visual cables: input → pedals → amp → cab → output. Textured bezier curves or cable sprites. Redraws on pedal reorder. Optional sag physics.

### TASK-323: Input node — TRS guitar cable plug asset + rendering
- **Status**: queued
- **Phase**: 3
- **Priority**: P1
- **Depends**: TASK-312
- **Notes**: Generate TRS cable plug PNG (angled upward, cable leading right). Replace green INPUT rectangle.

### TASK-324: Cable segment assets between signal chain nodes
- **Status**: queued
- **Phase**: 3
- **Priority**: P2
- **Depends**: TASK-312
- **Notes**: Straight cable segment PNGs between pedals and amps. Patch cable + speaker cable styles.

### TASK-325: Output node — XLR microphone cable end asset + rendering
- **Status**: queued
- **Phase**: 3
- **Priority**: P1
- **Depends**: TASK-312
- **Notes**: Generate XLR cable end PNG (angled upward, cable leading left). Replace green OUTPUT rectangle.

### TASK-331: Overlay knobs on amp/pedal images in detail view
- **Status**: queued
- **Phase**: 3
- **Priority**: P1
- **Depends**: TASK-203, TASK-312
- **Notes**: Knobs rendered on amp face / pedal body images at mapped positions. Interactive — drag to change param.

### TASK-332: Show pedal image in detail view when selected
- **Status**: queued
- **Phase**: 3
- **Priority**: P1
- **Depends**: TASK-312
- **Notes**: Full pedal body image in detail panel when pedal selected. Knobs overlaid once TASK-331 is done.

---

## Phase 6: Plugin GUI Embedding

### TASK-071: GUI embedding for CLAP
- **Status**: queued
- **Phase**: 6
- **Priority**: P2
- **Depends**: TASK-069 (infra - CLAP process), TASK-026
- **Notes**: ImGui+SDL2 in host-provided native window. Must render correctly in plugin context.
