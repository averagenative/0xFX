# Competitive Analysis — Guitar Amp Simulator Market

**Prepared for:** 0xFX project
**Date:** March 2026
**Tasks:** TASK-108 (Neural DSP) + TASK-109 (Broader market survey)

---

## 1. Neural DSP — Deep Dive

### 1.1 Product Line

Neural DSP operates two product categories:

**Archetype Plugins** — artist-endorsed amp/effects plugins, sold individually:

| Plugin | Focus Artist | Genre |
|---|---|---|
| Archetype: Tim Henson | Tim Henson (Polyphia) | Clean/poly technical |
| Archetype: Gojira | Joe Duplantier | Death metal |
| Archetype: Nolly | Nolly Getgood | Modern metal/prog |
| Archetype: Plini | Plini | Modern prog/fusion |
| Archetype: Petrucci | John Petrucci | Progressive metal |
| Archetype: Rabea | Rabea Massaad | Extended-range modern |
| Archetype: Cory Wong | Cory Wong | Funk/clean |
| Archetype: Abasi | Tosin Abasi | Extended-range metal |
| Archetype: Parallax | (generic, no artist) | Modern high-gain |
| Archetype: Omega Ampworks | (boutique brand collab) | High-gain boutique |

**Quad Cortex** — hardware floorboard unit with the same neural capture engine:
- Physical pedalboard format (~$1,649 USD)
- 4 signal paths, 8 cores of DSP
- Touchscreen UI
- Can capture (profile) real amps via direct DI method
- Cloud sync for captures and presets (Cortex Cloud)

### 1.2 Feature Set Per Product

Each Archetype plugin is a self-contained signal chain, not a modular platform. A typical Archetype includes:

- **Amp models:** 1–3 amp heads, each with 2–3 gain channels. Usually modeled on the artist's actual rigs (e.g., Tim Henson uses Friedman BE-100 + clean platform).
- **Effects:** 10–20 effects total, covering essential categories. Effects are fixed (not drag-and-drop extensible). Most plugins include: gate, comp, OD, distortion, chorus, delay (digital + tape), reverb (hall + plate + shimmer), EQ, phaser.
- **Cabinet IR:** 2–4 cabs per plugin, with mic position controls (close/room mic blend, mic angle). Some include a "NEURAL IR" format—compact neural-network-approximated IRs. Third-party .wav IRs can be loaded.
- **UI approach:** Fixed-layout skeuomorphic design. The amp section always occupies the center-bottom; the pedalboard runs along the top or bottom strip. No drag-and-drop reordering—effects are in predefined slots or toggleable positions. Dark, premium finish with artist branding (album art style colors, artist signatures).

### 1.3 Pricing Model

- Archetype plugins: **$99–$149 USD** per plugin (perpetual license, no subscription required)
- Upgrade pricing: occasional sales at 50% off
- No bundle pricing at time of writing (each plugin sold separately)
- Quad Cortex: **$1,649 USD** hardware unit
- No free tier / no trial (some limited free demos have been offered)
- Platform: VST3, AU, AAX. **No CLAP support.** No Linux support.

### 1.4 Key Differentiators

**Neural Capture Technology**
The defining feature. Neural DSP trains small neural networks on recordings of real amps to produce a highly accurate model. Unlike static waveshaping, neural captures adapt to the amp's dynamic response. Users can capture their own amps on the Quad Cortex and share them via Cortex Cloud. This is fundamentally different from traditional DSP modeling and is perceived as more "authentic."

**Artist Branding + Presets**
Each plugin comes with artist-curated presets built by the name artist in their own studio. For fans, this means "the actual tone from that album" is a download away. This drives purchase intent powerfully—buyers are paying for an artist's sonic signature, not just an amp model.

**Quality of Presets**
Neural DSP's factory presets are widely regarded as immediately usable, not demo-grade. The clean/neutral preset structure (the artist's actual working patches) is a differentiator versus competitors whose presets feel like feature demos.

**Visual Identity**
Each plugin has a distinct premium UI that carries the artist's visual branding. The dark/cinematic aesthetic is consistent and polished. UI layout is fast to navigate because everything is fixed—no hunting for modules.

**Cortex Cloud Ecosystem**
Quad Cortex users share captures and presets on a cloud platform. This community-driven content ecosystem is a significant moat. A player can download a capture of a vintage amp they'll never own.

### 1.5 What Neural DSP Does That We Should Consider

| Practice | Their implementation | 0xFX consideration |
|---|---|---|
| **Preset sharing** | Cortex Cloud — community uploads, browse by genre/artist | A community hub for .0xfx preset sharing. Even a GitHub-linked JSON registry would help. |
| **Tone matching / neural capture** | On-device capture via Quad Cortex; NAM-style neural models | 0xFX already imports NAM profiles (`fx_preset_import_nam()`). Expose this prominently. |
| **Dual signal paths** | Parallel amps with per-path mix | 0xFX already supports up to 4 parallel chains. Need to surface this in UI (TASK-310). |
| **Mic position for cabs** | Blend between close mic and room mic; mic angle control | We have full IR loading + synthetic IR generation. Could add parametric mic position to synthetic IR params. |
| **Artist/signature presets** | Deep artist involvement, real studio patches | Could do community artist presets. Invite well-known online guitarists to create and share. |
| **Premium preset quality** | Presets made by the actual artist in their real studio | Our 5 factory presets (Clean Sparkle, Brit Crunch, etc.) should be tuned to be immediately impressive. |

### 1.6 UI/UX Patterns That Feel Premium

- **Minimal chrome, maximum signal.** No menu bars, no toolbars with 15 buttons. The amp face IS the UI.
- **Dark themes with selective color.** Deep black/charcoal backgrounds, amber or red accents only where meaningful (active bypass, clip indicators).
- **Instant-response knobs.** Drag vertically on any knob—no mode switching, no right-click menus required for basic operation.
- **Contextual tooltips, not labels.** Knob labels are tiny. Hovering reveals the full name and current value.
- **Preset navigation is always visible.** Forward/back preset arrows at top. Never buried in a file dialog.
- **No empty space.** Every pixel of the plugin window is occupied by either a control or a texture. Felt vs. chrome vs. wood grain fills all backgrounds.

---

## 2. Broader Market Survey

### 2.1 AmpliTube 5 (IK Multimedia)

**Model:** Tiered product with a gear "store" inside the plugin. A free version exists with limited amps; additional gear is purchased à la carte or via MAX bundle ($399).

**Amp models:** 400+ amp/cabinet combinations in the full MAX version. This includes officially licensed models of Fender, Marshall, Orange, Mesa/Boogie, Hiwatt, and many others. The licensed branding is a major selling point—buyers pay partly for the legal right to say "this is a Fender Bassman."

**Effects:** 70+ effects across all categories.

**Cabinet/IR:** Includes a dedicated "Cab Room" with mic selection (5+ mic types), mic positioning on a 2D grid in front of the cabinet graphic, room ambience control. Supports loading third-party IRs.

**Preset system:** Factory presets organized by genre. User presets saved locally. No built-in community sharing.

**Plugin formats:** VST, VST3, AU, AAX. No CLAP. No Linux.

**Signal routing:** Linear chain only. No parallel amp paths.

**Tone matching:** No built-in tone matching or neural capture.

**Key differentiator:** Licensed amp brands. Players who want "the real Fender name" buy AmpliTube.

**Relevant gaps vs. 0xFX:** Licensed amp names, more amp variety, advanced cab room with mic positioning grid. What we have: parallel chains, open format (.0xfx), NAM import, Linux support, CLAP.

---

### 2.2 Helix Native (Line 6)

**Model:** $99 standalone plugin ($399 retail, but bundled free with Helix hardware). Subscription option: $19.99/month.

**Amp models:** 100+ amp models. Line 6 uses "creative" non-trademark names that gesture at real amps (e.g., "Litigator" = Dumble, "Placater" = Friedman, "Revv Gen Red" = licensed Revv). Mix of original names and some officially licensed brands.

**Effects:** 300+ effects and models total (amps, cabs, effects combined).

**Cabinet/IR:** 100+ factory cabs. Full third-party IR loading. Dual-microphone cab setup (two mics independently positioned on the same cabinet).

**Preset system:** Setlist-based organization. Factory bundles + user setlists. Large community of patch-sharing sites (CustomTone.com, TGP threads). No integrated sharing.

**Plugin formats:** VST, VST3, AU, AAX. No CLAP. No Linux.

**Signal routing:** Fully modular. Parallel paths, series/parallel switching, stereo/mono routing, true parallel amp paths with per-path level. Loop blocks for FX send/return. This is one of the most flexible signal chains in the category.

**Tone matching:** No neural capture. Helix IR capture tool captures cabinet IRs from physical cabs (not amps).

**Key differentiator:** Same engine as Helix hardware (HX) — hardware players who take Helix Native can use the exact same patches on stage. Deepest signal routing of any plugin in this list.

**Relevant gaps vs. 0xFX:** Scale of content (300+ vs. our 20+), dual-mic cab setup, modular routing beyond our split/merge model, DAW preset sharing via CustomTone ecosystem. What we have: open .0xfx format, true open-source codebase, CLAP-first, Linux, NAM import, real-time-safe C99 engine.

---

### 2.3 Guitar Rig 7 (Native Instruments)

**Model:** $99 plugin. Standalone + plugin. Subscription available via NI subscription service.

**Amp models:** ~20 amp heads, more focused on variety than volume. Includes clean, crunch, lead, and some unusual tones (synth-style filtering, clean through dirt chains).

**Effects:** 100+ components. The defining feature is the **modular rack format**: every component (amp, cab, effect, utility) is a draggable "rack module" that can be inserted anywhere in a vertical rack GUI. Signal flow is top-to-bottom, fully user-defined. Send/return loops, crossover splits, parallel branches—all via drag-and-drop rack modules.

**Cabinet/IR:** Cabinet module uses built-in IRs. Third-party IR loading supported. No mic positioning beyond basic controls.

**Preset system:** Factory presets by genre + NI User Library for sharing (basic).

**Plugin formats:** VST, VST3, AU, AAX. No CLAP. No Linux.

**Signal routing:** The most modular of all competitors. The rack metaphor means routing is essentially unlimited—any module can feed any other module via the rack hierarchy.

**Tone matching:** No neural capture.

**Key differentiator:** Modular rack routing. Creative/experimental players who want unusual signal paths (e.g., guitar through a vocoder into a chorus into a simulated tape machine) find Guitar Rig uniquely flexible.

**Relevant gaps vs. 0xFX:** Deeper modular routing than our current split model, wider variety of creative/non-guitar-amp effects (synth filters, spectral processing). What we have: simpler UX (modular routing adds complexity), CLAP, Linux, open format, better performance (C99 vs. heavy NI framework).

---

### 2.4 BIAS FX 2 (Positive Grid)

**Model:** Three tiers: Standard ($99), Professional ($149), Elite ($199). Each tier adds more amp/effect content.

**Amp models:** 100–200+ depending on tier. Also has **BIAS Amp** as a separate product ($99–$199) for designing custom amp circuits. BIAS FX and BIAS Amp integrate: you can open a custom amp from BIAS Amp and use it in BIAS FX.

**Effects:** 100+ effects. Standard pedal categories plus some extended options.

**Cabinet/IR:** Full IR loading. Mic selection and positioning. Dual-mic support.

**Preset system:** ToneCloud — online community preset sharing built directly into the plugin UI. Browse, preview, and download presets without leaving the plugin. This is the gold standard for in-plugin community integration.

**Plugin formats:** VST, VST3, AU, AAX. No CLAP. No Linux.

**Signal routing:** Dual signal chain — two parallel signal paths with mixer. "Dual amp" UI mode. Not as deep as Helix Native but more accessible.

**Tone matching:** **Yes — ToneMatch.** A flagship feature. Load a reference audio clip (e.g., a song recording), and BIAS FX will automatically tune amp and effect parameters to approximate the tonal character of that recording. Results vary but the concept is a significant differentiator.

**Key differentiator:** ToneCloud community preset sharing + ToneMatch automated tone matching + BIAS Amp integration for custom amp design.

**UI/UX:** The most polished skeuomorphic UI in the category. Full photorealistic amp heads on a virtual stage. Individual pedal graphics. The "NO SIGNAL DETECTED" status bar (referenced in our TASK-308) originated from BIAS FX 2. Widely considered the visual benchmark for this product category.

**Relevant gaps vs. 0xFX:** ToneCloud community, ToneMatch, BIAS Amp integration (custom amp circuit design), more amp/effect content, more polished skeuomorphic rendering. What we have: CLAP, Linux, open .0xfx format, NAM import, more parallel chains (4 vs. 2), open-source codebase.

---

### 2.5 TH-U (Overloud)

**Model:** $149 base. Expansion packs for additional rigs sold separately ($29–$89 each). Rig expansion packs are meticulously modeled from specific physical amp + cab + effect configurations.

**Amp models:** 80+ in base version, 200+ with expansions. Focus on boutique and vintage amps that other vendors overlook.

**Effects:** 90+ effects.

**Cabinet/IR:** **Cab Sim is the primary differentiator.** TH-U uses "Cockos Impulse Response" format and their own cab simulation is particularly accurate. The "Rig Player" feature: each rig expansion is modeled as a complete signal chain (amp + cab + mic + room) from a single recording session, not just an amp model. This is analogous to Neural DSP's neural captures but done via convolution/profiling methodology.

**Preset system:** Rig library organized by genre. No community sharing hub.

**Plugin formats:** VST, VST3, AU, AAX. No CLAP. No Linux.

**Signal routing:** Parallel chains. Relatively standard routing.

**Tone matching:** Rig Player is the closest thing—it captures a full rig convolution, not just amp parameters.

**Key differentiator:** Realistic cabinet simulation and Rig Player complete chain captures. Boutique/vintage amp coverage.

**Relevant gaps vs. 0xFX:** Rig Player / complete-chain capture methodology, deeper cab simulation, boutique amp coverage. What we have: CLAP, Linux, open format, NAM import, synthetic IR generation (unique — parametric cab modeling from physics), more parallel chains.

---

### 2.6 STL ToneHub

**Model:** $9.99/month subscription or $99/year. Individual signature tones from guitarists available as add-on packs ($9–$49).

**Amp models:** ~30 base amps. Focus on modern metal and high-gain. Signature tone packs include complete rig configurations from metal guitarists (Josh Middleton, Andy James, etc.).

**Effects:** Standard set, ~30–50 effects. Not trying to be the most comprehensive.

**Cabinet/IR:** Standard IR loading. No advanced mic positioning.

**Preset system:** Signature tone packs are the core product. Buy a pack from a specific guitarist and get their complete rig. Similar concept to Neural DSP Archetypes but accessed via subscription add-ons rather than per-plugin purchases.

**Plugin formats:** VST, VST3, AU, AAX. No CLAP. No Linux.

**Signal routing:** Standard linear chain.

**Tone matching:** No.

**Key differentiator:** Subscription model makes artist signatures more accessible. Appeals to metal/djent players who want specific artist sounds without a $99+ outlay.

**Relevant gaps vs. 0xFX:** Subscription-accessible artist signatures, established metal community relationships. What we have: open format, CLAP, Linux, more DSP variety, parallel chains, NAM import.

---

### 2.7 Guitarix (Open Source)

**Model:** Completely free and open source (GPL). Linux-first.

**Amp models:** ~20 amp models, implemented via LV2 plugins. Models are traditional DSP-based (not neural). Cover standard archetypes (clean, crunch, lead).

**Effects:** 100+ effects available as LV2 plugins. The effect library is large because the LV2 ecosystem is rich—Guitarix can host any LV2 effect plugin.

**Cabinet/IR:** Basic IR convolution. Zita-convolver backend.

**Preset system:** Local file-based. Limited preset sharing (some community sites).

**Plugin formats:** Standalone JACK application + LV2 plugin. Not VST/CLAP/AAX. Linux only (some macOS users via Homebrew).

**Signal routing:** LV2 patch bay routing (via Carla or JACK patchbay). Fully modular at the system level.

**Tone matching:** No.

**Key differentiator:** Only professional-grade open-source guitar amp simulator for Linux. The only real option for Linux JACK-based rigs. LV2 ecosystem integration means unlimited DSP extensibility.

**Relevant gaps vs. 0xFX:** LV2 ecosystem compatibility, JACK routing integration, larger community (it is the Linux guitar software). What we have: CLAP + VST3 for DAW integration, Windows support, more polished GUI (ImGui vs. GTK), cleaner API architecture, NAM import.

**Strategic note:** Guitarix is a peer, not primarily a competitor. There is natural alignment and potential for collaboration. 0xFX's CLAP support and Windows compatibility make it complementary to Guitarix's Linux/JACK ecosystem.

---

### 2.8 NAM — Neural Amp Modeler (Open Source)

**Model:** Completely free and open source (MIT). Available as a standalone application and as VST3/AU/CLAP plugins.

**What it does:** NAM is not a full guitar amp simulator — it is a neural amp capture engine. Users record a test tone through their real amp, run it through the trainer, and get a `.nam` capture file (~few KB) that can be loaded into the NAM plugin. The result is a highly accurate amp model (often perceptually indistinguishable from the real amp on a blind test).

**Amp models:** Unlimited — the capture model. A large community repository (ToneHunt.org) hosts thousands of captures: vintage amps, modern high-gain, boutique, bass amps.

**Effects:** NAM provides only the amp/cab section. Users run it in a DAW with other effect plugins for the full chain. No built-in effects.

**Cabinet/IR:** Standard IR loading alongside captures.

**Plugin formats:** VST3, AU, **CLAP**, AAX. Linux supported via CLAP/LV2.

**Signal routing:** Not applicable — single amp/cab node only.

**Key differentiator:** Open ecosystem of community captures. The most accurate amp modeling methodology available for free. The NAM capture format is becoming a community standard (like the IR format for cabs).

**Relevant gaps vs. 0xFX:** No effects chain, no full rig management, no preset system. What we have: full signal chain, effects, preset system, parallel chains, GUI pedalboard. The two projects are complementary.

**Strategic note:** 0xFX already supports `fx_preset_import_nam()`. This is a significant advantage — 0xFX can be a full-featured front-end for the entire NAM capture ecosystem. This should be promoted prominently. 0xFX + NAM together cover what Neural DSP covers with a fully open stack.

---

## 3. Feature Matrix

| Feature | 0xFX | Neural DSP | AmpliTube 5 | Helix Native | Guitar Rig 7 | BIAS FX 2 | TH-U | STL ToneHub | Guitarix | NAM |
|---|---|---|---|---|---|---|---|---|---|---|
| **Amp models (count)** | 5 | 1–3 / plugin | 400+ | 100+ | ~20 | 100–200+ | 80–200+ | ~30 | ~20 | ∞ (captures) |
| **Effects (count)** | 35+ | 10–20 | 70+ | 300+ | 100+ | 100+ | 90+ | 30–50 | 100+ (LV2) | 0 |
| **Cabinet IR loading** | Yes | Yes | Yes | Yes | Yes | Yes | Yes | Yes | Basic | Yes |
| **Custom IR (.wav)** | Yes | Yes | Yes | Yes | Yes | Yes | Yes | Yes | Yes | Yes |
| **Synthetic IR generation** | **Yes (unique)** | No | No | No | No | No | No | No | No | No |
| **Parallel amp chains** | Yes (up to 4) | No | No | Yes | Yes | Yes (2) | Yes | No | Via JACK | No |
| **Preset system** | Yes (.0xfx JSON) | Yes | Yes | Yes (setlists) | Yes | Yes | Yes | Yes (artist packs) | Basic | No |
| **Open preset format** | **Yes (.0xfx)** | No | No | No | No | No | No | No | Partial | No |
| **Community preset sharing** | No | Cortex Cloud | No | CustomTone.com | NI User Library | ToneCloud (built-in) | No | Artist packs | No | ToneHunt.org |
| **Tone matching** | No | Neural capture | No | No | No | ToneMatch | Rig Player | No | No | Capture-based |
| **NAM import** | **Yes** | No | No | No | No | No | No | No | No | Native |
| **Plugin formats** | CLAP, VST3 | VST3, AU, AAX | VST, VST3, AU, AAX | VST, VST3, AU, AAX | VST, VST3, AU, AAX | VST, VST3, AU, AAX | VST, VST3, AU, AAX | VST, VST3, AU, AAX | LV2, JACK | VST3, AU, CLAP |
| **CLAP support** | **Yes** | No | No | No | No | No | No | No | No | Yes |
| **Linux support** | **Yes** | No | No | No | No | No | No | No | Yes (primary) | Yes |
| **Windows support** | Yes | Yes | Yes | Yes | Yes | Yes | Yes | Yes | Partial | Yes |
| **macOS support** | Planned | Yes | Yes | Yes | Yes | Yes | Yes | Yes | Partial | Yes |
| **Open source** | **Yes (MIT)** | No | No | No | No | No | No | No | Yes (GPL) | Yes (MIT) |
| **Price** | Free | $99–$149/plugin | Free–$399 | $99 (or $19.99/mo) | $99 | $99–$199 | $149 | $9.99/mo | Free | Free |
| **Drag-and-drop reorder** | Yes | No (fixed slots) | No | No (drag blocks) | Yes (rack modules) | No | No | No | No | N/A |
| **Chromatic tuner** | Yes | Yes | Yes | Yes | Yes | Yes | Yes | Yes | Yes | No |
| **Noise gate** | Yes | Yes | Yes | Yes | Yes | Yes | Yes | Yes | Yes | No |
| **Looper** | Yes | Some | No | Yes | No | No | No | No | No | No |
| **License model** | Perpetual (MIT) | Perpetual | Perpetual + IAP | Perpetual / subscription | Perpetual / subscription | Perpetual | Perpetual + IAP | Subscription | Free | Free |

---

## 4. Parity Gaps — Ranked by User Impact

These are features competitors have that 0xFX currently lacks, ranked by estimated importance to a typical guitarist evaluating the product.

### P1 — High Impact

**1. Community preset sharing hub**
Every major competitor has a preset sharing ecosystem (Cortex Cloud, ToneCloud, CustomTone.com, ToneHunt.org). Users discover and download other people's rigs. Without this, the preset system is islands — each user's presets exist only locally. The .0xfx JSON open format makes this technically trivial to add as an external service, a GitHub repository, or even just a documented sharing convention. This is the highest-impact missing feature.

**2. Amp model breadth**
We ship 5 amp models. Helix Native ships 100+. AmpliTube ships 400+. While our 5 models are each well-implemented (cascaded gain stages, per-model EQ voicing, power amp sag), the number is a significant barrier for users who want to explore diverse tonal territory. Every new amp model also increases replayability and preset variety. Target: 15–20 before public alpha, ideally with a broader style range (jazz clean, bass amp, hi-fi studio preamp).

**3. Tone matching / capture integration**
BIAS FX 2's ToneMatch and Neural DSP/NAM's capture ecosystem are the most differentiating features in the market. 0xFX already imports NAM profiles — this is the foundation. What's missing is: (a) surfacing NAM import prominently in the UI as "Load Amp Profile," (b) a guided capture workflow (like BIAS Amp's "Amp Designer"), or (c) a direct integration with ToneHunt.org for discovering captures in-app.

**4. Preset browser UI**
TASK-307 covers session/save state, but a preset browser with list view, search, filtering by genre/tag, and one-click A/B comparison is not yet implemented. BIAS FX 2 and Helix Native both have this. Users need to audition presets quickly without manually loading files.

### P2 — Medium Impact

**5. Mic positioning for cab IR**
AmpliTube, Helix Native, and BIAS FX 2 all offer a visual mic-positioning interface for cabs (move a virtual mic on a 2D grid in front of the speaker cone). We have synthetic IR generation with parametric models — we could expose mic distance, angle, and on-axis position as intuitive parameters in the cab UI rather than requiring users to load different IRs.

**6. More amp content via parameter extension**
Adding more amp voicings within the existing 5 base types (e.g., Clean-A and Clean-B channels, crunch-to-lead progression within one amp) would improve coverage without requiring full new amp model implementations. Neural DSP does this — each plugin has 2–3 channels per amp head.

**7. MIDI mapping (CC to params)**
TASK-203 covers MIDI device enumeration. Hardware pedalboard users need to map footswitches and expression pedals to specific parameters. Every competitor supports MIDI CC mapping. Without it, 0xFX cannot be used in a live rig with MIDI-capable hardware.

**8. Parametric EQ in the amp chain**
Most competitors (Helix Native, Guitar Rig 7, AmpliTube) include a flexible parametric EQ as a standard chain component, often placed post-amp for final tone shaping. We have Tone Sculptor (7-band graphic) and Precision EQ as pedals, which covers this technically — the gap is discoverability and default placement.

### P3 — Lower Impact (but notable)

**9. Officially licensed amp names**
AmpliTube's licensed amp brands ("Fender Twin," "Marshall JCM800") carry marketing value. Our trademark-free naming convention is a deliberate choice (and legally correct), but some users associate product quality with licensed branding. Mitigation: lean into "inspired by" framing in documentation and use the original names in context (e.g., "Fullerton Clean — inspired by American clean sounds of the 1950s–70s").

**10. macOS support**
Currently planned but not implemented. macOS is the largest DAW platform (Pro Tools, Logic, Ableton on Mac). Every commercial competitor supports macOS. This is a hard blocker for a significant portion of the market.

**11. AAX format (Pro Tools)**
Pro Tools users require AAX. All major commercial competitors support AAX. This requires an AVID developer license and SDK, so it is a harder technical path, but it blocks the professional recording studio market.

**12. Artist/signature preset packs**
Neural DSP and STL ToneHub's artist collab model drives purchase intent through fan bases. Could be replicated via community partnerships with prominent online guitarists who create and credit official presets for 0xFX. Cost: relationship management, not licensing fees.

---

## 5. Unique Advantages — What 0xFX Offers That No Competitor Does

These are features that are genuinely unique to 0xFX as of March 2026, not just "roughly equivalent to."

### U1 — Open Source (MIT License)
No commercial guitar amp plugin is open source. All competitors are proprietary. 0xFX being MIT-licensed means: users can inspect, modify, and redistribute the engine; researchers can use it for DSP experimentation; developers can build on top of it; the community can contribute new amp models and effects. This is a structural, not marketing, differentiator.

### U2 — CLAP Plugin Format (Primary)
Neural DSP, AmpliTube, Helix Native, Guitar Rig, BIAS FX, TH-U, and STL ToneHub all ship VST3/AU/AAX only. CLAP support is limited to NAM and Guitarix. 0xFX ships CLAP as its first-class plugin format. CLAP is gaining adoption in Reaper, Bitwig, and Ardour. DAW users on those platforms currently have almost no guitar amp options with CLAP — 0xFX fills this gap.

### U3 — Linux Support (Professional DAW-grade)
Guitarix supports Linux but is JACK/LV2-only (not DAW-ready via CLAP/VST3). NAM supports Linux via CLAP but is amp-only. No full guitar amp sim with a complete signal chain, effects, GUI, and CLAP plugin support runs on Linux. 0xFX is the only product in this category that targets Linux as a first-class platform.

### U4 — Synthetic IR Generation (Parametric Physics Model)
No competitor generates cabinet IRs from a physics model. All competitors ship static IR collections (.wav files) captured from physical cabinets. 0xFX's `fx_cab_generate_ir()` API uses a Thiele-Small speaker model, open/closed cabinet simulation, and mic position model to synthesize IRs without requiring any captured samples. This means: (a) the bundled cabs are truly zero-third-party-IP, (b) the cab parameters are continuously variable (not just choosing between 10 static IRs), and (c) users who want to design a fictional cabinet (e.g., a 6x9 bass cabinet with a close-mic at 45°) can do so parametrically.

### U5 — Open Preset Format (.0xfx JSON)
Competitor preset formats are proprietary binary files. Helix's `.hlx` format is technically JSON but undocumented. BIAS FX `.biasptch` is binary. 0xFX's `.0xfx` format is documented, human-readable JSON with explicit version fields. This enables: version control of presets (git diff between two rigs), scripting/automation (generate 100 preset variations with a Python script), external tooling (web-based preset editors), and community sharing without format lock-in.

### U6 — NAM Profile Import
NAM neural amp captures are becoming a community standard for amp modeling (thousands of captures on ToneHunt.org). 0xFX is the only full-featured guitar rig app with a GUI pedalboard that natively imports NAM profiles. Neural DSP does not import NAM. BIAS FX does not import NAM. Helix Native does not import NAM. This makes 0xFX the best front-end for the NAM ecosystem — users get the complete pedalboard, effects chain, and preset system around their NAM captures.

### U7 — Up to 4 Parallel Amp Chains
Most competitors support 0–2 parallel paths. Helix Native and Guitar Rig are the most flexible, but they represent the high end. 0xFX supports up to 4 fully independent parallel chains, each with its own amp model, cabinet IR, pre/post effects, and mix level. This enables complex rig configurations (quad-amp blending, wet/dry/wet rigs, bass+guitar splits) that no competitor supports in a single plugin.

### U8 — Real-Time-Safe Pure C99 Engine
The 0xFX DSP engine makes no heap allocations in the audio callback, uses no locks, and has no dependencies beyond standard C. This is a stronger real-time safety guarantee than any commercial competitor provides. Most competitors use C++ with STL allocators and virtual dispatch in the audio path. 0xFX's engine can be embedded in any real-time audio context (embedded systems, custom hardware, other plugins) without risk of priority inversion or unbounded latency. This is primarily a developer/integrator advantage but is also relevant for latency-sensitive live use.

---

## 6. Summary Recommendations

Based on this analysis, the following actions would most improve 0xFX's competitive position:

1. **Surface NAM import prominently** — a "Load Amp Profile (.nam)" button in the amp node UI. This immediately connects 0xFX to the largest open amp capture community. Zero additional engineering required.

2. **Add 10+ more amp models** — even simple expansions (adding clean-to-dirty channels within existing models, adding a jazz amp, bass amp, hi-fi studio preamp) would close the content gap significantly. Each amp model is a compelling reason for a new user to stay.

3. **Build a basic preset gallery UI** — a built-in browser for locally saved presets with tags and search. Then establish a convention (a GitHub repo or simple web index) for community preset sharing using the existing .0xfx format.

4. **Implement MIDI CC mapping** (TASK-203) — this unblocks the live performance market entirely. Without MIDI, hardware pedalboard users cannot use 0xFX on stage.

5. **Expose parallel chains in the UI** (TASK-310 Y-split node) — the engine supports 4 parallel chains already; the UI must make this discoverable and intuitive. This is a genuine differentiator that no one else does as well.

6. **Add mic position parameters to synthetic IR** — convert the physics-based IR generation into a user-facing cab UI with mic distance/angle sliders. This would be unique in the market and showcase the synthetic IR system.

7. **macOS port** — unlocks the largest DAW platform segment. Architecturally feasible since the engine is pure C99 and ImGui/SDL2 are cross-platform.
