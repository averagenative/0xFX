#include "fx_theme.h"
#include "imgui_internal.h"
#include <math.h>

/* ── Palette table ────────────────────────────────────────────── */

static const fx_theme_t THEMES[FX_THEME_COUNT] = {

/* 0. GRIME_DARK — original "worn grime" default. Dark brown base, warm
 *                 amber accent, rust-tinted dirt. */
{
    "Worn Grime",
    "Warm dark wood + amber accents",
    /* bg              */ ImVec4(0.08f, 0.07f, 0.06f, 1.0f),
    /* panel           */ ImVec4(0.10f, 0.09f, 0.08f, 1.0f),
    /* popup           */ ImVec4(0.12f, 0.10f, 0.09f, 0.95f),
    /* frame           */ ImVec4(0.14f, 0.12f, 0.10f, 1.0f),
    /* frame_hover     */ ImVec4(0.20f, 0.17f, 0.14f, 1.0f),
    /* frame_active    */ ImVec4(0.25f, 0.20f, 0.15f, 1.0f),
    /* title_bg        */ ImVec4(0.06f, 0.05f, 0.04f, 1.0f),
    /* title_bg_active */ ImVec4(0.12f, 0.10f, 0.08f, 1.0f),
    /* menu_bar_bg     */ ImVec4(0.10f, 0.08f, 0.07f, 1.0f),
    /* header          */ ImVec4(0.18f, 0.15f, 0.12f, 1.0f),
    /* header_hover    */ ImVec4(0.30f, 0.24f, 0.18f, 1.0f),
    /* header_active   */ ImVec4(0.40f, 0.30f, 0.20f, 1.0f),
    /* text            */ ImVec4(0.85f, 0.80f, 0.72f, 1.0f),
    /* text_dim        */ ImVec4(0.45f, 0.40f, 0.35f, 1.0f),
    /* accent          */ ImVec4(0.60f, 0.40f, 0.15f, 1.0f),
    /* accent_hover    */ ImVec4(0.80f, 0.55f, 0.15f, 1.0f),
    /* accent_active   */ ImVec4(0.90f, 0.65f, 0.20f, 1.0f),
    /* accent_glow     */ ImVec4(0.90f, 0.65f, 0.20f, 1.0f),
    /* border          */ ImVec4(0.25f, 0.22f, 0.18f, 0.5f),
    /* separator       */ ImVec4(0.25f, 0.22f, 0.18f, 0.5f),
    /* dirt_inner      */ ImVec4(0.06f, 0.05f, 0.04f, 0.85f),
    /* dirt_outer      */ ImVec4(0.06f, 0.05f, 0.04f, 0.0f),
    /* dirt_radius     */ 220.0f,
},

/* 1. HACKER_GREEN — phosphor green on near-black. Dirt is deep forest +
 *                   burn-in smudge at corners. */
{
    "Hacker Green",
    "Phosphor-green CRT terminal",
    ImVec4(0.025f, 0.035f, 0.025f, 1.0f),
    ImVec4(0.04f,  0.06f,  0.04f,  1.0f),
    ImVec4(0.05f,  0.08f,  0.05f,  0.95f),
    ImVec4(0.06f,  0.10f,  0.06f,  1.0f),
    ImVec4(0.10f,  0.18f,  0.10f,  1.0f),
    ImVec4(0.15f,  0.28f,  0.15f,  1.0f),
    ImVec4(0.02f,  0.04f,  0.02f,  1.0f),
    ImVec4(0.05f,  0.10f,  0.05f,  1.0f),
    ImVec4(0.04f,  0.06f,  0.04f,  1.0f),
    ImVec4(0.08f,  0.16f,  0.08f,  1.0f),
    ImVec4(0.15f,  0.32f,  0.15f,  1.0f),
    ImVec4(0.25f,  0.50f,  0.25f,  1.0f),
    ImVec4(0.55f,  0.90f,  0.55f,  1.0f),
    ImVec4(0.25f,  0.45f,  0.25f,  1.0f),
    ImVec4(0.15f,  0.55f,  0.25f,  1.0f),
    ImVec4(0.25f,  0.80f,  0.35f,  1.0f),
    ImVec4(0.35f,  0.95f,  0.45f,  1.0f),
    ImVec4(0.30f,  1.00f,  0.40f,  1.0f),
    ImVec4(0.15f,  0.30f,  0.15f,  0.6f),
    ImVec4(0.15f,  0.30f,  0.15f,  0.6f),
    ImVec4(0.05f,  0.14f,  0.05f,  0.90f),
    ImVec4(0.05f,  0.14f,  0.05f,  0.0f),
    260.0f,
},

/* 2. CYBERPUNK_NEON — magenta + cyan on dark rust. Corner dirt is
 *                     oxblood/purple smog. */
{
    "Cyberpunk Neon",
    "Magenta + cyan neon over rusted chrome",
    ImVec4(0.045f, 0.030f, 0.055f, 1.0f),
    ImVec4(0.06f,  0.04f,  0.07f,  1.0f),
    ImVec4(0.08f,  0.05f,  0.09f,  0.95f),
    ImVec4(0.10f,  0.06f,  0.12f,  1.0f),
    ImVec4(0.20f,  0.10f,  0.22f,  1.0f),
    ImVec4(0.30f,  0.15f,  0.30f,  1.0f),
    ImVec4(0.04f,  0.02f,  0.05f,  1.0f),
    ImVec4(0.10f,  0.06f,  0.12f,  1.0f),
    ImVec4(0.06f,  0.04f,  0.07f,  1.0f),
    ImVec4(0.12f,  0.07f,  0.14f,  1.0f),
    ImVec4(0.35f,  0.12f,  0.35f,  1.0f),
    ImVec4(0.55f,  0.18f,  0.55f,  1.0f),
    ImVec4(0.95f,  0.90f,  1.00f,  1.0f),
    ImVec4(0.55f,  0.50f,  0.65f,  1.0f),
    ImVec4(0.60f,  0.10f,  0.55f,  1.0f),
    ImVec4(0.90f,  0.15f,  0.75f,  1.0f),
    ImVec4(1.00f,  0.20f,  0.85f,  1.0f),
    ImVec4(0.20f,  0.95f,  1.00f,  1.0f),
    ImVec4(0.40f,  0.20f,  0.45f,  0.6f),
    ImVec4(0.40f,  0.20f,  0.45f,  0.6f),
    ImVec4(0.12f,  0.02f,  0.10f,  0.85f),
    ImVec4(0.12f,  0.02f,  0.10f,  0.0f),
    280.0f,
},

/* 3. STUDIO_PALE — dark walnut with cream accents, low-contrast. Still
 *                  dark overall so pedal/amp bodies still read, but the
 *                  accent palette is creams and beiges. */
{
    "Studio Pale",
    "Dark walnut + pale cream accents",
    ImVec4(0.18f,  0.16f,  0.14f,  1.0f),
    ImVec4(0.22f,  0.20f,  0.17f,  1.0f),
    ImVec4(0.25f,  0.22f,  0.19f,  0.95f),
    ImVec4(0.28f,  0.25f,  0.21f,  1.0f),
    ImVec4(0.36f,  0.32f,  0.27f,  1.0f),
    ImVec4(0.45f,  0.40f,  0.33f,  1.0f),
    ImVec4(0.15f,  0.13f,  0.11f,  1.0f),
    ImVec4(0.22f,  0.20f,  0.17f,  1.0f),
    ImVec4(0.20f,  0.18f,  0.15f,  1.0f),
    ImVec4(0.32f,  0.28f,  0.23f,  1.0f),
    ImVec4(0.45f,  0.40f,  0.32f,  1.0f),
    ImVec4(0.55f,  0.48f,  0.38f,  1.0f),
    ImVec4(0.95f,  0.92f,  0.85f,  1.0f),
    ImVec4(0.60f,  0.55f,  0.48f,  1.0f),
    ImVec4(0.55f,  0.48f,  0.36f,  1.0f),
    ImVec4(0.75f,  0.65f,  0.48f,  1.0f),
    ImVec4(0.90f,  0.78f,  0.58f,  1.0f),
    ImVec4(1.00f,  0.92f,  0.75f,  1.0f),
    ImVec4(0.50f,  0.44f,  0.36f,  0.5f),
    ImVec4(0.50f,  0.44f,  0.36f,  0.5f),
    ImVec4(0.10f,  0.07f,  0.05f,  0.50f),
    ImVec4(0.10f,  0.07f,  0.05f,  0.0f),
    180.0f,
},

/* 4. BLUEPRINT — deep indigo with ice-blue accents, technical drawing
 *                feel. Dirt is a deeper navy at the corners. */
{
    "Blueprint",
    "Deep indigo with ice-blue technical-drawing accents",
    ImVec4(0.04f,  0.06f,  0.11f,  1.0f),
    ImVec4(0.06f,  0.09f,  0.15f,  1.0f),
    ImVec4(0.08f,  0.11f,  0.18f,  0.95f),
    ImVec4(0.09f,  0.13f,  0.20f,  1.0f),
    ImVec4(0.14f,  0.20f,  0.30f,  1.0f),
    ImVec4(0.20f,  0.28f,  0.42f,  1.0f),
    ImVec4(0.03f,  0.05f,  0.09f,  1.0f),
    ImVec4(0.07f,  0.10f,  0.16f,  1.0f),
    ImVec4(0.05f,  0.07f,  0.12f,  1.0f),
    ImVec4(0.10f,  0.15f,  0.22f,  1.0f),
    ImVec4(0.20f,  0.30f,  0.48f,  1.0f),
    ImVec4(0.30f,  0.45f,  0.65f,  1.0f),
    ImVec4(0.85f,  0.92f,  1.00f,  1.0f),
    ImVec4(0.45f,  0.55f,  0.70f,  1.0f),
    ImVec4(0.20f,  0.40f,  0.65f,  1.0f),
    ImVec4(0.35f,  0.65f,  0.90f,  1.0f),
    ImVec4(0.55f,  0.85f,  1.00f,  1.0f),
    ImVec4(0.65f,  0.90f,  1.00f,  1.0f),
    ImVec4(0.25f,  0.35f,  0.50f,  0.55f),
    ImVec4(0.25f,  0.35f,  0.50f,  0.55f),
    ImVec4(0.02f,  0.03f,  0.08f,  0.80f),
    ImVec4(0.02f,  0.03f,  0.08f,  0.0f),
    240.0f,
},

/* 5. NATURE_DIRT — earthy forest palette. Mossy green-gold accents,
 *                  rich soil-brown dirt in corners. */
{
    "Nature Dirt",
    "Forest + earthy soil — mossy accents",
    ImVec4(0.07f,  0.08f,  0.055f, 1.0f),
    ImVec4(0.09f,  0.10f,  0.07f,  1.0f),
    ImVec4(0.11f,  0.12f,  0.08f,  0.95f),
    ImVec4(0.13f,  0.14f,  0.10f,  1.0f),
    ImVec4(0.20f,  0.22f,  0.14f,  1.0f),
    ImVec4(0.28f,  0.30f,  0.18f,  1.0f),
    ImVec4(0.05f,  0.06f,  0.04f,  1.0f),
    ImVec4(0.11f,  0.12f,  0.08f,  1.0f),
    ImVec4(0.08f,  0.09f,  0.06f,  1.0f),
    ImVec4(0.15f,  0.16f,  0.11f,  1.0f),
    ImVec4(0.28f,  0.30f,  0.18f,  1.0f),
    ImVec4(0.40f,  0.42f,  0.24f,  1.0f),
    ImVec4(0.88f,  0.84f,  0.70f,  1.0f),
    ImVec4(0.50f,  0.48f,  0.38f,  1.0f),
    ImVec4(0.45f,  0.48f,  0.22f,  1.0f),
    ImVec4(0.60f,  0.65f,  0.30f,  1.0f),
    ImVec4(0.75f,  0.80f,  0.38f,  1.0f),
    ImVec4(0.85f,  0.90f,  0.45f,  1.0f),
    ImVec4(0.30f,  0.32f,  0.20f,  0.55f),
    ImVec4(0.30f,  0.32f,  0.20f,  0.55f),
    ImVec4(0.14f,  0.09f,  0.04f,  0.85f),  /* rich dark soil */
    ImVec4(0.14f,  0.09f,  0.04f,  0.0f),
    240.0f,
},

/* 6. VAPORWAVE — 80s Miami sunset. Hot pink + cyan on deep purple-indigo,
 *                teal corner haze. */
{
    "Vaporwave",
    "Hot pink + cyan on purple sunset",
    ImVec4(0.07f,  0.04f,  0.12f,  1.0f),
    ImVec4(0.10f,  0.05f,  0.17f,  1.0f),
    ImVec4(0.12f,  0.06f,  0.20f,  0.95f),
    ImVec4(0.14f,  0.07f,  0.22f,  1.0f),
    ImVec4(0.22f,  0.11f,  0.32f,  1.0f),
    ImVec4(0.32f,  0.16f,  0.46f,  1.0f),
    ImVec4(0.05f,  0.03f,  0.10f,  1.0f),
    ImVec4(0.12f,  0.06f,  0.20f,  1.0f),
    ImVec4(0.08f,  0.04f,  0.14f,  1.0f),
    ImVec4(0.18f,  0.09f,  0.28f,  1.0f),
    ImVec4(0.40f,  0.18f,  0.55f,  1.0f),
    ImVec4(0.60f,  0.24f,  0.78f,  1.0f),
    ImVec4(0.98f,  0.85f,  1.00f,  1.0f),
    ImVec4(0.58f,  0.48f,  0.75f,  1.0f),
    ImVec4(0.95f,  0.25f,  0.65f,  1.0f),
    ImVec4(1.00f,  0.40f,  0.80f,  1.0f),
    ImVec4(0.25f,  0.95f,  0.98f,  1.0f),
    ImVec4(0.50f,  1.00f,  1.00f,  1.0f),
    ImVec4(0.50f,  0.22f,  0.60f,  0.55f),
    ImVec4(0.50f,  0.22f,  0.60f,  0.55f),
    ImVec4(0.10f,  0.30f,  0.40f,  0.80f),  /* teal haze */
    ImVec4(0.10f,  0.30f,  0.40f,  0.0f),
    260.0f,
},

/* 7. TOXIC_SLIME — radioactive hazard. Lime-yellow on asphalt black with
 *                  acid-green glow and sickly yellow corner smog. */
{
    "Toxic Slime",
    "Radioactive lime + hazard yellow on asphalt",
    ImVec4(0.04f,  0.05f,  0.02f,  1.0f),
    ImVec4(0.06f,  0.07f,  0.03f,  1.0f),
    ImVec4(0.08f,  0.09f,  0.04f,  0.95f),
    ImVec4(0.10f,  0.11f,  0.05f,  1.0f),
    ImVec4(0.18f,  0.22f,  0.08f,  1.0f),
    ImVec4(0.28f,  0.34f,  0.12f,  1.0f),
    ImVec4(0.03f,  0.04f,  0.02f,  1.0f),
    ImVec4(0.08f,  0.09f,  0.04f,  1.0f),
    ImVec4(0.06f,  0.07f,  0.03f,  1.0f),
    ImVec4(0.12f,  0.15f,  0.06f,  1.0f),
    ImVec4(0.25f,  0.32f,  0.10f,  1.0f),
    ImVec4(0.40f,  0.50f,  0.15f,  1.0f),
    ImVec4(0.90f,  1.00f,  0.60f,  1.0f),
    ImVec4(0.55f,  0.60f,  0.35f,  1.0f),
    ImVec4(0.55f,  0.80f,  0.05f,  1.0f),
    ImVec4(0.75f,  1.00f,  0.10f,  1.0f),
    ImVec4(0.90f,  1.00f,  0.20f,  1.0f),
    ImVec4(0.80f,  1.00f,  0.30f,  1.0f),
    ImVec4(0.35f,  0.45f,  0.12f,  0.6f),
    ImVec4(0.35f,  0.45f,  0.12f,  0.6f),
    ImVec4(0.14f,  0.20f,  0.02f,  0.85f),  /* sickly green-yellow smog */
    ImVec4(0.14f,  0.20f,  0.02f,  0.0f),
    280.0f,
},

/* 8. BLOOD_MOON — gothic horror. Deep crimson accents on near-black with
 *                 wet-blood corner pools. */
{
    "Blood Moon",
    "Deep crimson on obsidian — gothic horror",
    ImVec4(0.05f,  0.02f,  0.02f,  1.0f),
    ImVec4(0.08f,  0.03f,  0.03f,  1.0f),
    ImVec4(0.10f,  0.04f,  0.04f,  0.95f),
    ImVec4(0.12f,  0.04f,  0.04f,  1.0f),
    ImVec4(0.22f,  0.06f,  0.06f,  1.0f),
    ImVec4(0.32f,  0.08f,  0.08f,  1.0f),
    ImVec4(0.04f,  0.01f,  0.01f,  1.0f),
    ImVec4(0.10f,  0.03f,  0.03f,  1.0f),
    ImVec4(0.07f,  0.02f,  0.02f,  1.0f),
    ImVec4(0.15f,  0.05f,  0.05f,  1.0f),
    ImVec4(0.30f,  0.08f,  0.08f,  1.0f),
    ImVec4(0.45f,  0.10f,  0.10f,  1.0f),
    ImVec4(0.92f,  0.82f,  0.78f,  1.0f),
    ImVec4(0.55f,  0.38f,  0.35f,  1.0f),
    ImVec4(0.55f,  0.05f,  0.08f,  1.0f),
    ImVec4(0.75f,  0.08f,  0.10f,  1.0f),
    ImVec4(0.90f,  0.12f,  0.15f,  1.0f),
    ImVec4(1.00f,  0.25f,  0.20f,  1.0f),
    ImVec4(0.35f,  0.08f,  0.08f,  0.6f),
    ImVec4(0.35f,  0.08f,  0.08f,  0.6f),
    ImVec4(0.20f,  0.02f,  0.02f,  0.90f),  /* pooled blood */
    ImVec4(0.20f,  0.02f,  0.02f,  0.0f),
    300.0f,
},

/* 9. AMBER_CRT — monochrome vintage terminal. Warm amber phosphor on near-
 *                black with burn-in at the corners. */
{
    "Amber CRT",
    "Monochrome amber phosphor — vintage terminal",
    ImVec4(0.04f,  0.03f,  0.015f, 1.0f),
    ImVec4(0.06f,  0.04f,  0.02f,  1.0f),
    ImVec4(0.08f,  0.05f,  0.025f, 0.95f),
    ImVec4(0.10f,  0.07f,  0.03f,  1.0f),
    ImVec4(0.18f,  0.12f,  0.05f,  1.0f),
    ImVec4(0.28f,  0.18f,  0.07f,  1.0f),
    ImVec4(0.03f,  0.02f,  0.01f,  1.0f),
    ImVec4(0.08f,  0.05f,  0.025f, 1.0f),
    ImVec4(0.06f,  0.04f,  0.02f,  1.0f),
    ImVec4(0.12f,  0.08f,  0.03f,  1.0f),
    ImVec4(0.24f,  0.16f,  0.06f,  1.0f),
    ImVec4(0.36f,  0.24f,  0.09f,  1.0f),
    ImVec4(1.00f,  0.70f,  0.22f,  1.0f),   /* amber text */
    ImVec4(0.55f,  0.38f,  0.12f,  1.0f),
    ImVec4(0.75f,  0.45f,  0.08f,  1.0f),
    ImVec4(0.95f,  0.60f,  0.15f,  1.0f),
    ImVec4(1.00f,  0.75f,  0.25f,  1.0f),
    ImVec4(1.00f,  0.80f,  0.35f,  1.0f),
    ImVec4(0.40f,  0.25f,  0.08f,  0.6f),
    ImVec4(0.40f,  0.25f,  0.08f,  0.6f),
    ImVec4(0.18f,  0.10f,  0.02f,  0.85f),  /* screen burn-in */
    ImVec4(0.18f,  0.10f,  0.02f,  0.0f),
    280.0f,
},

/* 10. MOLTEN_FORGE — glowing hot steel inside a blacksmith's shop. Orange
 *                    and red-orange highlights on dark iron, fire-pool
 *                    corners. */
{
    "Molten Forge",
    "Glowing hot iron + coal dust",
    ImVec4(0.06f,  0.04f,  0.03f,  1.0f),
    ImVec4(0.08f,  0.05f,  0.04f,  1.0f),
    ImVec4(0.10f,  0.06f,  0.04f,  0.95f),
    ImVec4(0.12f,  0.07f,  0.05f,  1.0f),
    ImVec4(0.22f,  0.11f,  0.06f,  1.0f),
    ImVec4(0.34f,  0.15f,  0.07f,  1.0f),
    ImVec4(0.04f,  0.03f,  0.02f,  1.0f),
    ImVec4(0.10f,  0.06f,  0.04f,  1.0f),
    ImVec4(0.07f,  0.04f,  0.03f,  1.0f),
    ImVec4(0.14f,  0.08f,  0.05f,  1.0f),
    ImVec4(0.30f,  0.13f,  0.06f,  1.0f),
    ImVec4(0.48f,  0.18f,  0.08f,  1.0f),
    ImVec4(0.98f,  0.82f,  0.58f,  1.0f),
    ImVec4(0.60f,  0.45f,  0.32f,  1.0f),
    ImVec4(0.85f,  0.28f,  0.05f,  1.0f),   /* orange-red iron */
    ImVec4(1.00f,  0.45f,  0.08f,  1.0f),
    ImVec4(1.00f,  0.60f,  0.15f,  1.0f),
    ImVec4(1.00f,  0.70f,  0.25f,  1.0f),
    ImVec4(0.45f,  0.18f,  0.06f,  0.6f),
    ImVec4(0.45f,  0.18f,  0.06f,  0.6f),
    ImVec4(0.35f,  0.08f,  0.02f,  0.88f),  /* ember glow */
    ImVec4(0.35f,  0.08f,  0.02f,  0.0f),
    300.0f,
},

};

/* ── Public lookups ───────────────────────────────────────────── */

const fx_theme_t *fx_theme_get(fx_theme_id_t id) {
    if (id < 0 || id >= FX_THEME_COUNT) id = FX_THEME_GRIME_DARK;
    return &THEMES[id];
}

const char *fx_theme_name(fx_theme_id_t id) {
    return fx_theme_get(id)->name;
}

/* ── Apply palette to ImGui style ─────────────────────────────── */

void fx_theme_apply(ImGuiStyle &style, fx_theme_id_t id) {
    const fx_theme_t *t = fx_theme_get(id);
    ImVec4 *c = style.Colors;

    c[ImGuiCol_WindowBg]         = t->bg;
    c[ImGuiCol_ChildBg]          = t->panel;
    c[ImGuiCol_PopupBg]          = t->popup;
    c[ImGuiCol_Border]           = t->border;
    c[ImGuiCol_FrameBg]          = t->frame;
    c[ImGuiCol_FrameBgHovered]   = t->frame_hover;
    c[ImGuiCol_FrameBgActive]    = t->frame_active;
    c[ImGuiCol_TitleBg]          = t->title_bg;
    c[ImGuiCol_TitleBgActive]    = t->title_bg_active;
    c[ImGuiCol_MenuBarBg]        = t->menu_bar_bg;
    c[ImGuiCol_Header]           = t->header;
    c[ImGuiCol_HeaderHovered]    = t->header_hover;
    c[ImGuiCol_HeaderActive]     = t->header_active;
    c[ImGuiCol_Button]           = t->accent;
    c[ImGuiCol_ButtonHovered]    = t->accent_hover;
    c[ImGuiCol_ButtonActive]     = t->accent_active;
    c[ImGuiCol_CheckMark]        = t->accent_glow;
    c[ImGuiCol_SliderGrab]       = t->accent_hover;
    c[ImGuiCol_SliderGrabActive] = t->accent_active;
    c[ImGuiCol_Text]             = t->text;
    c[ImGuiCol_TextDisabled]     = t->text_dim;
    c[ImGuiCol_Tab]              = t->frame;
    c[ImGuiCol_TabHovered]       = t->header_hover;
    c[ImGuiCol_TabActive]        = t->header;
    c[ImGuiCol_Separator]        = t->separator;
}

/* ── Corner dirt vignettes ─────────────────────────────────────── */

/* Each corner is drawn as a quarter-disc of radial gradient. ImGui doesn't
 * expose radial gradients, so we approximate with a fan of triangles: a
 * single opaque vertex at the corner, and a ring of transparent vertices on
 * the quarter arc at `radius`. Cheap, one draw call per corner. */
static void draw_one_corner(ImDrawList *dl,
                             ImVec2 corner,
                             ImVec2 arc_center,
                             float radius,
                             ImU32 col_inner,
                             ImU32 col_outer,
                             float angle_start, float angle_end)
{
    const int segments = 28;
    dl->PathClear();
    ImDrawListFlags saved_flags = dl->Flags;
    dl->Flags &= ~ImDrawListFlags_AntiAliasedFill;

    /* Build vertex buffer: one inner vertex at the corner, arc of outer
     * vertices along the quarter circle. */
    const int vtx_count = segments + 2;
    dl->PrimReserve(segments * 3, vtx_count);
    ImDrawIdx base = (ImDrawIdx)dl->_VtxCurrentIdx;

    /* Center vertex — at the corner itself, opaque dirt color. */
    dl->PrimWriteVtx(corner, dl->_Data->TexUvWhitePixel, col_inner);

    for (int i = 0; i <= segments; i++) {
        float t = (float)i / (float)segments;
        float a = angle_start + (angle_end - angle_start) * t;
        ImVec2 p(arc_center.x + cosf(a) * radius,
                 arc_center.y + sinf(a) * radius);
        dl->PrimWriteVtx(p, dl->_Data->TexUvWhitePixel, col_outer);
    }

    for (int i = 0; i < segments; i++) {
        dl->PrimWriteIdx((ImDrawIdx)(base));
        dl->PrimWriteIdx((ImDrawIdx)(base + 1 + i));
        dl->PrimWriteIdx((ImDrawIdx)(base + 2 + i));
    }

    dl->Flags = saved_flags;
}

void fx_theme_draw_corner_dirt(ImDrawList *dl,
                                float x, float y, float w, float h,
                                fx_theme_id_t id) {
    const fx_theme_t *t = fx_theme_get(id);
    if (t->dirt_inner.w <= 0.001f || t->dirt_radius <= 0.0f) return;

    ImU32 c_in  = ImGui::ColorConvertFloat4ToU32(t->dirt_inner);
    ImU32 c_out = ImGui::ColorConvertFloat4ToU32(t->dirt_outer);
    float r = t->dirt_radius;

    /* Each corner: the arc is centered on the corner and sweeps 90° inward.
     * angles are in the normal ImGui coordinate system (y grows downward),
     * so top-left sweeps from 0 (right) to +π/2 (down). */
    const float PI2 = 1.5707963f;
    const float PI  = 3.1415927f;

    /* Top-left: fan from the corner sweeping right then down. */
    draw_one_corner(dl, ImVec2(x, y),        ImVec2(x, y),
                    r, c_in, c_out, 0.0f,      PI2);
    /* Top-right: sweep left then down. */
    draw_one_corner(dl, ImVec2(x + w, y),    ImVec2(x + w, y),
                    r, c_in, c_out, PI2,       PI);
    /* Bottom-right: sweep left then up. */
    draw_one_corner(dl, ImVec2(x + w, y + h),ImVec2(x + w, y + h),
                    r, c_in, c_out, PI,        PI + PI2);
    /* Bottom-left: sweep right then up. */
    draw_one_corner(dl, ImVec2(x, y + h),    ImVec2(x, y + h),
                    r, c_in, c_out, PI + PI2, 2.0f * PI);
}
