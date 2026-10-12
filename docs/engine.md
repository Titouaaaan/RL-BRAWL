# The C++ engine (`core/`)

The whole game, independent of any graphics or engine. Godot draws it and
Python trains on it, but neither contains game logic.

## Files

| File | What |
|---|---|
| `sim.h` | Every type: `Input`, `MoveId` and `MoveDef`, `FighterState`, `Fighter`, `GameState`, `GameConfig` (all tuning numbers), `StepEvents`. Start reading here: the controls summary and move table are at the top. |
| `sim.cpp` | The game: `step()` and everything it calls. |
| `moves.cpp` | Loads `data/moves/*.json` into `GameConfig::moves`; `MOVE_NAMES` is the single list of moves. Never throws (the Godot build has C++ exceptions off); returns an error string instead. |
| `obs.h` / `obs.cpp` | `write_obs()`: what an agent sees (173 numbers). Shared by the Python env and the Godot live bot. |
| `vecenv.h` / `vecenv.cpp` | The RL layer: N games stepped together, frame skip, reaction delay, rewards, stats, auto-reset (see [python.md](python.md)). |
| `smoke.cpp` | A small native program run under AddressSanitizer (a compiler mode that catches memory bugs) and checking determinism. |

A *header* (`.h`) declares what exists; the `.cpp` files hold the code. Each
`.cpp` is compiled on its own and the *linker* joins them into one program or
library (the Python module `.pyd`, the Godot `.dll`).

## The core idea: one struct, one pure function

```cpp
struct GameState {
    Fighter f[2];
    int32_t frame = 0;
    bool over = false;
};

void step(GameState& s, const Input in[2], const GameConfig& cfg, StepEvents& ev);
```

`step()` advances exactly one frame (1/60 s). It is *pure*: no globals, no
randomness, no clock, so the same state plus the same inputs always gives the
same result. That's what makes replays tiny (inputs only) and checkable
(`hash_state()` hashes every field; a replay stores the final hash), and it's
like seeding an RL environment. Two tests guard it: the same random inputs run
twice give identical hashes, and a mirrored game stays exactly mirrored.

`GameConfig` holds every number that doesn't change during a game (speeds,
gravity, frame counts, knockback constants, the stage). Values from Smash
Ultimate are noted in comments (Mario, Battlefield; 1 sim unit = 5 Smash units).
Changing them needs a rebuild; move data doesn't.

## What `step()` does, in order

1. **Hitlag and DI:** fighters frozen by a hit count down; the victim's stick
   during the freeze bends its launch (directional influence).
2. **`step_fighter()` for each player:** the *state machine*. Every fighter is
   in one of 24 states (IDLE, RUN, JUMPSQUAT, AIRBORNE, ATTACK, HITSTUN,
   SHIELD, LEDGE, ...) with `state_frame` counting frames in it; the state, the
   frame and the input decide the next state. Then physics: speeds, gravity,
   landing on floors, the solid stage block. The input buffer and smash
   charging live here.
3. **Ledge grabs**, then **grab holds** (the held fighter follows the holder,
   mashing, pummels, escapes).
4. **Push apart:** grounded fighters can't overlap.
5. **Clanks:** two ground attacks whose hitboxes touch cancel; the rebounding
   fighters slide `clank_push` (0.75) apart, stopping at the stage edge.
6. **Hits:** both players' hits are *found* before any is *applied*, so a trade
   (both hit on the same frame) is symmetric. Attacks beat grabs, grabs beat
   shields, shields beat attacks; a dodging fighter's i-frames (intangible
   frames) let hitboxes pass (reported as an evade).
7. **Streaks, counters and combos** are updated, for both players at once.
8. **State-change events** (tech, knockdown, shield break, jumps), then
   **KOs** (leaving the blast zone) and respawns: the fighter waits `respawn_wait` (120) frames off the field (skipped by `step()`, intangible), then drops in with `respawn_invuln` (120) frames of invulnerability, which only start counting after the wait. The obs shows both as one number, (invuln + respawn_wait) / 120, which is above 1 only while waiting.

Everything that happened is reported in `StepEvents` (hits, damage, KOs,
combos, counters, parries...): rewards and statistics are built from these,
never by comparing states.

## Combat formulas (SmashWiki)

- **Knockback** = `((p/10 + p*d/20) * 200/(w+100) * 1.4 + 18) * KBG/100 + BKB`
  (p = victim % after the hit, d = damage, w = weight 98): `knockback()`.
- **Launch speed** = knockback x 0.006 units/frame, shrinking by 0.0102 per frame.
- **Hitstun** = floor(0.4 x knockback) - 1 frames; above 80 knockback the
  victim **tumbles** (must tech on landing or is knocked down).
- **Hitlag** (the freeze on hit) = floor(0.65 x damage + 6), max 30 frames.
- **Angle 361** (the Sakurai angle): along the ground for weak hits on a
  grounded victim, about 38 degrees otherwise.
- **Stale moves:** a move repeated among your last 9 hits loses up to ~45%;
  an unused one gets +5%. **Smash charge** adds up to +40%.
- **Shield stun**, **parry** window, **dodge staling**, **tech** window and
  lockout, **ledge** rules: see the constants in `GameConfig`.

## The moves

23 moves, all Mario in Smash Ultimate. Which one comes out is decided by
`pick_move()`: ground or air, light or heavy, and the stick:

|  | down | neutral | side | up |
|---|---|---|---|---|
| ground light | down_tilt | jab (-> jab2 -> jab3) | forward_tilt | up_tilt |
| ground heavy | down_heavy | heavy | heavy | up_heavy |
| air light | down_air | air | air | up_air |
| air heavy | heavy_down_air | heavy_air | heavy_air | heavy_up_air |

Plus `dash_attack` (light while running), `grab` (light while shielding) and
four throws, `getup_attack`, `ledge_attack`. Pummels aren't a move file (they
are handled in `step()`).

## Move files

Each move is a file in `data/moves/`, named as in the table above (`jab.json`, `up_tilt.json`, `heavy_down_air.json`, ...), plus `grab.json` and the four `throw_*.json`. Edit one, save, and press **R** in the game: no rebuild needed. If the file has a mistake, the error shows in red at the top of the game window.

| Field | Meaning |
|---|---|
| `total_frames` | How long the move lasts, in frames (60 = 1 second). |
| `landing_lag` | Aerial only: frames stuck if you land during the move. |
| `autocancel_before`, `autocancel_after` | Optional, aerials: landing before frame `before` or after frame `after` is a normal landing instead (auto-cancel). |
| `throw` | Optional, `true` for throws: the hitbox isn't a circle, it's applied to the held opponent on frame `start`. |
| `hitboxes` | Up to 4 circles. The first one listed wins if several touch. |
| `start`, `end` | The move frames when this hitbox is active (the first frame is 1). Frames before the first hitbox = *startup*, after the last = *recovery*. |
| `x`, `y` | Circle centre relative to the attacker's feet; `x` points the way they face. The body (hurtbox) is 2 wide, 3 tall. |
| `radius` | Circle size. |
| `damage` | % added to the victim. |
| `base_kb`, `kb_growth` | Smash's BKB and KBG. Knockback = `((p/10 + p*d/20) * 200/(weight+100) * 1.4 + 18) * kb_growth/100 + base_kb` (p = victim % after the hit, d = damage). Launch speed = knockback * 0.006; hitstun = 0.4 frames per knockback, minus 1. |
| `angle` | Launch direction in degrees: 0 = straight forward, 90 = straight up, negative = downward (a spike), above 90 = backward. 361 = the Sakurai angle (along the ground for weak hits on the ground, ~38 degrees otherwise). |
| `intangible_until` | Optional: the attacker can't be hit before this move frame (get-up and ledge attacks). |
| `grab` | Optional per hitbox, `true` for the grab: it catches instead of hitting, and goes through shields. |

Everything else (dash and run speed, gravity, jump heights, dodge and roll frames, shield size and drain, hitlag, DI strength, stale-move steps, platforms, blast zones and so on) lives in `GameConfig` in `core/sim.h`. Those do need a rebuild, which `play.bat` does for you.


## Adding a mechanic: checklist

1. State and numbers in `sim.h` (a `Fighter` field, a `GameConfig` value, a
   `StepEvents` flag if rewards or stats need it).
2. Logic in `sim.cpp`; add every new `Fighter` field to `hash_state()` (or
   replays can't catch a desync involving it).
3. If agents must see it, a feature in `obs.cpp` (and the size in `obs.h`);
   that changes `OBS_DIM`, so old checkpoints won't load.
4. A test in `tests/`, then rebuild both front ends (`pip install` and `play.bat`).

C++ style: plain structs, `std::vector`, fixed-size arrays, no `new`/`delete`;
readable over clever.
