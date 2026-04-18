# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

0xFX — Guitar amp simulator & effects pedalboard. Standalone app + CLAP/VST3 plugins. Sibling project to [0x808](https://github.com/averagenative/0x808) (drum machine) and 0xSYNTH, sharing build tooling but with a distinct API-driven engine architecture.

**Status**: 1.0.0 — Engine, GUI, and plugins functional. 12 circuit-modeled amps, 41 pedals, 9 rack effects, 10 mic models. CLAP/VST3 plugins with thread-safe multi-instance support. Windows + Linux + macOS.

## Build Commands

```bash
# Linux
sudo apt install libsdl2-dev libgl-dev g++
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# macOS
brew install cmake python3
# For universal binaries (arm64 + x86_64), use the official SDL2 framework instead of Homebrew:
#   curl -LO https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10.dmg
#   hdiutil attach SDL2-2.32.10.dmg
#   sudo cp -R /Volumes/SDL2/SDL2.framework /Library/Frameworks/
#   hdiutil detach /Volumes/SDL2
# Homebrew SDL2 (`brew install sdl2`) works but produces x86_64-only builds.
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build -j$(sysctl -n hw.ncpu)

# Windows (cross-compile from Linux via MinGW)
cmake -B build_win -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build_win -j$(nproc)
```

### Asset Pipeline

Source PNGs live in `resources/` (committed to git). The build auto-generates `src/gui/embedded_assets.c` (gitignored, ~95MB) via Python3:

```bash
python3 tools/generate_embedded_assets.py            # regenerate
python3 tools/generate_embedded_assets.py --dry-run  # list without writing
```

CMake runs this automatically when PNGs change. If Python3 isn't available, a pre-existing `embedded_assets.c` is used.

### Windows Test Deploy

After building for Windows, copy binaries to desktop for testing:

```bash
DEST="/mnt/c/Users/Dan Michael/Desktop/0xFX-test"
mkdir -p "$DEST"
cp build_win/0xfx_gui.exe build_win/0xfx_standalone.exe "$DEST/"
cp -r presets "$DEST/"
# Run 0xfx_gui.exe from Windows — double-click or run from PowerShell
```

### Logging

Uses `src/core/log.h` — industry-standard levels with timestamps:
```c
FX_DEBUG("detail");     // [2026-03-19 18:45:23.456] [DEBUG] [file.c:123] detail
FX_INFO("started");     // [INFO ] green
FX_WARN("fallback");    // [WARN ] yellow
FX_ERROR("failed");     // [ERROR] red
FX_FATAL("crash");      // [FATAL] red+bold
```
Initialize with `fx_log_init(NULL)` (stderr) or `fx_log_init("0xfx.log")` (file).

### Crash Handling

`fx_crash_init()` registers signal handlers for SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS. On crash: logs signal via FX_FATAL, prints backtrace (Linux), re-raises for core dump.

### GDB MCP Server

C debugging MCP server available at `~/projects/mcp-gdb-glib/`. Can be used for breakpoint debugging of engine DSP and crash investigation.

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
Input → [Gate] → [Pre Pedals] → AMP → CAB → [Mic Sim] → [Rack FX] → Output
                                  ↕ (parallel chains with mix)
```

- 12 amp models with circuit-accurate tone stacks (Fender TMB, Marshall TMB, Vox Cut, Tilt EQ)
- 41 effect pedals, 9 rack effects, 9 microphone models
- Dual chain support with independent amp/cab/mic per chain
- Recording: WAV/MP3/FLAC with timestamped filenames

### Plugin Architecture — CLAP First

CLAP is the first-class plugin format (open, MIT licensed, better parameter model). VST3 is secondary for DAW compatibility. Both built via CPLUG. 137 automatable parameters.

### Open Rig Format (.0xfx)

JSON-based open format for presets. Human-readable, shareable, version-controllable. Genre-organized factory preset library. Import/export supported.

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

### Plugin GUI Thread Safety
- **CRITICAL**: The plugin GUI renderer MUST be fully thread-safe for multi-instance use. Artists commonly load the same plugin on multiple tracks. A crash during recording is unacceptable.
- **NO mutable static variables** in `gui_render.cpp` — ALL mutable state goes in `fx_gui_state_t`. Each plugin instance gets its own state struct.
- `const static` data (knob maps, pedal categories, lookup tables) is fine — read-only, thread-safe.
- **Texture cache** uses mutex + per-thread owner tracking. Each GL context has its own texture IDs.
- Each plugin instance has its **own ImGui context** — call `ImGui::SetCurrentContext()` in WndProc before processing events.
- **No SDL2 in plugins** — use native Win32+WGL (Windows) or X11+GLX (Linux). SDL2 causes multi-plugin conflicts. Standalone app keeps SDL2.

### Code Style
- **Everything is C** — engine and GUI. C99 for engine, C for GUI with ImGui's C API or thin C++ wrappers where ImGui requires it.
- `fx_` prefix for all public types/functions.
- No unnecessary abstractions. Keep it direct.

### Cross-Platform
- Maintain Linux + Windows (MinGW cross-compile) builds
- WSLg audio is broken — use offline render tests or Windows exe for real audio testing
- Use `#ifdef _WIN32` guards for platform-specific code
- Plugin GUI: native Win32+WGL (Windows), X11+GLX (Linux), NSView+NSOpenGL (macOS future)

### Single Frontend
- One GUI frontend (ImGui). No GTK. No frontend parity burden.
- Skeuomorphic design — turnable knobs, photorealistic pedal/amp/cab graphics.
- "Worn grime" visual aesthetic — beat-up, scuffed, road-worn gear. Dark theme with warm accents.
- **Dropdown labels**: Always `Label | [Dropdown]` (label on the left, selector on the right). English readers read left-to-right — the label identifies what the dropdown controls before you see the value.
- **Generated assets must not include labels or knobs** — knobs are rendered as interactive overlays, labels are added programmatically. Asset images should show the body/faceplate with knob holes (no knobs, no printed text labels).

### Naming Convention — No Trademarks
- **Never use trademarked names** in code, API enums, UI labels, or preset files (no "Tube Screamer", "Marshall", "Fender", etc.)
- Use **original creative names**: `jade_drive` (not tubescreamer), `brit_crunch` (not marshall), `fullerton_clean` (not fender_twin)
- Real names acceptable in **docs/comments only** with "inspired by" framing
- Follow the Line 6 Helix / LePou naming pattern (geographic hints, wordplay, original names)
- **Avoid racially insensitive terms** — use "silver panel" instead of "blackface" when describing Fender amp eras
- README includes trademark disclaimer

### Open Format
- .0xfx JSON for all presets and profiles. Human-readable, community-shareable.
- User-designed custom pedals via JSON definitions.
- Import NAM (.nam) amp profiles, standard .wav cabinet IRs.

## Task Coordination

Task tracking lives in `tasks/`. Two formats: `queue.json` (machine-readable for agents) and `*.md` (human-readable details).

```
tasks/
├── queue.json     — Source of truth: structured task queue with claims + acceptance criteria
├── engine.md      — Engine task details (Layers 1-2)
├── frontend.md    — GUI task details (Layer 3)
├── infra.md       — Build/CI/packaging task details (Layer 4 + cross-cutting)
├── testing.md     — Test task details
├── distribute.sh  — Status report script
└── README.md      — Workflow docs
```

### Agent Task Pickup (queue.json)

```json
{
  "id": "TASK-034",
  "priority": "HIGH",
  "title": "Overlap-add FFT convolution",
  "domain": "engine",
  "status": "queued",
  "claimedBy": null,
  "depends": ["TASK-002"],
  "estimatedTokens": 30000,
  "acceptanceCriteria": ["Load .wav IR via dr_wav", "FFT via KissFFT", "Test: convolve impulse with known IR"],
  "files": ["src/engine/internal/cab_ir.c"]
}
```

**Priority**: CRITICAL → HIGH → MEDIUM → LOW
**Status flow**: `queued → claimed → in_progress → done` (or `→ blocked`)
**Claim**: Set `status: "claimed"` + `claimedBy: "agent-name"` before coding. Prevents duplicate work.

### Status Check

```bash
./tasks/distribute.sh              # Full report
./tasks/distribute.sh queued       # What's available
./tasks/distribute.sh in_progress  # What's active
cat tasks/queue.json | jq '.[] | select(.status=="queued") | {id, priority, title}'
```

## Autonomous Worker Pattern

When working through the roadmap autonomously, follow this cycle:

1. **Sync** — `git pull --rebase` to get latest from remote
2. **Read** `tasks/queue.json` — find highest-priority `queued` task with all dependencies `done`
3. **Claim** — set `status: "claimed"` + `claimedBy: "your-name"` in queue.json. This is a lock.
4. **Start** — set `status: "in_progress"`. Read `acceptanceCriteria` — these define done.
5. **Implement** the code (only touch files listed in `files` field unless necessary)
6. **Write tests** that verify each acceptance criterion. Tests are non-negotiable.
7. **Run tests** — `cmake --build build -j$(nproc) && ./build/fx_api_test`. All must pass.
8. **Done** — set `status: "done"` in queue.json + corresponding .md file
9. **Commit + push** — `git add <files> && git commit && git push`. If push fails, `git pull --rebase`, resolve conflicts, re-test, then push.
10. **Repeat** from step 1

### Parallel Agent Coordination

Multiple agents may work simultaneously. The claim system in queue.json prevents collisions:

- **Always read queue.json before starting** — check for `claimed`/`in_progress` tasks.
- **Claim by writing queue.json** — set `claimedBy` before touching source files. First writer wins.
- **Domain separation** — prefer picking tasks from different domains (engine vs frontend vs infra).
- **Check `files` field** — don't modify files that belong to another agent's claimed task.
- **Build before commit** — if another agent's changes broke the build, investigate before overwriting.
- **Stale claims** — a `claimed` task with no progress after 10 minutes can be reclaimed by another agent.

### Git Discipline

All work happens on `master` unless explicitly working a release branch. Keep the repo in sync:

1. **Pull before starting work**: `git pull --rebase` before picking up a task. If another agent pushed, you need their changes.
2. **Commit after each completed task**: Don't batch multiple tasks into one commit. One task = one commit (or a small focused series).
3. **Push after each commit**: `git push` immediately. Don't sit on local commits — other agents need to see your changes.
4. **Resolve merge conflicts immediately**: If `git push` fails due to divergence:
   ```bash
   git pull --rebase     # rebase your commit on top of remote
   # resolve any conflicts
   cmake --build build -j$(nproc) && ./build/fx_api_test  # verify build+tests still pass
   git push
   ```
5. **Never force push to master**: If rebase produces a mess, ask the user before `--force`.
6. **Commit messages**: Use the format from previous commits — summary line, bullet points for changes, test count, co-author tag.

### Release Builds

When building release packages, always commit and push source changes first, then build and upload:

1. **Commit + push source changes** before running packaging scripts. The release must correspond to a committed state.
2. **Build the release**: run the appropriate packaging script(s).
3. **Upload assets to GitHub release** immediately after building: `./scripts/packaging/upload_release.sh {VERSION}`.
4. **Update release notes** if the release gains new platforms or features: `gh release edit v{VERSION} --notes "..."`.

**Every release MUST include Windows installers (.exe) alongside zip/tarball packages.** Standalone zips are for portable use; installers handle plugin paths, Start Menu shortcuts, and uninstall registry.

Artifact naming convention: `0xFX-<version>-<platform>-<arch>.<ext>`
- `linux-x64`, `linux-arm64`, `windows-x64`, `windows-arm64`, `macos-universal`
- AppImages use `<arch>.AppImage` naming (`x86_64`, `aarch64`) — preserved for backwards compat.

Packaging scripts:
```bash
# Build every arch a toolchain is available for (Linux x64/arm64 + Windows x64/arm64):
./scripts/packaging/package_release.sh 1.1.0

# Or restrict to a single arch:
./scripts/packaging/package_release.sh 1.1.0 --arch x64
./scripts/packaging/package_release.sh 1.1.0 --arch arm64

# macOS universal (arm64 + x86_64) — must run on a Mac:
# Requires SDL2.framework in /Library/Frameworks/ (see Build Commands above)
./scripts/packaging/package_macos.sh 1.1.0

# Upload everything in release/ to the v1.1.0 GitHub release:
./scripts/packaging/upload_release.sh 1.1.0
```

Toolchain prerequisites (Fedora; adjust for Ubuntu):
```bash
# Windows x64:    sudo dnf install mingw64-gcc mingw64-gcc-c++ mingw64-SDL2
# Windows arm64:  download llvm-mingw from https://github.com/mstorsjo/llvm-mingw/releases
#                 extract to ~/tools/ (auto-detected) or export LLVM_MINGW_PREFIX
# Linux arm64:    sudo dnf install gcc-aarch64-linux-gnu gcc-c++-aarch64-linux-gnu
# NSIS installer: sudo dnf install mingw32-nsis   # package name varies by distro
# AppImage:       wget the appimagetool binary to ~/tools/appimagetool and chmod +x
```

Manual ARM64 builds (when not using package_release.sh):
```bash
LLVM_MINGW_PREFIX=~/tools/llvm-mingw-* cmake -B build_win_arm64 \
  -DCMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw-arm64.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build_win_arm64 -j$(nproc)
makensis scripts/packaging/0xfx_installer_arm64.nsi   # ARM64 installer

cmake -B build_linux_arm64 \
  -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux-gnu.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build_linux_arm64 -j$(nproc)
```

NSIS installer scripts:
- `scripts/packaging/0xfx_installer.nsi` — Windows x64
- `scripts/packaging/0xfx_installer_arm64.nsi` — Windows ARM64

### Quality Gates (Per-Commit)

Every commit must pass all gates. No exceptions.

| Gate | Command | Checks |
|------|---------|--------|
| Pull latest | `git pull --rebase` | Up to date with remote |
| Unit tests | `cmake --build build --target test` | All test_*.c pass |
| Build (Linux) | `cmake --build build` | No errors or warnings |
| Push | `git push` | Remote updated |

### Testing Rules

- **No task is `done` without passing tests.** If tests don't exist for a component, write them first.
- Use offline WAV render tests for DSP (WSLg audio is broken).
- Test through the API, not internal structs — tests enforce the same boundary as the GUI.
- Preset round-trip tests: save → load → process → compare output.
- Effect DSP tests: sine sweep → FFT → verify spectral characteristics.
