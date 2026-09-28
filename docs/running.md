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

**R** reloads the move files and restarts. **Esc** quits.

- Light or heavy on the ground: **jab** or **heavy**. Hold the stick left or right while pressing to attack in that direction.
- Light or heavy in the air: **aerial**. You can turn around in the air, so hold the stick backward to hit behind you. Facing locks once the aerial starts. Landing during it costs landing lag.
- Holding the attack button repeats the move. Jumping needs a fresh press.
- Hold **down** while falling to **fast-fall** (drop faster).
- **Dodge** on the ground: a *spot dodge*. You're intangible (hits pass through you) for frames 3-15 of 26, then stuck for a moment.
- **Dodge** in the air: an *air dodge*. Intangible for frames 3-18, and afterwards you're **helpless**: you can drift but can't jump, attack or dodge until you touch the ground (then 10 frames of landing lag). Careful off the stage: an air dodge there usually means falling to your doom.
- While intangible, a fighter is drawn see-through with a white outline.
- Leave the faint blast-zone area and you lose a stock. You respawn above the stage, blinking and invulnerable for 2 seconds.

**Reading the screen:** hitboxes are circles with a red rim, faded once the move has already hit. A fighter flashes pale while in hitstun. The text next to the stocks is the state machine's state, e.g. `ATTACK heavy 12` = frame 12 of the heavy, `HITSTUN 3 (17 left)`.

## Tuning moves

Each move is a file in `data/moves/` (`jab.json`, `heavy.json`, `aerial.json`). Edit one, save, and press **R** in the game: no rebuild needed. If the file has a mistake, the error shows in red at the top of the game window.

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
| `angle` | Launch direction in degrees: 0 = straight forward, 90 = straight up. |

Movement and dodge numbers (walk speed, gravity, dodge frames, blast zones and so on) live in `GameConfig` in `core/sim.h`. Those do need a rebuild, which `play.bat` does for you.

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
