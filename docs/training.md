# Training

## TL;DR

```
python -u train/ppo.py --opponent scripted --total-timesteps 20000000    # sanity check, ~10 min
python -u train/ppo.py --opponent selfplay --total-timesteps 150000000   # the real run, ~2.5 h
python -m tensorboard.main --logdir runs   # curves, at http://localhost:6006
play.bat replay                            # watch the newest recorded game in Godot
```

`-u` makes the log appear line by line. Add `> runs\selfplay.log 2>&1` to keep it in a file.
A stopped run continues with `python -u train/ppo.py --resume runs\<run folder>`.

Run the commands from the repo root. After any C++ change, rebuild the Python module first: `python -m pip install --no-build-isolation .`

## The sanity ladder

Climb one rung at a time. Each rung catches a different kind of bug, and a
failure on an easy rung means something is broken, not that the agent needs
more training.

| Rung | Command | Pass when | Catches |
|---|---|---|---|
| 1 | `--opponent dummy` | wins ~100% vs dummy, games get short | env, reward, observation bugs |
| 2 | `--opponent random` | wins > 95% vs random | basic combat |
| 3 | `--opponent scripted` | beats all five bots (the mix trains against all of them) | real skill, reward hacking, over-fitting to one opponent |
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
   The 7-minute limit is a real ending (the time left is part of the
   observation), and whoever is ahead then gets a small tie-breaker reward
   (±0.3, `--timeout-reward`), so waiting out the clock is worse than
   landing hits.
3. **Update.** 4 epochs over the rollout in 4 minibatches: the clipped PPO
   objective, clipped value loss, entropy bonus, gradient clipping. Then the
   rollout is thrown away (PPO is on-policy: no replay buffer).
4. **Every 25 iterations:** 32 games each against dummy, random, jabber,
   spacer and fighter (the `eval/win_vs_*` charts) and `runs/<run>/agent_latest.pt`.
   A permanent checkpoint every 250 iterations (`agent_*.pt`, ~1 MB each)
   and a recorded game every 100 (`replays/<run>/iter_*.json`), so a long
   run stays around 20-40 MB. `runs/` and `replays/` are not in git; delete
   old runs freely.

**Opponents:** `dummy` never moves. `random` presses random buttons.
`scripted` splits the games evenly between five bots in `train/bots.py`:
*jabber* walks up and jabs, *spacer* keeps its distance and punishes whiffs
with the heavy, *fighter* uses the whole toolkit (shields, spot dodges and rolls
away from attacks it sees starting, then counterattacks: grab out of shield,
tilts, smashes at high %; punishes whiffs; juggles; throws toward the edge;
techs; mixes get-up and ledge options), plus dummy and random. Under the
6-frame reaction delay the fighter beats dummy and random ~97% and jabber ~75%,
and splits ~50/50 with spacer: `eval/win_vs_fighter` is the strongest fixed yardstick. (Training against jabber and spacer
only produced an agent that waited for the opponent to come to it, and
stood idle against a dummy.) `selfplay`: 70% of games against the current
opponent, 20% against a random retired opponent (so the agent doesn't forget
how to beat older versions of itself), and 10% against the five scripted
bots (`--bot-frac`), so it never forgets how to beat simple play (a previous
run ended up drawing 97% of games against a standing dummy). The 70% opponent is a **frozen** copy of the agent
("gated" self-play): it stays fixed while the learner trains against it,
and is replaced by the current weights once the learner wins ≥ 55% of at
least 200 games against it (or after 100 iterations at most). With the live
weights on both sides, the two players mirrored each other and settled into
standoffs. `selfplay/win_rate_vs_frozen` and `selfplay/generation` track it.

**Actions** are 45 discrete choices: stick x (3) × stick y (3) × button
(none, jump, light, heavy, dodge). **Observations** are 173 numbers from the
player's own point of view; player 1's world is mirrored so that both
players see themselves starting on the left (layout in `core/obs.h`, code in
`core/obs.cpp`, which the live Godot bot uses too). Besides positions, speeds,
states and hitboxes they include everything the rewards depend on: the combo
and hit-streak counts, the counter window and the smash charge.

**No input buffer for agents:** the game's 9-frame input buffer (a press made
just before you can act still comes out) is for humans only; the training
env uses `input_buffer=0`. With it, agents learned about half as fast (hits/min
4.7 vs 10.7 and 83% vs 0.5% timeouts at 21M decisions): an agent decides every
4 frames and sees the game 6 frames late, so presses from earlier decisions
fired at moments it hadn't chosen, invisible in its observation. Found by
ablation (time limit, reward, seed, smash charge and buffer tested one at a
time; details in `runs/monitor_notes.md`).
**Rewards:** +1 for a KO, −1 for being KO'd. On top of that, two shaping
terms give a denser learning signal early on, then fade out:

- **Damage:** ±0.02 per % of damage dealt/taken (`--damage-reward`).
- **Approach:** potential-based shaping. With Φ = −0.1 × distance / 10
  (`--approach-reward`), each step gets γ·Φ(next) − Φ(now): closing in pays,
  backing off costs. Because it's a difference of potentials, it provably
  doesn't change which policy is optimal (Ng, Harada & Russell, 1999), so
  it can't be farmed by standing next to the opponent forever.
- **Streak bonus (replaces the combo bonus below, which is now off):** the
  damage reward of the k-th hit of a *hit streak* (hits landed since you last
  got hit; dodges, blocks and pauses don't end it) is multiplied by
  1.1^(k-1), capped at 1.5× (`--streak-growth`, `--streak-cap`), zero-sum and
  annealed. It rewards hitting without getting hit, not just true combos. A
  combo finder (`python tools/combos.py`, frame-perfect scripted follow-ups
  against a victim who doesn't DI) showed the game has plenty of true combos
  (up-tilt, up/down throw, down-tilt, down-air at 0-80%), but the agent favors
  moves that don't lead anywhere, so true combos stayed at ~0.1/min and the
  combo bonus never paid out. The agent observes its streak.
- **Combo bonus (off by default now):** the damage reward of the k-th hit of a combo is
  multiplied by 1.15^(k-1), capped at 2× (`--combo-growth`, `--combo-cap`):
  hit 2 counts 1.15×, hit 5 about 1.75×, hit 6+ double. Only *true* combos
  count, like Smash's combo counter: a hit continues your combo only if the
  victim was still stunned (in hitstun or knocked down). The game tracks it,
  and the agent observes its own count. (A first version counted any hit
  within a second without being hit back; that also rewarded interrupting
  an opponent who had already recovered, and helped up-tilt take over.) It's zero-sum: the
  player being comboed loses exactly as much. The game itself keeps combos
  honest: stale moves weaken repeats and the victim can DI out.
- **Counterattack bonus:** +0.1 at the start (`--counter-reward`, zero-sum,
  annealed like the rest) for a hit that is either a **counter**, landed within
  1 second of a successful defense (dodging through an attack with i-frames, a
  parry, or a shield block), or a **whiff punish**, landed on an opponent stuck
  in the recovery of a missed attack or grab, an aerial's landing lag, or a
  clank rebound. One bonus per hit even if it's both. Defending by itself earns
  nothing, so dodge spam isn't rewarded; the defense has to be converted. The
  zero-sum side makes whiffing costly, which pushes against spamming one move.
  The agent observes how much of its (and the opponent's) counter window is left.
- **Annealing:** all of these are multiplied by a scale that goes linearly
  from 1 down to 0.2 over the first half of training (`--shaping-anneal 0.5`,
  `--shaping-floor 0.2`), so KOs dominate later on. (Fading all the way to 0
  made a run drift into cautious spacing: hits per minute halved and
  timeouts rose from 8% to 37% once only KOs paid.)
  `charts/shaping_scale` shows it. The `charts/episodic_return` chart
  includes the damage term, so it drifts as the scale falls; judge progress
  by the win rates.
- **Exploration:** the entropy bonus starts at 0.03 and is lowered to 0.01
  over the first half (`--ent-coef`, `--ent-coef-final`, `--ent-anneal`;
  `charts/ent_coef`), so early training tries many different actions.

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
| `actions/effective_count` | well above 1 (45 = every action equally) | drops toward 1-3: collapsed onto a few actions |
| `actions/stick_*` | a spread of stick directions | |
| `charts/draw_rate`, `charts/timeout_rate` | low, falling | rising: stalling (the old runs' main failure) |
| `charts/win_rate_vs_frozen`, `_vs_pool`, `_vs_bots` | ~0.5-0.6 vs frozen; high vs bots | vs bots falling: forgetting basics |
| `selfplay/generation` | climbing steadily | flat for long: the learner can't beat its frozen copy |
| `states/*` | time spread over many states (RUN, AIRBORNE, SHIELD, ...) | IDLE very high: passivity |
| `mech/*_per_min` | hits, blocks, grabs, techs, short hops all > 0 | a mechanic at 0: unused (worth checking why) |
| `mech/streak_hit_share`, `mech/max_streak` | rising: hitting without getting hit | near 100% with one move dominating `moves/*`: a safe poke is being farmed |
| `mech/combo_hit_share`, `mech/max_combo` | some combos (share 10-40%) | near 100% with max_combo climbing: a loop is being farmed |
| `mech/di_rate`, `mech/tech_rate`, `mech/short_hop_share` | rising: the agent learns defense and movement tech | |
| `mech/counter_hit_share`, `mech/whiff_punish_share`, `mech/evade_conversion` | rising: defenses turn into punishes | `evades_per_min` high with conversion near 0: dodging for its own sake |

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
python train/record.py latest --opponent jabber   # the newest run (or give a path to any .pt); opponents: dummy, random, jabber, spacer, fighter, self
play.bat replay
```

`python train/replay.py` records a random-vs-random game, handy for testing
the viewer. Add `--p1 ID --p2 ID` to `play.bat replay` to pick the
characters ([assets.md](assets.md)).

To **play against** a trained agent yourself: `play.bat --vs` (the newest run)
or `play.bat --vs runs\<run>\agent_00600.pt`. It runs `train/serve.py`
(the network in PyTorch, over a local connection, about 1 ms per decision) with
the same reaction delay and frame skip as in training.

Replays store inputs, not the game code, so a replay recorded before a game
change desyncs on the new game. The previous version's replays play in the
frozen copy `D:\BOOTLEG-SSM-v1` (a git worktree at commit 98cdc91).

## History

`runs/monitor_notes.md` is the lab notebook: every overnight run, what was
watched, each intervention with its evidence, and the morning reports. The
decisions it led to are summarized in `docs/design.md`.
