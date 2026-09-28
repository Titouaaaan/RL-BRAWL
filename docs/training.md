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
| 3 | `--opponent scripted` | beats all four bots (the mix trains against all of them) | real skill, reward hacking, over-fitting to one opponent |
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
2. **Advantages.** GAE (γ = 0.995, λ = 0.95) over the rollout.
   The 3-minute limit is a real ending (the time left is part of the
   observation), and whoever is ahead then gets a small tie-breaker reward
   (±0.3, `--timeout-reward`), so waiting out the clock is worse than
   landing hits.
3. **Update.** 4 epochs over the rollout in 4 minibatches: the clipped PPO
   objective, clipped value loss, entropy bonus, gradient clipping. Then the
   rollout is thrown away (PPO is on-policy: no replay buffer).
4. **Every 25 iterations:** 32 games each against dummy, random, jabber and
   spacer (the `eval/win_vs_*` charts) and `runs/<run>/agent_latest.pt`.
   A permanent checkpoint every 250 iterations (`agent_*.pt`, ~1 MB each)
   and a recorded game every 100 (`replays/<run>/iter_*.json`), so a long
   run stays around 20-40 MB. `runs/` and `replays/` are not in git; delete
   old runs freely.

**Opponents:** `dummy` never moves. `random` presses random buttons.
`scripted` splits the games evenly between four bots in `train/bots.py`:
*jabber* walks up and jabs, *spacer* keeps its distance and punishes whiffs
with the heavy, plus dummy and random. (Training against jabber and spacer
only produced an agent that waited for the opponent to come to it, and
stood idle against a dummy.) `selfplay`: 80% of games against the current opponent, 20% against a
random retired opponent, so the agent doesn't forget how to beat older
versions of itself. The 80% opponent is a **frozen** copy of the agent
("gated" self-play): it stays fixed while the learner trains against it,
and is replaced by the current weights once the learner wins ≥ 55% of at
least 200 games against it (or after 100 iterations at most). With the live
weights on both sides, the two players mirrored each other and settled into
standoffs. `selfplay/win_rate_vs_frozen` and `selfplay/generation` track it.

**Actions** are 45 discrete choices: stick x (3) × stick y (3) × button
(none, jump, light, heavy, dodge). **Observations** are 121 numbers from the
player's own point of view; player 1's world is mirrored so that both
players see themselves starting on the left (layout in `core/vecenv.h`).
**Rewards:** +1 for a KO, −1 for being KO'd. On top of that, two shaping
terms give a denser learning signal early on, then fade out:

- **Damage:** ±0.02 per % of damage dealt/taken (`--damage-reward`).
- **Approach:** potential-based shaping. With Φ = −0.1 × distance / 10
  (`--approach-reward`), each step gets γ·Φ(next) − Φ(now): closing in pays,
  backing off costs. Because it's a difference of potentials, it provably
  doesn't change which policy is optimal (Ng, Harada & Russell, 1999), so
  it can't be farmed by standing next to the opponent forever.
- **Annealing:** both terms are multiplied by a scale that goes linearly
  from 1 to 0 over the first half of training (`--shaping-anneal 0.5`),
  leaving only the KO reward. `charts/shaping_scale` shows it. The
  `charts/episodic_return` chart includes the damage term, so it drifts as
  the scale falls; judge progress by the win rates.

## Reading TensorBoard

| Chart | Healthy | Worry if |
|---|---|---|
| `eval/win_vs_*` | rising | stuck at 0 on an easy rung |
| `charts/episodic_length` | falls as the agent learns to KO | always the maximum (2700 decisions = time out) |
| `charts/episodic_return` | rising (except self-play: ~0) | |
| `losses/entropy` | slowly falling from ln 45 ≈ 3.81 | collapses fast: stuck on one strategy |
| `losses/approx_kl` | ~0.01–0.02 | > 0.05 regularly: updates too big |
| `losses/explained_variance` | rising toward 1 | stays ≤ 0: the critic learns nothing |
| `actions/*` | several buttons used | a button at 0%: the agent is stuck |
| `moves/*_use` | every move above a few % | a move at ~0%: it's dominated by another move; tune it in `data/moves/` |
| `moves/*_hit_rate` | varies by move | a move used a lot but almost never hitting: the agent is flailing |

## Useful options

**Continuing a run** after a crash, a reboot or Ctrl+C:
`python train/ppo.py --resume runs\<run>`. It restarts from the last
evaluation (at most 25 iterations lost) with the run's own settings, and
TensorBoard continues the same curves.

`python train/ppo.py --help` lists them all. Common ones: `--total-timesteps`
(learner decisions, default 20M), `--seed`, `--num-envs`, `--ent-coef`,
`--damage-reward`, `--approach-reward`, `--shaping-anneal` (0 turns shaping
off), `--timeout-reward`, `--frame-skip`, `--reaction-delay` (default 6
frames = 0.1 s: the agent sees the game slightly late, so it can't simply
dodge every attack on reaction), `--eval-every`.

## Watching games

`play.bat replay` plays the newest recording; `play.bat replay path\to\file.json`
plays a specific one. Space pauses, the right arrow steps one frame while
paused, up/down change speed (¼× to 8×), R restarts. When the recording ends,
Godot checks that its re-simulation ends in exactly the recorded state and
shows "Replay verified" (or "DESYNC", which would mean the Python and Godot
builds of the core disagree: a bug worth reporting).

To watch a specific checkpoint against a specific opponent:

```
python train/record.py runs\<run>\agent_latest.pt --opponent jabber   # dummy, random, jabber, spacer, or self
play.bat replay
```

`python train/replay.py` records a random-vs-random game, handy for testing
the viewer.
