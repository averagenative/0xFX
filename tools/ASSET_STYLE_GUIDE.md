# 0xFX Asset Style Guide

## Core Aesthetic: "Worn Grime"

Every visual asset in 0xFX should look like gear that has been **gigged hard for years**. Not pristine showroom renders — not destroyed junkyard trash either. The sweet spot is **a working musician's pedalboard after 5 years of touring**: loved, used, beat-up, but still functional.

### Visual Pillars

1. **Worn, not wrecked** — scuffs, scratches, paint wear around knobs and footswitches. Not smashed or broken.
2. **Grime accumulates** — dust in crevices, fingerprint smudges on metal, dirt around rubber feet and edges. Subtle, not caked-on.
3. **Dark, moody lighting** — low-key, warm side lighting. Deep shadows. Like a dimly lit stage or rehearsal space.
4. **Warm color temperature** — amber/orange accent lighting. No cool blue/white clinical lighting.
5. **Tactile materials** — you should "feel" the metal, tolex, rubber, and wood through the image.

### Color Palette

| Role | Colors | Usage |
|------|--------|-------|
| Background | #141210, #1a1714 | App background, dark surfaces |
| Surface | #1e1b17, #252119 | Panel backgrounds, pedal bodies |
| Accent warm | #CC8822, #E6A030 | LED indicators, active knob arcs |
| Accent rust | #884422, #A65533 | Worn paint, rust highlights |
| Text | #D9CDB8, #B3A894 | Labels, knob markings |
| Metal | #4A4540, #6B6358 | Scuffed chrome, aged aluminum |

## Prompt Architecture

Every prompt follows this structure:

```
[SUBJECT] [MATERIAL/FINISH] [WEAR DETAILS] [KNOB/CONTROL DETAILS] [LIGHTING] [COMPOSITION] [STYLE LOCK]
```

### Style Lock Suffix (append to ALL prompts)

```
dark moody side-lighting, warm amber tones, shallow depth of field, photorealistic product photography, matte finish, studio shot on dark worn surface, 0xfx worn-grime aesthetic
```

### Negative Prompt (use when supported)

```
pristine, new, clean, shiny, glossy, bright white lighting, clinical, cartoon, illustration, text overlay, watermark, brand logos, trademark names
```

## Asset Templates

### Pedals (Top-Down View)

```
[color] guitar effects stomp pedal, die-cast aluminum enclosure, [N] small chicken-head knobs, [N] toggle switches, single footswitch with rubber cap, scuffed paint showing bare metal at edges and corners, fingerprint smudges on surface, dust in knob crevices, small scratches across faceplate, tiny LED indicator light [color], top-down flat-lay view on dark pedalboard surface, [STYLE LOCK]
```

**Color variations by effect type:**
- Overdrive: forest green, olive drab, army green
- Distortion: burnt orange, rust red, oxblood
- Fuzz: deep purple, charcoal, matte black
- Delay: sky blue, steel blue, teal
- Reverb: dark blue, navy, midnight blue
- Modulation: coral, dusty pink, amber
- Compressor: silver/aluminum, gunmetal
- Wah/Filter: chrome, brushed steel
- EQ: matte black, dark gray
- Utility: cream, off-white, sand

### Amp Panels (Front View)

```
vintage guitar amplifier front control panel, [style] aesthetic, aged [covering] covering with wear marks, [N] cream chicken-head knobs in a row, printed white control labels slightly faded, chrome input jacks with patina, pilot light glowing amber, scratched chrome faceplate, [width] panel aspect ratio, straight-on front view, [STYLE LOCK]
```

**Per-model details:**
- Fullerton Clean: black tolex, silver faceplate, 7 knobs, "blackface" era look
- British Crunch: dark green/gold tolex, gold faceplate, 8 knobs, plexi-era look
- Southwest Lead: black tolex, brushed aluminum faceplate, 8 knobs, industrial look
- Essex Chime: dark brown/tan tolex, copper faceplate, 7 knobs, diamond grille cloth visible
- Tweed Blues: worn tweed cloth covering, brown/tan faceplate, 6 knobs, vintage 50s look

### Cabinets (3/4 View)

```
guitar speaker cabinet, [config] speakers visible through dark grille cloth, [covering] covering with road wear on corners, metal corner protectors with scratches, recessed handles with wear marks, caster wheels [if 4x12], slightly dusty grille cloth, 3/4 angle view showing front and side, [STYLE LOCK]
```

**Configurations:**
- 1x12 open back: compact, combo-style, worn tweed or tolex
- 2x12 closed: medium, horizontal, dark tolex
- 4x12 straight: full stack bottom, dark tolex, metal corners
- 4x12 slant: full stack top, dark tolex, angled baffle
- Direct/Flat: small DI box or flat-response monitor

### Theme Textures (Tileable)

```
seamless tileable texture, [material], worn and aged with [wear type], dark moody lighting, high detail macro photography, [STYLE LOCK]
```

**Materials:**
- Pedalboard surface: black industrial carpet/velcro, scuffed, with adhesive residue marks
- Metal panel: brushed aluminum with patina, fingerprints, micro-scratches
- Tolex: black vinyl amp covering, cracked at folds, slight shine from wear
- Leather: dark brown aged leather, oil stains, creases, worn smooth in spots

## Generation Parameters

### OpenAI (DALL-E 3 / gpt-image-1)
- Size: 1024x1024 (square) for pedals, 1792x1024 (landscape) for amp panels
- Quality: "hd" for final assets, "standard" for test runs
- Style: "natural" (not "vivid")

### Consistency Strategy

Without LoRA (pure prompt-based), maintain cohesion through:

1. **Same style lock suffix** on every prompt — this is non-negotiable
2. **Same negative prompt** (where supported)
3. **Same lighting setup description** — "dark moody side-lighting, warm amber tones"
4. **Same surface/background** — "dark worn surface" or "dark pedalboard surface"
5. **Same wear vocabulary** — use the exact same wear descriptors across all prompts
6. **Post-process** — apply consistent color grading (warm, desaturated, dark) and vignette in batch
7. **Generate in batches** — do all pedals in one session, all amps in another, to leverage model consistency within a session
8. **Cherry-pick and iterate** — generate 3-4 variants per asset, pick the one closest to the reference images, then refine

## File Naming

```
resources/
├── pedals/
│   ├── jade_drive.png
│   ├── gold_drive.png
│   └── rodent.png
├── amps/
│   ├── fullerton_clean.png
│   └── brit_crunch.png
├── cabs/
│   ├── 1x12_open.png
│   └── 4x12_straight.png
├── theme/
│   ├── pedalboard_surface.png
│   └── metal_panel.png
└── reference/
    ├── ref_01_pedal_aesthetic.png
    └── ref_02_amp_aesthetic.png
```

All final assets: PNG, pre-multiplied alpha where transparency is needed.
