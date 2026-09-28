# Running the game

## TL;DR

Double-click **`play.bat`** in the repo root (or run `.\play.bat` in a terminal).

It rebuilds the C++ (only what changed, usually a few seconds) and opens the game. The very first run takes about 30 seconds.

`.\play.bat edit` opens the project in the Godot editor instead.

## Controls

Keys are matched by **position** on the keyboard, so on AZERTY, "WASD" is really ZQSD.

| | Player 1 (blue) | Player 2 (red) |
|---|---|---|
| Move / stick | W A S D | Arrow keys |
| Jump | Space | Enter |
| Light attack | J | `.` (`:` on AZERTY) |
| Heavy attack | K | `/` (`!` on AZERTY) |

**R** resets the game and **Esc** quits. Attacks do nothing yet; they arrive in phase 1. If you walk off the stage you fall forever (blast zones are also phase 1), so press R.

The label above each fighter shows its state-machine state and how many frames it has been in that state, e.g. `P1 AIRBORNE 12`.

## What happens when you run it

1. **CMake + MSVC compile the C++** (`core/sim.cpp` plus `bindings/godot/fightgame.cpp`) into `godot/bin/fightgame.windows.template_debug.x86_64.dll`. That DLL is the whole game logic.
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
| Memory-bug check (AddressSanitizer) | `python -m cmake -S . -B build/debug` then `python -m cmake --build build/debug --config Debug` then `build\debug\Debug\smoke.exe` |

Always use `python -m cmake`, not plain `cmake`: the Strawberry Perl install on this PC puts an old CMake 3.29 on the PATH, and it doesn't know Visual Studio 2026.

## When something goes wrong

| Message | Meaning | Fix |
|---|---|---|
| `LNK1104: cannot open file '...fightgame....dll'` | The linker can't overwrite the DLL because Godot is still running and has the file open. Windows locks DLLs while they're loaded. | Close the game or the Godot editor, then run `play.bat` again. |
| `Could not find Godot` | play.bat didn't find `Godot_v4*_win64.exe` under Downloads. | Set `GODOT` to the full path of Godot's .exe. |
| Godot says `FightGame` is not declared / "Identifier not found" | Godot doesn't know about the extension. Either the DLL didn't build, or the project was never imported. | Delete the `godot/.godot` folder and run `play.bat` again; it re-imports. |
| `could not create CMAKE_GENERATOR "Visual Studio 18 2026"` | The old Strawberry CMake was used. | Use `python -m cmake ...`. |
| The build is weird after pulling big changes | Stale build folder. | Delete `build/godot` (it's safe; it's all generated) and rerun. |
