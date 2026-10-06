# Playing

## Quick start

Double-click **`play.bat`** in the repo root (or run `.\play.bat` in a terminal).
It rebuilds the C++ (only what changed, usually a few seconds) and opens the
**menu**. The very first run takes about 30 seconds.

## The menu

Mouse, keyboard (arrows + Enter, Esc = back) or controller (d-pad / stick,
Cross, Circle = back).

| Item | What it does |
|---|---|
| Player vs Bot | Pick the bot's level, then fight it (you are P1) |
| Player vs Player | Two humans (keyboard and/or controllers) |
| Bot vs Bot | Pick a level for each bot, then watch them fight live |
| Character Select | Pick P1's and P2's characters; saved for next time |
| Controls | Keyboard and controller bindings |
| Replays | Every recording under `replays/`, newest first, with who won: select one to watch it |

In a game, **Esc** goes back to the menu.

Bot levels (all from the best training run so far, spaced along its Elo
ladder; the list is `DIFFICULTIES` in `godot/session.gd`):

| Level | Checkpoint | Trained for |
|---|---|---|
| Very easy | `agent_00250` | 8M decisions |
| Easy | `agent_01500` | 50M |
| Normal | `agent_03000` | 100M |
| Hard | `agent_06250` | 200M |
| Very hard | `agent_10750` | 350M |
| Latest bot | the newest run's final agent | whatever it reached |

A bot needs a few seconds to load (shown at the top left until it's ready);
the game waits for it. There's no help text during a fight: the controls are
in the menu's Controls screen and below.

The command-line options below skip the menu and go straight to a game.

| Command | What it does |
|---|---|
| `.\play.bat` | Play, two humans (keyboard and/or controllers) |
| `.\play.bat --vs` | Fight the newest trained agent: you are P1 (see [Fighting a bot](#fighting-a-bot)) |
| `.\play.bat --vs fighter` | Fight a scripted bot: `dummy`, `random`, `jabber`, `spacer`, `fighter` |
| `.\play.bat replay` | Watch the newest recorded game (see [Watching replays](#watching-replays)) |
| `.\play.bat replay path\to\game.json` | Watch a specific recording |
| `.\play.bat edit` | Open the project in the Godot editor |
| add `--p1 ID --p2 ID` | Choose the characters (ids in [assets.md](assets.md)) |

The options can come in any order, e.g. `.\play.bat replay --p1 6 --p2 2` or
`.\play.bat --vs fighter --p2 5`.

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

The layout is **Smash Ultimate's default**, by button position (a Switch Pro
controller's A / B / X / Y are a PS4 pad's Circle / Cross / Triangle / Square):

| PS4 controller | Smash Ultimate | Action here |
|---|---|---|
| Left stick (or d-pad) | Control stick | Move (push past halfway) |
| Circle | A: attack | Light attack |
| Cross | B: special | Heavy attack (we have no specials yet) |
| Triangle / Square | X / Y: jump | Jump |
| Right stick | C-stick: smash attacks | Heavy in that direction (smash attacks on the ground, heavy aerials in the air); holding it charges a smash |
| L2 / R2 | ZL / ZR: shield | Shield / dodge |
| L1 / R1 | L / R: grab | Grab (a 2-frame shortcut: shield, then light; an air dodge in the air) |
| Options | + : pause | Reload moves + restart (like R) |

Not copied from Smash: *tap jump* (up on the stick jumps), because our stick
has no gentle "tilt" vs hard "flick" distinction and up + light must stay an
up-tilt; the d-pad moves instead of taunting.

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
- **Clank:** if two ground attacks hit each other at the same moment, they cancel. If their damage is within 9%, both fighters rebound (stuck briefly, drawn dark); otherwise only the weaker attack's owner does. Rebounding fighters slide 0.75 apart, so two jabs at point-blank can't clank over and over: each clank widens the gap, out of jab range after at most three.
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
- Each player has **3 stocks**. Leave the faint blast-zone area and you lose a stock. You respawn above the stage, blinking and invulnerable for 2 seconds.
- A game ends when someone loses their last stock, or after **7 minutes** (the timer at the top, as in competitive Smash). At the time limit the player with more stocks wins, then the one with less damage.

**Reading the screen:** fighters are drawn as their character's sprite (see [assets.md](assets.md)), with the character's name above the damage. Hitboxes are circles with a red rim (a grab's rim is yellow), faded once the move has already hit. The sprite is tinted to show what's happening:

| Look | Meaning |
|---|---|
| bright flash, shaking | in hitstun / the hit freeze (hitlag) |
| spinning | tumbling after a strong hit: tech or be knocked down |
| lying on its side | knocked down |
| squashed | crouching |
| see-through with a white outline | intangible (dodging, get-up, ledge) |
| blinking | invulnerable after respawning |
| golden glow, growing | charging a smash attack |
| dark | rebounding after a clank or a parried attack: open to a punish |
| cyan | the parry window (just let go of the shield) |
| purple | being held in a grab |
| pulsing yellow | dazed after a shield break |
| red blink while hanging | about to let go of the ledge |

A shield is a bubble that shrinks as it wears down. "N HIT COMBO" appears above a player during a true combo. The text next to the stocks is the state machine's state, e.g. `ATTACK heavy 12` = frame 12 of the heavy, `HITSTUN 3 (17 left)`.

## Fighting a bot

```
play.bat --vs                  # the newest trained agent
play.bat --vs fighter          # a scripted bot: dummy, random, jabber, spacer, fighter
play.bat --vs runs\<run>\agent_00600.pt --p1 1 --p2 5
```

You are P1 (keyboard or controller 1); the bot is P2. The game starts the bot
in the background (`train/serve.py`, running the network in PyTorch, about 1
ms per decision) and connects to it on this machine; it waits until the bot
has loaded (a few seconds, shown at the top left; the line disappears once the
bot is ready). The bot plays exactly as in training: it sees the game 0.1 s
late and picks an action every 4 frames, and it has no input buffer (it was
trained without one); you keep yours. Leaving the game stops the bot. If it
can't connect, the top left says why in red (for example the game and the
Python module were built from different code: rebuild both).

## Watching replays

To (re)generate a set of fights: `python train/record_vs.py` records the
latest agent against every checkpoint and scripted bot (`replays/latest_vs_all/`).

`.\play.bat replay` plays the newest recording under `replays/`;
`.\play.bat replay path\to\file.json` a specific one. To record a fresh game of
the newest agent first: `python train/record.py latest --opponent self` (or
`fighter`, `spacer`, `jabber`, `random`, `dummy`).

| Key | Action |
|---|---|
| Space | pause |
| Right arrow (paused) | next frame (hold to scrub) |
| Up / Down | faster / slower (1/4x to 8x) |
| R | restart the replay |
| Esc | quit |

At the end Godot checks that its re-simulation reached exactly the recorded
final state: "Replay verified", or "DESYNC" (see [setup.md](setup.md#when-something-goes-wrong)).
A replay stores the inputs, not the game code, so recordings made with an
older version of the game desync; the previous version's replays play in the
frozen copy `D:\BOOTLEG-SSM-v1` (run its `play.bat replay <full path>`).

To change how the game plays (move damage, frames, knockback), see
[engine.md](engine.md#move-files).
