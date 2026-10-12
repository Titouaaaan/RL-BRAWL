# Assets: characters, sprites and animations

Characters are **visuals only**: every character has the same hurtbox (the
2 x 3 box that can be hit) and the same moves. Pick them with `--p1 ID --p2 ID`
on any `play.bat` command.

## Folders

| Folder | What | Who writes it |
|---|---|---|
| `assets/sprites/<Name>/` | your source images, one folder per character: `static.png` and/or animation strips | you (tools never modify it) |
| `assets/maps/` | stage art: backgrounds (2304x1296) and 32 px tiles, used as they are (no tool) | you |
| `assets/characters/<id>/` | what the game loads: the same images processed, plus `characters.json` (ids, sizes, fps) | `python tools/sprites.py` |

`sprites/` is what you edit; `characters/` is generated from it (like a build
output) and is never edited by hand, except the `fps` and `faces` values in
`characters.json`.

After adding or changing anything in `assets/sprites/`, run
`python tools/sprites.py` (it prints the list of ids).

## Characters and ids

Each fighter can be drawn as any character (it's only the look: every
character plays exactly the same). Pick them with `--p1 ID --p2 ID`, for
playing or watching replays, in any order:

```
play.bat --p1 3 --p2 5
play.bat replay --p1 4 --p2 1
play.bat replay path\to\game.json --p2 5
```

Without them, P1 is character 1 and P2 is character 2.

| ID | Character |
|---|---|
| 1 | DinoSailor (blue) |
| 2 | DinoSailorP2 (red) |
| 3 | Car |
| 4 | Justin |
| 5 | OliverPip |
| 6 | Titou (animated idle and run) |
| 7 | Fighter (animated) |
| 8 | Samurai (animated) |
| 9 | Shinobi (animated) |

**Adding a character:** make a folder `assets/sprites/<Name>/` with a
`static.png` (any size) and/or animation strips (below), then run
`python tools/sprites.py`. A lone `static.png` gets its empty space trimmed, a
plain white page background removed and is fitted to 64x64 (feet at the
bottom). Without `static.png`, the first idle frame is the still image. The
tool saves `assets/characters/<id>/` and prints the new id. Ids
never change: new images get the next number. Your originals are not modified.
Draw characters facing **right** (or set `"faces": "left"` for that character
in `assets/characters/characters.json`).

## Animations

Put a horizontal strip of square frames in the character's folder, named
after the animation (for example `Titou/idle.png`: 8 frames of 64x64 side by
side, 512x64) and run `python tools/sprites.py`. Frames are kept exactly as
drawn: 64x64 art at 61 px = the fighter's height; bigger frames (the
Fighter/Samurai/Shinobi sheets are 128x128) are centered on the idle body and
scaled so the idle body's height = the fighter's height (`ref_h` in
`characters.json`). All strips of a character must have the same frame size.
The Fighter, Samurai and Shinobi sheets were renamed to these names:
Attack_1 -> `attack` (every attack without its own animation), Attack_2 -> `air`,
Attack_3 -> `heavy`. Their `walk` and `dead` strips are unused (the game has no
walk or KO animation state). Rename a file to retarget it, e.g. `heavy.png` -> `up_heavy.png`.
The game picks the animation from what the fighter is doing:

| Animation | Shown when |
|---|---|
| `idle` | standing (and during landing lag) |
| `run` | dashing / running |
| `jump`, `fall` | in the air, rising / falling (either one is used for both if only one exists) |
| `hurt` | in hitstun |
| `shield` | shielding |
| `crouch` | crouching |
| a move name (`jab`, `forward_tilt`, `heavy`, `up_air`, ...) | doing that attack; stretched over the whole move, so the last drawing ends with it |
| `attack` | any attack without its own animation |

Looping animations play at their `fps` (see [Animation timing](#animation-timing)).
Anything without an animation uses the static sprite. Other file names
(`walk.png`, backups like `idle_old.png`) are ignored by the tool.


## Sizes and the pixel scale

The game measures in *sim units*: the fighter's body is 2 units wide and 3
tall. A character canvas is 64x64 pixels and is drawn so that **61 pixels =
the body height (3 units)**, about 20 pixels per unit; the character's feet go
on the bottom row, horizontally centered. Keep the idle body inside roughly
the middle 32-48 pixels; attacks can reach into the rest of the canvas.

| Thing | Sim size | At 16 px per unit (a comfortable drawing scale) |
|---|---|---|
| Fighter standing | 2 x 3 | 32 x 48 |
| Crouching | 2 x 2 | 32 x 32 |
| Lying (knocked down) | about 3.3 x 1.2 | about 52 x 19 |
| Main stage top | 35.2 wide (x from -17.6 to +17.6) | 563 px wide |
| Main stage body | a solid block 6 deep | 96 px |
| Side platforms | 7.05 wide at height 4.8 | 113 px, 77 px above the stage |
| Top platform | 7 wide at height 9.4 | 112 px, 150 px above the stage |
| Blast zones (KO lines) | x +-49.6, top 38.4, bottom -28 | background about 1590 x 1070 |

Platforms are pass-through: only their top line matters for collision.

## Animation timing

The game runs at 60 frames per second and every move is defined in frames
(`data/moves/*.json`, and the move list in [engine.md](engine.md#the-moves)).

- **Loops** (idle, run...): play at the animation's `fps` (drawings per second),
  set per animation in `assets/characters/characters.json` (default 7.5;
  `python tools/sprites.py` keeps your value when it re-runs). Titou: idle 8
  drawings at 7 fps, run 8 drawings at 10 fps.
- **Attacks** are stretched over the move: with 5 drawings for a 47-frame
  forward smash, each shows about 9 frames. Put the "hit" drawing where the
  hitbox is out (forward smash: frames 15-17, a third of the way in).
- The code: `anim_for()` and `sprite_frame()` in `godot/main.gd` pick the
  animation and the frame.

Sprites are drawn with nearest-neighbor filtering, so pixel art stays sharp.

## Maps

`godot/main.gd` draws the stage from `assets/maps/` directly (no tool to run):
the background `BACKGROUND` (default `orange_clouds.png`; `blue_clouds.png`
and `grey_clouds.png` also fit) stretched over the window, and tiles where
**one tile = 1 sim unit**, stretched slightly so each surface fits exactly:

| Surface | Size (sim units = tiles) | Tiles |
|---|---|---|
| Main stage | 35.2 x 6 (35 x 6 tiles) | top row Tile_01 (left), Tile_02 (middle), Tile_03 (right); 5 rows of Tile_04 below |
| Side platforms | 7.05 x 1 (7 tiles) | Tile_06, Tile_07, Tile_08 |
| Top platform | 7 x 1 (7 tiles) | Tile_06, Tile_07, Tile_08 |

Tile_01-03 are 06-08 without the bottom border, so they join the fill below.
Collision only uses the top edge of each surface; the art doesn't change gameplay.
