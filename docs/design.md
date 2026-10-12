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

- **More moves (Sep 28, 2026):** 12 moves: ground/air x light/heavy x stick down/neutral/up (e.g. up-tilt, down-heavy, heavy down-air spike). Fast-fall needs down held 6 frames without attacking, so down + attack is a down move; the hold counter is observed so the policy stays Markov. A down input used by an attack must be released before it can fast-fall, and a whiffed air attack locks out air attacks until landing (hitting keeps them, so juggles work); dash then run; grounded fighters push each other apart but can't be pushed off the stage. Added after self-play converged to point-blank jab trading (40% of hits were same-frame trades, since fighters could stand inside each other).
- **Anti-collapse training (Sep 28, 2026):** scripted bots keep 10% of self-play games; entropy bonus 0.03 -> 0.01 over the first half; a zero-sum combo bonus multiplies the damage reward (1.15 per extra hit, cap 2x), annealed with it; the game tracks combos and the agent observes its own; the env counts mechanics (blocks, grabs, techs, DI, short hops, combos...) for logging.
- **Smash mechanics pass (Sep 28, 2026):** after self-play collapsed onto an up-tilt loop (1,105 up-tilt -> up-tilt combo links in 16 games) and 90% jab + up-tilt, the game gained Smash's counter-mechanics from SmashWiki: stale-move negation (9-hit queue, fresh bonus 1.05), hitlag and DI (15 degrees), shield / shield stun / shield break, roll, grab (light out of shield) with 4 throws, tech vs knockdown, short hop, aerial auto-cancel windows and less landing lag, two whiffed aerials allowed per trip in the air, 3 pass-through platforms, smoother and floatier movement, render interpolation in Godot. All mapped onto the existing 5 buttons + stick, so the action space is unchanged. The stale queue is deliberately not observed (it would be keyed by move ID).
- **Smash-accurate pass (Sep 29, 2026):** "agents can only be as good as the game", so the numbers now come from Smash Ultimate with Mario as the reference character and Battlefield as the stage (1 unit = 5 Smash units): Mario's speeds, jump heights, gravity and fall speeds; every move's frame data from ultimateframedata.com and BKB/KBG/angles from KuroganeHammer; SmashWiki's knockback formula (percent, damage, weight), launch speed 0.03/unit and decay 0.051, hitstun 0.4/unit - 1, tumble above 80, hitlag floor(0.65d + 6), Sakurai angle, crouch cancel 0.85, Ultimate shieldstun, parry, clanks and rebound, dodge staling, hitstun cancelling, 11-frame tech window with a 40-frame lockout, get-up options, grab hold 40 + 0.8% with mashing, directional air dodge, ledges (auto-grab, shrinking intangibility, 6-grab limit, edge-hogging, climb/jump/attack/roll) on a solid stage. New moves: jab 2 and 3, forward tilt, dash attack, get-up attack, ledge attack (23 moves, 24 states, obs 167). Still 5 buttons + stick. Kept on purpose: helpless after any air dodge (not Ultimate's rule), no specials.
- **Feel pass (Sep 29, 2026):** stage 10% wider (blast zones still 32 past the ledges), gravity and fall speeds x1.2 with jump heights kept, the most recently pressed button wins when several are held (holding jump used to swallow a mid-jump attack), ledge auto-release after 3 s.
- **Counterattack reward (Sep 29, 2026):** see Reward below; plus a scripted `fighter` bot that uses the whole toolkit, as a stronger yardstick.
- **Game additions (Sep 30, 2026):** 9-frame input buffer (Ultimate), smash attack charging (hold heavy, up to 60 frames, x1.4), pummels (light while holding, 1.3%, costs hold time), 7-minute games (at 3 minutes, evenly matched agents defending well timed out 27% of the time).
- **Input buffer for humans only (Sep 30, 2026):** ablations at 21M decisions (time limit, reward, seed, smash charge, buffer, one at a time) showed the buffer alone halved learning speed: an agent deciding every 4 frames with a 6-frame reaction delay has presses from earlier decisions fire unseen. `GameConfig::input_buffer` is per player: 9 for humans, 0 for agents (training env and live bot); replays record it.
- **Live bot over Python (Sep 30, 2026):** `play.bat --vs` runs the policy in PyTorch (`train/serve.py`) and Godot talks to it over local TCP, sending the same observation as training (`core/obs.cpp`, shared by both) with the same delay and frame skip. Chosen over running the network in C++ inside the game: the bot is exactly the training code, any future architecture works unchanged, and games can be logged for learning from humans later. About 1.1-1.5 ms per decision, far below the 16.7 ms frame.
- **Characters and animations (Sep 30 - Oct 1, 2026):** characters are visuals only (every character has the same hurtbox and moves). `tools/sprites.py` turns images of any size into 64x64 characters with permanent ids and horizontal-strip animations picked from the fighter's state (idle, run, jump/fall, hurt, shield, crouch, a move name, attack).
- **Character folders (Oct 12, 2026):** one folder per character in `assets/sprites/` (`static.png` + `<anim>.png` strips); bigger frames (128x128 sheets) are kept at their resolution and scaled per character (`px`, `ref_h`) instead of shrunk to 64x64.
- **Clank slide (Oct 1, 2026):** agent-vs-self replays showed jab-vs-jab clank loops for up to 20 s (96% of the time both jabbed again on the frame the rebound ended: the same network on both sides of an exactly mirrored game, and a clank costs nothing). Rebounding fighters now slide 0.75 apart (stopping at the edge), so each clank widens the gap and the loop ends within 3 clanks. Self-play replays went from 244 rebounds in 3 minutes to 0-6.
- **Dodge:** on the ground a spot dodge (i-frames, short recovery); in the air an air dodge (i-frames, then helpless until landing, as in Smash).

Left out so far: special moves (an up-B recovery is planned, together with the stage and blast zones), characters that play differently (they are skins for now).

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
| Episode | One game: stocks out or 7 min (25,200 frames, 6,300 decisions) | Time limit is a real ending (time left is observed) with a small tie-breaker reward |
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
- Counterattack bonus (Sep 29, 2026): a zero-sum bonus for hits that follow a successful defense (dodge through a hitbox, parry, shield block) within 60 frames, or that punish a whiffed move. Emitted by `step()` as `StepEvents::counter` / `whiff_punish`; the counter window is in the observation so the reward stays Markov.
- Hit-streak bonus (Sep 30, 2026): the damage reward of the k-th hit landed since you last got hit is multiplied by 1.1^(k-1), capped at 1.5x, zero-sum and annealed; it replaced the true-combo bonus, which paid out ~0.1 times per minute. A frame-perfect combo finder (`tools/combos.py`) showed the game allows plenty of true combos; the agents just preferred moves that don't lead anywhere. All shaping (damage, approach, streak, counter) anneals to a 0.2 floor over the first half of training.

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

- **Godot debug view (from phase 1 on):** a GDExtension that calls the same C++ step(), drawing fighters (now as character sprites), hitboxes as circles and damage % as text. You can play it with the keyboard now, and it plays back replay files once they exist. This is how you tune game feel and catch broken moves before agents exploit them, and every phase is checked visually here. It replaces the separate Python viewer that was planned earlier.
- **Tests (pytest via the bindings):** a determinism test (same random inputs twice, compare state hashes), unit tests for knockback and hitstun math, and a throughput benchmark run regularly.
- **Logging (TensorBoard or W&B):** KO rate, game length, damage per stock, move usage frequencies, action entropy. If a move's usage drops to 0%, the move is bad or the agent is stuck.

## Repo layout

```
RL-BRAWL/
  core/             # C++ sim: sim (state, step), moves (JSON loader), obs (what an agent sees), vecenv (N games for RL)
  bindings/python/  # nanobind module "fightcore": VecEnv for Python
  bindings/godot/   # GDExtension: the same core for Godot
  godot/            # the Godot project: main.gd (input, drawing, replays, live bot client)
  data/moves/       # JSON move definitions, shared by core and Godot
  assets/sprites/   # source images, one folder per character (static.png, animation strips)
  assets/characters/# processed characters, one folder per id, + characters.json (made by tools/sprites.py)
  train/            # ppo.py, bots.py, replay.py, record.py, serve.py (live bot)
  tools/            # bench.py, combos.py, sprites.py
  tests/            # pytest through the bindings
  docs/             # README (index), playing, setup, assets, engine, godot, python, training, design
  runs/, replays/   # training output and recorded games (not in git); runs/monitor_notes.md = lab notebook
```

## Phases and milestones

Step 0 is done at the end of phase 4; each phase has a gate that must pass before the next starts.

&#91;embedded content: phases 0–5 · each with its gate\]

**Decisions made during phases 1-3 (Sep 28, 2026):**

- Actions are egocentric like observations: player 1's world is mirrored, so its stick x is negated before reaching the game.
- Action space implemented as Discrete(36), so CleanRL's PPO is used unchanged apart from marked adaptations (`train/ppo.py`).
- Learner is always player 0; only its experience is trained on (both sides of latest-vs-latest games could be added later for 2x data).
- Timeouts: first implemented as truncations (bootstrap from V), then changed to real endings, since the time left is in the observation, plus a ±0.3 tie-breaker for the player ahead so stalling loses to dealing damage.
- Reaction delay 6 frames (0.1 s) by default in training: with 0, the agent could dodge every attack on reaction, so attacking first never paid and self-play stalled.
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
