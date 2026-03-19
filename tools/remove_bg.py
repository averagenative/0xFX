#!/usr/bin/env python3
"""
Remove backgrounds from generated assets, saving transparent PNGs.

Usage:
    python3 tools/remove_bg.py                    # Process all asset dirs
    python3 tools/remove_bg.py resources/pedals   # Process one dir
    python3 tools/remove_bg.py resources/pedals/jade_drive.png  # One file
"""
import sys
from pathlib import Path

def process_file(src: Path):
    """Remove bg from src.png, save as src_nobg.png alongside it."""
    from rembg import remove
    from PIL import Image
    import io

    nobg = src.parent / f"{src.stem}_nobg.png"
    if nobg.exists():
        print(f"  skip (exists): {nobg.name}")
        return

    print(f"  processing: {src.name} ... ", end="", flush=True)
    img = Image.open(src)
    result = remove(img)
    result.save(nobg)
    print(f"✓ → {nobg.name}")

def process_dir(d: Path):
    pngs = sorted(d.glob("*.png"))
    # Skip _nobg files
    pngs = [p for p in pngs if not p.stem.endswith("_nobg")]
    if not pngs:
        print(f"  no PNGs in {d}")
        return
    print(f"\n═══ {d} ({len(pngs)} files) ═══")
    for p in pngs:
        process_file(p)

def main():
    root = Path(__file__).parent.parent / "resources"
    if len(sys.argv) > 1:
        target = Path(sys.argv[1])
        if target.is_file():
            process_file(target)
        elif target.is_dir():
            process_dir(target)
        else:
            print(f"Not found: {target}")
    else:
        for subdir in ["pedals", "amps", "cabs", "cables", "leds", "logo"]:
            d = root / subdir
            if d.is_dir():
                process_dir(d)

if __name__ == "__main__":
    main()
