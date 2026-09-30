"""Turns the source images in assets/sprites/ into game characters.

    python tools/sprites.py

Characters: each image <Name>.png becomes a 64x64 character in
assets/characters/<id>.png: a plain background (like a white page) made
transparent, empty space trimmed and the character scaled to fit, feet on the
bottom row, centered. Art already drawn at exactly 64x64 is kept pixel for
pixel where it was placed.

Animations: <Name>_<animation>.png (e.g. Titou_idle.png) is a horizontal strip
of square frames for character <Name>; see ANIMATIONS for the names the game
uses. Every frame of a strip gets the same crop, scale and position, so the
animation doesn't jitter. Saved as assets/characters/<id>_<animation>.png.

assets/characters/characters.json lists the characters by id; ids are
permanent: re-running keeps them and gives new characters the next free id.
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
SIZE = 64  # the game's character canvas: drawn so 61 px = the fighter's height (godot/main.gd)

# Animation names the game looks for (godot/main.gd, anim_for()). Attacks can
# also use a move name from data/moves (jab, heavy, up_air...).
ANIMATIONS = ["idle", "run", "jump", "fall", "hurt", "shield", "crouch", "attack",
              "jab", "jab2", "jab3", "forward_tilt", "up_tilt", "down_tilt", "dash_attack",
              "heavy", "up_heavy", "down_heavy", "air", "up_air", "down_air",
              "heavy_air", "heavy_up_air", "heavy_down_air", "grab"]


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


def fit_frames(frames):
    """Places frames (same-size RGBA images) on 64x64 canvases, all with the
    same crop, scale and offset. Returns (canvases, body width, body height)."""
    if frames[0].size == (SIZE, SIZE):  # drawn at the game's size: keep the artist's placement
        boxes = [f.split()[-1].getbbox() for f in frames if f.split()[-1].getbbox()]
        return list(frames), max(b[2] for b in boxes) - min(b[0] for b in boxes), \
            max(b[3] for b in boxes) - min(b[1] for b in boxes)
    boxes = [f.split()[-1].getbbox() for f in frames if f.split()[-1].getbbox()]
    box = (min(b[0] for b in boxes), min(b[1] for b in boxes), max(b[2] for b in boxes), max(b[3] for b in boxes))
    w, h = box[2] - box[0], box[3] - box[1]
    k = min(1.0, SIZE / w, SIZE / h)  # too big: scale down to fit, keeping the proportions
    nw, nh = max(1, round(w * k)), max(1, round(h * k))
    out = []
    for f in frames:
        f = f.crop(box)
        if k < 1.0:
            f = f.resize((nw, nh), Image.LANCZOS)
        canvas = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
        canvas.paste(f, ((SIZE - nw) // 2, SIZE - nh))  # centered, feet on the bottom row
        out.append(canvas)
    return out, nw, nh


def load_strip(path):
    """A horizontal strip of square frames -> list of frames."""
    im = Image.open(path).convert("RGBA")
    side = im.height
    n = max(1, im.width // side)
    return [clear_background(im.crop((i * side, 0, (i + 1) * side, side))) for i in range(n)]


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    chars = json.loads(MANIFEST.read_text()) if MANIFEST.exists() else {}
    by_source = {c["source"]: cid for cid, c in chars.items()}
    names = {p.stem for p in SRC.glob("*.png")}

    def animation_of(stem):  # "Titou_idle" -> ("Titou", "idle") if Titou.png exists
        for anim in ANIMATIONS:
            if stem.endswith("_" + anim) and stem[: -len(anim) - 1] in names:
                return stem[: -len(anim) - 1], anim
        return None

    # Characters first (animations need their ids).
    for path in sorted(SRC.glob("*.png")):
        if animation_of(path.stem):
            continue
        cid = by_source.get(path.name) or str(max((int(i) for i in chars), default=0) + 1)
        [img], bw, bh = fit_frames([clear_background(Image.open(path).convert("RGBA"))])
        img.save(OUT / f"{cid}.png")
        old = chars.get(cid, {})
        chars[cid] = {
            "name": path.stem,
            "source": path.name,
            "file": f"{cid}.png",
            "faces": old.get("faces", "right"),  # which way the drawing faces; edit to "left" if needed
            "body_w": bw,  # the character's size inside the 64x64 canvas, in pixels
            "body_h": bh,
            "anims": {},
        }
        by_source[path.name] = cid

    # Then animations.
    ids_by_name = {c["name"]: cid for cid, c in chars.items()}
    for path in sorted(SRC.glob("*.png")):
        found = animation_of(path.stem)
        if not found:
            continue
        name, anim = found
        cid = ids_by_name[name]
        frames, _, _ = fit_frames(load_strip(path))
        strip = Image.new("RGBA", (SIZE * len(frames), SIZE), (0, 0, 0, 0))
        for i, f in enumerate(frames):
            strip.paste(f, (i * SIZE, 0))
        strip.save(OUT / f"{cid}_{anim}.png")
        chars[cid]["anims"][anim] = {"file": f"{cid}_{anim}.png", "frames": len(frames)}

    MANIFEST.write_text(json.dumps(dict(sorted(chars.items(), key=lambda kv: int(kv[0]))), indent=2) + "\n")
    for cid, c in sorted(chars.items(), key=lambda kv: int(kv[0])):
        anims = ", ".join(f"{a} ({v['frames']} frames)" for a, v in c["anims"].items())
        print(f"{cid:>3}  {c['name']:<16} {c['body_w']}x{c['body_h']} px, faces {c['faces']}"
              + (f"; animations: {anims}" if anims else ""))


if __name__ == "__main__":
    main()
