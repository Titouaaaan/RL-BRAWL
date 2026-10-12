"""Turns the source images in assets/sprites/ into game characters.

    python tools/sprites.py

One folder per character: assets/sprites/<Name>/. Inside, <animation>.png is a
horizontal strip of square frames (names in ANIMATIONS; anything else, like
walk.png or *_old.png backups, is ignored) and static.png is the still image.
Without static.png, the first idle frame is the still image.

- 64x64 art is kept exactly as drawn (61 px = the fighter's height).
- Bigger frames (e.g. 128x128 sheets) are kept at their resolution, shifted so
  the idle body is centered; "ref_h" (the idle body's height in pixels) tells
  the game how to scale them to the fighter's height.
- A static.png alone (any size) gets its plain background cleared, is trimmed
  and fitted to 64x64, feet on the bottom row.

Output: assets/characters/<id>/ + characters.json. Ids are permanent (matched
by name); each animation's "fps" (drawings per second, default DEFAULT_FPS) can
be edited in characters.json and is kept on re-runs. Sources are never modified.
"""
import json
from collections import deque
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "assets" / "sprites"
OUT = ROOT / "assets" / "characters"
MANIFEST = OUT / "characters.json"
DEFAULT_FPS = 7.5  # drawings per second for a new animation (8 game frames each)
REF = 61  # pixels of fighter height on a 64x64 canvas
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


def place(frames, px, dx):
    """Frames on px-by-px canvases, all shifted dx pixels right."""
    out = []
    for f in frames:
        c = Image.new("RGBA", (px, px), (0, 0, 0, 0))
        c.paste(f, (dx, 0))
        out.append(c)
    return out


def bbox(frames):
    boxes = [f.split()[-1].getbbox() for f in frames if f.split()[-1].getbbox()]
    return min(b[0] for b in boxes), min(b[1] for b in boxes), max(b[2] for b in boxes), max(b[3] for b in boxes)


def main():
    chars = json.loads(MANIFEST.read_text()) if MANIFEST.exists() else {}
    ids = {c["name"]: cid for cid, c in chars.items()}
    for d in sorted(p for p in SRC.iterdir() if p.is_dir()):
        strips = {p.stem: load_strip(p) for p in sorted(d.glob("*.png")) if p.stem in ANIMATIONS}
        static = clear_background(Image.open(d / "static.png").convert("RGBA")) if (d / "static.png").exists() else None
        if not strips and not static:
            print(f"skipped {d.name}: no static.png or animation")
            continue
        cid = ids.get(d.name) or str(max(map(int, chars), default=0) + 1)
        ids[d.name] = cid
        if strips:
            px = next(iter(strips.values()))[0].width
            l, t, r, b = bbox(strips.get("idle") or next(iter(strips.values())))
            dx, ref = (0, REF) if px == SIZE else (px // 2 - (l + r) // 2, b - t)
            strips = {a: place(fr, px, dx) for a, fr in strips.items()}
            still = place([static], px, dx)[0] if static and static.size == (px, px) else (strips.get("idle") or next(iter(strips.values())))[0]
        else:
            px, ref = SIZE, REF
            [still], _, _ = fit_frames([static])
        out = OUT / cid
        out.mkdir(parents=True, exist_ok=True)
        still.save(out / "static.png")
        old = chars.get(cid, {})
        anims = {}
        for a, frames in strips.items():
            strip = Image.new("RGBA", (px * len(frames), px), (0, 0, 0, 0))
            for i, f in enumerate(frames):
                strip.paste(f, (i * px, 0))
            strip.save(out / f"{a}.png")
            anims[a] = {"file": f"{cid}/{a}.png", "frames": len(frames),
                        "fps": old.get("anims", {}).get(a, {}).get("fps", DEFAULT_FPS)}
        l, t, r, b = bbox([still])
        chars[cid] = {"name": d.name, "file": f"{cid}/static.png",
                      "faces": old.get("faces", "right"),  # which way the drawing faces; edit to "left" if needed
                      "px": px, "ref_h": ref, "body_w": r - l, "body_h": b - t, "anims": anims}

    MANIFEST.write_text(json.dumps(dict(sorted(chars.items(), key=lambda kv: int(kv[0]))), indent=2) + "\n")
    for cid, c in sorted(chars.items(), key=lambda kv: int(kv[0])):
        anims = ", ".join(f"{a} ({v['frames']})" for a, v in c["anims"].items())
        print(f"{cid:>3}  {c['name']:<14} {c['px']} px canvas, faces {c['faces']}" + (f"; {anims}" if anims else ""))


if __name__ == "__main__":
    main()
