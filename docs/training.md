# Training

## TL;DR

```
python train/ppo.py --opponent dummy       # rung 1: learn to KO a standing dummy
python -m tensorboard.main --logdir runs   # curves, at http://localhost:6006
play.bat replay                            # watch the newest recorded game in Godot
```

Run the commands from the repo root. After any C++ change, rebuild the Python module first: `python -m pip install --no-build-isolation .`

## The sanity ladder

Climb one rung at a time. Each rung catches a different kind of bug, and a
failure on an easy rung means something is broken, not that the agent needs
more training.

| Rung | Command | Pass when | Catches |
|---|---|---|---|
| 1 | `--opponent dummy` | wins ~100% vs dummy, games get short | env, reward, observation bugs |
| 2 | `--opponent random` | wins > 95% vs random | basic combat |
| 3 | `--opponent scripted` | clearly beats jabber and spacer | real skill, reward hacking |
| 4 | `--opponent selfplay` | win rate vs the bots keeps rising; replays look like real play | the goal |

Self-play reward hovers around zero by design (you're playing yourself), so
judge rung 4 by the `eval/` charts and by watching replays.

## How one training iteration works

`train/ppo.py` is CleanRL's single-file PPO with minimal, marked changes (the
header lists them; `git log -p train/ppo.py` shows the exact diff).

1. **Rollout.** 256 games run in parallel (one C++ `VecEnv` call steps them
   all). For 128 decisions, player 0 (the learner) samples an action from the
   current policy for every game, player 1 (the opponent) picks its own, and
   the game advances 4 frames (frame skip). Only player 0's observations,
   actions, log-probs, values and rewards are stored. That's 32,768 steps.
2. **Advantages.** GAE (γ = 0.995, λ = 0.95) over the rollout. When a game
   hits the 3-minute limit, it was cut short rather than finished, so
   γ·V(last observation) is added to that step's reward (the "truncation
   bootstrap").
3. **Update.** 4 epochs over the rollout in 4 minibatches: the clipped PPO
   objective, clipped value loss, entropy bonus, gradient clipping. Then the
   rollout is thrown away (PPO is on-policy: no replay buffer).
4. **Every 25 iterations:** 32 games each against dummy, random, jabber and
   spacer (the `eval/win_vs_*` charts), a checkpoint (`runs/<run>/agent_*.pt`)
   and one recorded game (`replays/<run>/iter_*.json`).

**Opponents:** `dummy` never moves. `random` presses random buttons.
`scripted` splits the games between two bots in `train/bots.py`: *jabber*
walks up and jabs, *spacer* keeps its distance and punishes whiffs with the
heavy. `selfplay`: 80% of games against the current weights, 20% against a
random past snapshot (one is added every 20 iterations), so the agent
doesn't forget how to beat older versions of itself.

**Actions** are 36 discrete choices: stick x (3) × stick y (3) × button
(none, jump, light, heavy). **Observations** are 107 numbers from the
player's own point of view; player 1's world is mirrored so that both
players see themselves starting on the left (layout in `core/vecenv.h`).
**Rewards:** +1 for a KO, −1 for being KO'd, ±0.01 per % of damage dealt/taken.

## Reading TensorBoard

| Chart | Healthy | Worry if |
|---|---|---|
| `eval/win_vs_*` | rising | stuck at 0 on an easy rung |
| `charts/episodic_length` | falls as the agent learns to KO | always the maximum (2700 decisions = time out) |
| `charts/episodic_return` | rising (except self-play: ~0) | |
| `losses/entropy` | slowly falling from ln 36 ≈ 3.58 | collapses fast: stuck on one strategy |
| `losses/approx_kl` | ~0.01–0.02 | > 0.05 regularly: updates too big |
| `losses/explained_variance` | rising toward 1 | stays ≤ 0: the critic learns nothing |
| `actions/*` | several buttons used | a button at 0%: the move is useless or the agent is stuck |

## Useful options

`python train/ppo.py --help` lists them all. Common ones: `--total-timesteps`
(learner decisions, default 20M), `--seed`, `--num-envs`, `--ent-coef`,
`--frame-skip`, `--reaction-delay`, `--eval-every`.

## Watching games

`play.bat replay` plays the newest recording; `play.bat replay path\to\file.json`
plays a specific one. Space pauses, the right arrow steps one frame while
paused, up/down change speed (¼× to 8×), R restarts. When the recording ends,
Godot checks that its re-simulation ends in exactly the recorded state and
shows "Replay verified" (or "DESYNC", which would mean the Python and Godot
builds of the core disagree: a bug worth reporting).

`python train/replay.py` records a random-vs-random game, handy for testing
the viewer.
