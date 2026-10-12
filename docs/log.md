# Decision log

One entry per significant change, oldest first: what the owner asked for, what was decided and by whom, what came out.
Request "—" = not recorded. Entries before 2026-10-12 were rebuilt from commit messages and `docs/runs.md`.
Rule: add an entry for every significant change. When the hash isn't known yet, write `pending`, and fill it in the next time the log is updated.

### 2026-09-28 · a8b9472 · Phase 0: build plumbing
- Goal: a C++ core usable from Python, fast and deterministic.
- Request: build v0 as planned in design.md.
- Decisions: plain-struct core with a pure `step()`; nanobind batched VecEnv; MSVC + pip CMake; ASan smoke build.
- Output: movement-only sim, VecEnv, determinism test, benchmark.

### 2026-09-28 · da23677 · Godot view moved up from phase 5
- Goal: watch every phase.
- Request: —
- Decisions: GDExtension over the same `step()`; `play.bat` builds and launches.
- Output: playable debug view.

### 2026-09-28 · 1c778cd, b0942f8 · Phase 1: combat
- Goal: moves, hits, knockback, KOs.
- Request: —
- Decisions: moves as JSON data loaded without exceptions; events from `step()`.
- Output: state machine, 3 moves, hitboxes, stocks, Godot HUD, math tests.

### 2026-09-28 · d151b99, a8d0d49 · Phase 2: RL interface
- Goal: environment for PPO.
- Request: —
- Decisions: egocentric obs and actions (player 1 mirrored), rewards from StepEvents, frame skip, reaction-delay ring buffer, auto-reset.
- Output: obs 107, ~10M frames/s, symmetry tests.

### 2026-09-28 · 1cb4577 · Replays
- Goal: watch training games, catch Python/Godot drift.
- Request: —
- Decisions: inputs + move files + final hash; Godot re-simulates and verifies.
- Output: `train/replay.py`, `play.bat replay`.

### 2026-09-28 · 70c71dc, ac960e7, 25c2bb0 · Phase 3: PPO
- Goal: train agents.
- Request: —
- Decisions: CleanRL ppo.py vendored verbatim, every change marked `# OURS`, update untouched; learner = player 0; scripted bots.
- Output: rung 1 passed (100% vs dummy at 5M); `record.py`.

### 2026-09-28 · c8ad3ca · Dodge button
- Goal: defense.
- Request: —
- Decisions: 5th button (actions 36 -> 45); changing the button set again means retraining.
- Output: spot dodge, air dodge into helpless; obs 115.

### 2026-09-28 · fccb24d · Annealed shaping, mixed scripted rung
- Goal: denser early signal without hackable shaping.
- Request: —
- Decisions: potential-based approach + damage reward, annealed to 0; scripted rung mixes 4 bots (jabber + spacer alone gave a passive agent).
- Output: settable `damage_reward`.

### 2026-09-28 · 12113d5, cb580b8, e8a6b02, e0ff87f · Move grid, dash, body push
- Goal: end point-blank jab trading (40% of hits were same-frame trades).
- Request: —
- Decisions: 12-move grid by stick direction; fighters push apart; hold-to-fast-fall; whiffed aerials lock aerials until landing.
- Output: obs 121, controller support, checkpoint/replay cadence that keeps `runs/` small.

### 2026-09-28 · 1ec6ebf · Per-move stats
- Goal: spot unused or spammed moves.
- Request: —
- Decisions: per-move use and hit counts from StepEvents.
- Output: `moves/*` charts.

### 2026-09-28 · d753859 · Gated self-play
- Goal: fix live-vs-live mirroring (70% idle, down-tilt 78%).
- Request: —
- Decisions: frozen copy replaced at 55% wins over at least 200 games; retired pool 20%.
- Output: `selfplay/generation` chart.

### 2026-09-28 · db8435c · Reaction delay, rebalance, timeouts
- Goal: make attacking worthwhile; stop timeouts being free.
- Request: —
- Decisions: 6-frame delay; down-tilt and down-heavy nerfed; ±0.3 timeout tie-breaker; timeouts become real endings.
- Output: —

### 2026-09-28 · cfffdbf · `--resume`
- Goal: survive crashes and reboots.
- Request: —
- Decisions: resume.pt saved at every eval; pool capped at 50.
- Output: runs continue with the same TensorBoard curves.

### 2026-09-28 · 6e0f731 · Smash mechanics pass
- Goal: answer the up-tilt loop with game mechanics, not RL tweaks.
- Request: —
- Decisions: Smash counter-mechanics (SmashWiki) on the same 5 buttons; stale queue deliberately left out of the obs.
- Output: shield, grab and throws, roll, hitlag + DI, staling, tech, platforms; obs 145, 40 tests.

### 2026-09-28 · 3a9079e, 9ef4a6a · Anti-collapse training
- Goal: keep strategies diverse.
- Request: overnight run, monitor for collapse and fix it (owner).
- Decisions: combo bonus, 10% bots in self-play, entropy 0.03 -> 0.01, ~117 charts.
- Output: obs 149; `record.py latest`.

### 2026-09-29 · b9e6613 · Up-tilt collapse fix
- Goal: up-tilt reached 43% of moves at 96M.
- Request: overnight monitoring (owner).
- Decisions (Claude, overnight): nerf up-tilt, count only true combos, fix a grab-clash symmetry bug, restart the run.
- Output: 12-seed symmetry test.

### 2026-09-29 · 6dcceab · Shaping floor 0.2
- Goal: stop the drift to passivity once shaping hit 0 (hits/min 22 -> 10).
- Request: overnight monitoring (owner).
- Decisions (Claude): shaping floor 0.2.
- Output: run 3 had 0% timeouts and beat run 2 in 93% of games.

### 2026-09-29 · c7f5622, 3c56ba5 · Smash-accurate game + feel pass
- Goal: wide move variety, a realistic and complete game.
- Request: "agents can only be as good as the game" (owner, night 2 objectives); owner tested the feel in Godot.
- Decisions: Mario/Battlefield numbers, ledges, parry, clank, tumble; stage +10%, gravity x1.2, newest button wins.
- Output: 23 moves, 24 states, obs 167.

### 2026-09-29 · 91275b1 · Counterattack reward
- Goal: reward converting defense into hits.
- Request: —
- Decisions: zero-sum bonus for counters and whiff punishes; defense alone earns nothing.
- Output: obs 169, `test_counter.py`.

### 2026-09-29 · 98cdc91 · Fighter bot
- Goal: a stronger fixed yardstick.
- Request: —
- Decisions: a scripted bot that uses the whole toolkit.
- Output: `eval/win_vs_fighter`.

### 2026-09-30 · 6e52f52 · Input buffer, smash charge, pummel, 7-minute games
- Goal: a more complete game; fewer timeouts (27% at 3 minutes).
- Request: —
- Decisions: Ultimate's 9-frame buffer, charge up to x1.4, pummels, 7-minute limit.
- Output: obs 171.

### 2026-09-30 · f0ec6cb · Hit-streak reward + combo finder
- Goal: the combo bonus paid out ~0.1 times per minute.
- Request: —
- Decisions: streak multiplier 1.1^(k-1) capped at 1.5x; combo bonus off; `tools/combos.py` showed combos exist and the agent just doesn't pick combo starters.
- Output: obs 173.

### 2026-09-30 · a16736c, 3c20c3d · Input buffer off for agents
- Goal: find why learning ran 2x slower.
- Request: —
- Decisions (Claude): ablations of time limit, reward, seed, charge and buffer; the buffer was the cause, so it stays on for humans and off for agents; back to 7-minute games.
- Output: replays record the buffer setting; main run 150M (99.7% vs fighter).

### 2026-09-30 → 10-01 · 9f1fb97, 818e193, 84b2b52 · Characters and animations
- Goal: real visuals.
- Request: owner's sprites (DinoSailor, Titou).
- Decisions: characters are visuals only; `tools/sprites.py` makes 64x64 characters with permanent ids; animation strips.
- Output: `--p1/--p2`, state-driven animations.

### 2026-09-30 · a208326, 09023ab · Live bot
- Goal: play against an agent.
- Request: —
- Decisions: PyTorch over local TCP instead of running the network in C++ (same code as training); obs code shared in `core/obs.cpp`.
- Output: `play.bat --vs`, ~1.1-1.5 ms per decision.

### 2026-10-01 · 1eb3fc2, 2d1cc42 · Docs per theme
- Goal: owner-facing docs.
- Request: —
- Decisions: one file per theme plus an index.
- Output: docs/*.md.

### 2026-10-01 · 79aca79, 1744d90, 7f251d9 · League run + Elo ladder
- Goal: a stronger agent.
- Request: from-scratch run, ~8 h budget, older checkpoints as opponents (owner).
- Decisions: main run's checkpoints picked by p(1-p); anneals end at 150M; 480M decisions.
- Output: best agent so far (Elo 2251 vs 1367), `tools/ladder.py`.

### 2026-10-01 · 0971a91 · Controller layout
- Goal: Smash muscle memory.
- Request: —
- Decisions: Ultimate's default layout; right stick = smash attacks.
- Output: —

### 2026-10-01 · 602ac2e · Clank slide
- Goal: jab-vs-jab clank loops of up to 20 s in mirror replays.
- Request: owner saw greyed-out fighters in replays.
- Decisions: owner chose option 1 (push fighters apart) from Claude's three.
- Output: 244 rebounds -> 0-6 per game.

### 2026-10-01 · 8e15f99, d8c1ddc · Menu + replay set
- Goal: a usable game.
- Request: —
- Decisions: 6 bot levels from league-run checkpoints; the game starts its own bot servers; old replays deleted.
- Output: menu, `record_vs.py`.

### 2026-10-06 · e6431e7, 4135692, fc19ad3 · Animation polish
- Goal: owner's new Titou animations.
- Request: owner's art.
- Decisions: per-animation fps; `*_old` images ignored; less text on the fight screen.
- Output: —

### 2026-10-10 · 73feddc · Rename to RL-BRAWL
- Goal: project identity, GitHub.
- Request: owner.
- Decisions: private repo, push only when asked; v1 worktree dropped.
- Output: README.

### 2026-10-12 · ff0e60d · Decision log, cleanup, compact code
- Goal: track why things change; free disk space.
- Request: owner: v1 is done (up-B and a bigger stage come later); clean up `runs/`; code as compact as possible (ponytail ultra); this log.
- Decisions: keep only the league and main runs; log rule added to CLAUDE.md.
- Output: this file; runs/ 876 MB -> ~350 MB.

### 2026-10-12 · 1f154f6, d25c680 · Character folders + 3 animated characters
- Goal: easier navigation of assets; use the owner's 3 royalty-free sprite sheets (Fighter, Samurai, Shinobi).
- Request: owner: one folder per character, missing animations fall back to the static sprite.
- Decisions (Claude): `assets/sprites/<Name>/` with `static.png` and `<anim>.png`; sheets renamed to game names (Attack_1 -> attack, Attack_2 -> air, Attack_3 -> heavy; walk/dead unused); 128 px frames kept at full resolution, Godot scales per character (`px`, `ref_h`); generated output in `assets/characters/<id>/`.
- Output: characters 7-9 animated (idle, run, jump, hurt, shield, attacks); characters 1-6 regenerated pixel-identical. Fix after the owner's test: sprites sank to the knees (the canvas was centered on the body; 128 px sheets have the body only in the bottom 81 px), so the canvas bottom is now anchored at the feet.

### 2026-10-12 · 57b70d3 · Workflow: permissions, notebook in git, one record per purpose, log hook
- Goal: fewer permission stops; no lost records; less duplicated upkeep; logging guaranteed.
- Request: owner asked how to use Claude Code better; chose Claude's items 1-4 and 8.
- Decisions: project allowlist (`.claude/settings.json`: tests, module/extension builds, tools; read-only git is already auto-allowed; no delete rule, since a prefix rule can't stop `runs/../`); notebook moved to `docs/runs.md`; CLAUDE.md status cut to pointers, each record has one purpose; Claude renders frames before asking the owner to test visuals; `commit-msg` hook refuses code commits without a log entry (`[no-log]` for trivial fixes, never `--no-verify`).
- Output: `.githooks/commit-msg` (tested: refuses, escape works), rules in CLAUDE.md, setup.md step 5.

### 2026-10-12 · 258c97e, e80d508, 26cf51d · Map art: cloud background, tiled stage
- Goal: replace the grey debug stage with the owner's map assets.
- Request: owner: orange clouds as the default background; Tile_06/07/08 for platforms, Tile_04 to thicken the main stage.
- Decisions (Claude): one tile = 1 sim unit, tiles stretched slightly to fit each surface; main stage top row uses Tile_01/02/03 (same art without the bottom border, so it joins the Tile_04 fill seamlessly); background stretched over the window, no parallax.
- Then (owner): main stage drawn as an inverted pyramid (corners 1 tile thick, each lower row 1 tile shorter per side: 35 -> 25 wide); ledge dots hidden (`SHOW_LEDGES = false`). Claude flagged that collision stays the full rectangle (changing it = game change + retrain); owner to decide with the bigger-stage work.
- Then (owner): standalone editable pictures; Claude made the game draw them (`assets/maps/stage.png` 1120x192, `platform.png` 224x32, assembled from the tiles) instead of assembling tiles at runtime, so edits show in game.
- Output: `draw_map()` in `godot/main.gd`; Maps section in `docs/assets.md`. Rendered and checked.

### 2026-10-12 · 5949abd, pending · 2 s respawn wait + KO explosion
- Goal: Smash-like KOs: the fighter vanishes, an explosion plays, then it drops back in.
- Request: owner: explosion from `assets/effects/Explosion_1` (10 PNGs -> sprite sheet); after a KO wait 2 s before dropping in; the drop-in and its i-frames exactly as before.
- Decisions (Claude): game change in `core/`: `Fighter::respawn_wait` (`GameConfig::respawn_wait` = 120): `step()` skips the waiting fighter (frozen, intangible, inputs ignored); `invuln` (120) only starts counting at the drop-in. Obs: the invuln feature becomes (invuln + respawn_wait) / 120 (up to 2 while waiting): OBS_DIM unchanged, so old checkpoints load (they never saw values > 1, harmless for 2 s). Explosion: frames packed to 10 x 128 px; drawn at the KO point clamped into view and pinned to the screen while the camera moves; the camera holds still until the drop-in (owner: following the survivor made the explosion drift).
- Output: test extended (wait, ignored inputs, full i-frames after); all replays regenerated (old ones desync); KO rendered and checked.
