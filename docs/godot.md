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
   | `load_moves_from_text(texts)` | load moves (and reset); GDScript reads the files, since C++ can't see inside a web export's packed files |
   | `get_move_names()`, `get_state_names()` | the names, from C++ |
   | `get_hash()` | the state hash, to verify replays |
   | `set_input_buffer(player, frames)` | 9 for humans, 0 for a bot or a Python replay |
   | `get_obs(player)`, `get_obs_dim()` | what an agent sees (for the live bot) |
   | `load_bot(player, bytes)`, `bot_act(player, obs, u)` | a network bot (`core/net.cpp`): load an exported `.bin`, pick an action |

2. **`godot/fightgame.gdextension`** tells Godot where the DLL is (and the
   `.wasm` of the web export, see [setup.md](web.md)).
3. **`godot/menu.tscn` + `menu.gd`** is the first scene: the menu. Each screen
   is built in code by one `show_*()` function. Starting a game calls
   `Session.start_game(options)` and switches to `main.tscn`. If Godot was
   started with options (play.bat replay, --vs, --p1...), the menu skips
   itself. The `MENU_SCREEN` environment variable opens a given screen
   directly (for screenshots).
4. **`godot/session.gd`** is an *autoload* (a global object Godot creates once
   and keeps across scene changes, registered in `project.godot`): the bot
   levels (`DIFFICULTIES`), the saved character choices
   (`user://settings.cfg`, i.e. `%APPDATA%\Godot\app_userdata\RL-BRAWL`),
   and the options the menu passes to the game.
5. **`godot/main.gd`** (on the scene `godot/main.tscn`) runs the game.

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
- **`_draw()`**: background, stage and platforms (`stage.png`, `platform.png` from `assets/maps`, `draw_map()`), ledge markers (off: `SHOW_LEDGES`), KO explosions (pinned to the screen edge where the fighter left; a fighter waiting to respawn isn't drawn and the camera holds still until it drops in), fighters (`draw_fighter`),
  hitboxes, HUD (`draw_hud`).

### Input

- `read_input()` / `read_pad()` give each button's held state; `merge()`
  combines keyboard and controller.
- `pick_button()`: the game takes one button per frame (like the agents'
  action space); the **most recently pressed** one wins, so holding jump for a
  full hop and pressing light mid-jump gives an air attack.
- Keys are physical positions (WASD is ZQSD on AZERTY); the `KEYS` and `PAD`
  tables at the top remap them. Controllers use Smash Ultimate's default
  layout (see [playing.md](playing.md#controls)); the right stick is read as
  heavy + direction, and `human_input()` turns the grab button into a 2-frame
  shortcut (shield, then light).

### Drawing

- **Render interpolation** (`shown()`): the game ticks at 60 Hz, the screen
  may refresh faster, so fighters are drawn between the last two game frames.
- **Camera** (`update_camera()`): follows both fighters and zooms out as they
  separate; it eases 10% of the way per 60 Hz frame.
- **Sprites** (`draw_fighter()`): the character's canvas (`px`, 64 by default) is scaled so
  `ref_h` px (61 by default) = the body height, centered on the fighter, mirrored to face left,
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

With `--bot1=SPEC` / `--bot2=SPEC` (from the menu, or `play.bat --vs`), that
player is a trained agent. SPEC is a checkpoint path (`.bin`, or a
`.pt` whose `.bin` sits next to it; relative to the repo), `latest` (the
newest `runs/*/agent_latest.bin`), or a scripted bot name. Everything is per
player:

- `start_bot(p)` turns off that player's input buffer. A network bot is loaded
  into the game (`FightGame.load_bot`, running `core/net.cpp`) and is ready at
  once. A scripted bot is Python, so it starts `train/serve.py SPEC --port
  5055+p` with `OS.create_process`.
  `stop_bots()` (Esc, or leaving the scene) disconnects and stops them; each
  server also quits by itself when the game disconnects.
- `bot_update(p)` (scripted bots) connects (retrying while Python starts, up to 30 s), then
  exchanges observation sizes (see [python.md](python.md#the-live-bot-server));
  the game waits until every bot is ready.
- `bot_decide(p)` keeps the bot's last 7 observations (`get_obs(p)`); every 4
  frames (`BOT_SKIP`) it decides on the one from 6 frames ago (`BOT_DELAY`):
  `FightGame.bot_act` (the network's action, sampled with `randf()`) or the
  server's answer. It holds that until the next decision, exactly like training. The stick
  x is un-mirrored for player 2. Its status shows at the top left only while
  it loads or after an error.

## `play.bat`

1. Fetches godot-cpp if missing, configures and builds the extension (moving
   a locked DLL aside so a running Godot doesn't block the build).
2. Finds Godot (the `GODOT` variable, or `Godot_v4*_win64.exe` under Downloads)
   and imports the project the first time.
3. Reads the options (`edit`, `replay [file]`, `--vs [bot]`, `--p1`, `--p2`, any
   order) and starts Godot with them (`--vs X` becomes `--bot2=X`). With no
   options Godot opens the menu.

Batch-file pitfalls met so far (comments in `play.bat` mark them): no `)` in a
`rem` inside a `( ... )` block, and no substring of a possibly-empty variable
in a chained `if`.

## Editing in the Godot editor

`.\play.bat edit` opens the project. `main.gd` draws everything with
`_draw()` (no nodes per fighter), so most visual changes are code in
`main.gd`, not scene edits.
