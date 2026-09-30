# The Godot side (`bindings/godot/`, `godot/`)

Godot is the game's window: it reads the keyboard and controllers, asks the
C++ core to advance, and draws. It contains no game logic.

## How the pieces connect

1. **The GDExtension** (`bindings/godot/fightgame.cpp`): the C++ core compiled
   into a DLL (`godot/bin/fightgame.windows.template_debug.x86_64.dll`), built
   with godot-cpp (a git submodule in `extern/godot-cpp`). It registers a
   `FightGame` class that GDScript can use:

   | Method | What |
   |---|---|
   | `step(p1x, p1y, p1b, p2x, p2y, p2b)` | advance one frame with both inputs |
   | `get_fighter(i)`, `get_hitboxes()`, `get_frame()`, `is_over()`, `get_winner()` | read the state for drawing |
   | `get_config()` | stage, sizes, time limit, move lengths... |
   | `load_moves(dir)`, `load_moves_from_text(texts)` | load moves (and reset) |
   | `get_move_names()`, `get_state_names()` | the names, from C++ |
   | `get_hash()` | the state hash, to verify replays |
   | `set_input_buffer(player, frames)` | 9 for humans, 0 for a bot or a Python replay |
   | `get_obs(player)`, `get_obs_dim()` | what an agent sees (for the live bot) |

2. **`godot/fightgame.gdextension`** tells Godot where the DLL is.
3. **`godot/main.gd`** (on the scene `godot/main.tscn`) does everything else.

## `main.gd` in order

- **`_ready()`**: loads the characters (`load_characters`, from
  `assets/characters/characters.json`), reads the command-line options
  (`--replay=`, `--vs=`, `--p1=`, `--p2=`, `--verify`), then either loads the
  moves for play or loads a replay.
- **`_physics_process()`** runs exactly 60 times per second: one call = one
  game frame. It reads player 1 (keyboard + controller), player 2 (keyboard +
  controller, or the bot), and calls `game.step()`. In a replay it feeds the
  recorded inputs instead, at the chosen speed.
- **`_process()`** runs once per screen refresh (can be 144 Hz): moves the
  camera and asks for a redraw.
- **`_draw()`**: stage, platforms, ledge markers, fighters (`draw_fighter`),
  hitboxes, HUD (`draw_hud`).

### Input

- `read_input()` / `read_pad()` give each button's held state; `merge()`
  combines keyboard and controller.
- `pick_button()`: the game takes one button per frame (like the agents'
  action space); the **most recently pressed** one wins, so holding jump for a
  full hop and pressing light mid-jump gives an air attack.
- Keys are physical positions (WASD is ZQSD on AZERTY); the `KEYS` and `PAD`
  tables at the top remap them.

### Drawing

- **Render interpolation** (`shown()`): the game ticks at 60 Hz, the screen
  may refresh faster, so fighters are drawn between the last two game frames.
- **Camera** (`update_camera()`): follows both fighters and zooms out as they
  separate; it eases 10% of the way per 60 Hz frame.
- **Sprites** (`draw_fighter()`): the character's 64x64 canvas is scaled so
  61 px = the body height, centered on the fighter, mirrored to face left,
  squashed when crouching, rotated when knocked down or tumbling, and tinted
  for states (hit flash, charge glow, parry window...). `sprite_frame()` picks
  the animation frame (see [assets.md](assets.md)). Nearest-neighbor filtering
  keeps pixel art sharp.

### Replays

A replay (`replays/**/*.json`, written by `train/replay.py`) holds the move
files used, the input buffer setting, every frame's inputs and the final
state hash. Godot loads the recorded moves, re-simulates, and compares hashes:
"Replay verified" or "DESYNC". `--verify` does it headless for the test
`tests/test_replay_godot.py`:
`godot --headless --path godot -- --replay=FILE --verify`.

### The live bot client

With `--vs=PORT`, player 2 is a trained agent served by `train/serve.py` over
a local TCP connection (see [python.md](python.md#the-live-bot-server)):

- `bot_update()` connects (retrying while PyTorch loads, up to 30 s), then
  exchanges observation sizes; the game waits until this succeeds.
- `bot_decide()` keeps the bot's last 7 observations (`get_obs(1)`); every 4
  frames (`BOT_SKIP`) it sends the one from 6 frames ago (`BOT_DELAY`) and
  holds the answer until the next decision, exactly like training. The stick
  x is un-mirrored for player 2. The round trip (about 1 ms) shows on screen.

## `play.bat`

1. Fetches godot-cpp if missing, configures and builds the extension (moving
   a locked DLL aside so a running Godot doesn't block the build).
2. Finds Godot (the `GODOT` variable, or `Godot_v4*_win64.exe` under Downloads)
   and imports the project the first time.
3. Reads the options (`edit`, `replay [file]`, `--vs [bot]`, `--p1`, `--p2`, any
   order) and starts Godot with them; with `--vs` it first starts
   `train/serve.py` in a minimized window.

Batch-file pitfalls met so far (comments in `play.bat` mark them): no `)` in a
`rem` inside a `( ... )` block, and no substring of a possibly-empty variable
in a chained `if`.

## Editing in the Godot editor

`.\play.bat edit` opens the project. `main.gd` draws everything with
`_draw()` (no nodes per fighter), so most visual changes are code in
`main.gd`, not scene edits.
