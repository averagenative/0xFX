# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

0xFX — Guitar amp simulator & effects pedalboard. Standalone app + CLAP/VST3 plugins. Sibling project to [0x808](https://github.com/averagenative/0x808) (drum machine) and 0xSYNTH, sharing build tooling but with a distinct API-driven engine architecture.

**Status**: Pre-alpha — architecture designed, proposal formalized in `openspec/changes/fx-engine/proposal.md`.

## Build Commands

```bash
# Linux
sudo apt install libsdl2-dev libgl-dev g++
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Windows (cross-compile from Linux via MinGW)
cmake -B build_win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake
cmake --build build_win -j$(nproc)
```

## Architecture

**Critical design rule: The engine exposes a public C API. Frontends and plugins call the API — never touch internal structs directly.** This is a deliberate departure from 0x808's direct-struct-access pattern.

4-layer design — no upward dependencies:

- **Layer 1 — Engine** (`src/engine/`): Pure C99. Public API header (`fx_engine.h`) with opaque handles. Private internals for effects DSP, amp models, IR convolution + synthesis, tuner. Zero deps, real-time safe.
- **Layer 2 — Audio Device Manager** (`src/audio/`): miniaudio-based. USB class-compliant (iRig, Scarlett), Bluetooth (with latency warning). Standalone only — plugins get I/O from host.
- **Layer 3 — GUI** (`src/gui/`): C / Dear ImGui / SDL2 / OpenGL. Single frontend. Skeuomorphic pedalboard with drag-and-drop reorder. Turnable knobs on amps and pedals. Calls engine API only.
- **Layer 4 — Host Wrappers** (`src/standalone/`, `src/plugin/`): SDL2 standalone host, CLAP (first-class) and VST3 (secondary) via CPLUG.

Out-of-process AI agent layer for tone matching (optional, BYOK — user's own LLM API key, later phase).

### Engine API Pattern

```c
// Opaque handle — callers never see internal structs
fx_engine_t *fx_engine_create(float sample_rate);
void         fx_engine_destroy(fx_engine_t *engine);
void         fx_engine_process(fx_engine_t *engine, const float *in, float *out, int frames);

// Pedals — drag-and-drop reorder in GUI, turnable knobs
fx_pedal_id  fx_chain_add_pedal(fx_engine_t *engine, fx_pedal_type_t type, fx_chain_pos_t pos);
void         fx_chain_move_pedal(fx_engine_t *engine, fx_pedal_id id, fx_chain_pos_t pos, int index);
void         fx_pedal_set_param(fx_engine_t *engine, fx_pedal_id id, int param, float value);

// Multi-amp routing — parallel chains with mix/blend
fx_chain_id  fx_chain_create(fx_engine_t *engine);
void         fx_chain_set_mix(fx_engine_t *engine, fx_chain_id id, float level);
void         fx_amp_set_model(fx_engine_t *engine, fx_chain_id chain, fx_amp_type_t type);
void         fx_amp_set_param(fx_engine_t *engine, fx_chain_id chain, fx_amp_param_t param, float value);

// Cabinet IR — load .wav or generate synthetic
bool         fx_cab_load_ir(fx_engine_t *engine, fx_chain_id chain, const char *wav_path);
bool         fx_cab_generate_ir(fx_engine_t *engine, fx_chain_id chain, const fx_cab_params_t *params);

// Open rig format (.0xfx JSON)
bool         fx_preset_save(fx_engine_t *engine, const char *path);
bool         fx_preset_load(fx_engine_t *engine, const char *path);
bool         fx_preset_import_nam(fx_engine_t *engine, const char *nam_path);
bool         fx_pedal_load_custom(fx_engine_t *engine, const char *json_path);
```

### Signal Flow

```
Input → [Gate] → [Pre Pedals] → SPLIT → [Amp A] → [Cab A] → [Post FX A] ─┐
                  (drag reorder)      → [Amp B] → [Cab B] → [Post FX B] ─┤→ [Mix] → Output
```

Supports single chain (default) or multi-amp split routing with per-chain blend.

### Plugin Architecture — CLAP First

CLAP is the first-class plugin format (open, MIT licensed, better parameter model). VST3 is secondary for DAW compatibility. Both built via CPLUG, but CLAP gets new features first.

### Open Rig Format (.0xfx)

JSON-based open format for presets, amp profiles, cab IR metadata, and user-designed pedals. Human-readable, shareable, version-controllable. Importers for NAM (.nam) amp profiles.

### Shared Code from 0x808/0xSYNTH

14 effects from 0x808 + 4 additional filters from 0xSYNTH, ported and wrapped in new API. CPLUG plugin framework, ImGui GUI components, CMake build system. When porting, adapt `sq_`/`oxs_` prefix to `fx_` prefix.

## Rules

### API Boundary
- **CRITICAL**: GUI and plugin layers MUST interact with the engine exclusively through the public API (`fx_engine.h`). No `#include` of private engine headers from Layer 3 or 4.
- Internal structs live in private headers only.
- The AI tone matcher also uses the public API — no special internal access.

### Audio Path
- `fx_engine_process()` must be real-time safe: no malloc, no printf, no file I/O, no locks.
- No silent fallbacks in audio code — explicit failure over silent corruption.

### Code Style
- **Everything is C** — engine and GUI. C99 for engine, C for GUI with ImGui's C API or thin C++ wrappers where ImGui requires it.
- `fx_` prefix for all public types/functions.
- No unnecessary abstractions. Keep it direct.

### Cross-Platform
- Maintain Linux + Windows (MinGW cross-compile) builds
- WSLg audio is broken — use offline render tests or Windows exe for real audio testing
- Use `#ifdef _WIN32` guards for platform-specific code

### Single Frontend
- One GUI frontend (ImGui). No GTK. No frontend parity burden.
- Skeuomorphic design — turnable knobs, photorealistic pedal/amp/cab graphics.
- "Worn grime" visual aesthetic — beat-up, scuffed, road-worn gear. Dark theme with warm accents.

### Naming Convention — No Trademarks
- **Never use trademarked names** in code, API enums, UI labels, or preset files (no "Tube Screamer", "Marshall", "Fender", etc.)
- Use **original creative names**: `jade_drive` (not tubescreamer), `brit_crunch` (not marshall), `fullerton_clean` (not fender_twin)
- Real names acceptable in **docs/comments only** with "inspired by" framing
- Follow the Line 6 Helix / LePou naming pattern (geographic hints, wordplay, original names)
- README includes trademark disclaimer

### Open Format
- .0xfx JSON for all presets and profiles. Human-readable, community-shareable.
- User-designed custom pedals via JSON definitions.
- Import NAM (.nam) amp profiles, standard .wav cabinet IRs.

## Task Coordination

File-based task tracking in `tasks/`. Tasks are distributed by domain, not by openspec phase. This is the **authoritative** task tracker — `openspec/changes/fx-engine/tasks.md` is the original roadmap/proposal, `tasks/*.md` is the working state.

```
tasks/
├── engine.md      — Engine API, DSP, amp models, effects, cab IR (Layers 1-2)
├── frontend.md    — GUI, ImGui, knobs, pedalboard, visual assets (Layer 3)
├── infra.md       — Build system, CI, packaging, plugin wrappers (Layer 4 + cross-cutting)
├── testing.md     — Test infrastructure, test cases, quality gates
├── distribute.sh  — Status report script
└── README.md      — Workflow docs
```

### Task Format

```markdown
### TASK-NNN: Short description
- **Status**: queued | in_progress | blocked | done
- **Phase**: 1-11
- **Priority**: P0 | P1 | P2 | P3
- **Depends**: TASK-NNN
- **Notes**: Details
```

### Status Check

```bash
./tasks/distribute.sh              # Full report
./tasks/distribute.sh queued       # What's available
./tasks/distribute.sh in_progress  # What's active
./tasks/distribute.sh done         # What's finished
```

## Autonomous Worker Pattern

When working through the roadmap autonomously, follow this cycle:

1. **Check status first**: Run `./tasks/distribute.sh in_progress` before picking a task. If another agent is working on something, don't duplicate that work. Read the task files to see what's `in_progress` vs `queued`.
2. **Pick** next queued task (lowest phase, highest priority, dependencies met)
3. **Set** status to `in_progress` in the task file — this is a lock. Other agents must see it.
4. **Implement** the code
5. **Write tests** (tests are non-negotiable — a task without tests is not done)
6. **Run tests** — all must pass before proceeding
7. **Set** status to `done`
8. **Move** to next task

### Parallel Agent Coordination

Multiple agents may work simultaneously on different tasks. To avoid conflicts:

- **Always read task files before starting** — check for `in_progress` tasks to avoid collisions.
- **Claim by writing** — set status to `in_progress` before writing code. If two agents both try to claim the same task, the file system is the arbiter.
- **Domain separation** — prefer picking tasks from different files (engine.md vs frontend.md vs infra.md) when running in parallel.
- **Build before commit** — always `cmake --build build` and run tests after your changes. If another agent's changes broke the build, investigate before overwriting.
- **Don't modify files another agent owns** — if TASK-018 (engine.md) is `in_progress`, don't edit `src/engine/internal/pedals.c` unless your task also needs it. Coordinate via the task file.

### Quality Gates (Per-Commit)

Every commit must pass all gates. No exceptions.

| Gate | Command | Checks |
|------|---------|--------|
| Unit tests | `cmake --build build --target test` | All test_*.c pass |
| Build (Linux) | `cmake --build build` | No errors or warnings |
| Build (MinGW) | `cmake --build build_win` | Cross-compile succeeds |
| ASAN | `cmake -DENABLE_ASAN=ON && build && test` | No memory errors |

### Testing Rules

- **No task is `done` without passing tests.** If tests don't exist for a component, write them first.
- Use offline WAV render tests for DSP (WSLg audio is broken).
- Test through the API, not internal structs — tests enforce the same boundary as the GUI.
- Preset round-trip tests: save → load → process → compare output.
- Effect DSP tests: sine sweep → FFT → verify spectral characteristics.
