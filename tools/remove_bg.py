#!/usr/bin/env python3
"""
Remove backgrounds from generated assets, saving transparent PNGs.

Uses remove.bg API if REMOVEBG_API_KEY is set in .env (better quality).
Falls back to local rembg library otherwise.

Usage:
    python3 tools/remove_bg.py                    # Process all asset dirs
    python3 tools/remove_bg.py resources/pedals   # Process one dir
    python3 tools/remove_bg.py resources/pedals/jade_drive.png  # One file
"""
import sys
import urllib.request
import urllib.error
from pathlib import Path

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
REMOVEBG_KEY = ENV.get("REMOVEBG_API_KEY")

def remove_bg_api(src: Path, dst: Path) -> bool:
    """Use remove.bg API for high-quality background removal."""
    img_data = src.read_bytes()

    boundary = "----0xFXBoundary"
    body = (
        f"--{boundary}\r\n"
        f"Content-Disposition: form-data; name=\"image_file\"; filename=\"{src.name}\"\r\n"
        f"Content-Type: image/png\r\n\r\n"
    ).encode() + img_data + (
        f"\r\n--{boundary}\r\n"
        f"Content-Disposition: form-data; name=\"size\"\r\n\r\nauto\r\n"
        f"--{boundary}--\r\n"
    ).encode()

    req = urllib.request.Request(
        "https://api.remove.bg/v1.0/removebg",
        data=body,
        headers={
            "X-Api-Key": REMOVEBG_KEY,
            "Content-Type": f"multipart/form-data; boundary={boundary}",
        },
    )

    try:
        with urllib.request.urlopen(req, timeout=60) as resp:
            dst.write_bytes(resp.read())
        return True
    except urllib.error.HTTPError as e:
        err = e.read().decode()[:200]
        print(f"API error {e.code}: {err}")
        return False

def remove_bg_local(src: Path, dst: Path) -> bool:
    """Use local rembg library as fallback."""
    from rembg import remove
    from PIL import Image

    img = Image.open(src)
    result = remove(img)
    result.save(dst)
    return True

def process_file(src: Path):
    """Remove bg from src.png, save as src_nobg.png alongside it."""
    nobg = src.parent / f"{src.stem}_nobg.png"
    if nobg.exists():
        print(f"  skip (exists): {nobg.name}")
        return

    method = "remove.bg API" if REMOVEBG_KEY else "rembg (local)"
    print(f"  processing: {src.name} [{method}] ... ", end="", flush=True)

    if REMOVEBG_KEY:
        ok = remove_bg_api(src, nobg)
    else:
        ok = remove_bg_local(src, nobg)

    if ok:
        print(f"✓ → {nobg.name}")
    else:
        print(f"✗ FAILED")

def process_dir(d: Path):
    pngs = sorted(d.glob("*.png"))
    # Skip _nobg and _body files (bodies get their own nobg pass)
    pngs = [p for p in pngs if not p.stem.endswith("_nobg") and not p.stem.endswith("_test")]
    if not pngs:
        print(f"  no PNGs in {d}")
        return
    print(f"\n═══ {d} ({len(pngs)} files) ═══")
    for p in pngs:
        process_file(p)

def main():
    if REMOVEBG_KEY:
        print("Using remove.bg API (high quality)\n")
    else:
        print("Using rembg local (no REMOVEBG_API_KEY in .env)\n")

    root = ROOT / "resources"
    if len(sys.argv) > 1:
        target = Path(sys.argv[1])
        if target.is_file():
            process_file(target)
        elif target.is_dir():
            process_dir(target)
        else:
            print(f"Not found: {target}")
    else:
        for subdir in ["pedals", "amps", "cabs", "cables", "leds", "knobs", "logo", "icon"]:
            d = root / subdir
            if d.is_dir():
                process_dir(d)

if __name__ == "__main__":
    main()
