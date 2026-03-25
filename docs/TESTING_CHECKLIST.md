# 0xFX Testing Checklist

## Standalone GUI

### Startup
- [ ] App launches without console window (Windows)
- [ ] Logo renders with faded edges in toolbar
- [ ] Last session preset restores (pedals visible in chain)
- [ ] Preset name shows in toolbar

### Audio
- [ ] Select input device (guitar interface)
- [ ] Select output device (headphones)
- [ ] Click LIVE — audio passes through
- [ ] VU meters respond to input signal
- [ ] "NO SIGNAL" shows when no input
- [ ] No feedback/noise with clean preset (no stacked pedals)

### Amp Models (test each of 11)
- [ ] Scroll wheel cycles through models on hover
- [ ] Tooltip shows model description
- [ ] Amp face image loads (no black rectangle, transparent bg)
- [ ] Overlay knobs sit on knob holes (not offset)
- [ ] Knobs rotate clockwise (7 o'clock min → 5 o'clock max)
- [ ] Knob labels readable with black outline
- [ ] Param changes affect sound
- [ ] "Model | [dropdown]" layout (label left, selector right)
- [ ] Dummy knobs fill extra holes (no glow/label)

### Pedals
- [ ] Pre-amp [+] shows pedal gallery popup
- [ ] Post-amp [+] shows studio rack menu
- [ ] Add pedal — appears in signal chain
- [ ] Select pedal — detail view shows body image
- [ ] Overlay knobs on correct positions (per-pedal map)
- [ ] Dummy knobs fill extra holes
- [ ] LED glows green when active, dim red when bypassed
- [ ] Right-click or double-click node → toggles bypass
- [ ] Stomp switch button toggles bypass in detail view
- [ ] Arrow rockers reorder pedals
- [ ] Remove (red X) button removes pedal
- [ ] All 39 pedal types load without texture errors
- [ ] Active/Remove buttons render below image (not overlapping)

### Cabinets
- [ ] Cab image shows in detail view (large, centered)
- [ ] "Cab Type | [dropdown]" layout centered
- [ ] Active/Bypass button centered with consistent styling
- [ ] Cab type names show in signal chain (e.g., "4x12 Straight")
- [ ] Switching cab type changes sound

### Studio Processors
- [ ] Post-amp [+] shows studio menu (not pedal gallery)
- [ ] Add Iron Squeeze / Glass EQ / Reel Warmth / Brick Wall
- [ ] Detail view shows knobs
- [ ] Bypass toggle works
- [ ] Remove works
- [ ] Sound changes with parameter adjustments

### Signal Chain Visual
- [ ] INPUT shows TRS cable plug (flipped — tip faces left)
- [ ] OUTPUT shows XLR connector
- [ ] Cables: thick dark rubber with droop on INPUT/OUTPUT connections
- [ ] Cables: straight between pedals/amps/cabs
- [ ] [+] buttons render ON TOP of cables (not behind)
- [ ] Node selection highlight (yellow border)
- [ ] Cable connects to TRS plug endpoint (not floating)

### Dual Chain
- [ ] SINGLE/DUAL toggle button works
- [ ] Y-split diamond + merge diamond render
- [ ] Chain A/B have independent amp/cab selection
- [ ] Mix level sliders appear and work
- [ ] Switching back to SINGLE destroys Chain B

### Recording
- [ ] REC button visible in toolbar
- [ ] Recording indicator pulses red while active
- [ ] Stop recording saves file to disk
- [ ] Recording plays back correctly in external player
- [ ] Format selector (WAV/MP3/FLAC) works

### Presets
- [ ] Ctrl+S saves to last_session.0xfx
- [ ] Ctrl+Shift+S opens save-as dialog
- [ ] Preset load restores full chain state
- [ ] All pedals visible after preset load (no invisible ghost pedals)
- [ ] Factory presets load correctly

### MIDI
- [ ] MIDI devices enumerate in Settings popup
- [ ] Open MIDI device works
- [ ] MIDI Learn button activates learn mode
- [ ] CC messages control mapped parameters
- [ ] CC mappings display with unmap buttons

### Settings
- [ ] Audio device dropdowns work
- [ ] Buffer size selector works
- [ ] Sample rate selector works
- [ ] Settings button is last before window controls

### Window
- [ ] Minimize/maximize/close buttons work
- [ ] Window resize works (elements reflow)
- [ ] Borderless window drag works

---

## CLAP/VST3 Plugin (test in Reaper)

### Loading
- [ ] CLAP: 0xfx_clap.clap appears in Reaper FX list
- [ ] VST3: 0xFX.vst3 appears in Reaper FX list
- [ ] Insert on track — no crash
- [ ] Audio passes through (mono in → stereo out)

### Parameters (137 total)
- [ ] All parameters visible in host automation lane
- [ ] Amp model selector (0-10) switches models
- [ ] Amp knobs respond to automation
- [ ] Pedal type selector adds/removes effects
- [ ] Cab type and bypass controls work
- [ ] Mic type and placement params work
- [ ] Noise gate threshold/attack/release/hold respond
- [ ] Studio processor slots work
- [ ] Chain mode (single/dual) switches correctly

### Presets
- [ ] Factory presets available (6 entries)
- [ ] Selecting preset changes sound
- [ ] "Init (Clean)" resets to defaults

### State Persistence
- [ ] Save DAW project → reopen → plugin state restored
- [ ] All parameter values survive project reload
- [ ] Pedal chain state survives save/load

### MIDI
- [ ] MIDI CC input from host routed to mapped params
- [ ] CC mapping table functional

### Stability
- [ ] Multiple plugin instances don't crash
- [ ] Bypass/enable plugin rapidly
- [ ] Rapid preset switching
- [ ] Remove and re-add plugin on track
- [ ] Long session stability (30+ minutes)

### GUI (when embedded)
- [ ] GUI opens in host window
- [ ] Full visual parity with standalone
- [ ] Resize follows host window
- [ ] Close/reopen GUI window
- [ ] Multiple instances have independent GUIs

---

## Platform-Specific

### Windows
- [ ] No console window on launch
- [ ] App icon shows in taskbar/Explorer
- [ ] Audio devices enumerate (WASAPI)
- [ ] MIDI devices enumerate (WinMM)

### Linux
- [ ] Builds with apt dependencies only
- [ ] Audio works (if not WSL)
- [ ] MIDI works via ALSA (if available)

### macOS
- [ ] Builds with Homebrew SDL2 (`brew install sdl2 python3`)
- [ ] Asset generation works (`python3 tools/generate_embedded_assets.py`)
- [ ] CoreAudio device enumeration
- [ ] CoreMIDI device enumeration
- [ ] CLAP plugin loads in DAW (~/Library/Audio/Plug-Ins/CLAP/)
- [ ] VST3 plugin loads in DAW (~/Library/Audio/Plug-Ins/VST3/)
- [ ] .icns icon displays correctly
