# Running the game

## TL;DR

Double-click **`play.bat`** in the repo root (or run `.\play.bat` in a terminal).

It rebuilds the C++ (only what changed, usually a few seconds) and opens the game. The very first run takes about 30 seconds.

`.\play.bat edit` opens the project in the Godot editor instead.

`.\play.bat replay` plays back the newest recorded game (from training, see `docs/training.md`): Space pauses, the right arrow steps one frame while paused, up/down change speed, R restarts.

## Controls

Keys are matched by **position** on the keyboard, so on AZERTY, "WASD" is really ZQSD.

| | Player 1 (blue) | Player 2 (red) |
|---|---|---|
| Move / stick | W A S D | Arrow keys |
| Jump | Space | Enter |
| Light attack | J | `.` (`:` on AZERTY) |
| Heavy attack | K | `/` (`!` on AZERTY) |
| Dodge | L | `,` (`;` on AZERTY) |

**Controllers** work alongside the keyboard: the first controller connected plays P1, the second P2. A PS4 controller works over USB or Bluetooth with no setup. If Godot doesn't see it, close Steam: its "Steam Input" feature can grab controllers.

| PS4 controller | Action |
|---|---|
| Left stick or d-pad | Move (the stick has to be pushed past halfway) |
| Cross or Triangle | Jump |
| Square | Light attack |
| Circle | Heavy attack |
| L1 / R1 / L2 / R2 | Dodge |
| Options | Reload moves + restart (like R) |

To remap, edit the `PAD` table near the top of `godot/main.gd`.

**R** reloads the move files and restarts. **Esc** quits.

### Moving
- Speeds and jump heights are Mario's from Smash Ultimate, scaled so 1 unit = 5 Smash units; gravity and fall speed are 20% stronger than his for a less floaty feel. The stage is Battlefield made 10% wider (35 units, ledges at the ends, platforms at Battlefield's heights).
- **Input buffer:** a button pressed up to 9 frames (0.15 s) before your fighter can act again (landing lag, the end of an attack) still comes out on the first free frame, as in Smash Ultimate. (Only for human play: agents train without it, see `docs/training.md`.)
- **Several buttons at once:** the game takes one button per frame, and the one you pressed most recently wins. So you can keep holding jump for a full hop and press light mid-jump for an air attack.
- Moving starts with a fast **dash** (12 frames), then a **run**. Speed changes smoothly rather than instantly. Turning around during the dash dashes the other way (a "dash dance").
- **Jump:** hold jump for a full jump; **tap** it (let go within 3 frames), or hold **down + jump**, for a lower **short hop**. One double jump in the air.
- **Fast-fall:** hold **down without attacking** for 6 frames (0.1 s) while falling. Down + an attack does the down move instead. A down that was part of an attack is "used up": return the stick to neutral and press down again to fast-fall.
- **Platforms:** three floating platforms (the "Battlefield" layout). Jump up through them from below; hold **down** while standing on one to drop through.
- **Crouch:** hold **down** on the main stage. You're shorter (some high attacks miss) and you take 15% less knockback and a shorter hit freeze (*crouch cancel*). Down + light from a crouch = down-tilt.
- **Ledges:** the two top corners of the main stage. Fall (or rise) next to one without holding down and you **grab** it automatically, briefly intangible. While hanging: **toward the stage or up** = climb, **jump** = ledge jump, **light/heavy** = ledge attack, **dodge** = ledge roll, **away or down** = let go. After 3 seconds you let go by yourself (the fighter flashes red in the last second). Each regrab before touching the stage gives less intangibility (none from the 4th), max 6 grabs. Only one fighter per ledge: grabbing it first blocks the opponent (*edge-hogging*). The stage is a solid block: you can't pass through its sides or underside.
- Fighters on the same floor **push each other** apart instead of walking through each other. Nobody can be pushed off a floor: a fighter at the edge acts as a wall.

### Attacking
- **Attacks:** light or heavy, combined with the stick, on the ground or in the air. Every move's frame data (startup, active frames, total length, landing lag), damage and knockback comes from Mario in Smash Ultimate (ultimateframedata.com, KuroganeHammer); each file in `data/moves/` says where its numbers come from. In the air, left/right only sets which way you face (hold it backward to turn and hit behind you). Facing locks once the move starts.

  | | down | neutral | side | up |
  |---|---|---|---|---|
  | ground, light | down-tilt: low poke, pops up | jab 1 → 2 → 3 (keep pressing) | forward tilt: mid-range poke | up-tilt: anti-air, juggle starter |
  | ground, heavy | down-heavy: hits both sides | heavy (forward smash) | heavy | up-heavy (up smash): KOs upward |
  | air, light | down-air | air (neutral air) | air | up-air: juggles |
  | air, heavy | heavy down-air: **spike** (sends downward) | heavy air | heavy air | heavy up-air: launches high |

  **Hold heavy** during a smash attack (heavy on the ground) to charge it: it pauses just before hitting, for up to 1 second, and hits up to 40% harder (the fighter glows). **Light while holding someone = pummel** (1.3%, uses up some of the hold).

  Plus: **light while running = dash attack** (fast, far, very punishable), **light while shielding = grab**, and the get-up attack and ledge attack.
- **Knockback** follows Smash's formula: it grows with the victim's % *and* the hit's damage, and the heavy (forward smash) KOs Mario from the middle of the stage around 100%, up smash around 140%. Weak hits on a grounded opponent send them along the ground; stronger ones launch at up to ~38 degrees (the "Sakurai angle"). Knockback above 80 makes you **tumble** (spinning): you can act once the hitstun ends, but if you hit the ground still tumbling you must tech or be knocked down.
- **Clank:** if two ground attacks hit each other at the same moment, they cancel. If their damage is within 9%, both fighters rebound (stuck briefly, drawn dark); otherwise only the weaker attack's owner does.
- **Landing during an air attack** costs its landing lag, **unless** you land in its *auto-cancel* windows (the first few frames, or late in the move): then it's a normal, quick landing. A short hop into an early air attack lands clean.
- **Missed air attacks:** after **two** air attacks that hit nothing, you can't attack again until you land (the HUD shows `[no air attacks]`). Movement, double jump and dodge still work. Hits don't count, so juggles work; getting hit clears it.
- **Hitlag:** when a hit connects, both fighters freeze for a few frames (longer for stronger hits). That's the "impact" feel, and it's when DI happens.
- **Stale moves:** the game remembers your last 9 moves that hit. Using the same move again makes it weaker (up to ~45% weaker if you spam it); a move you haven't used recently gets a small 5% bonus. Mixing your moves pays.

### Defending (the Smash triangle: attacks beat grabs, grabs beat shields, shields beat attacks)
- **Shield:** hold **dodge** on the ground. Attacks bounce off (you're pushed back and stuck for a moment: *shield stun*), but the shield shrinks while held and when hit; the bar under your damage shows it. If it runs out: **shield break**, dazed for 2.5 seconds.
- Out of your shield: **dodge + left/right = roll** (intangible for most of it, you end up facing back where you came from), **dodge + down = spot dodge** (intangible in place), **jump** = jump out of shield, **light = grab**, let go = drop the shield (a few frames of lag).
- **Grab:** light while shielding. It catches the opponent **even through their shield**, but it's slow and has a lot of lag if it misses, so an attack or a dodge beats it. Once you hold someone: press a direction to **throw** (forward, back, up or down); the hold ends in a forward throw by itself. Down throw is the combo starter, back throw the strongest.
- **Parry:** let go of the shield (after holding it at least 3 frames) just as a hit arrives: if the hit lands in the first 5 frames of the shield drop, you take nothing and the attacker is frozen for extra frames, a free punish (the fighter flashes cyan during that window).
- **Dodges get worse when spammed** (Ultimate's dodge staling): each dodge within a second of the last one lasts longer and its intangibility starts later.
- **Air dodge:** dodge in the air, optionally with a direction to dash that way. Intangible for frames 3-18, then **helpless**: you can drift but can't jump, attack or dodge until you touch the ground (10 frames of landing lag, 14 after a directional one). An air dodge off the stage is usually fatal.
- **Grabbed:** press buttons to struggle out faster. The hold lasts longer the more % you have.
- **DI (directional influence):** when you get hit, hold a direction during the hit freeze to bend your launch angle by up to 15 degrees. Holding away from your opponent escapes combos; at high %, angling toward the stage helps you survive.
- **Tech:** if you hit the ground while tumbling, press **dodge** just before impact (within 11 frames) to recover instantly, intangible (+ left/right = tech roll). Mashing doesn't work: a press less than 40 frames after the previous one is ignored. Miss it and you're **knocked down**, lying on the floor: then **up/jump** = get up, **light/heavy** = get-up attack, **dodge + side** = get-up roll (all intangible at first), or wait and you get up by yourself after a second.
- While intangible, a fighter is drawn see-through with a white outline.

### Rules
- Leave the faint blast-zone area and you lose a stock. You respawn above the stage, blinking and invulnerable for 2 seconds.

**Reading the screen:** hitboxes are circles with a red rim (a grab's rim is yellow), faded once the move has already hit. A fighter flashes pale while in hitstun and shakes during hitlag, turns purple while held, and pulses yellow while dazed. A shield is a bubble that shrinks as it wears down. The text next to the stocks is the state machine's state, e.g. `ATTACK heavy 12` = frame 12 of the heavy, `HITSTUN 3 (17 left)`.

## Characters

Each fighter can be drawn as any character (it's only the look: every
character plays exactly the same). Pick them with `--p1 ID --p2 ID`, for
playing or watching replays, in any order:

```
play.bat --p1 3 --p2 5
play.bat replay --p1 4 --p2 1
play.bat replay path	o\game.json --p2 5
```

Without them, P1 is character 1 and P2 is character 2.

| ID | Character |
|---|---|
| 1 | DinoSailor (blue) |
| 2 | DinoSailorP2 (red) |
| 3 | Car |
| 4 | Justin |
| 5 | OliverPip |

**Adding a character:** put the image (any size, PNG) in `assets/sprites/`,
then run `python tools/sprites.py`. It trims the empty space, removes a plain
white page background, scales the character to fit a 64x64 canvas (feet at the
bottom), saves it as `assets/characters/<id>.png` and prints the new id. Ids
never change: new images get the next number. Your originals are not modified.
Draw characters facing **right** (or set `"faces": "left"` for that character
in `assets/characters/characters.json`).

## Tuning moves

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

## What happens when you run it

1. **CMake + MSVC compile the C++** (`core/sim.cpp`, `core/moves.cpp` and `bindings/godot/fightgame.cpp`) into `godot/bin/fightgame.windows.template_debug.x86_64.dll`. That DLL is the whole game logic.
2. **Godot loads the DLL.** `godot/fightgame.gdextension` tells it where the DLL is.
3. **`godot/main.gd` runs 60 times per second.** Each time it reads the keyboard, calls `step()` in C++, and draws the boxes.

The Python training code calls exactly the same `step()`, so what you see is exactly what the bot trains on.

## One-time setup on a new machine

This machine already has everything installed. On another PC you would need:

1. **Visual Studio 2026 (Community)** with the *Desktop development with C++* workload, which provides the MSVC compiler.
2. **Python 3.13** from python.org, then run:
   `python -m pip install "cmake>=4.2" "scikit-build-core>=0.10" "nanobind>=2.4" pytest numpy`
3. **Godot 4.7** (standard build, not .NET). Unzip it anywhere under `Downloads`, or set the `GODOT` environment variable to the path of its `.exe`.
4. `git clone` the repo. `play.bat` downloads the `godot-cpp` submodule itself if it's missing.

## Other commands (Python side)

| What | Command |
|---|---|
| Rebuild the Python module after a C++ change | `python -m pip install --no-build-isolation .` |
| Run the tests | `python -m pytest -q` |
| Speed benchmark | `python tools/bench.py` |
| Train an agent | `python train/ppo.py --opponent dummy` (see `docs/training.md`) |
| Memory-bug check (AddressSanitizer) | `python -m cmake -S . -B build/debug` then `python -m cmake --build build/debug --config Debug` then `build\debug\Debug\smoke.exe` |

Always use `python -m cmake`, not plain `cmake`: the Strawberry Perl install on this PC puts an old CMake 3.29 on the PATH, and it doesn't know Visual Studio 2026.

## When something goes wrong

| Message | Meaning | Fix |
|---|---|---|
| `LNK1104: cannot open file '...fightgame....dll'` | The linker can't overwrite the DLL because Godot is running and has the file open. Windows locks DLLs while they're loaded. `play.bat` works around this by renaming the old DLL first, so you only see this when building by hand. | Close the game or the Godot editor, or build through `play.bat`. |
| `Could not find Godot` | play.bat didn't find `Godot_v4*_win64.exe` under Downloads. | Set `GODOT` to the full path of Godot's .exe. |
| Godot says `FightGame` is not declared / "Identifier not found" | Godot doesn't know about the extension. Either the DLL didn't build, or the project was never imported. | Delete the `godot/.godot` folder and run `play.bat` again; it re-imports. |
| `could not create CMAKE_GENERATOR "Visual Studio 18 2026"` | The old Strawberry CMake was used. | Use `python -m cmake ...`. |
| `DESYNC` at the end of a replay | Godot's re-simulation didn't end in the recorded state: the Python module and the Godot DLL disagree. Usually one of them is stale. | Rebuild both (`pip install --no-build-isolation .` and `play.bat`), record again. `tests/test_replay_godot.py` checks this automatically. |
| The build is weird after pulling big changes | Stale build folder. | Delete `build/godot` (it's safe; it's all generated) and rerun. |
