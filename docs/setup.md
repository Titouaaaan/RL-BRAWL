# Setup, commands and troubleshooting

## One-time setup on a new machine

This machine already has everything installed. On another PC you would need:

1. **Visual Studio 2026 (Community)** with the *Desktop development with C++* workload, which provides the MSVC compiler.
2. **Python 3.13** from python.org, then run:
   `python -m pip install "cmake>=4.2" "scikit-build-core>=0.10" "nanobind>=2.4" pytest numpy torch tensorboard tyro pillow`
   (the first three build the game module; torch, tensorboard and tyro are for training and the live bot; pillow for `tools/sprites.py`), then build the module once: `python -m pip install --no-build-isolation .`
3. **Godot 4.7** (standard build, not .NET). Unzip it anywhere under `Downloads`, or set the `GODOT` environment variable to the path of its `.exe`.
4. `git clone` the repo. `play.bat` downloads the `godot-cpp` submodule itself if it's missing.
5. `git config core.hooksPath .githooks` turns on the commit check (a commit that changes code without a `docs/log.md` entry is refused).

## Building

| What | Command |
|---|---|
| The game (Godot extension) + launch | `.\play.bat` (rebuilds only what changed) |
| The game without launching | `python -m cmake -S bindings/godot -B build/godot` then `python -m cmake --build build/godot --config Release --parallel` (fails with LNK1104 while Godot runs: the DLL is locked) |
| The Python module, after any C++ change | `python -m pip install --no-build-isolation .` |
| Memory-bug check (AddressSanitizer) | `python -m cmake -S . -B build/debug` then `python -m cmake --build build/debug --config Debug` then `build\debug\Debug\smoke.exe` |

Always use `python -m cmake`, not plain `cmake`: the Strawberry Perl install
on this PC puts an old CMake 3.29 on the PATH, and it doesn't know Visual
Studio 2026. The root `CMakeLists.txt` builds the Python module when pip runs
it and the `smoke` test program otherwise; `bindings/godot/CMakeLists.txt`
builds the Godot extension. `build/` is generated and safe to delete.

## Web export (the game in a browser)

`python tools/web_export.py`: everything (setup, hosting, how it works) is in
[web.md](web.md).

## Commands

| What | Command |
|---|---|
| Rebuild the Python module after a C++ change | `python -m pip install --no-build-isolation .` |
| Run the tests | `python -m pytest -q` |
| Speed benchmark | `python tools/bench.py` |
| Train an agent | `python train/ppo.py --opponent dummy` (see [training.md](training.md)) |
| Record a game of the newest agent (then `play.bat replay`) | `python train/record.py latest --opponent self` (or `fighter`, `spacer`, ...) |
| Process new or changed character images | `python tools/sprites.py` (see [assets.md](assets.md)) |
| Which true combos the game allows (frame-perfect search) | `python tools/combos.py` |
| Run the scripted bot server by hand (normally `play.bat --vs fighter` does it) | `python train/serve.py fighter --port 5055` |
| Make old checkpoints playable in the game (new ones already are) | `python tools/export_bot.py runs/<run>` |
| Memory-bug check (AddressSanitizer) | `python -m cmake -S . -B build/debug` then `python -m cmake --build build/debug --config Debug` then `build\debug\Debug\smoke.exe` |

## When something goes wrong

| Message | Meaning | Fix |
|---|---|---|
| `LNK1104: cannot open file '...fightgame....dll'` | The linker can't overwrite the DLL because Godot is running and has the file open. Windows locks DLLs while they're loaded. `play.bat` works around this by renaming the old DLL first, so you only see this when building by hand. | Close the game or the Godot editor, or build through `play.bat`. |
| `Could not find Godot` | play.bat didn't find `Godot_v4*_win64.exe` under Downloads. | Set `GODOT` to the full path of Godot's .exe. |
| Godot says `FightGame` is not declared / "Identifier not found" | Godot doesn't know about the extension. Either the DLL didn't build, or the project was never imported. | Delete the `godot/.godot` folder and run `play.bat` again; it re-imports. |
| `could not create CMAKE_GENERATOR "Visual Studio 18 2026"` | The old Strawberry CMake was used. | Use `python -m cmake ...`. |
| `DESYNC` at the end of a replay | Godot's re-simulation didn't end in the recorded state: the Python module and the Godot DLL disagree. Usually one of them is stale. Or the replay was recorded with an older version of the game: a replay stores the inputs, not the game code. | Rebuild both (`pip install --no-build-isolation .` and `play.bat`), record again. `tests/test_replay_godot.py` checks this automatically. Replays recorded before a game change: regenerate them (`python train/record_vs.py`). |
| `P2: could not reach the bot` / `lost the connection to the bot` | The game couldn't talk to `train/serve.py` (a scripted bot; it crashed, or took over 30 s to load). | Look at the minimized "bot" window for its error. |
| `no agent_X.bin (export it: ...)` / `no trained run yet` | A network bot needs its exported `.bin` (`latest` = the newest `runs/*/agent_latest.bin`). | Run the command shown: `python tools/export_bot.py runs/<run>`. |
| `this bot sees N inputs, the game gives M` | The checkpoint was trained on an older game version (the observation changed). | Use a newer checkpoint, or retrain. |
| `the bot expects N inputs, this game gives M` | The Python module and the Godot DLL were built from different code (the observation size differs). | Rebuild both: `python -m pip install --no-build-isolation .` and `play.bat`. |
| Anything in the browser version | | See [web.md](web.md#troubleshooting). |
| The build is weird after pulling big changes | Stale build folder. | Delete `build/godot` (it's safe; it's all generated) and rerun. |
