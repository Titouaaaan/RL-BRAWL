# Adaptive Fighting Game Bot — v0 Design Plan

Sep 28, 2026 · @Titouan

## Overview

v0 is done when two agents, trained purely through self-play, fight competently in a minimal Smash-style game. Every early decision is made so the later goals (a learned opponent latent z, a growing move pool, adapting to human players) slot in without a rewrite.

&#91;embedded content: system architecture · one core, two front ends\]

The core never calls the engine. Godot and the training code drive the same step() function, so the bot learns exactly the game the player plays.

The core is engine-free for speed. Training needs roughly 10^8 to 10^9 frames: months in real time at 60 fps, minutes of pure simulation at about 10^6 steps/s. The goal is that the RL side, not the game, sets training speed.

## v0 game scope

v0 is one character in a mirror match on one flat stage. This is enough to produce spacing, whiff-punishing, combos and edgeguarding, which is all self-play needs to show whether it learns anything interesting.

- **Simulation:** 60 Hz fixed timestep, integer frame counter.
- **Stage:** one flat floor with blast zones on all four sides. Platforms and ledges come later.
- **Rules:** Smash-style percent damage, knockback that grows with percent, KO by leaving a blast zone, 3 stocks or a time limit.
- **Movement:** walk, one ground jump plus one double jump, fast-fall.
- **Light attack:** fast startup, low damage, little knockback.
- **Heavy attack:** slow startup, high damage, KO-capable knockback.
- **Aerial attack:** one attack usable in the air.

- **More moves (Sep 28, 2026):** 12 moves: ground/air x light/heavy x stick down/neutral/up (e.g. up-tilt, down-heavy, heavy down-air spike). Fast-fall needs down held 6 frames without attacking, so down + attack is a down move; the hold counter is observed so the policy stays Markov; dash then run; grounded fighters push each other apart but can't be pushed off the stage. Added after self-play converged to point-blank jab trading (40% of hits were same-frame trades, since fighters could stand inside each other).
- **Dodge:** on the ground a spot dodge (i-frames, short recovery); in the air an air dodge (i-frames, then helpless until landing, as in Smash).

Left out of v0: shield, grabs, ledges, specials, multiple characters.

## Simulation core (C++)

The whole game state is one plain struct and the whole game is one pure function. No hidden state, no globals, no engine calls.

```cpp
struct GameState { Fighter f[2]; std::vector<Hitbox> hitboxes; int frame; /* ... */ };
struct Input { int8_t stick_x, stick_y; uint8_t buttons; };

// Pure and deterministic: same state + same inputs -> same result.
void step(GameState& s, const Input in[2], const GameConfig& cfg, StepEvents& ev);
```

This gives determinism, trivial clone and reset, and free replays: a replay is just a seed plus the input sequence.

### Fighter state machine

Each fighter has a state (Idle, Walk, JumpSquat, Airborne, Landing, Attacking, Hitstun, Respawning) plus a frame counter within it. Most game logic is "given state, frame counter and input, what is the next state?" The same state and counter go straight into observations.

### Moves as data

Each move is a JSON file: startup, active and recovery frames, and per active frame a list of hitboxes (offset, radius, damage, base knockback, knockback growth, launch angle). The core loads these into a MoveTable.

- Tune the game without recompiling.
- Godot reads the same files, so visuals and simulation never drift apart.
- Move randomization for training later is just perturbing the table.

### Physics

Custom kinematics, no physics engine: gravity, max fall speed, fast-fall, air drift, ground friction, and knockback velocity that decays each frame. Stage collision is a floor segment. Hitboxes are circles; each fighter's hurtbox is a capsule or box.

### Combat

When an active hitbox overlaps the opponent's hurtbox: apply damage, compute knockback as roughly base + growth × damage%, set velocity along the launch angle, and put the victim in hitstun for frames proportional to knockback. Each move use hits a given opponent at most once. Hitlag can wait.

### Events

step() also returns what happened that frame: hits, damage amounts, KOs. The RL wrapper builds rewards from these events rather than by diffing state.

### C++ rules for a beginner

Plain structs, std::vector and fixed-size arrays only; no raw new or delete. Develop in debug mode with AddressSanitizer on, so memory bugs crash immediately and readably.

## RL interface

Actions are controller inputs and moves are observed through their properties, so neither space changes when the move pool grows.

| Element | v0 choice | Why |
| --- | --- | --- |
| Action space | stick x ∈ {−1,0,1}, stick y ∈ {−1,0,1}, button ∈ {none, jump, light, heavy, dodge}: 45 discrete | Fixed (dodge added Sep 28, 2026; changing it again means retraining); the game decides which move comes out |
| Frame skip | Decide every 4 frames (15 Hz), repeat action between; configurable | Human-like input rate, 4× shorter horizon |
| Reaction delay | Observe the state k frames ago via a ring buffer; k = 0 in v0 | Nearly free now, painful to retrofit; needed later for fairness |
| Episode | One game: stocks out or 3 min (10,800 frames, 2,700 decisions) | Time limit = truncation, bootstrap from V |
| Discount | γ ≈ 0.995 (about 13 s horizon at 15 Hz); sweep it | Roughly one stock's worth of planning |

### Observation (v0: flat vector, egocentric)

Canonicalize so the agent is always player 0 and mirror x to a consistent side or facing. One policy plays both sides, which doubles the data.

- **Per fighter:** position, velocity, damage %, stocks, grounded flag, jumps left, facing, state one-hot, frame within state, hitstun frames left.
- **Relative:** position between fighters, distances to blast zones and stage edges.
- **Hitbox entities:** padded list of N = 4, each with relative position, radius, damage, knockback, frames left, owner, mask bit.

In v0 these are flattened into an MLP input. Because hitboxes are already entities, moving to a set or transformer encoder later changes only the network.

### Reward

- +1 for KOing the opponent, −1 for being KOed.
- Shaping: c × (damage dealt − damage taken), c ≈ 0.01 per percent; consider annealing c toward 0.
- No positional shaping such as "stay near center"; it is easy to hack.

### Vectorization

The core exposes a batched VecEnv holding N games, stepped in one C++ call. It takes an action array and writes observations, rewards and done flags into preallocated NumPy buffers (zero-copy via nanobind), with auto-reset inside. A thin Gymnasium-compatible wrapper sits on top. No Python loop over environments.

## Training plan (step 0: self-play)

PPO with shared weights for both players, trained against a pool of past snapshots, and judged by fixed baselines and by watching games.

### Algorithm

A single-file CleanRL-style PPO you fully control rather than a large framework. Only the learning side's experience is used for updates; the opponent side is frozen.

### Opponent sampling

Pure "play the latest self" cycles and forgets. Save a checkpoint every K updates and sample opponents about 80% latest, 20% from the pool, optionally weighted toward snapshots the current agent loses to (prioritized fictitious self-play).

### Evaluation

Self-play reward always hovers around zero, so it says nothing about progress. Track instead:

- Win rate against fixed scripted baselines: random, "walk toward opponent and jab", and "space and punish with heavy".
- Elo across the snapshot pool.
- Videos. Watching games is the most important evaluation; degenerate strategies only show up this way.

### Sanity-check ladder

Each rung catches a different class of bug (environment, reward, or PPO). Only move up once the current rung works.

1. Stationary dummy: the agent learns to approach and KO it.
2. Random agent.
3. Scripted bots.
4. Full self-play.

## Tooling

Build a debug viewer, tests and logging early; they are how you will see what the agents are actually doing.

- **Godot debug view (from phase 1 on):** a GDExtension that calls the same C++ step(), drawing fighters as boxes, hitboxes as circles and damage % as text. You can play it with the keyboard now, and it plays back replay files once they exist. This is how you tune game feel and catch broken moves before agents exploit them, and every phase is checked visually here. It replaces the separate Python viewer that was planned earlier.
- **Tests (pytest via the bindings):** a determinism test (same random inputs twice, compare state hashes), unit tests for knockback and hitstun math, and a throughput benchmark run regularly.
- **Logging (TensorBoard or W&B):** KO rate, game length, damage per stock, move usage frequencies, action entropy. If a move's usage drops to 0%, the move is bad or the agent is stuck.

## Repo layout

```
fightgame/
  core/            # C++ sim: state, step, moves, collision, VecEnv (CMake project)
  bindings/python/ # nanobind module exposing VecEnv to Python
  bindings/godot/  # GDExtension + Godot project, built from phase 1 on
  data/moves/      # JSON move definitions, shared by core and Godot
  train/           # PPO, self-play pool, eval, logging (PyTorch)
  tools/           # debug viewer, replay tools, benchmarks
  tests/
```

## Phases and milestones

Step 0 is done at the end of phase 4; each phase has a gate that must pass before the next starts.

&#91;embedded content: phases 0–5 · each with its gate\]

**Decisions made during phases 1-3 (Sep 28, 2026):**

- Actions are egocentric like observations: player 1's world is mirrored, so its stick x is negated before reaching the game.
- Action space implemented as Discrete(36), so CleanRL's PPO is used unchanged apart from marked adaptations (`train/ppo.py`).
- Learner is always player 0; only its experience is trained on (both sides of latest-vs-latest games could be added later for 2x data).
- Timeouts are truncations: PPO bootstraps with γ·V(final observation).
- Reward shaping: potential-based approach shaping (Φ = −distance) plus a damage term, both annealed linearly to 0 over the first half of training, leaving KO-only reward. No per-step time penalty: γ already favors fast KOs, and a penalty invites self-destructing when behind.
- The `scripted` rung trains against all four bots mixed; jabber+spacer only produced an agent that never attacked a passive opponent.
- Replays are JSON: per-frame inputs of both players, a copy of the move files, and the final state hash. Godot re-simulates and verifies the hash, which also checks that the Python and Godot builds of the core agree.
- No Gymnasium wrapper: two-player self-play doesn't fit its single-agent API.
- A move's hitboxes each have a start..end frame window (instead of a list per active frame); respawn uses an invulnerability timer instead of a Respawning state.

**Plan change (Sep 28, 2026):** the Godot front end moves from phase 5 to right after phase 0 and grows alongside the core, so that every phase can be followed visually. Phase 5 keeps only the player-facing work (playing against a trained bot, polish).

Phase 0 is small but, for a C++ beginner, it is where the most frustrating hours go (build systems, not the language), so it is kept separate. Claude Code is well suited to it.

## Known pitfalls

| Symptom | Likely cause | Fix |
| --- | --- | --- |
| Agents run away and time out | Being hit costs more than hitting gains | Draws worth 0, symmetric shaping, small tie-breaker on stocks and damage at timeout |
| One move spammed, jab locks, infinite combos | A broken game mechanic, not an RL bug | Fix it in the move JSON |
| Entropy drops, usage collapses to 1–2 moves | Self-play collapsed to a narrow strategy | Snapshot pool, entropy bonus; later the style-diverse league |
| Silent corruption, random crashes | C++ memory bugs | Plain structs and std::vector, AddressSanitizer in debug builds |

## After step 0: hooks already in place

The v0 choices above are the foundation for the adaptive bot; this is what each one enables later.

- **Learned opponent latent z:** the reaction-delay ring buffer and replay format already store the history an encoder needs. Train a bot conditioned on z against a style-diverse league, with an auxiliary identity or contrastive loss; at runtime only z (or the player model) updates.
- **Growing move pool:** controller-level actions never change; hitbox entities describe moves by properties, not IDs; data-driven moves allow randomizing frame data during training so new moves are in-distribution.
- **Human-like, fair play:** reaction delay, frame skip, and later a KL penalty toward a behavior-cloned human prior (piKL-style).
- **Difficulty:** two separate knobs. Knowledge of the player only grows; execution strength (delay, noise, skill conditioning or checkpoint choice) is tuned to keep the player's win rate around 40–60%.
