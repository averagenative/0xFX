# Bundled Cabinet IRs

These WAV impulse responses ship as the default cab tones in 0xFX.

**Source**: From the "650 Assorted Cabinet Impulses" pack on musical-artifacts.com
(https://musical-artifacts.com/artifacts/252) — released into the **public domain**
by the original contributor (zoyd). The four files chosen here use original
geographic names (no trademarked brand names), matching the 0xFX naming
convention.

| File              | Source name          | Maps to       |
| ----------------- | -------------------- | ------------- |
| 1x12_open.wav     | cambridge 1x12       | FX_CAB_1X12_OPEN |
| 2x12_closed.wav   | allston 2x12         | FX_CAB_2X12_CLOSED |
| 4x12_straight.wav | northbridge 4x12     | FX_CAB_4X12_STRAIGHT |
| 4x12_slant.wav    | worcester stack      | FX_CAB_4X12_SLANT |

Format: IEEE float, stereo, 44.1 kHz. Loaded via `fx_cab_load_ir()` and
downmixed to mono by the engine.
