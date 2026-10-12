# Overnight monitoring notes (2026-09-29)

- 00:31 rung 3 (scripted, 20M) done: 100% vs dummy/jabber/spacer, 97% vs random; effective actions 27/45; moves heavy 42%, jab 15%, down_heavy 15%; idle 1%, draws 0. Self-play started (iter 125/4577, ~4M): moves spread (max 19%), 1 generation cycle in progress, eval vs bots 69-100%. Healthy, no action.
- 00:45 self-play ~19M: effective actions 43/45, max move 19%, idle 1%, draws 0, vs bots 100%, 10 generations. Entropy still high (3.54, bonus 0.03 until annealed); timeouts 40-90% because play is still near-random. No action. Added 'record.py latest' for the owner.
- 01:07 self-play ~43M (iter 1320): effective actions 41.6, max move heavy 24%, aerials 2-6% each, idle 1%, draws 0, timeouts fell to 0 (games ~1650 decisions, real KOs), combo hits 3.1/min, blocks 0.9/min, techs 3.1/min, eval 100% vs all 4 bots, 24 generations, entropy 3.09 (bonus now 0.02). Healthy, no action.
- 01:43 INTERVENTION. Self-play ~96M: up_tilt share rose steadily 8% -> 43% since ~48M (still rising after the anneals ended at 75M). Effective actions 36 (from 42). Combo analysis of the latest agent (12 games): up_tilt -> up_tilt = 60% of combo links. At those follow-up hits the victim was ATTACKING 42% of the time, in hitstun only 30%: up-tilt beat everything (fast 4f startup, hitbox covering the front, 20f total), and the "hit within 1 s" combo rule gave those interrupts a combo bonus before 75M.
  Fixes (commit after 9ef4a6a): up_tilt nerf (5f startup, higher smaller hitbox, 24f total); combos = true combos only (victim still stunned); ALSO found and fixed a mirror-symmetry bug: a same-frame grab clash was resolved in player order (1 in 20 random games diverged; test now 12 seeds x 1500 steps). Old run archived as runs/uptilt-collapse__selfplay__...; self-play restarted from scratch (150M).
- 02:20 new run ~34M (iter ~1050): moves spread (heavy 24-30%, down_heavy ~18%, up_heavy ~14%, jab ~10%, up_tilt 5%, aerials 1-7% each), no move rising steadily; eval vs bots 94-100%; 20+ generations, each beaten at 55-60%. True combos are rare now (0.1-0.4/min, max 3), as expected with the stricter rule. Gap to watch: grabs/throws ~0 and blocks 0.1-0.5/min (shield-grab game unused). (Bash safety check was unavailable; read the log with Grep.) No action.
- 03:01 ~95M (iter 2880, past the 75M anneal end): effective actions 39 (stable 37-39 since 60M), jab rising 14% -> 30-33% over the last ~20M, then heavy/down_tilt/up_tilt 10-12% each, aerials 2-7% each; blocks up to 2.6/min (shield use emerging), grabs 0.2/min; eval 100% vs all bots; 57 generations; idle 2%, draws 0. Watch: jab trend and timeout_rate (0.07 -> 0.28). Not a collapse yet; no action.
- 03:32 ~143M (iter 4357/4577, ends in minutes): jab settled back to ~19% (peak 33% was temporary); spread: jab 19, heavy 16, up_tilt 12, down_tilt 12, up_heavy 10, down_heavy 7, aerials 2-5% each; effective actions ~39 steady; eval 100% vs all bots; 81 generations; idle 2%, draws 0. Timeout rate crept up 0.08 -> 0.37 since ~100M with longer games (720 -> 1530 decisions), while blocks 2.2/min, DI 95%. Not a diversity collapse; run left to finish. Final analysis after it ends.
- 03:40 RUN 2 FINISHED (150M, commit b9e6613). Diversity held all the way: effective actions ~39 at the end; final moves jab 19, heavy 17, up_tilt 12, down_tilt 11, up_heavy 10, down_heavy 8, aerials 2-6% each; grabs/throws ~0; eval 100% vs all 4 bots; 83 generations; idle 2%, draws 0. Replays recorded (replays/recorded/...agent_latest_vs_{self,jabber,spacer}.json, all verified).
  BUT after the shaping hit 0 at 75M the agent engaged less: hits/min 22 -> 10.5, damage/min 154 -> 74, time up close 41% -> 28%, RUN 6% -> 10%, timeouts 8% -> 37%, games 720 -> 1530 decisions. Cautious spacing, not idling. INTERVENTION: shaping floor 0.2 (commit 6dcceab): damage/approach/combo shaping never drops below 20%. Run 2 archived as runs/baseline-nofloor__selfplay__...; run 3 started from scratch (identical until ~60M, then the floor).
- 04:24 run 3 ~55M (iter 1672): healthy and tracking run 2 so far (the floor only kicks in at ~60M). Effective actions 39.6; moves jab 19, heavy 23, down_tilt 14, air 7 ...; hits 22/min, blocks 3.7/min, grabs 0.9/min (more than run 2), eval 100% vs bots, 41 generations, idle 2%, draws 0. No action. Key comparison comes at 75-150M (hits/min and timeout_rate vs run 2).
- 05:10 run 3 ~125M (iter 3825): the floor works. vs run 2 at the same point: hits/min 31-34 vs 14, time up close 43% vs 31%, timeouts 0% vs 43%, draws 0. Diversity similar (effective actions ~37 vs ~39). Moves: jab 22, up_tilt 18, down_tilt 17, heavy 8, up_heavy 8, aerials 1-6% each; grabs 1.5/min, throws appearing, combo hits 2.1/min. Eval 100% vs bots, 70 generations. Watching the light moves' growth (up_tilt 6% at 55M -> 18%). No action.
- 05:26 RUN 3 FINISHED (150M, commit 6dcceab, shaping floor 0.2).

## Morning report

Three self-play runs overnight; two interventions.

| | Run 1 (stopped at 96M) | Run 2 (150M, no floor) | Run 3 (150M, floor 0.2) |
|---|---|---|---|
| Top moves at the end | up_tilt 43% and rising | jab 19, heavy 17, up_tilt 12 | jab 20, up_tilt 20, heavy 15, down_tilt 14 |
| Effective actions (of 45) | 36, falling | ~40 | ~38 |
| Hits / min | - | 10.5 (from 22) | 31-35 |
| Timeouts | - | 32-43% | 0% |
| Combo hits / min (true combos) | (old rule) | 0.2 | 2.0 |
| Grabs / min | ~0 | 0.05 | ~1 (throws appear) |
| Eval vs 4 bots | 100% | 100% | 100% |
| Head-to-head | | | run 3 beats run 2 in 93% of 128 games |

Interventions: (1) up-tilt dominance -> up-tilt nerf, true-combo rule, grab-clash symmetry bug fix (b9e6613); (2) disengagement after the shaping hit 0 -> shaping floor 0.2 (6dcceab).
Open points: aerials stay at 1-6% each and throws ~1%; up_tilt plateaued at 16-23% in run 3 (watch, not a loop: true combos max 3).
Replays: replays/recorded/selfplay__ppo__1__1790645938_agent_latest_vs_{self,jabber,spacer}.json (verified).

# Night 2 (2026-09-30)

Objectives (owner): wide move variety without one-move abuse (combos OK); a realistic, responsive, complete game.
- Resumed run selfplay__smash__1__1790682066 at iter 3101 (of 4577). At ~102M: effective actions ~37, 13 moves >= 2% (top: heavy 18, air 18, forward_tilt 12, down_heavy 11, heavy_air 10), timeouts back to ~4% after a ~26% bump, eval vs fighter 99%, 74 generations. Techs ~1% of knockdowns (agents don't bother: get-ups are intangible, knockdowns rarely punished). Jab chain and tilts ~0-2%.
- Prepared while it runs (built after it ends, can't rebuild the module while training has it loaded): 9-frame input buffer, smash attack charging (hold heavy, up to 60 f, x1.4), pummels (light while holding, 1.3%, costs 12 f of hold). Obs 169 -> 171 (charge). Bots release heavy while attacking.
- Owner stopped the smash run at iter ~3840 (~126M) to move on. Replays recorded: replays/recorded/selfplay__smash__1__1790682066_agent_latest_vs_{self,fighter,spacer}.json. They DESYNC on the new game, so a frozen copy of the old game lives in D:\BOOTLEG-SSM-v1 (git worktree at 98cdc91, Godot project imported); verified there.
- Game (6e52f52): input buffer 9 f, smash charge, pummel, 7-minute games (timeouts had hit 27%: evenly matched defenders, not stalling).
- Combo finder (tools/combos.py): true combos exist with frame-perfect play from up-tilt, up/down throw, down-tilt, down-air, jab chain at 0-80%, mostly reactable (start 8+ f after the hit). Agent's favorite moves (fsmash, nair, ftilt, dsmash, heavy air) are poor starters -> true combos 0.1/min is an agent choice, not a game limit. No move data change.
- Reward (f0ec6cb): hit-streak multiplier 1.1^(k-1) cap 1.5 (hits since last hit taken; dodges don't break it), zero-sum, annealed; combo bonus off. Obs 173.
- New run started: selfplay__streak__... (150M), log runs/streak_train.log. Watch: moves/* concentration (streak farming with one safe poke), streak_hit_share, timeouts (7-min limit), hits/min, share_close.
- 30 min, streak run ~21M (iter 650, ~21 iter/min -> ~3 h total): healthy diversity (effective actions 42, top move heavy 20%, 15 moves >= 1%), no timeouts, streak hits ~48% of hits (max 17). Learning looks slower than the smash run at the same point (hits/min 5.8 vs ~10, time close 16% vs 23%, eval vs fighter 32% vs 52%, 11 vs 14 generations), plausibly because 7-minute games give fewer game endings per decision early on. Watching; no action.
- 45M check (7-min run, iter 1400): catching up but still behind the old 3-min run at the same point: hits/min 8.3 vs 11.8, KOs/min 0.85 vs 1.5, time close 19% vs 33%, generations 19 vs 35, eval vs fighter 88% (latest) vs 92%. Per the agreed rule (< ~9 hits/min), stopped it (archived as runs/7min-ablation__selfplay__streak__...) and restarted with 3-minute games, everything else identical (commit a16736c): run selfplay__streak3min__..., log runs/streak3min_train.log. Compare at 20M and 45M: if it tracks the old run, the time limit was the cause; if not, investigate buffer / charge / pummel / streak reward.
- ~21M check of the 3-min rerun: it tracks the 7-min run, not the old run (hits/min 4.7 vs 5.3 vs old 11.5; attack share 75% vs 74% vs old 57%; eval vs fighter 38% vs 37% vs old 65%). The time limit is NOT the cause.
  Game-dynamics checks (same policies, scratch builds of the module): input buffer off -> random policy attack share 62% vs 61%, 21M agent hits/min 5.2 vs 5.0; charge off -> 61% / 5.5. Neither the buffer nor charging changes how the game plays for these policies. The old run just learned to attack less by 15-21M; the new ones haven't.
  Next: reward ablation (old reward: streak off, combo 1.15 cap 2), same game, 3-min. Run archived: runs/3min-ablation__selfplay__streak3min__...
- ~21M reward ablation (old reward: combo on, streak off, current game, 3-min): just as slow (hits/min 5.2, timeouts 81%, eval vs fighter 40% vs old run 11.5 / 0.3% / 65%). The streak reward is NOT the cause. Archived: runs/reward-ablation__...
  Remaining: (1) seed luck (all new runs share seed 1 and near-identical code, so they follow near-identical trajectories), (2) something else in the game changes.
  Decisive test, two runs in parallel (half speed each): A = OLD game+code (worktree D:\BOOTLEG-SSM-v1 @98cdc91, module built to scratch fc_v1) seed 2, log runs/oldgame_seed2.log, run dir D:\BOOTLEG-SSM-v1\runs; B = current code seed 2, log runs/newgame_seed2.log. Compare at ~21M.
- ~21M seed test: old game seed 2 is fast like old seed 1 (hits/min 10.6, timeouts 0.9%, 17 generations); new game seed 2 is slow like new seed 1 (5.0, 78%, 9). NOT luck: the game changes (buffer / charge / pummel) slow learning, even though fixed-policy dynamics looked unchanged.
  Next: training ablations in parallel, seed 1 (compare with new-game seed 1): C = buffer off (runs/nobuffer.log), D = charge off (runs/nocharge.log). Modules built to scratch fc_buf0 / fc_charge0.
- ~21M buffer/charge ablations (seed 1): buffer OFF restores the old learning speed (hits/min 10.7 vs 4.7 with it, KOs 1.75 vs 0.50, timeouts 0.5% vs 83%, 17.6 vs 9.9 generations); charge off changes nothing (5.2). CAUSE: the 9-frame input buffer. The agent decides every 4 frames and sees the game 6 frames late; buffered presses from earlier decisions fire later, invisible in its observation, so its actions happen at moments it didn't choose (hurts credit assignment).
  FIX: buffer stays for humans (GameConfig default 9, Godot) but the Python VecEnv defaults to input_buffer=0; replays record it and Godot sets it (set_input_buffer). Back to 7-minute games (the time-limit ablation showed no effect on learning). All ablation runs archived as runs/*ablation*.
- ~21M main run (buffer off for agents, 7-min, streak reward; commit 3c20c3d): learning speed recovered (hits/min 9.3 vs 4.7 with the buffer; old run 11.5, 3-min no-buffer ablation 10.7), no timeouts, 13 generations (old 17), eval vs fighter 46%, effective actions 43. Slightly behind the 3-min no-buffer ablation, possibly the longer games; within noise for now. Moves: heavy 30% (the old run had 32% at this stage), down_heavy 15%, down_tilt 9%. No action; next check ~45M.
- ~46M main: caught up with the old run (38-46M: hits/min 12.7 vs 11.8, KOs 1.50 vs 1.53, time close 33% vs 33%, eval vs fighter 92% vs 92%, timeouts 0% vs 7.5%). Streaks: 52% of hits, max ~11. Counters 1.8/min, blocks 2.3/min rising. Moves: heavy 27, down_heavy 14, forward_tilt 14, air 13, then 11 moves at 1-5%; effective actions 41. Healthy; no action. Next check ~75M (end of the shaping anneal).
- ~75M main (shaping now at its 0.2 floor, entropy anneal done): hits/min 15.2, time close 41%, 0 timeouts, eval vs fighter 99%, 54 generations. Defense growing: blocks 5.1/min, counters 3.0/min, grabs 0.7/min. Streak share steady ~53% (max ~11): no streak farming. WATCH: moves are concentrating on a core of three: heavy 30, air 20, forward_tilt 20 (68% together, vs 54% at 46M); effective actions 43 -> 38; 10 moves >= 1% (15 at 46M). Shares fluctuate rather than climb steadily (heavy 35->19->28%), and all three have healthy hit rates (30-40%). Not a one-move collapse; next check ~100M. Intervene if one move keeps climbing past ~40% or the top three pass ~80%.
- ~100M main: stable. hits/min 14-15, time close ~40%, 0 timeouts, eval vs fighter 100%, 67 generations, blocks ~4.8/min, counters ~2.1-2.9/min, streak share ~51%. Moves now: heavy 25, air 25, forward_tilt 16, down_heavy 8, heavy_air 6, dash_attack 4, then 8 moves at 1-3% (14 moves >= 1%). Top three ~66% and flat since 75M; neutral air rose slowly (12 -> 26% since 45M), forward smash flat ~25%. Effective actions 41 -> 35.5 (entropy anneal). Below the intervention thresholds; letting it finish (~150M).
- MAIN RUN FINISHED (150M, commit 3c20c3d). Replays: replays/recorded/selfplay__main__1__1790733656_agent_latest_vs_{self,fighter,spacer}.json (all verified in Godot).

## Morning report (night 2)

What happened:
1. Resumed the smash run, then stopped it at ~126M (owner) and recorded its replays; they only verify in the frozen old-game copy D:\BOOTLEG-SSM-v1.
2. Game upgrade (6e52f52): 9-frame input buffer, smash charging (hold heavy, up to 1.4x), pummels, 7-minute games.
3. Combo finder (tools/combos.py): the game supports true combos (up-tilt, up/down throw, down-tilt, down-air, jab chain at 0-80%), mostly reactable; agents just don't pick combo starters. Added the hit-streak reward instead (f0ec6cb).
4. The new run learned ~2x slower. Ablations at 21M (time limit, reward, seed, charge, buffer): the input buffer alone was the cause. Fix (3c20c3d): buffer kept for humans, off for agents; replays record it.
5. Main run, 150M: healthy all the way, no intervention needed.

| end of run | night-1 run 3 (old game) | smash run (126M) | main run (150M) |
|---|---|---|---|
| hits / min | 33 | 16 | 15 |
| KOs / min | 2.1 | 1.5 | 1.4 |
| time close | 44% | 37% | 39% |
| timeouts | 0% (3 min) | 20% (3 min) | 0% (7 min) |
| effective actions (of 45) | 38 | 37 | 38 |
| moves >= 1% / top 3 share | 13 / 56% | 13 / 52% | 13 / 67% |
| top moves | jab 21, up_tilt 20, down_tilt 15 | air 22, ftilt 16, fsmash 14 | air 27, ftilt 21, fsmash 19 |
| true combo hits / min | 2.3 (old combo rule) | 0.1 | 0.3 |
| streak hits (share) | - | - | 50% |
| counters / whiff punishes / min | - | 1.6 / 4.6 | 2.1 / 4.4 |
| blocks / parries / grabs / min | 2.2 / - / 1.0 | 2.3 / 0.5 / 1.0 | 4.1 / 0.2 / 0.5 |
| wins vs fighter bot | - | 99.6% | 99.7% |

Open points:
- Variety: no single-move abuse (max 27%) and 13 moves in use, but a core of three (neutral air, forward tilt, forward smash) carries 67%. Jab chain, throws and tilts other than forward tilt are rarely used.
- True combos are up 3x with the streak reward but still rare (0.3/min); the old game had 2.3/min (different rules and knockback).
- Techs ~0 (knockdowns rarely punished). Parries fell back to 0.2/min.
- Not yet tried by the owner: input buffer, smash charging, pummels in Godot.

# League run (2026-10-01)

Owner asked for a from-scratch run with an ~8 h budget and older checkpoints as opponents.
- Commit 79aca79. 480M decisions; shaping + entropy anneals end at 150M (0.3125); LR decays over the whole run.
- Opponents: 55% frozen (gate), 15% own retired pool, 20% league = the 19 checkpoints of runs/selfplay__main__1__1790733656 (agent_00250..04500 + latest), picked with weight p(1-p)+0.02 (p = learner's moving-average win rate vs that checkpoint; switch every 12 iterations), 10% scripted bots.
- New yardstick: eval/win_vs_league_best (32 games vs the old 150M agent at every eval); league/win_rate_* per checkpoint.
- Log: runs/league_train.log. Watch: move concentration (top-3 was 67% at the end of the main run), timeouts, hits/min, and when win_vs_league_best crosses 50%.
- ~46M league run (iter 1400): healthy diversity (effective actions 42.8, top move heavy 22%, 17 moves >= 1%), 0 timeouts, eval vs fighter 61%, 21 generations, vs old 150M agent 0% (expected this early). League picking works: learner beats the 8M/16M checkpoints (p 0.65/0.68), ~0-0.36 vs the rest. Behind the main run at the same step (hits/min 9.2 vs 12.7, eval vs fighter 65% vs 92%), but not comparable: shaping fades slower by design (0.72 vs 0.44 at 45M) and 20% of games are vs much stronger old agents.
  Speed 11.3k decisions/s vs 15.2k in the main run at the same point. Found a headless Godot stuck since Sep 30 01:07 (my v1 replay check before the project was imported) spinning one CPU core (3.2 h of CPU); it ran during the main run too. Killed it. Lesson: always wrap Godot calls in `timeout`.
- After killing the stuck Godot: recent rate ~14.1k decisions/s (up from ~11.3k); ~8.5 h left at 48M, so ~9.6 h total instead of 8 (game length and the extra league network). Left as is; owner informed.
- ~101M league run (iter 3070): healthy and ahead of the main run in engagement. hits/min 16.4 (main ~14 at 100M), time close 35%, 0 timeouts, eval vs fighter 98-100%, 43 generations. Moves more varied than the main run at this point: air 20, forward_tilt 17, heavy 15 (top 3 = 52% vs 66%), 14 moves >= 1%, effective actions 39. Defense growing: blocks 2.8/min, counters 2.9/min, grabs 1.2/min, streak share 55%.
  League: beats every old checkpoint up to ~74M (p 0.86-1.00), mixed vs 82-90M (0.45/0.64), loses to 98-131M (0.00-0.13), but ~0.5 vs 139M/148M and 19% vs the old final agent (eval/win_vs_league_best 0 -> 0.22 over the last ~25M). Non-transitive (some later checkpoints are easier than middle ones): exactly the variety the league was meant to add. Rate 14.7k/s, ~7.2 h left. No action.
- ~151M league run (iter 4600; anneals just ended): MILESTONE: the new agent beats the old 150M champion: eval/win_vs_league_best 0.33 -> 0.58 -> 0.84 over 90-151M, 0.94 at the latest eval. It beats every old checkpoint (p 0.97-1.00) except agent_04000 (131M; p 0.00). Same decision count as the old run's end, so the league + longer anneal produced a clearly stronger agent.
  No drift at the floor: hits/min 17.0 (stable), time close 40%, 0 timeouts, blocks 3.6/min, counters 3.4/min, grabs 2.4/min (throws now used: forward 2%, back 1%), streak share 55%. Moves: air 18, heavy 17, forward_tilt 16, grab 11, then 10 moves at 1-6% (top 3 = 51%). Effective actions 35 (entropy anneal done).
  Note: the league is now mostly beaten, so its 20% of games are easy (p(1-p) weights near the floor); it still guards against forgetting. Possible later change: refresh the league with this run's checkpoints. ~6.7 h left (13.7k/s). No action.
- ~252M league run (iter 7698): stable and strong. vs old champion 99% (0.97 at 170-190M), every old checkpoint now at p = 1.00. hits/min ~17.8, KOs 1.7/min, time close 41%, 0 timeouts, blocks 3.3, counters 3.3-3.5, grabs 2.3/min, streak share 58%, 151 generations (gate still passing at ~59%). Moves: air 19, heavy 16, forward_tilt 16, grab 10, then 13 moves at 1-6% (jab chain and throws now appear); effective actions ~33.7, flat since 170M. No collapse, no passivity.
  The league is now fully beaten (20% of games are easy wins) and eval/win_vs_league_best is saturated, so neither measures progress any more; generations still advance. After the run: head-to-head between this run's own checkpoints (150M vs 250M vs 480M) to see whether the second half still adds skill. Next run: refresh the league with this run's checkpoints. ~3.8 h left (16.7k/s). No action.
- ~351M league run (iter 10697): unchanged and healthy. hits/min 17.5, KOs 1.8/min, time close 41%, 0 timeouts, grabs 2.3/min, streak share 59%, counters 3.0 and blocks 2.7/min (slowly declining), 255 generations (win vs frozen rising 59 -> 62%: each frozen copy is beaten faster). Moves broaden further: air 17, forward_tilt 15, heavy 13, grab 11, jab 6, then 12 moves at 1-7% (top 3 = 45%). Effective actions ~33.5 flat. ~3.2 h left (11.3k/s now). No action.
- LEAGUE RUN FINISHED (480M, commit 79aca79, ~9.5 h). Replays: replays/recorded/selfplay__league__1__1790809954_agent_latest_vs_{self,fighter}.json (verified).

## Report: league run

Ladder (tools/ladder.py, 128 games per pair, training conditions; runs/ladder_league.txt):
| checkpoint | Elo |
|---|---|
| league final (480M) | 2251 |
| league 400M | 2158 |
| league 450M | 2117 |
| league 350M | 2002 |
| league 300M | 1924 |
| league 250M | 1839 |
| league 200M | 1703 |
| league 150M | 1530 |
| old main run final (150M) | 1367 |
| league 100M | 1258 |
| league 50M | 1000 |
| fighter bot | 924 |
- Skill rose through the whole run: final beats 450M 63%, 400M 75%, 350M 84%. Gains slow late (~+100 Elo per 50M vs ~+170 earlier) but haven't stopped.
- At equal training (150M), the league run beats the old main run's agent 87%: league + anneals ending at 150M made training more efficient.
- End-of-run play (460-480M): hits 18.6/min, KOs 1.8/min, time close 40%, 0 timeouts, true combo hits 2.1/min (up from 0.3: the jab chain is now used), streak share 62% (max ~17), counters 3.0, whiff punishes 4.4, blocks 2.7, parries 0.55, grabs 1.9, ledge grabs 1.0 per min. 346 generations.
- Moves: 18 used >= 1%; air 15, heavy 13, jab 12, forward_tilt 11, grab 8, heavy_up_air 6, down_heavy 5, dash_attack 4, heavy_air 4, jab2 4, up_heavy 3, jab3 3, ... (top 3 = 40%, lowest of any run). Effective actions 34.7.
- Still weak: techs ~0 (0.06/min).
- Next ideas: refresh the league with this run's checkpoints (the old league was fully beaten by ~250M); longer runs still pay, at a slowing rate.

## Clank loops in agent-vs-self replays (2026-10-01, investigation only)

Owner saw both fighters greyed out (REBOUND) for ~10 s at a time in replays/recorded/selfplay__league__1__1790809954_agent_latest_vs_self.json.
- Re-simulated frame by frame: 122 clanks in 2.9 min (85 rebounds/min); longest loop 4.7-27.2 s (136 rebounds), all jab vs jab at distance 2.00 (bodies touching). Rebounds last 11 frames for both, so both exit on the same frame.
- At that exit frame both press light (jab) in 117 of 122 cases (96%): jab (frame 2) is the agent's learned answer out of a clank. Loop continues with p ~ 0.96 per 20-frame cycle (~25 cycles, ~8 s on average).
- Only in the mirror match: same network on both sides + an exactly mirror-symmetric game = identical choices on identical frames. vs the fighter bot: 0 clanks. In training (vs older copies): 0.7/min at 100-150M rising to 3.4/min at 430-480M as jab use grew to 12%.
- Clanks cost nothing (zero-sum, both rebound equally), so nothing teaches the agent to vary its answer. The rebound frames left are not observed (lag_left only for LANDING); inferable from state_frame.
- Fix options (not applied): (1) push both fighters apart on a clank so the next jabs can't reach (deterministic, breaks the loop at its root; recommended); (2) occasionally train against the live network itself so it learns its own predictability; (3) watch vs the fighter bot or older checkpoints.
- FIX applied (owner's choice): rebounding fighters slide clank_push = 0.75 apart (exact per-frame friction formula; the slide stops at the stage edge). Clanks are hitbox vs hitbox and two jab hitboxes meet up to 4.4 apart, so the gap grows 2.0 -> 3.5 -> 5.0 and a loop ends within 3 clanks. Three new self-play games of the league agent (replays/recorded/clankfix_self_{1,2,3}.json, verified): 4, 6 and 0 rebounds (before: 244 in one game), longest streak 0.7 s. The agent was trained without the slide; it still plays fine, the next run will learn with it.
