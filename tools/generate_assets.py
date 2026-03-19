#!/usr/bin/env python3
"""
0xFX Asset Generator — "Worn Grime" aesthetic

Uses OpenAI's image generation API (gpt-image-1 / dall-e-3).
Reads OPENAI_API_KEY from ../.env

Usage:
    python3 tools/generate_assets.py reference     # Generate style reference images
    python3 tools/generate_assets.py pedal jade_drive
    python3 tools/generate_assets.py pedal all
    python3 tools/generate_assets.py amp fullerton_clean
    python3 tools/generate_assets.py amp all
    python3 tools/generate_assets.py cab 1x12_open
    python3 tools/generate_assets.py texture pedalboard_surface
    python3 tools/generate_assets.py test           # Quick test run (1 pedal)
"""

import os
import sys
import json
import base64
import urllib.request
import urllib.error
from pathlib import Path

# ── Load API key from .env ──────────────────────────────────────

ROOT = Path(__file__).parent.parent
ENV_FILE = ROOT / ".env"

def load_env():
    env = {}
    if ENV_FILE.exists():
        for line in ENV_FILE.read_text().splitlines():
            line = line.strip()
            if line and not line.startswith('#') and '=' in line:
                k, v = line.split('=', 1)
                env[k.strip()] = v.strip()
    return env

ENV = load_env()
API_KEY = ENV.get("OPENAI_API_KEY")

if not API_KEY:
    print("ERROR: OPENAI_API_KEY not found in .env")
    sys.exit(1)

# ── Style constants ─────────────────────────────────────────────

STYLE_LOCK = (
    "dark moody side-lighting, warm amber tones, shallow depth of field, "
    "photorealistic product photography, matte finish, "
    "studio shot on dark worn surface, 0xfx worn-grime aesthetic"
)

# ── Pedal prompts ───────────────────────────────────────────────

PEDAL_DEFS = {
    "jade_drive": {
        "color": "forest green",
        "knobs": 3,
        "desc": "overdrive stomp pedal",
    },
    "gold_drive": {
        "color": "metallic gold with dark patina",
        "knobs": 3,
        "desc": "transparent overdrive stomp pedal",
    },
    "blues_grit": {
        "color": "worn blue-gray",
        "knobs": 3,
        "desc": "gritty overdrive stomp pedal",
    },
    "rodent": {
        "color": "matte black with white paint",
        "knobs": 3,
        "desc": "distortion stomp pedal",
    },
    "mammoth_fuzz": {
        "color": "deep purple with silver",
        "knobs": 3,
        "desc": "fuzz stomp pedal, large enclosure",
    },
    "round_fuzz": {
        "color": "charcoal gray",
        "knobs": 2,
        "desc": "compact fuzz stomp pedal, round enclosure shape",
    },
    "echo_delay": {
        "color": "sky blue",
        "knobs": 4,
        "desc": "digital delay stomp pedal",
    },
    "carbon_delay": {
        "color": "dark teal with orange accents",
        "knobs": 4,
        "desc": "analog delay stomp pedal",
    },
    "tape_machine": {
        "color": "cream and brown two-tone",
        "knobs": 5,
        "desc": "tape echo stomp pedal, vintage aesthetic",
    },
    "drip_verb": {
        "color": "dark blue",
        "knobs": 3,
        "desc": "spring reverb stomp pedal",
    },
    "hall_verb": {
        "color": "midnight blue with silver",
        "knobs": 3,
        "desc": "hall reverb stomp pedal",
    },
    "liquid_chorus": {
        "color": "coral pink",
        "knobs": 3,
        "desc": "chorus stomp pedal",
    },
    "phase_sweep": {
        "color": "burnt orange",
        "knobs": 4,
        "desc": "phaser stomp pedal",
    },
    "squeeze_box": {
        "color": "brushed silver aluminum",
        "knobs": 2,
        "desc": "compressor stomp pedal, minimal design",
    },
    "howl_wah": {
        "color": "chrome and matte black",
        "knobs": 2,
        "desc": "wah pedal with rocker treadle",
    },
    "noise_gate": {
        "color": "matte black",
        "knobs": 4,
        "desc": "noise gate utility stomp pedal",
    },
}

def pedal_prompt(name):
    d = PEDAL_DEFS[name]
    return (
        f"{d['color']} guitar effects {d['desc']}, die-cast aluminum enclosure, "
        f"{d['knobs']} small chicken-head knobs, single footswitch with rubber cap, "
        f"scuffed paint showing bare metal at edges and corners, "
        f"fingerprint smudges on surface, dust in knob crevices, "
        f"small scratches across faceplate, tiny LED indicator light, "
        f"top-down flat-lay view on dark pedalboard surface, {STYLE_LOCK}"
    )

# ── Amp panel prompts ───────────────────────────────────────────

AMP_DEFS = {
    "fullerton_clean": {
        "style": "classic American blackface-era",
        "covering": "black tolex",
        "faceplate": "silver anodized aluminum",
        "knobs": 7,
    },
    "brit_crunch": {
        "style": "classic British plexi-era",
        "covering": "dark green and gold tolex",
        "faceplate": "brushed gold aluminum",
        "knobs": 8,
    },
    "southwest_lead": {
        "style": "modern American high-gain",
        "covering": "black tolex with diamond pattern",
        "faceplate": "brushed aluminum industrial",
        "knobs": 8,
    },
    "essex_chime": {
        "style": "British chime-era with diamond grille",
        "covering": "dark brown and tan rexine",
        "faceplate": "copper-toned chrome",
        "knobs": 7,
    },
    "tweed_blues": {
        "style": "1950s American tweed-era",
        "covering": "worn lacquered tweed cloth",
        "faceplate": "brown marbled bakelite",
        "knobs": 6,
    },
}

def amp_prompt(name):
    d = AMP_DEFS[name]
    return (
        f"vintage guitar amplifier front control panel, {d['style']} aesthetic, "
        f"aged {d['covering']} covering with wear marks, "
        f"{d['knobs']} cream chicken-head knobs in a row, "
        f"printed white control labels slightly faded, chrome input jacks with patina, "
        f"pilot light glowing amber, scratched {d['faceplate']} faceplate, "
        f"wide landscape panel aspect ratio, straight-on front view, {STYLE_LOCK}"
    )

# ── Cabinet prompts ─────────────────────────────────────────────

CAB_DEFS = {
    "1x12_open": "compact 1x12 open-back guitar speaker cabinet, single speaker visible through dark grille cloth, worn tweed or tolex covering, no casters",
    "2x12_closed": "2x12 closed-back guitar speaker cabinet, two speakers behind dark grille cloth, black tolex covering, horizontal orientation",
    "4x12_straight": "4x12 straight guitar speaker cabinet, four speakers behind dark grille cloth, black tolex covering, metal corner protectors, caster wheels",
    "4x12_slant": "4x12 angled/slant guitar speaker cabinet, four speakers behind dark grille cloth, black tolex covering, angled top baffle, metal corner protectors",
    "direct_flat": "compact guitar DI box / direct recording unit, small metal enclosure, minimal controls, brushed aluminum",
}

def cab_prompt(name):
    return (
        f"{CAB_DEFS[name]}, road wear on corners, scuffed edges, "
        f"slightly dusty grille cloth, recessed handles with wear marks, "
        f"3/4 angle view showing front and side, {STYLE_LOCK}"
    )

# ── Texture prompts ─────────────────────────────────────────────

TEXTURE_DEFS = {
    "pedalboard_surface": "seamless tileable texture, black industrial carpet pedalboard surface, scuffed with adhesive residue marks and velcro patches, worn fiber",
    "metal_panel": "seamless tileable texture, brushed aluminum with dark patina, fingerprints, micro-scratches, aged industrial metal",
    "tolex_surface": "seamless tileable texture, black vinyl amp covering tolex, cracked at stress points, slight shine from wear, leather-like grain",
    "dark_wood": "seamless tileable texture, dark stained wood grain, aged and worn smooth, vintage guitar amp cabinet material",
}

def texture_prompt(name):
    return f"{TEXTURE_DEFS[name]}, dark moody lighting, high detail macro photography, {STYLE_LOCK}"

# ── Cable prompts ───────────────────────────────────────────────

CABLE_DEFS = {
    "patch_cable_short": {
        "desc": "short 6-inch guitar patch cable",
        "color": "black braided cloth",
        "shape": "right-angle pancake plugs on both ends, tightly coiled between pedals",
    },
    "patch_cable_medium": {
        "desc": "12-inch guitar patch cable",
        "color": "dark gray rubber",
        "shape": "right-angle plugs, gentle S-curve drape",
    },
    "instrument_cable": {
        "desc": "guitar instrument cable, 10-foot coiled",
        "color": "black with dark red braided cloth wrap",
        "shape": "straight plug on one end, right-angle on other, coiled spring shape",
    },
    "speaker_cable": {
        "desc": "heavy-gauge speaker cable between amp head and cabinet",
        "color": "thick black rubber jacket",
        "shape": "straight 1/4-inch plugs, thick stiff cable with slight curve",
    },
    "cable_bundle": {
        "desc": "tangled mess of 5-6 patch cables behind a pedalboard",
        "color": "mix of black, dark gray, and dark red cables",
        "shape": "cables overlapping and crossing, zip-tied in spots, slightly messy",
    },
}

def cable_prompt(name):
    d = CABLE_DEFS[name]
    return (
        f"{d['desc']}, {d['color']}, {d['shape']}, "
        f"worn connectors with patina on the metal jacks, "
        f"cable slightly scuffed from use, lying on dark pedalboard surface, "
        f"top-down view, {STYLE_LOCK}"
    )

# ── LED glow prompts ───────────────────────────────────────────

LED_DEFS = {
    "led_red_on": {
        "color": "bright red",
        "desc": "glowing intensely with soft red halo bloom",
    },
    "led_red_off": {
        "color": "dark red",
        "desc": "unlit, dark translucent red plastic dome, no glow",
    },
    "led_green_on": {
        "color": "bright green",
        "desc": "glowing intensely with soft green halo bloom",
    },
    "led_green_off": {
        "color": "dark green",
        "desc": "unlit, dark translucent green plastic dome, no glow",
    },
    "led_amber_on": {
        "color": "bright amber/orange",
        "desc": "glowing warmly with soft amber halo bloom, like a tube amp pilot light",
    },
    "led_amber_off": {
        "color": "dark amber",
        "desc": "unlit, dark translucent amber plastic dome, no glow",
    },
    "led_blue_on": {
        "color": "bright ice blue",
        "desc": "glowing intensely with soft blue halo bloom",
    },
}

def led_prompt(name):
    d = LED_DEFS[name]
    return (
        f"extreme close-up macro photograph of a single small {d['color']} LED indicator light "
        f"on a guitar effects pedal, {d['desc']}, "
        f"mounted in scuffed dark metal enclosure surface, "
        f"surrounding metal has scratches and grime, "
        f"black background, very shallow depth of field, {STYLE_LOCK}"
    )

# ── Reference prompts ───────────────────────────────────────────

REFERENCE_PROMPTS = [
    (
        "ref_01_pedal_aesthetic",
        "collection of 4 beat-up guitar effects pedals arranged on a dark pedalboard, "
        "top-down view, scuffed metal enclosures in forest green, dark blue, burnt orange, "
        "and matte black, worn paint showing bare metal at edges, dust and grime in crevices, "
        "small chicken-head knobs, LED indicators, patch cables visible, "
        f"{STYLE_LOCK}"
    ),
    (
        "ref_02_amp_aesthetic",
        "vintage guitar amplifier head sitting on top of a 4x12 speaker cabinet, "
        "aged black tolex with road wear, cream chicken-head knobs, "
        "glowing amber pilot light, scratched chrome faceplate, "
        "dusty grille cloth on cabinet, scuffed metal corners, backstage atmosphere, "
        f"{STYLE_LOCK}"
    ),
    (
        "ref_03_grime_closeup",
        "extreme close-up macro shot of a beat-up guitar pedal surface, "
        "scuffed green paint showing bare aluminum underneath, "
        "dust and grime packed around a small knob base, fingerprint smudges, "
        "tiny scratches, rubber footswitch cap worn smooth, "
        f"{STYLE_LOCK}"
    ),
]

# ── OpenAI API call ─────────────────────────────────────────────

def generate_image(prompt, output_path, size="1024x1024", quality="medium", model="gpt-image-1"):
    """Generate image via OpenAI API. Returns True on success."""
    print(f"  Generating: {output_path.name}")
    print(f"  Prompt: {prompt[:120]}...")

    body = json.dumps({
        "model": model,
        "prompt": prompt,
        "n": 1,
        "size": size,
        "quality": quality,
    }).encode()

    req = urllib.request.Request(
        "https://api.openai.com/v1/images/generations",
        data=body,
        headers={
            "Content-Type": "application/json",
            "Authorization": f"Bearer {API_KEY}",
        },
    )

    try:
        with urllib.request.urlopen(req, timeout=120) as resp:
            data = json.loads(resp.read())

        # gpt-image-1 returns b64_json, dall-e-3 returns url
        img_data = data["data"][0]
        if "b64_json" in img_data:
            img_bytes = base64.b64decode(img_data["b64_json"])
            output_path.write_bytes(img_bytes)
        elif "url" in img_data:
            img_url = img_data["url"]
            urllib.request.urlretrieve(img_url, str(output_path))
        else:
            print(f"  ERROR: unexpected response format")
            return False

        size_kb = output_path.stat().st_size / 1024
        print(f"  ✓ Saved: {output_path} ({size_kb:.0f} KB)")
        return True

    except urllib.error.HTTPError as e:
        body = e.read().decode()
        print(f"  ERROR {e.code}: {body[:300]}")
        return False
    except Exception as e:
        print(f"  ERROR: {e}")
        return False

# ── Commands ────────────────────────────────────────────────────

def cmd_test():
    """Quick test: generate 1 pedal to verify API works."""
    print("═══ Test Run: 1 pedal (jade_drive) ═══\n")
    out = ROOT / "resources" / "pedals" / "jade_drive_test.png"
    ok = generate_image(pedal_prompt("jade_drive"), out, quality="medium")
    if ok:
        print(f"\n✓ Test passed! Check {out}")
    else:
        print(f"\n✗ Test failed. Check API key and quota.")

def cmd_reference():
    """Generate 3 style reference images."""
    print("═══ Generating Style References ═══\n")
    for name, prompt in REFERENCE_PROMPTS:
        out = ROOT / "resources" / "reference" / f"{name}.png"
        generate_image(prompt, out, size="1024x1024", quality="medium")
        print()

def cmd_pedal(name):
    """Generate pedal asset(s)."""
    if name == "all":
        names = list(PEDAL_DEFS.keys())
    elif name in PEDAL_DEFS:
        names = [name]
    else:
        print(f"Unknown pedal: {name}")
        print(f"Available: {', '.join(PEDAL_DEFS.keys())}")
        return
    print(f"═══ Generating {len(names)} Pedal(s) ═══\n")
    for n in names:
        out = ROOT / "resources" / "pedals" / f"{n}.png"
        generate_image(pedal_prompt(n), out)
        print()

def cmd_amp(name):
    """Generate amp panel asset(s)."""
    if name == "all":
        names = list(AMP_DEFS.keys())
    elif name in AMP_DEFS:
        names = [name]
    else:
        print(f"Unknown amp: {name}")
        print(f"Available: {', '.join(AMP_DEFS.keys())}")
        return
    print(f"═══ Generating {len(names)} Amp Panel(s) ═══\n")
    for n in names:
        out = ROOT / "resources" / "amps" / f"{n}.png"
        generate_image(amp_prompt(n), out, size="1536x1024")
        print()

def cmd_cab(name):
    """Generate cabinet asset(s)."""
    if name == "all":
        names = list(CAB_DEFS.keys())
    elif name in CAB_DEFS:
        names = [name]
    else:
        print(f"Unknown cab: {name}")
        return
    print(f"═══ Generating {len(names)} Cabinet(s) ═══\n")
    for n in names:
        out = ROOT / "resources" / "cabs" / f"{n}.png"
        generate_image(cab_prompt(n), out)
        print()

def cmd_texture(name):
    """Generate texture asset(s)."""
    if name == "all":
        names = list(TEXTURE_DEFS.keys())
    elif name in TEXTURE_DEFS:
        names = [name]
    else:
        print(f"Unknown texture: {name}")
        return
    print(f"═══ Generating {len(names)} Texture(s) ═══\n")
    for n in names:
        out = ROOT / "resources" / "theme" / f"{n}.png"
        generate_image(texture_prompt(n), out)
        print()

# ── Main ────────────────────────────────────────────────────────

def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return

    cmd = sys.argv[1]
    arg = sys.argv[2] if len(sys.argv) > 2 else None

    if cmd == "test":
        cmd_test()
    elif cmd == "reference":
        cmd_reference()
    elif cmd == "pedal":
        if not arg: print("Usage: pedal <name|all>"); return
        cmd_pedal(arg)
    elif cmd == "amp":
        if not arg: print("Usage: amp <name|all>"); return
        cmd_amp(arg)
    elif cmd == "cab":
        if not arg: print("Usage: cab <name|all>"); return
        cmd_cab(arg)
    elif cmd == "texture":
        if not arg: print("Usage: texture <name|all>"); return
        cmd_texture(arg)
    else:
        print(f"Unknown command: {cmd}")
        print(__doc__)

if __name__ == "__main__":
    main()
