# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project status

v1 done (Oct 12, 2026): a Smash-style mirror match (Mario's frame data, Battlefield stage; controls at the top of `core/sim.h`, mechanics in `docs/playing.md`) and PPO self-play agents. Best agent: `runs/selfplay__league__1__1790809954` (Elo 2251; the menu's bot levels use its checkpoints, `godot/session.gd`). Next: up-B, a bigger stage, then retrain. Read `docs/design.md` before starting a phase.

Where things are recorded (keep each one current, don't duplicate between them):
- `docs/log.md`: every significant change: why, the owner's request, who decided, commits (see Development process).
- `docs/runs.md`: the lab notebook: every training run, its numbers, interventions and reports.
- `docs/design.md`: the plan; its dated decisions stop at Oct 12, 2026 (later ones go to `log.md`).
- Owner-facing docs per theme, indexed in `docs/README.md`.

## Commands

Toolchain: MSVC from Visual Studio 2026 (it must match python.org Python's compiler), CMake 4.x from pip. Use `python -m cmake`, **not** bare `cmake`: the Strawberry Perl CMake 3.29 on PATH is too old for VS 2026.

- **Play the game (build + launch Godot):** `play.bat` opens the menu (`godot/menu.gd`: PvB with difficulty levels, PvP, BvB, character select, controls, replays, training dashboard; levels in `godot/session.gd`); `play.bat edit` opens the editor. Owner-facing docs, one per theme, indexed in `docs/README.md`: `playing.md` (controls, mechanics, bot, replays), `setup.md` (install, build, commands, troubleshooting), `assets.md` (characters, sprites, animations), `engine.md` (C++ core, move files), `godot.md`, `python.md` (VecEnv API, bots, serve protocol, tools, tests), `training.md` (RL), `design.md` (plan + dated decisions). Keep the relevant one current when behavior, controls or commands change.
- Godot extension only: `python -m cmake -S bindings/godot -B build/godot && python -m cmake --build build/godot --config Release --parallel` (fails with LNK1104 if Godot is running: the DLL is locked).
- Web export (the game in a browser, for the owner's site): `python tools/web_export.py` -> `build/web_export/` (Emscripten 4.0.20 in `../emsdk`, Godot web templates; `docs/setup.md#web-export-the-game-in-a-browser`).
- Build/rebuild Python module after any C++ change: `python -m pip install --no-build-isolation .`
- Tests: `python -m pytest -q`
- Benchmark: `python tools/bench.py`
- Train: `python -u train/ppo.py --opponent dummy|random|scripted|selfplay`; continue a stopped run with `--resume runs/<run>` (logs/checkpoints in `runs/`, replays in `replays/`, both gitignored). Owner-facing guide: `docs/training.md`.
- Fight a bot live: `play.bat --vs [latest|<checkpoint.pt/.bin>|fighter|...]`. Network bots run inside the game (`core/net.cpp`, the actor exported to `.bin` by `ppo.py` / `tools/export_bot.py`; menu levels in `data/bots/`); scripted bots start `train/serve.py` (local TCP). Godot feeds `write_obs` from `core/obs.cpp` with the 6-frame delay, every 4 frames; per-player `input_buffer`: human 9, bot 0.
- Watch the newest replay: `play.bat replay`; record one first with `python train/record.py latest --opponent self|fighter|...`. Replays desync after any game change (they store inputs only): regenerate with `python train/record_vs.py`.
- Characters: `python tools/sprites.py` after adding/changing images in `assets/sprites/` (one folder per character: `static.png`, `<anim>.png` = horizontal strip of square frames); `play.bat ... --p1 ID --p2 ID`.
- Combo finder: `python tools/combos.py` (which true combos exist, frame-perfect). Headless replay check: `<godot_console.exe> --headless --path godot -- --replay=FILE --verify`.
- ASan debug smoke test: `python -m cmake -S . -B build/debug && python -m cmake --build build/debug --config Debug && build/debug/Debug/smoke.exe`

Root `CMakeLists.txt` builds the Python module when driven by pip (`SKBUILD` set) and the native `smoke` exe otherwise.

## What this is

A minimal Smash-style fighting game (one character mirror match, one flat stage) whose simulation core is used both for RL self-play training (Python/PyTorch) and a Godot front end (built from phase 1 on, so every phase can be watched). v0 is done when two PPO self-play agents fight competently.

## Architecture (planned, see `docs/design.md`)

One engine-free C++ core, two front ends:

- **`core/`** (C++, CMake): the whole game is `GameState` (one plain struct) plus a pure, deterministic `step(state, inputs[2], config, events)` (`sim.*`); `moves.cpp` loads the JSON moves; `obs.*` builds an agent's observation (shared by the Python env and the Godot live bot; kept separate from `vecenv.cpp` because that file throws exceptions and the Godot build has them off); `net.*` runs a trained actor for the game's bots; `vecenv.*` is the RL layer. No globals, no hidden state, no engine calls. Same state + same inputs must give the same result; replays are just a seed plus the input sequence.
- **`bindings/python/`**: nanobind module exposing a batched `VecEnv` (N games stepped in one C++ call, auto-reset inside, writes obs/reward/done into preallocated NumPy buffers zero-copy). Output buffers (`obs`, `final_obs`, `reward`, `terminated`, `truncated`, `winner`) are NumPy views of C++ memory, updated in place by `step()`; every per-player output has a player axis (`obs[i, p]` = game i from p's view). No Gymnasium wrapper (skipped: two-player self-play doesn't fit its single-agent API; PPO uses `VecEnv` directly). Never loop over environments in Python.
- **`bindings/godot/`** + **`godot/`**: GDExtension exposing the same core to Godot, and the Godot project (`main.gd`: keyboard/controller play, replays, character sprites and animations, live-bot client). It moved up from phase 5 so each phase can be checked visually. Keep it building whenever the core changes.
- **`data/moves/`**: one JSON file per move (`total_frames`, `landing_lag`, and up to 4 hitboxes, each active on a `start`..`end` frame window; field reference in `docs/engine.md#move-files`). Loaded by `core/moves.cpp`, which must never throw (the Godot build has exceptions off). `MOVE_NAMES` in moves.cpp is the single list of moves; Python (`fightcore.MOVE_NAMES`), Godot (`get_move_names()`) and replays read it. Adding a move does not change the obs layout (moves are described by properties), so checkpoints stay loadable. Godot gets them through the core too, so visuals never drift from simulation. Tune the game here, not by recompiling. Broken mechanics (spammed moves, jab locks, infinite combos) are fixed in these files, not in RL code.
- **`train/`**: CleanRL ppo.py adapted (Discrete(45) actions, learner always player 0, frozen opponent = bot or network, timeouts are real endings with a ±`timeout_reward` tie-breaker, `reaction_delay` 6 frames by default, potential-based approach shaping + damage reward with a zero-sum hit-streak multiplier + counter/whiff-punish bonus, all annealed to a 0.2 floor over `shaping_anneal`; entropy bonus annealed 0.03 -> 0.01; ~117 TensorBoard charts incl. `mech/` counters from `VecEnv.stats` / `STAT_NAMES`). Self-play is gated: ~70% of envs vs a frozen copy of the agent (+10% scripted bots, `bot_frac`), replaced when the learner wins >= 55% of >= 200 games vs it (or after 100 iterations); ~20% vs retired opponents (with `--league <older run>`: 55% frozen, 15% retired, 20% the older run's checkpoints picked by closeness p(1-p), 10% bots; `eval/win_vs_league_best` = head-to-head vs that run's final agent). (Live-vs-live self-play mirrored and stalled into standoffs.) Only the learner's experience is used (using both sides in latest-vs-latest games is a possible later speedup).
- **`tools/`**: `bench.py` (throughput), `combos.py` (which true combos exist, with frame-perfect scripted follow-ups), `sprites.py` (character images -> game characters), `ladder.py` (round-robin + Elo between checkpoints), `export_bot.py` (`.pt` -> `.bin` for the game's C++ bot), `web_export.py` (the game as a web page), `run_summary.py` (runs -> `summary.json` for the menu's Training screen). (No separate Python viewer; Godot plays replays.)
- **`assets/`**: `sprites/` = the owner's source art (never modified by tools), `characters/` = generated.
- **`tests/`**: pytest through the bindings.

## Invariants that later goals depend on

These are deliberate choices; don't change them without revisiting the design doc.

- **Action space is controller-level and fixed:** stick x, stick y in {-1,0,1}, button in {none, jump, light, heavy, dodge} (Discrete(45) in PPO). The game decides which move comes out. Dodge was added Sep 28, 2026, which invalidated older checkpoints; changing the button set again means retraining from scratch.
- **Observations are egocentric:** agent is always player 0, x mirrored to a consistent side. Hitboxes are a padded entity list (N=4) described by properties, never by move ID.
- **Reaction delay ring buffer** (observe state k frames ago; 6 in training) and **frame skip** (default 4) must stay configurable. The Godot live bot reproduces both.
- **Agents play without the input buffer** (`input_buffer=0` in VecEnv, the live bot, and replays recorded from Python). Turning it on for agents halved their learning speed (ablation, Sep 30, 2026).
- **Rewards come from `StepEvents`** (hits, damage, KOs) emitted by `step()`, not from diffing state. No positional shaping.
- **`train/ppo.py`** is CleanRL's ppo.py (commit fe8d8a0); every change is marked `# OURS`; never modify the PPO update itself.
- **Hidden state the rules depend on is in the obs** (hold counters, locks, windows), except each fighter's stale-move queue, deliberately (it would be keyed by move ID).
- Simulation is 60 Hz fixed timestep with an integer frame counter. Fighter logic is a state machine (state + frame-in-state + input -> next state).

## About the owner: how to explain work

The owner is comfortable with ML and RL (PPO, self-play, PyTorch, reward design) but is a beginner at game development, C++ and Godot. Adjust explanations to match:

- **ML and training code:** no need to explain standard concepts. Be concise.
- **Game and engine work:** explain in detail. This covers C++, CMake and build tooling, nanobind bindings, the simulation (state machine, physics, collision, hitboxes, knockback, frame data), Godot and GDExtension, and replay or viewer code.
- **For that work, always say what and why.** Say what the code does, why it is built that way, and how it connects to the rest of the system. Define game-dev and C++ terms the first time they appear, such as hitstun, frame data, hurtbox, header files, linking, or compile targets.
- **Relate to ML where it helps.** For example, the core's `step()` plays the role of a Gym environment's step, and determinism matters for the same reasons as seeding experiments.
- **Build and tooling errors:** explain what the error means and what caused it, not just the fix, so the owner can recognize it next time.
- **Commands:** when asking the owner to run something, say what it does.

## C++ conventions

Keep C++ simple: plain structs, `std::vector`, fixed-size arrays, no raw `new`/`delete`. Debug builds run with AddressSanitizer enabled. Code as compact as possible (owner's choice, Oct 12, 2026; applies to all code): fewest lines and files, no clever or heavily templated code; comment non-obvious C++ idioms.

## Testing expectations

- Determinism test: same random inputs run twice must produce identical state hashes.
- Unit tests for knockback and hitstun math.
- A throughput benchmark run regularly (target on the order of 10^6 steps/s; the RL side, not the sim, should set training speed).

## Development process

- **Decision log:** every significant change gets an entry in `docs/log.md` (date, commit, title, goal, the owner's request, decisions and who made them, output), kept terse. Write the hash as `pending` and fill it in at the next log update.
- **Enforced by a git hook** (`.githooks/commit-msg`; enable once per clone with `git config core.hooksPath .githooks`): a commit touching `core/`, `train/`, `data/`, `bindings/`, `godot/`, `tools/` or `assets/sprites/` is refused unless it also stages `docs/log.md`. `[no-log]` in the message is allowed only for trivial fixes (typos, comments). Never bypass it with `--no-verify`.
- **Check visuals yourself before handing over:** for any change that shows on screen, render frames and look at them: `<godot_console.exe> --path godot --write-movie <scratchpad>/f.png --quit-after 600 -- --replay=<file> [--p1=ID --p2=ID]` (one PNG per frame; crop/zoom with PIL). Only then ask the owner to test the feel.
- **Approved destructive steps** (cleanups the owner agreed to) are run directly, not handed back as commands.

Work proceeds in phases 0 to 5; each phase has a gate that must pass before the next starts. Training follows a sanity ladder: stationary dummy, random agent, scripted bots, then full self-play. Self-play reward hovers near zero, so judge progress by win rate vs scripted baselines, Elo across snapshots, and watching replays.
