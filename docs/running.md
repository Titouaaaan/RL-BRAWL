# Running the game

## TL;DR

Double-click **`play.bat`** in the repo root (or run `.\play.bat` in a terminal).

It rebuilds the C++ (only what changed, usually a few seconds) and opens the game. The very first run takes about 30 seconds.

`.\play.bat edit` opens the project in the Godot editor instead.

`.\play.bat replay` plays back the newest recorded game (from training, see `docs/training.md`): Space pauses, the right arrow steps one frame while paused, up/down change speed, R restarts.

## Controls

Keys are matched by **position** on the keyboard, so on AZERTY, "WASD" is really ZQSD.

| | Player 1 (blue) | Player 2 (red) |
|---|---|---|
| Move / stick | W A S D | Arrow keys |
| Jump | Space | Enter |
| Light attack | J | `.` (`:` on AZERTY) |
| Heavy attack | K | `/` (`!` on AZERTY) |
| Dodge | L | `,` (`;` on AZERTY) |

**Controllers** work alongside the keyboard: the first controller connected plays P1, the second P2. A PS4 controller works over USB or Bluetooth with no setup. If Godot doesn't see it, close Steam: its "Steam Input" feature can grab controllers.

| PS4 controller | Action |
|---|---|
| Left stick or d-pad | Move (the stick has to be pushed past halfway) |
| Cross or Triangle | Jump |
| Square | Light attack |
| Circle | Heavy attack |
| L1 / R1 / L2 / R2 | Dodge |
| Options | Reload moves + restart (like R) |

To remap, edit the `PAD` table near the top of `godot/main.gd`.

**R** reloads the move files and restarts. **Esc** quits.

- Moving starts with a short, fast **dash** (8 frames), then a **run**. Turning around during the dash dashes the other way (a "dash dance").
- Fighters on the ground **push each other** apart instead of walking through each other. Nobody can be pushed off the stage: a fighter at the edge acts as a wall.
- **Attacks:** light or heavy, combined with the stick's up/down, on the ground or in the air, gives 12 moves. Left/right only sets which way you face (hold it backward to turn and hit behind you, also in the air). Facing locks once the move starts. Landing during an air move costs its landing lag.

  | | down | neutral / side | up |
  |---|---|---|---|
  | ground, light | down-tilt: low poke | jab | up-tilt: launches up, anti-air |
  | ground, heavy | down-heavy: hits both sides | heavy | up-heavy: KOs upward |
  | air, light | down-air: quick kick below | air | up-air: juggles |
  | air, heavy | heavy down-air: **spike** (sends downward) | heavy air | heavy up-air: launches high |
- Holding the attack button repeats the move. Jumping needs a fresh press.
- **Fast-fall:** hold **down without attacking** for 6 frames (0.1 s) while falling. Down + an attack does the down move instead and doesn't drop you. A down that was part of an attack is "used up": to fast-fall afterwards, return the stick to neutral and press down again.
- **Missed air attacks:** if an air attack ends without hitting, you can't attack again until you land (the HUD shows `[no air attacks]`). You can still move, double jump and dodge. If it connects, you keep your attacks, so juggles work. Getting hit also clears the lock.
- **Dodge** on the ground: a *spot dodge*. You're intangible (hits pass through you) for frames 3-15 of 26, then stuck for a moment.
- **Dodge** in the air: an *air dodge*. Intangible for frames 3-18, and afterwards you're **helpless**: you can drift but can't jump, attack or dodge until you touch the ground (then 10 frames of landing lag). Careful off the stage: an air dodge there usually means falling to your doom.
- While intangible, a fighter is drawn see-through with a white outline.
- Leave the faint blast-zone area and you lose a stock. You respawn above the stage, blinking and invulnerable for 2 seconds.

**Reading the screen:** hitboxes are circles with a red rim, faded once the move has already hit. A fighter flashes pale while in hitstun. The text next to the stocks is the state machine's state, e.g. `ATTACK heavy 12` = frame 12 of the heavy, `HITSTUN 3 (17 left)`.

## Tuning moves

Each move is a file in `data/moves/`, named as in the table above (`jab.json`, `up_tilt.json`, `heavy_down_air.json`, ...). Edit one, save, and press **R** in the game: no rebuild needed. If the file has a mistake, the error shows in red at the top of the game window.

| Field | Meaning |
|---|---|
| `total_frames` | How long the move lasts, in frames (60 = 1 second). |
| `landing_lag` | Aerial only: frames stuck if you land during the move. |
| `hitboxes` | Up to 4 circles. The first one listed wins if several touch. |
| `start`, `end` | The move frames when this hitbox is active (the first frame is 1). Frames before the first hitbox = *startup*, after the last = *recovery*. |
| `x`, `y` | Circle centre relative to the attacker's feet; `x` points the way they face. The body (hurtbox) is 2 wide, 3 tall. |
| `radius` | Circle size. |
| `damage` | % added to the victim. |
| `base_kb`, `kb_growth` | Knockback (launch speed) = `base_kb + kb_growth * victim %`. Hitstun frames = knockback * 25. |
| `angle` | Launch direction in degrees: 0 = straight forward, 90 = straight up, negative = downward (a spike). |

Movement and dodge numbers (dash and run speed, gravity, dodge frames, blast zones and so on) live in `GameConfig` in `core/sim.h`. Those do need a rebuild, which `play.bat` does for you.

## What happens when you run it

1. **CMake + MSVC compile the C++** (`core/sim.cpp`, `core/moves.cpp` and `bindings/godot/fightgame.cpp`) into `godot/bin/fightgame.windows.template_debug.x86_64.dll`. That DLL is the whole game logic.
2. **Godot loads the DLL.** `godot/fightgame.gdextension` tells it where the DLL is.
3. **`godot/main.gd` runs 60 times per second.** Each time it reads the keyboard, calls `step()` in C++, and draws the boxes.

The Python training code calls exactly the same `step()`, so what you see is exactly what the bot trains on.

## One-time setup on a new machine

This machine already has everything installed. On another PC you would need:

1. **Visual Studio 2026 (Community)** with the *Desktop development with C++* workload, which provides the MSVC compiler.
2. **Python 3.13** from python.org, then run:
   `python -m pip install "cmake>=4.2" "scikit-build-core>=0.10" "nanobind>=2.4" pytest numpy`
3. **Godot 4.7** (standard build, not .NET). Unzip it anywhere under `Downloads`, or set the `GODOT` environment variable to the path of its `.exe`.
4. `git clone` the repo. `play.bat` downloads the `godot-cpp` submodule itself if it's missing.

## Other commands (Python side)

| What | Command |
|---|---|
| Rebuild the Python module after a C++ change | `python -m pip install --no-build-isolation .` |
| Run the tests | `python -m pytest -q` |
| Speed benchmark | `python tools/bench.py` |
| Train an agent | `python train/ppo.py --opponent dummy` (see `docs/training.md`) |
| Memory-bug check (AddressSanitizer) | `python -m cmake -S . -B build/debug` then `python -m cmake --build build/debug --config Debug` then `build\debug\Debug\smoke.exe` |

Always use `python -m cmake`, not plain `cmake`: the Strawberry Perl install on this PC puts an old CMake 3.29 on the PATH, and it doesn't know Visual Studio 2026.

## When something goes wrong

| Message | Meaning | Fix |
|---|---|---|
| `LNK1104: cannot open file '...fightgame....dll'` | The linker can't overwrite the DLL because Godot is running and has the file open. Windows locks DLLs while they're loaded. `play.bat` works around this by renaming the old DLL first, so you only see this when building by hand. | Close the game or the Godot editor, or build through `play.bat`. |
| `Could not find Godot` | play.bat didn't find `Godot_v4*_win64.exe` under Downloads. | Set `GODOT` to the full path of Godot's .exe. |
| Godot says `FightGame` is not declared / "Identifier not found" | Godot doesn't know about the extension. Either the DLL didn't build, or the project was never imported. | Delete the `godot/.godot` folder and run `play.bat` again; it re-imports. |
| `could not create CMAKE_GENERATOR "Visual Studio 18 2026"` | The old Strawberry CMake was used. | Use `python -m cmake ...`. |
| `DESYNC` at the end of a replay | Godot's re-simulation didn't end in the recorded state: the Python module and the Godot DLL disagree. Usually one of them is stale. | Rebuild both (`pip install --no-build-isolation .` and `play.bat`), record again. `tests/test_replay_godot.py` checks this automatically. |
| The build is weird after pulling big changes | Stale build folder. | Delete `build/godot` (it's safe; it's all generated) and rerun. |
