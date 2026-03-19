# Testing Tasks

> Test infrastructure, test cases, quality gates.
> All test code under `tests/`.
> Tests are non-negotiable — no task is `done` without passing tests.

---

## Test Infrastructure

### TEST-001: Test harness setup
- **Status**: done
- **Phase**: 1
- **Priority**: P0
- **Depends**: TASK-003 (CMakeLists)
- **Notes**: Simple C test runner (no external framework needed). CMake `0xfx_tests` target. Return non-zero on failure. Test discovery via naming convention: `test_*.c`.

### TEST-002: Offline audio render helper
- **Status**: queued
- **Phase**: 1
- **Priority**: P0
- **Depends**: TASK-005 (engine impl)
- **Notes**: Helper that creates engine, feeds WAV buffer through fx_engine_process(), writes output WAV. Used by all DSP tests. Avoids need for real audio device (WSLg is broken).

---

## Quality Gates (Per-Commit)

> Every commit must pass all of these. No exceptions.

| Gate | Command | What it checks |
|------|---------|----------------|
| Unit tests | `cmake --build build --target test` | All test_*.c pass |
| Build (Linux) | `cmake --build build` | No compile errors/warnings |
| Build (MinGW) | `cmake --build build_win` | Cross-compile succeeds |
| AddressSanitizer | `cmake -DENABLE_ASAN=ON` | No memory errors |

---

## Phase 1 Tests

### TEST-003: Engine lifecycle test
- **Status**: done
- **Phase**: 1
- **Priority**: P0
- **Depends**: TASK-005
- **Notes**: Create engine, destroy engine. No leaks (ASAN). Create multiple engines.

### TEST-004: Passthrough test
- **Status**: done
- **Phase**: 1
- **Priority**: P0
- **Depends**: TASK-005
- **Notes**: Feed known buffer through passthrough engine, verify output == input.

### TEST-005: Audio device enumeration test
- **Status**: queued
- **Phase**: 1
- **Priority**: P0
- **Depends**: TASK-006
- **Notes**: Call fx_audio_get_device_count(), verify >= 0. Get names, verify non-null.

---

## Phase 2 Tests

### TEST-006: Amp model selection test
- **Status**: done
- **Phase**: 2
- **Priority**: P0
- **Depends**: TASK-009
- **Notes**: Covered by test_amp_processing in api_test.c — switches models, verifies different gain behavior.

### TEST-007: Amp distortion character test
- **Status**: done
- **Phase**: 2
- **Priority**: P0
- **Depends**: TASK-014
- **Notes**: test_amp_character verifies all 5 models produce unique output. test_amp_processing checks gain levels.

### TEST-008: Noise gate test
- **Status**: done
- **Phase**: 2
- **Priority**: P0
- **Depends**: TASK-013
- **Notes**: test_noise_gate verifies silence is attenuated, loud signal passes through.

---

## Phase 3 Tests

### TEST-009: Pedal add/remove/reorder test
- **Status**: done
- **Phase**: 3
- **Priority**: P0
- **Depends**: TASK-017
- **Notes**: Covered by test_pedals in api_test.c — add/remove/bypass/type check.

### TEST-010: Overdrive adds harmonics
- **Status**: done
- **Phase**: 3
- **Priority**: P0
- **Depends**: TASK-018
- **Notes**: test_overdrive verifies Jade Drive changes signal and produces energy at high drive.

### TEST-011: Delay produces echoes
- **Status**: done
- **Phase**: 3
- **Priority**: P0
- **Depends**: TASK-021
- **Notes**: test_delay sends tone burst, verifies echo energy persists after input stops.

### TEST-012: Signal chain order test
- **Status**: queued
- **Phase**: 3
- **Priority**: P0
- **Depends**: TASK-024
- **Notes**: OD→amp sounds different from amp→OD (pre vs post). Verify output differs.

---

## Phase 4 Tests

### TEST-013: IR convolution test
- **Status**: done
- **Phase**: 4
- **Priority**: P1
- **Depends**: TASK-034
- **Notes**: test_cab_ir + test_cab_load_api — writes temp .wav IR, loads, verifies convolution output differs from no-cab. Bypass test included.

### TEST-014: Preset round-trip test
- **Status**: done
- **Phase**: 4
- **Priority**: P1
- **Depends**: TASK-042, TASK-043
- **Notes**: test_preset_roundtrip — save rig with Jade Drive + Brit Crunch + delay, reload, verify identical audio output.

### TEST-015: Preset fuzzing
- **Status**: done
- **Phase**: 4
- **Priority**: P1
- **Depends**: TASK-043
- **Notes**: test_preset_fuzz — nonexistent file, empty, invalid JSON, wrong format, missing signal_chain, out-of-range values, unknown pedal types, NULL args. All handled without crash.

---

## Phase 11 Tests (Full Suite)

### TASK-103: Complete API test suite
- **Status**: queued
- **Phase**: 11
- **Priority**: P3
- **Depends**: all engine tasks
- **Notes**: Every public API function covered. Headless.

### TASK-104: Effect DSP sweep tests
- **Status**: queued
- **Phase**: 11
- **Priority**: P3
- **Depends**: all effect tasks
- **Notes**: Sine sweeps through each effect, verify spectral characteristics.

### TASK-106: AddressSanitizer CI build
- **Status**: queued
- **Phase**: 11
- **Priority**: P3
- **Depends**: TASK-111
- **Notes**: cmake -DENABLE_ASAN=ON. Run all tests. Fix findings.

### TASK-107: Fuzz test preset loader
- **Status**: queued
- **Phase**: 11
- **Priority**: P3
- **Depends**: TASK-043
- **Notes**: Random/corrupt JSON, extreme values. No crashes.
