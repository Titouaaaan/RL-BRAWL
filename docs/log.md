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

### 2026-10-12 · 5949abd, a65d35a · 2 s respawn wait + KO explosion
- Goal: Smash-like KOs: the fighter vanishes, an explosion plays, then it drops back in.
- Request: owner: explosion from `assets/effects/Explosion_1` (10 PNGs -> sprite sheet); after a KO wait 2 s before dropping in; the drop-in and its i-frames exactly as before.
- Decisions (Claude): game change in `core/`: `Fighter::respawn_wait` (`GameConfig::respawn_wait` = 120): `step()` skips the waiting fighter (frozen, intangible, inputs ignored); `invuln` (120) only starts counting at the drop-in. Obs: the invuln feature becomes (invuln + respawn_wait) / 120 (up to 2 while waiting): OBS_DIM unchanged, so old checkpoints load (they never saw values > 1, harmless for 2 s). Explosion: frames packed to 10 x 128 px; drawn at the KO point clamped into view and pinned to the screen while the camera moves; the camera holds still until the drop-in (owner: following the survivor made the explosion drift).
- Output: test extended (wait, ignored inputs, full i-frames after); all replays regenerated (old ones desync); KO rendered and checked.

### 2026-10-12 · 657a42d · Training dashboard in the menu
- Goal: browse training runs in the game, a nicer TensorBoard.
- Request: owner: a menu button -> list of models -> per-model page with curves, training stats, performance vs other models, network size, training regime; later: use the full window when full screen.
- Decisions (Claude): `tools/run_summary.py` caches each run in `runs/<run>/summary.json` (TensorBoard parsing takes ~1 min; curves bucket-averaged to 300 points, NaNs dropped; settings from `resume.pt`, layers from `agent_latest.pt`, Elo + head-to-head from `runs/ladder*.txt`); the menu starts it in the background. Godot draws charts itself (a Control's draw signal). Run page: Overview (regime in words, network, final agent vs the ladder, Elo chart, eval chart, all settings) + one tab per TensorBoard group. Training screens fill the window (`new_screen(..., fill)`); charts per row follow the width.
- Output: Training screen in `godot/menu.gd`; rendered at 1280x720 and 1920x1080 and checked. Not done: chart tooltips, text scaling with window size, auto-laddering new runs.

### 2026-10-12 · 0861fa6 · Game ending: 4 s after the last KO
- Goal: see the final KO's explosion and give the winner a moment before the result.
- Request: owner: result screen 2 s after the kill, the camera then pans to the survivor, who keeps control about 2 s, then the game freezes with "Player X won".
- Decisions (Claude): `step()` no longer returns early once `over` (VecEnv never steps a finished game, so training is unchanged; smoke hashes identical); a fighter with 0 stocks is skipped and intangible; after `over` a fall costs no stock (the smoke test caught the winner self-destructing, which could flip a 1-stock result). Godot: `END_DELAY` 240 frames (camera holds 120, then follows the winner), time-outs freeze at once; the final KO now triggers the explosion; replays stop reading at the game's end (recordings pad the last decision to 4 frames, which the sim used to ignore).
- Output: smoke test checks the ending; 66 tests pass; all 84 replays verify; ending rendered and checked.

### 2026-10-12 · 0a14142 · Repo made public
- Request: owner made https://github.com/Titouaaaan/RL-BRAWL public (it was private, see the 2026-10-10 entry).
- Decisions (owner): public; pushing still only when the owner asks.
- Then (owner): keep the art in the public repo (shown, not offered for download, as in public slides); credits + "don't reuse" in `assets/README.md` (homemade characters by the owner and @raigeki49, the rest from craftpix.net); root `LICENSE` = all rights reserved (CleanRL and godot-cpp stay MIT). Claude had flagged that craftpix.net's free license forbids redistributing the files in a way that makes them usable by others.

### 2026-10-13 · 9715734 · Network bots run in C++ (no Python to play)
- Goal: the game runs without Python/PyTorch: first step to a web export for the owner's site (spectate bot vs bot, replays), and to Linux.
- Request: owner wants the game on Windows and Linux, training staying on the Windows PC; then a page on their self-hosted Next.js site (Pi 5) to watch replays or spectate bots (playing later), working in all browsers, not only Chromium.
- Decisions: Claude proposed running the actor in C++ instead of PyTorch on the Pi; owner agreed. `core/net.cpp` (forward pass, then sampling by inverse CDF on a Godot `randf()`); weights as `.bin` (int32 obs/hidden/actions header, float32 layers), written by `ppo.py save_bot` next to every checkpoint and by `tools/export_bot.py`; menu levels exported to `data/bots/` (committed, 5 x 0.5 MB, since `runs/` isn't in git). Network bots take the C++ path on Windows too (one code path); `serve.py` keeps only the scripted bots and no longer imports PyTorch. Supersedes design.md's "Live bot over Python" (Sep 30). Unused `bot_ms` removed.
- Output: `tests/test_net.py` (C++ logits within 1e-4 of PyTorch, sampling, old-version refusal); a real checkpoint differs by 2e-5; 68 tests pass; headless and rendered bot vs bot checked.

### 2026-10-13 · 0c08f6c, 7e66e1b · Web export: the game runs in a browser
- Goal: a page on the owner's site (Next.js on a Pi 5) where visitors watch bot vs bot games or replays; the game runs in the visitor's browser, the Pi only serves files.
- Request: owner: test that it loads in all browsers, not only Chromium-based ones.
- Decisions (Claude): Godot web export with GDExtension, single-threaded (no cross-origin isolation headers, the usual Safari trouble); Compatibility renderer (OpenGL / WebGL 2) for every platform; everything the game reads goes through `Session.repo_root()`, which is `res://repo` on the web, filled by `tools/web_export.py` (PNGs kept raw with the "keep" importer); move files read by GDScript, the binding's `load_moves(dir)` removed (C++ can't read the packed files); Emscripten 4.0.20 (the version the engine reports; Godot's CI file said 4.0.11). Fix: our CMake project needs godot-cpp's `emsdkHack.cmake` to link a side module.
- Output: Menu, Bot vs Bot (Very hard vs Very easy) and KOs play in Chromium, Firefox and WebKit (Playwright); desktop replays still verify, OpenGL render checked. 52 MB (14 MB gzipped), 44 MB of it the GDExtension-capable engine. WebKit logs harmless `glBlitFramebuffer` WebGL errors. Testing note: a controller on the PC drives the menu in Chromium/Firefox (Gamepad API); the test hides it. Next: web-only spectate screen, replays from the site, then hosting.
- Then: the owner saw bots not loading on the web: the menus default to "Latest bot" (newest `runs/*/agent_latest.bin`), which the web build doesn't have. `Session.difficulties` drops it on the web (default becomes Very hard). Also fixed a step-1 regression: the Training list's "(bot levels)" tag compared run names to the bot paths, which now point to `data/bots/`; it uses `Session.LEVELS_RUN`.

### 2026-10-13 · pending · Web = desktop, docs/web.md
- Goal: the browser version has everything the desktop one has; one document to hand to the agent that will put it on the owner's site.
- Request: owner: "no difference between the browser and desktop game", keep "Latest bot" ("just fetch the latest weights"); a dedicated .md: how to build, use, how it was made.
- Decisions (Claude): "Latest bot" back on the web (reverts 7e66e1b's hiding): the export packs every `runs/*/agent_latest.bin`, the runs' `summary.json` (Training screen) and all of `replays/` (a snapshot: re-export after training); packed files have no dates, so the export writes `repo/mtimes.json` and `Session.modified_time()` reads it (newest bot, replay order). Kept differences, all forced by the browser: no Quit (it would freeze the page), summaries not rebuilt (no Python), no command-line options. Web section of setup.md moved to `docs/web.md` (build, hosting incl. Next.js iframe, caching, compression, how it works, testing, troubleshooting).
- Output: in Chromium, Firefox and WebKit: Latest vs Latest bot match, Training run page, and a Windows-recorded replay ending "Replay verified" (the WebAssembly sim matches MSVC bit for bit). 65 MB, 17 MB gzipped. Desktop unchanged (Quit, replay test, latest bot).
