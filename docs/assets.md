# Assets: characters, sprites and animations

Characters are **visuals only**: every character has the same hurtbox (the
2 x 3 box that can be hit) and the same moves. Pick them with `--p1 ID --p2 ID`
on any `play.bat` command.

## Folders

| Folder | What | Who writes it |
|---|---|---|
| `assets/sprites/` | your source images: characters and animation strips, any size | you (tools never modify it) |
| `assets/characters/` | the processed 64x64 characters, animation strips and `characters.json` | `python tools/sprites.py` |

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
| 6 | Titou (animated idle) |

**Adding a character:** put the image (any size, PNG) in `assets/sprites/`,
then run `python tools/sprites.py`. It trims the empty space, removes a plain
white page background, scales the character to fit a 64x64 canvas (feet at the
bottom), saves it as `assets/characters/<id>.png` and prints the new id. Ids
never change: new images get the next number. Your originals are not modified.
Draw characters facing **right** (or set `"faces": "left"` for that character
in `assets/characters/characters.json`).

## Animations

Name a horizontal strip of square frames `<Character>_<animation>.png`
(for example `Titou_idle.png`: 4 frames of 64x64 side by side, 256x64) and run
`python tools/sprites.py`. Every frame gets the same crop and position, so
nothing jitters (frames already drawn at 64x64 are kept exactly as drawn).
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

Looping animations show each drawing for 8 game frames (7.5 drawings per
second). Anything without an animation uses the static sprite.


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

- **Loops** (idle, run...): each drawing shows for 8 game frames (7.5 per second).
- **Attacks** are stretched over the move: with 5 drawings for a 47-frame
  forward smash, each shows about 9 frames. Put the "hit" drawing where the
  hitbox is out (forward smash: frames 15-17, a third of the way in).
- The code: `anim_for()` and `sprite_frame()` in `godot/main.gd` pick the
  animation and the frame; `ANIM_TICKS` sets the loop speed.

Sprites are drawn with nearest-neighbor filtering, so pixel art stays sharp.
