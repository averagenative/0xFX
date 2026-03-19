# Pedal Skin System — Design Document

## Overview

Each pedal in the GUI is rendered as a composite of layers:

```
┌──────────────────────────────┐
│  Background image (no knobs) │  ← pedal_body.png (knobless, transparent bg)
│  ┌────┐  ┌────┐  ┌────┐     │
│  │knob│  │knob│  │knob│     │  ← knob sprites, individually rotated per param value
│  └────┘  └────┘  └────┘     │
│        ● LED                 │  ← LED sprite (on/off based on bypass state)
│       [STOMP]                │  ← footswitch hit area (click to toggle bypass)
└──────────────────────────────┘
```

## Pedal Layout Descriptor

Each pedal type has a static layout descriptor that maps its visual skin:

```c
typedef struct {
    const char *body_image;      /* path to knobless body PNG */
    int         num_knobs;
    struct {
        float   x, y;            /* position (normalized 0-1 relative to body) */
        float   size;            /* knob diameter (normalized) */
        int     param_index;     /* which engine param this knob controls */
        const char *knob_style;  /* "chicken_cream", "skirted_black", etc. */
    } knobs[12];
    struct {
        float   x, y;            /* LED position (normalized) */
        const char *on_image;    /* "led_red_on", "led_amber_on", etc. */
        const char *off_image;   /* "led_red_off", "led_amber_off", etc. */
    } led;
    struct {
        float   x, y;            /* footswitch center (normalized) */
        float   radius;          /* clickable radius */
    } stomp;
} fx_pedal_layout_t;
```

## Knob Rendering

1. Load knob sprite (e.g., `knob_chicken_cream_nobg.png`) once as a GPU texture
2. For each pedal knob slot:
   - Get current param value via `fx_pedal_get_param(engine, pedal_id, param_index)`
   - Map value 0.0-1.0 to rotation angle (135° to -135°, same as arc knob widget)
   - Draw the knob sprite at (x,y) rotated to that angle
   - Overlay the knob's arc indicator (same drawing code as knobs.cpp)
   - On mouse drag: update param via `fx_pedal_set_param()`

## LED State

- `fx_pedal_get_bypass(engine, pedal_id)` → true = bypassed (LED off), false = active (LED on)
- Render `led.on_image` when active, `led.off_image` when bypassed
- Click on footswitch area → `fx_pedal_set_bypass(engine, id, !current)`

## Amp Panel Layout

Same concept but for amps:

```c
typedef struct {
    const char *panel_image;     /* amp panel body (knobless) */
    int         num_knobs;
    struct {
        float   x, y;
        float   size;
        int     param_index;     /* FX_AMP_PARAM_GAIN, etc. */
        const char *knob_style;
    } knobs[12];
    struct {
        float   x, y;
        const char *image;       /* pilot light, always on */
    } pilot_light;
} fx_amp_layout_t;
```

## Asset Requirements

### Knobless Body Images
- Each pedal type needs a version WITHOUT knobs rendered
- Option A: Generate new "blank" pedal images with empty knob holes
- Option B: Use the current images and overlay knob sprites on top (may look layered)
- **Recommended: Option A** — generate dedicated knobless bodies

### Knob Sprites
- Already generated: 5 styles (chicken cream/brown, skirted black, dome silver, pointer black)
- Each needs transparent background (done via rembg)
- Knob sprite is a single position — rotation is done in the shader/draw call

### LED Sprites
- Already generated: 7 variants (red/green/amber/blue, on/off states)
- Composited at the layout-defined position

## Signal Chain Visual

```
[Input] ──cable──> [Pedal 1] ──cable──> [Pedal 2] ──cable──> [Amp] ──cable──> [Cab] ──> [Output]
                    LED on              LED off (bypassed)
                    knobs active        knobs dimmed
```

- Cables rendered between pedals using cable_run assets or procedural bezier curves
- Bypassed pedals: dim the body image (multiply alpha by 0.5), show LED off
- Active pedals: full brightness, LED on, knob arcs lit
