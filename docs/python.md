# Python: the `fightcore` module, bots, tools and tests

## Building

`python -m pip install --no-build-isolation .` compiles `core/` and
`bindings/python/module.cpp` (with nanobind) into the `fightcore` module.
Rebuild after any C++ change. Windows can't replace the module while a Python
process (training, the bot server) has it loaded.

## `fightcore.VecEnv`

N games stepped in one C++ call, with frame skip, reaction delay, rewards and
automatic reset. Every output is a NumPy array that views C++ memory
directly (no copy): grab it once, `step()` updates it in place.

```python
import numpy as np, fightcore
env = fightcore.VecEnv(256, "data/moves", frame_skip=4, reaction_delay=6)
actions = np.zeros((256, 2, 3), dtype=np.int8)   # [game, player, (stick_x, stick_y, button)]
env.step(actions)
env.obs[:, 0]      # (256, 173) what player 0 sees; env.obs[:, 1] player 1 (mirrored)
env.reward[:, 0]   # player 0's reward this step
```

**Constructor options:** `frame_skip` (4), `reaction_delay` (0; training uses
6), `damage_reward`, `timeout_reward`, `combo_growth`/`combo_cap`,
`streak_growth`/`streak_cap`, `counter_reward`, `input_buffer` (0: agents play
without it). `damage_reward` and `counter_reward` can be changed while running
(annealing).

**Outputs:** `obs`, `final_obs` (the last observation of a game that just
ended; `obs` already shows the next game), `reward`, `terminated` (someone
lost all stocks), `truncated` (time limit), `winner` (0, 1, or -1 for a draw),
`final_hash`.

**Statistics** for player 0, summed until you reset them (`env.stats[:] = 0`):
`stats` (named by `STAT_NAMES`: hits, KOs, blocks, parries, counters, ...),
`move_uses`, `move_hits`.

**Actions and observations are egocentric:** each player sees itself starting
on the left, and player 1's stick x is mirrored before it reaches the game, so
one policy plays both sides. The layout is documented in `core/obs.h`.

**Debug helpers** (for tests, not training loops): `fighter(game, player)`
returns a dict of the fighter's state; `set_percent(game, player, pct)`;
`hashes(out)`.

**Constants:** `OBS_DIM`, `OBS_FIGHTER`, `MOVE_NAMES`, `STATE_NAMES`,
`STAT_NAMES`, `BLAST_X`, `BLAST_TOP`, `STAGE_HALF_WIDTH`, `SPAWN_X`, `WEIGHT`,
`LAUNCH_SPEED_SCALE`.

## `train/` besides PPO

| File | What |
|---|---|
| `bots.py` | Scripted opponents as functions `obs -> actions` (vectorized NumPy): `dummy`, `random`, `jabber`, `spacer`, `fighter`. Observation indices are named at the top. |
| `replay.py` | `play_and_record()` plays one game and returns a replay dict (inputs, move files, input buffer, final hash); `save()`. |
| `record.py` | Records a checkpoint's game: `python train/record.py latest --opponent self`. |
| `serve.py` | The live bot server for Godot (below). |
| `ppo.py` | Training: see [training.md](training.md). |

## The live bot server

`python train/serve.py [latest | checkpoint.pt | scripted bot] --port 5055`
(`play.bat --vs` starts it). Protocol over local TCP, little-endian:

1. Godot sends its observation size (int32); the server answers with its own
   and quits if they differ (the game and the module were built from
   different code).
2. Then, per decision: Godot sends `OBS_DIM` float32 (the bot's view, already
   delayed); the server answers 3 int8 (stick x, stick y, button), egocentric.

It runs the network with `net_policy()` from `ppo.py`, exactly as in training
and recording, and exits when the game disconnects (or if nothing connects
within 2 minutes). About 1.1-1.5 ms per decision.

## Tools

| Tool | What |
|---|---|
| `tools/bench.py` | Throughput benchmark (about 3 million game frames per second, one thread). |
| `tools/combos.py` | Which true combos exist: for each opener and %, thousands of frame-perfect follow-ups in parallel, against a victim who doesn't DI. |
| `tools/sprites.py` | Source images -> game characters (see [assets.md](assets.md)). |

## Tests

`python -m pytest -q` (about 20 s). Everything is tested through the Python
module:

| File | Covers |
|---|---|
| `test_determinism.py` | same inputs twice give identical state hashes |
| `test_combat.py`, `test_smash.py`, `test_feel.py` | every mechanic: knockback and hitstun math, jumps, platforms, moves, shield, grabs, tech, ledges, parry, clank, buffer, charge, pummel... |
| `test_env.py`, `test_counter.py` | the RL interface: mirrored observations and actions, rewards, streaks, counters, time limit, reaction delay |
| `test_bots.py` | the scripted bots and the named observation indices |
| `test_replay_godot.py` | a Python recording verifies in Godot (skipped if Godot isn't found) |
| `test_serve.py` | the live bot server: handshake, one decision, clean exit |
