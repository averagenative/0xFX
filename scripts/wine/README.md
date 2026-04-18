# Wine bottle for Windows smoke testing

Scripts to run 0xFX's Windows artifacts — the NSIS installer, the standalone
`0xfx_gui.exe`, and the VST3/CLAP plugins inside a Windows DAW — on a Linux
machine without dual-booting.

This is for **smoke testing only**. Audio under Wine is not representative
of native Windows latency or compatibility. Use a real Windows box (or a
user's report) for final playtest signoff.

## What lives here

| Script | Purpose |
|---|---|
| `setup.sh` | One-time init of the prefix at `~/.local/share/0xfx-wine-test/` |
| `install-0xfx.sh` | Run the NSIS installer silently inside the prefix |
| `launch-0xfx.sh` | Start the installed `0xfx_gui.exe` |
| `install-reaper.sh` | Download + install Reaper so plugin scanning can be tested |

All scripts point at the same prefix (`WINEPREFIX=$HOME/.local/share/0xfx-wine-test`)
so the app, plugins, and DAW end up in one place.

## Prerequisites

```bash
# Fedora
sudo dnf install wine wine-core wine-common wine-desktop winetricks

# Ubuntu / Debian
sudo apt install wine winetricks
```

`winetricks` is optional but recommended — `setup.sh` will use it to pull
in `corefonts` and `vcrun2019`, which Reaper and some VST3 plugins expect.

**Fedora gotcha:** `wine-common` is NOT pulled in by `wine` alone and
ships the `/usr/share/wine/winmd/*.winmd` files. Without it, `wineboot
--init` hangs forever in `setupapi:do_file_copyW` trying to copy missing
WinRT metadata. Symptom: two stuck `wineboot` dialogs at 99% CPU with no
error output. If you see this, `dnf install wine-common` and nuke the
prefix: `rm -rf ~/.local/share/0xfx-wine-test && ./scripts/wine/setup.sh`.

## Workflow

1. **Build the Windows artifact** (from Linux via mingw-w64):
   ```bash
   ./scripts/packaging/package_release.sh 1.1.0 --arch x64
   ```
   Produces `release/0xFX-1.1.0-windows-x64-setup.exe`.

2. **Initialize the prefix** (one-time, idempotent):
   ```bash
   ./scripts/wine/setup.sh
   ```

3. **Install 0xFX into the prefix**:
   ```bash
   ./scripts/wine/install-0xfx.sh
   ```
   Auto-picks the newest `release/0xFX-*-windows-x64-setup.exe`. Pass a
   path explicitly to override.

4. **Smoke-test the standalone**:
   ```bash
   ./scripts/wine/launch-0xfx.sh
   ```
   Verify: GUI renders, preset list loads, cab dropdowns work, knobs turn.

5. **(Optional) DAW plugin scan**:
   ```bash
   ./scripts/wine/install-reaper.sh
   WINEPREFIX=~/.local/share/0xfx-wine-test wine \
     '~/.local/share/0xfx-wine-test/drive_c/Program Files/REAPER (x64)/reaper.exe'
   ```
   In Reaper: **Options → Preferences → Plug-ins → VST** and **→ CLAP**,
   scan, look for `0xFX` in each list. If scanning crashes the sandbox,
   Reaper blacklists the plugin and continues — check the scan log for
   the stack trace.

## Test matrix (minimum before shipping)

Run through this list on every release build:

- [ ] `install-0xfx.sh` exits 0 and puts `0xfx_gui.exe` at
      `C:\Program Files\0xFX\0xfx_gui.exe`
- [ ] `launch-0xfx.sh` opens the GUI window without crashing
- [ ] Audio device dropdown populates (even if only Wine's MME/PulseAudio
      shim is listed)
- [ ] At least one factory preset loads and renders its pedal chain
- [ ] CLAP plugin discovered by Reaper (0xFX appears in `Options →
      Preferences → Plug-ins → CLAP`)
- [ ] VST3 plugin discovered by Reaper
- [ ] Loading 0xFX as an effect on a track doesn't blacklist it on the
      first insertion

Anything more involved — real audio latency, multi-instance stress,
exotic hardware — goes to a native Windows tester, not this prefix.

## Wine caveats

- **Audio is a smoke signal, not a benchmark.** Wine's winepulse/winealsa
  layer is not real WASAPI. Do NOT judge tone, glitchiness, or latency
  from what you hear here.
- **MIDI is limited.** Wine's MIDI support has gaps; MIDI learn will feel
  different from native Windows.
- **No hardware USB interfaces.** iRigs, Scarletts, etc. appear as
  generic WASAPI devices to the app (if at all). The plugin scan test
  above still validates the important parts (load, init, destroy).
- **Graphics driver surprises.** OpenGL under Wine uses DXGI↔GL bridging
  for some frontends; rendering bugs seen here are often Wine quirks,
  not 0xFX bugs. Reproduce on native Windows before filing.
- **First launch is slow.** `wineboot -i` takes ~15s and writes a lot
  of files. `winetricks` deps can take minutes on a fresh prefix.

## Nuking the prefix

When something goes sideways and you want a clean slate:

```bash
rm -rf ~/.local/share/0xfx-wine-test
./scripts/wine/setup.sh
```

Nothing here touches `~/.wine` — user's default wine config is untouched.

## Why not Bottles (the flatpak)?

Bottles adds a UI layer and its own sandbox semantics over wineprefixes.
For reproducible scripted smoke tests, a plain prefix is simpler: no
flatpak permissions to wrangle, no GUI-driven state, everything in one
directory on disk.
