"""Turns the source images in assets/sprites/ into game characters.

    python tools/sprites.py

Each image becomes a 64x64 character in assets/characters/<id>.png: empty
space trimmed, a plain background (like a white page) made transparent, and
the character scaled to fit, feet on the bottom row, centered. Images that
already fit (pixel art drawn at 64x64) are copied pixel for pixel, never
resampled. assets/characters/characters.json lists the characters by id; ids
are permanent: re-running keeps them and gives new images the next free id.
The source images are never modified.
"""
import json
from collections import deque
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "assets" / "sprites"
OUT = ROOT / "assets" / "characters"
MANIFEST = OUT / "characters.json"
SIZE = 64  # the game's character canvas: drawn so 64 px = the fighter's height (godot/main.gd)


def clear_background(im):
    """If the image has no transparency but a uniform light border (a drawing
    on white paper), make the background connected to the border transparent."""
    px = im.load()
    w, h = im.size
    border = [px[x, y] for x in (0, w - 1) for y in range(h)] + [px[x, y] for x in range(w) for y in (0, h - 1)]
    if all(p[3] == 0 for p in border):
        return im  # already has a transparent background
    corner = px[0, 0]
    if min(corner[:3]) < 200:
        return im  # not a light background: leave it alone

    def bg(p):  # light like the corner, or a faint haze (some exports leave a ~10% alpha white page)
        return p[3] > 0 and (p[3] < 64 or all(abs(p[c] - corner[c]) < 40 for c in range(3)))

    seen = set()
    todo = deque((x, y) for x in range(w) for y in (0, h - 1)) + deque((x, y) for x in (0, w - 1) for y in range(h))
    while todo:  # flood fill from the border, so white inside the drawing stays
        x, y = todo.popleft()
        if (x, y) in seen or not (0 <= x < w and 0 <= y < h) or not bg(px[x, y]):
            continue
        seen.add((x, y))
        px[x, y] = (0, 0, 0, 0)
        todo.extend(((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)))
    return im


def make_character(path):
    im = clear_background(Image.open(path).convert("RGBA"))
    im = im.crop(im.split()[-1].getbbox())  # trim empty space
    w, h = im.size
    if w > SIZE or h > SIZE:  # too big: scale down to fit, keeping the proportions
        k = min(SIZE / w, SIZE / h)
        im = im.resize((max(1, round(w * k)), max(1, round(h * k))), Image.LANCZOS)
    canvas = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
    canvas.paste(im, ((SIZE - im.width) // 2, SIZE - im.height))  # centered, feet on the bottom row
    return canvas, im.size


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    chars = json.loads(MANIFEST.read_text()) if MANIFEST.exists() else {}
    by_source = {c["source"]: cid for cid, c in chars.items()}
    for path in sorted(SRC.glob("*.png")):
        cid = by_source.get(path.name) or str(max((int(i) for i in chars), default=0) + 1)
        img, (bw, bh) = make_character(path)
        img.save(OUT / f"{cid}.png")
        old = chars.get(cid, {})
        chars[cid] = {
            "name": path.stem,
            "source": path.name,
            "file": f"{cid}.png",
            "faces": old.get("faces", "right"),  # which way the drawing faces; edit to "left" if needed
            "body_w": bw,  # the character's size inside the 64x64 canvas, in pixels
            "body_h": bh,
        }
        by_source[path.name] = cid
    MANIFEST.write_text(json.dumps(dict(sorted(chars.items(), key=lambda kv: int(kv[0]))), indent=2) + "\n")
    for cid, c in sorted(chars.items(), key=lambda kv: int(kv[0])):
        print(f"{cid:>3}  {c['name']:<16} {c['body_w']}x{c['body_h']} px, faces {c['faces']}")


if __name__ == "__main__":
    main()
