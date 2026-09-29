// sim.h: the whole game's data types and the functions that advance it.
//
// A header file (.h) is what other files #include to learn which types and
// functions exist. The function bodies live in sim.cpp and moves.cpp. The
// compiler turns each .cpp into machine code separately, and the linker then
// joins them together.
//
// The mechanics and numbers follow Super Smash Bros. Ultimate (SmashWiki,
// ultimateframedata.com), using Mario as the reference character and
// Battlefield as the stage. Units: 1 sim unit = 5 Smash units (Mario's height
// is about our 3-unit hurtbox), time in frames at 60 per second.
#pragma once  // stops this header from being pasted in twice by the same .cpp file

#include <cstdint>  // fixed-width integer types: int8_t, uint8_t, uint64_t
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

// Only one button can be pressed per frame. That matches the RL action space,
// which is button in {none, jump, light, heavy, dodge}. Every option in the
// game comes from these 5 buttons combined with the stick:
//
//   ground  jump: jump (tap it, or hold down + jump, for a lower "short hop")
//           light / heavy: attacks (see the move table below); light while
//             running = dash attack; keep pressing light = jab 1 -> 2 -> 3
//           dodge: shield; + left/right = roll; + down = spot dodge
//           let go of the shield just as a hit lands = parry
//           light while shielding = grab; then any direction = throw
//           down (no button) = crouch (on a platform: drop through)
//   air     jump: double jump;  light / heavy: aerials
//           dodge: air dodge (+ a direction = directional air dodge)
//           hold down (no attack): fast-fall
//   hit     hold the stick while being hit: directional influence (DI)
//           press dodge just before hitting the ground: tech (+ side = tech roll)
//   floor   (knocked down) up/jump = get up; light/heavy = get-up attack;
//           dodge + side = get-up roll
//   ledge   (hanging) toward the stage/up = climb; jump = ledge jump;
//           light/heavy = ledge attack; dodge = ledge roll; away/down = let go
//   grabbed press buttons to struggle out faster
enum Button : uint8_t { BTN_NONE = 0, BTN_JUMP = 1, BTN_LIGHT = 2, BTN_HEAVY = 3, BTN_DODGE = 4 };

// One player's controller input for a single frame.
struct Input {
    int8_t stick_x = 0;  // -1 left, 0 neutral, 1 right
    int8_t stick_y = 0;  // -1 down, 0 neutral, 1 up
    uint8_t button = BTN_NONE;
};

// ---------------------------------------------------------------------------
// Moves (loaded from data/moves/*.json by load_moves in moves.cpp)
// ---------------------------------------------------------------------------

// Which attack comes out is decided by the game, not the agent: ground or air,
// light or heavy, and the stick.
//
//                  down              neutral           side             up
//   ground light   down_tilt         jab (1, 2, 3)     forward_tilt     up_tilt
//   ground heavy   down_heavy        heavy             heavy            up_heavy
//   air light      down_air          air               air              up_air
//   air heavy      heavy_down_air    heavy_air         heavy_air        heavy_up_air
//
// Plus: dash attack (light while running), grab (light while shielding) and
// its four throws, get-up attack (from the floor), ledge attack.
// The first 17 keep their old numbers, so older data still lines up.
enum MoveId : int8_t {
    MOVE_NONE = -1,
    MOVE_DOWN_TILT, MOVE_JAB, MOVE_UP_TILT,
    MOVE_DOWN_HEAVY, MOVE_HEAVY, MOVE_UP_HEAVY,
    MOVE_DOWN_AIR, MOVE_AIR, MOVE_UP_AIR,
    MOVE_HEAVY_DOWN_AIR, MOVE_HEAVY_AIR, MOVE_HEAVY_UP_AIR,
    MOVE_GRAB, MOVE_THROW_FORWARD, MOVE_THROW_BACK, MOVE_THROW_UP, MOVE_THROW_DOWN,
    MOVE_JAB2, MOVE_JAB3, MOVE_FORWARD_TILT, MOVE_DASH_ATTACK, MOVE_GETUP_ATTACK, MOVE_LEDGE_ATTACK,
    NUM_MOVES,  // = 23 (an enum's values count up from 0, so this is the count)
};
// File names in data/moves/ (without .json), in MoveId order. Defined in moves.cpp;
// Python, Godot and replays all read the list from here.
extern const char* const MOVE_NAMES[NUM_MOVES];

const int MAX_HITBOXES_PER_MOVE = 4;

// A hitbox is a circle attached to the attacker that hurts the opponent if it
// overlaps the opponent's hurtbox (the box around their body) while active.
struct HitboxDef {
    int start = 1, end = 1;   // active on move frames start..end (the move's first frame is 1)
    float x = 0, y = 0;       // offset from the attacker's feet; x points the way they face
    float radius = 1;
    float damage = 0;         // added to the victim's damage %
    // Knockback, in Smash's units (see knockback() below): base knockback
    // (BKB, the part that doesn't grow) and knockback growth (KBG, 100 = normal).
    float base_kb = 0;
    float kb_growth = 100;
    // Launch angle in degrees: 0 = straight forward, 90 = straight up, above 90
    // = backward, negative = downward (a spike). 361 = the "Sakurai angle":
    // along the ground for weak hits on a grounded opponent, ~38 degrees otherwise.
    float angle = 45;
    bool grab = false;        // a grab box: catches the opponent (even through a shield) instead of hitting
};

// "Frame data": the timing of a move. Startup = frames before the first
// hitbox, active = frames with a hitbox, recovery = frames after, during which
// the attacker is still stuck in the move and can be punished.
struct MoveDef {
    int total_frames = 1;  // startup + active + recovery
    int landing_lag = 0;   // aerials: stuck frames if you land mid-move...
    // ...unless you land before frame autocancel_before or after frame
    // autocancel_after ("auto-cancel" windows): then it's a normal landing.
    int autocancel_before = 0, autocancel_after = 1000;
    // A throw: its (single) hitbox isn't a circle in the world, it is applied
    // to the grabbed opponent on frame `start`.
    bool is_throw = false;
    // Frames of the move during which the attacker is intangible (get-up and
    // ledge attacks), 0 = none.
    int intangible_until = 0;
    std::vector<HitboxDef> hitboxes;
};

// ---------------------------------------------------------------------------
// Game state
// ---------------------------------------------------------------------------

// A fighter's "state" in the state-machine sense: what it's doing right now.
// Each frame, (state, state_frame, input) decides the next state.
enum FighterState : uint8_t {
    ST_IDLE = 0,         // standing, can act
    ST_RUN = 1,          // moving: a fast dash for the first dash_frames, then a run; can act
    ST_JUMPSQUAT = 2,    // crouching for a few frames before leaving the ground
    ST_AIRBORNE = 3,     // in the air, can act (double jump, aerial, drift)
    ST_LANDING = 4,      // landing lag: stuck for a few frames
    ST_ATTACK = 5,       // doing a move (see Fighter::move), including grabs and throws
    ST_HITSTUN = 6,      // got hit: can't act until `hitstun` runs out
    ST_SPOTDODGE = 7,    // dodge in place: briefly intangible, then a short recovery
    ST_AIRDODGE = 8,     // dodge in the air: briefly intangible, then helpless
    ST_HELPLESS = 9,     // after an air dodge: can only drift until touching the ground
    ST_SHIELD = 10,      // holding the shield: attacks bounce off, grabs don't
    ST_SHIELDSTUN = 11,  // an attack hit the shield: stuck for a moment
    ST_ROLL = 12,        // rolling left or right: intangible for most of it
    ST_SHIELDBREAK = 13, // the shield ran out: dazed and defenceless for a while
    ST_HOLDING = 14,     // holding a grabbed opponent; any direction throws
    ST_GRABBED = 15,     // being held: can't act until thrown (or struggling free)
    ST_KNOCKDOWN = 16,   // hit the ground tumbling without teching: lying down
    ST_TECH = 17,        // teched a landing: a quick, intangible recovery
    ST_CROUCH = 18,      // holding down on the ground: lower, and takes less knockback
    ST_REBOUND = 19,     // an attack clanked with the opponent's (or was parried): stuck
    ST_LEDGE = 20,       // hanging from the edge of the stage
    ST_LEDGE_CLIMB = 21, // climbing up from the ledge (intangible)
    ST_GETUP = 22,       // standing up from the floor (intangible)
    ST_SHIELDDROP = 23,  // letting go of the shield; its first frames are the parry window
    NUM_STATES = 24,
};
// Names for the states above, for display (Godot) and debugging.
extern const char* const STATE_NAMES[NUM_STATES];

// Floors you can stand on. Surface 0 is the main stage (solid, with ledges at
// both ends); the others are platforms you can jump through from below and
// drop through with down.
struct Surface {
    float x0, x1, y;
};
const int NUM_SURFACES = 4;

struct Fighter {
    float x = 0, y = 0;    // position of the feet; y = 0 is the main floor, and y goes up
    float vx = 0, vy = 0;  // the fighter's own velocity (running, jumping, gravity)
    // Knockback velocity, kept separate from vx/vy because it follows its own
    // rule: it shrinks by a fixed amount every frame (GameConfig::kb_decay).
    float launch_vx = 0, launch_vy = 0;
    float percent = 0;     // damage taken; more % = flies further when hit
    float shield_hp = 50;  // shrinks while shielding and when attacks hit the shield
    int8_t facing = 1;     // 1 = facing right, -1 = facing left
    uint8_t state = ST_IDLE;
    int32_t state_frame = 0;  // frames spent in `state` so far (1 on the first frame)
    bool grounded = true;
    int8_t surface = 0;       // which Surface the fighter stands on (when grounded)
    uint8_t drop_frames = 0;  // > 0: falling through platforms (after pressing down on one)
    bool fastfall = false;
    // Consecutive frames the stick has been held down with no attack button.
    // Fast-fall needs GameConfig::fastfall_hold of them, so down + attack
    // does a down move without dropping.
    uint8_t down_frames = 0;
    // Set when an attack is pressed; cleared once the stick leaves "down".
    // A down input that was part of an attack can't turn into a fast-fall:
    // you have to let go and press down again.
    bool fastfall_blocked = false;
    bool short_hop = false;   // during jumpsquat: this jump will be a short hop
    // Air attacks that ended without hitting since the last landing. At
    // GameConfig::air_whiff_limit, no more air attacks until you land (or get hit).
    uint8_t air_whiffs = 0;
    uint8_t jumps_left = 2;   // one ground jump plus one double jump
    uint8_t stocks = 3;       // lives left
    int8_t move = MOVE_NONE;  // current move while state == ST_ATTACK
    bool move_hit = false;    // this use of the move already connected (each use hits at most once)
    bool move_evaded = false; // this use of the move went through a dodging opponent (counted once)
    int16_t hitstun = 0;      // frames of hitstun left
    bool tumble = false;      // launched by a strong hit (knockback > 80) and still tumbling: lasts past
                              // the hitstun until you act (jump, attack, ledge) or land (tech or knockdown)
    int16_t landing_lag = 0;  // length of the current ST_LANDING / ST_SHIELDSTUN / ST_REBOUND
    int16_t invuln = 0;       // frames left of intangibility (respawn, ledge, climbs, get-ups)
    // Hitlag ("freeze frames"): when a hit connects, both fighters freeze for
    // a few frames. The victim's stick meanwhile is their DI.
    uint8_t hitlag = 0;
    int8_t di_x = 0, di_y = 0;  // the stick held during hitlag, applied when it ends
    uint8_t roll_dir = 0;       // ST_ROLL: 1 = rolling right, 2 = rolling left
    // Dodge staling (Smash Ultimate): each dodge within a second of the last
    // one makes the next longer and its intangibility start later.
    uint8_t dodge_stale = 0;     // 0 (fresh) to 5 (fully stale)
    uint16_t dodge_timer = 999;  // frames since the last dodge started
    int16_t dodge_len = 0;       // length of the current dodge / roll (after staling)
    uint8_t dodge_shift = 0;     // its intangibility starts this many frames late
    bool airdodge_dir = false;   // the current air dodge is directional (slingshot)
    // Tech timing: frames since dodge was last pressed, and the gap before that
    // press. Pressing dodge too often locks the tech out (no mashing).
    uint8_t dodge_ago = 255;
    uint8_t dodge_prev_gap = 255;
    // Ledge.
    uint8_t ledge_grabs = 0;     // grabs since last landing (intangibility shrinks, max 6)
    int8_t ledge_side = 0;       // which ledge is held: -1 left, 1 right
    uint8_t ledge_cooldown = 0;  // frames before the ledge can be grabbed again (after letting go)
    // Grabs: frames the holder can still hold before the victim breaks free;
    // the victim's button presses (mashing) shorten it.
    int16_t grab_timer = 0;
    uint8_t mash = 0;            // struggle presses this frame (read by step())
    uint8_t pummel = 0;          // holder pressed light this frame: pummel (read by step())
    uint8_t charge = 0;          // frames a smash attack has been charged (held heavy)
    // Input buffer: the last fresh press, and how many frames ago it was
    // (255 = none). See input_buffer in GameConfig.
    uint8_t buf_button = 0;
    int8_t buf_sx = 0, buf_sy = 0;
    uint8_t buf_age = 255;
    bool parry_ok = false;       // ST_SHIELDDROP: the shield was up long enough to parry
    // The last 9 moves that connected, newest first (MOVE_NONE = empty).
    // Repeating a move that's in here weakens it (stale-move negation).
    int8_t stale[9] = {MOVE_NONE, MOVE_NONE, MOVE_NONE, MOVE_NONE, MOVE_NONE,
                       MOVE_NONE, MOVE_NONE, MOVE_NONE, MOVE_NONE};
    // Combo counter (like Smash's training mode): hits in this fighter's
    // current true combo. A hit continues it only if the opponent was still
    // stunned (hitstun or knocked down); it ends when they can act again,
    // or when this fighter gets hit.
    uint8_t combo = 0;
    uint16_t combo_timer = 0;  // frames since the string's last hit
    // Frames since this fighter last defended successfully (dodged through a
    // hitbox, parried, or blocked with the shield); 65535 = no defense pending.
    // A hit within cfg.counter_window of it is a counterattack.
    uint16_t since_defense = 65535;
    // Last frame's button. Holding jump must jump only once, so jumps react to
    // the frame the button goes from released to pressed.
    uint8_t prev_button = BTN_NONE;
};

// Everything that describes one game at one instant. step() reads nothing else,
// so copying this struct gives an exact save point.
struct GameState {
    Fighter f[2];
    int32_t frame = 0;
    bool over = false;  // someone ran out of stocks, or time ran out
};

// Tuning numbers. They are kept apart from GameState because they never change
// during a game. Distances in sim units (1 = 5 Smash units), per frame.
// Values in comments like "Mario 1.76" are the Smash Ultimate originals.
struct GameConfig {
    // Movement. Starting to move (or turning around) begins with a fast dash,
    // then settles into a run; speed changes smoothly (ground_accel).
    float dash_speed = 0.387f;      // Mario initial dash 1.936
    int dash_frames = 12;
    float run_speed = 0.352f;       // Mario run 1.76
    float ground_accel = 0.06f;
    float ground_friction = 0.0204f;// Mario traction 0.102
    float air_speed = 0.242f;       // Mario air speed 1.208
    float air_accel = 0.016f;       // Mario air acceleration 0.08
    float air_friction = 0.002f;    // floaty: you keep your momentum in the air
    // Jumps: velocities that reach Mario's heights (full hop 36.3, short hop
    // 17.5, double jump 36.3) under this gravity. Gravity and fall speeds are
    // 20% above Mario's (0.087, 1.5, 2.4) for a snappier, less floaty feel;
    // jump velocities are scaled by sqrt(1.2) so the heights stay Mario's.
    float jump_velocity = 0.551f;
    float short_hop_velocity = 0.383f;
    float double_jump_velocity = 0.551f;
    float gravity = 0.0209f;
    float max_fall_speed = 0.36f;
    float fastfall_speed = 0.576f;
    int fastfall_hold = 6;  // frames of holding down (no attack) before fast-falling
    int jumpsquat_frames = 3;       // universal in Ultimate
    int landing_lag = 2;    // after a normal landing (aerials use their own)
    int air_whiff_limit = 2;  // missed air attacks allowed per trip through the air
    // Input buffer (Ultimate: 9 frames): a button pressed this many frames or
    // less before the fighter can act again still comes out on the first free
    // frame, so presses during landing lag or the end of a move aren't lost.
    int input_buffer = 9;
    // Smash attack charge (Ultimate): hold heavy and the smash pauses
    // charge_lead frames before its first hitbox, for up to charge_max frames;
    // a full charge hits charge_bonus (40%) harder.
    int charge_max = 60;
    int charge_lead = 3;
    float charge_bonus = 0.4f;
    // Pummel (light while holding someone): a little damage, and it uses up
    // pummel_hold_cost frames of the hold, so pummels trade hold time for %.
    float pummel_damage = 1.3f;   // Mario
    int pummel_hold_cost = 12;
    // Dodges (Mario). "Intangible" (i-frames) = hitboxes pass through you.
    // Frames count from 1 like move frames: intangible on frames start..end.
    int spotdodge_frames = 25, spotdodge_iframe_start = 3, spotdodge_iframe_end = 17;
    int roll_frames = 31, roll_iframe_start = 4, roll_iframe_end = 15;
    float roll_distance = 5.0f;  // covered during the first roll_move_frames frames
    int roll_move_frames = 20;
    int airdodge_frames = 30, airdodge_iframe_start = 3, airdodge_iframe_end = 18;
    float airdodge_speed = 0.55f;   // directional air dodge: initial speed, fading over...
    int airdodge_move_frames = 18;  // ...this many frames (gravity is off meanwhile)
    int dodge_landing_lag = 10;       // landing during/after a neutral air dodge
    int dir_dodge_landing_lag = 14;   // ...after a directional one
    // Dodge staling (Ultimate): duration x (1 + 0.06 per stale level, max 0.3),
    // intangibility starts up to 3 frames later; resets after a second without dodging.
    float dodge_stale_step = 0.06f;
    int dodge_stale_reset = 60;
    // Shield (Ultimate: 50 HP, 11-frame shield drop, parry in its first 5 frames).
    float shield_max = 50.0f;
    float shield_drain = 0.15f;     // per frame while shielding
    float shield_regen = 0.08f;     // per frame while not shielding
    int shield_drop_lag = 11;
    int parry_window = 5;           // shield drop frames 1-5 parry a hit...
    int parry_min_shield = 3;       // ...if the shield was up at least this long
    int parry_attacker_lag = 14;    // extra freeze for a parried attacker: a punish window
    int shieldbreak_frames = 150;   // dazed after a shield break (2.5 s)
    // Grabs: hold for grab_hold_base + grab_hold_per_percent * victim % frames
    // (mashing shortens it by grab_mash each press), then the victim breaks free.
    int grab_hold_base = 40;
    float grab_hold_per_percent = 0.8f;
    int grab_mash = 4;
    int grab_release_lag = 12;
    float grab_hold_distance = 1.9f;  // the held opponent sits this far in front
    // Tech (Ultimate: 11-frame window) with a 40-frame lockout against mashing.
    // Missing it after a tumbling hit = knocked down.
    int tech_window = 11, tech_lockout = 40, tech_frames = 26, tech_iframes = 20;
    int knockdown_min = 10;      // lying frames before a get-up option can be chosen...
    int knockdown_max = 60;      // ...and the automatic get-up after this many
    int getup_frames = 30, getup_iframes = 22;
    // Ledges (the main stage's ends). Grab when falling near one (not holding
    // down); intangibility shrinks with regrabs; max 6 grabs before landing.
    int ledge_invuln = 45;
    int ledge_max_grabs = 6;
    int ledge_min_frames = 8;       // hang at least this long before choosing an option
    int ledge_hang_max = 180;       // then you let go (3 s; Ultimate is 6.5 s, too safe here)
    int ledge_climb_frames = 30, ledge_climb_invuln = 26;
    int ledge_option_invuln = 20;   // ledge attack / roll start intangible
    float ledge_hang_x = 1.0f, ledge_hang_y = -2.6f;  // where you hang, relative to the ledge
    // Body
    float hurtbox_w = 2.0f, hurtbox_h = 3.0f;
    float crouch_h = 2.0f, lying_h = 1.2f;
    // (Two grounded fighters can't overlap; see push_apart in sim.cpp.)
    // Combat (Smash formulas): knockback in Smash units (knockback()),
    // launch speed = knockback * launch_speed_scale (0.03 / 5), shrinking by
    // kb_decay per frame (0.051 / 5); hitstun = 0.4 per knockback unit - 1.
    float weight = 98.0f;              // Mario
    float launch_speed_scale = 0.006f;
    float kb_decay = 0.0102f;
    float hitstun_per_kb = 0.4f;
    float tumble_kb = 80.0f;           // stronger hits make you tumble (techable, knockdown)
    float crouch_kb_mult = 0.85f;      // crouch cancel (Ultimate)
    // Hitstun cancelling (Smash 4/Ultimate): a launched fighter may air dodge
    // after 40 frames if launch speed < 2.5, or attack after 45 if < 2.0 (Smash units).
    int hitstun_cancel_dodge = 40, hitstun_cancel_attack = 45;
    float hitstun_cancel_dodge_speed = 0.5f, hitstun_cancel_attack_speed = 0.4f;
    // Hitlag = floor(damage * 0.65 + 6) frames, capped (Ultimate).
    float hitlag_base = 6.0f, hitlag_per_damage = 0.65f;
    int hitlag_max = 30;
    float di_max_degrees = 15.0f;  // most a victim can bend their launch angle
    // Clanks: ground attacks colliding. Within clank_range % damage, both
    // rebound; otherwise the weaker one does. Rebound = floor((d + 4) * 15/8).
    float clank_range = 9.0f;
    int rebound_max = 58;
    // Stale-move negation: a move found at position i (0 = newest) of the
    // last-9 queue loses stale_steps[i] of its power, summed over every time it
    // appears. A move not in the queue at all gets fresh_bonus.
    float stale_steps[9] = {0.08f, 0.07594f, 0.06782f, 0.06028f, 0.05274f, 0.0452f, 0.03766f, 0.03012f, 0.02258f};
    float fresh_bonus = 1.05f;
    int combo_window = 60;  // scale for the "frames since my last hit" observation
    // Counterattacks: a hit landed within this many frames of a successful
    // defense (dodge through a hitbox, parry, shield block) is a counter.
    int counter_window = 60;
    // Stage: Battlefield. Surface 0 is the main floor; 1-3 are pass-through
    // platforms. Dropping through ignores platforms this long.
    Surface surfaces[NUM_SURFACES] = {{-17.6f, 17.6f, 0}, {-13.1f, -6.05f, 4.8f}, {6.05f, 13.1f, 4.8f}, {-3.5f, 3.5f, 9.4f}};
    // (Battlefield's layout made 10% wider; the heights are unchanged.)
    int drop_through_frames = 12;
    float stage_depth = 6.0f;        // the main stage is a solid block this deep: walls and an underside
    float blast_x = 49.6f;           // KO if |x| > 49.6 (32 past each ledge, as on Battlefield)...
    float blast_top = 38.4f;         // ...or y > 38.4...
    float blast_bottom = -28.0f;     // ...or y < -28
    float stage_half_width = 17.6f;  // = surfaces[0] extent: the ledges are at +-17.6
    float spawn_x = 8.8f;
    float respawn_y = 14.0f;
    int respawn_invuln = 120;
    uint8_t stocks = 3;
    int time_limit_frames = 7 * 60 * 60;  // 7 minutes (competitive Smash: 3 stocks, 7 min)
    // Filled in by load_moves(). Indexed by MoveId.
    MoveDef moves[NUM_MOVES];
};

// What happened during one step. Rewards are built from these, not from
// comparing states. Index = player.
struct StepEvents {
    bool hit[2] = {false, false};    // player p hit the other player
    float damage[2] = {0, 0};        // damage player p dealt
    bool ko[2] = {false, false};     // player p lost a stock
    int8_t move_started[2] = {MOVE_NONE, MOVE_NONE};  // move player p started this frame
    int8_t hit_move[2] = {MOVE_NONE, MOVE_NONE};      // move player p hit with this frame
    uint8_t combo[2] = {0, 0};       // if p hit: the length of p's combo, this hit included
    bool shield_block[2] = {false, false};  // p's shield blocked a hit
    bool shield_break[2] = {false, false};  // p's shield broke
    bool grab[2] = {false, false};          // p grabbed the opponent
    bool tech[2] = {false, false};          // p teched a landing
    bool knockdown[2] = {false, false};     // p was knocked down
    int8_t jump[2] = {0, 0};                // p left the ground: 1 = full jump, 2 = short hop
    bool di[2] = {false, false};            // p was launched while holding a DI direction
    bool parry[2] = {false, false};         // p parried a hit
    bool clank[2] = {false, false};         // p's attack clanked (p rebounded)
    bool ledge_grab[2] = {false, false};    // p grabbed the ledge
    bool escaped[2] = {false, false};       // p struggled out of a grab
    bool evade[2] = {false, false};         // p dodged through an attack (i-frames), once per attack
    // If p hit: why it was a punish. counter = within counter_window of p's
    // successful defense; whiff_punish = the victim was stuck recovering from
    // a missed attack or grab, an aerial's landing lag, or a clank rebound.
    bool counter[2] = {false, false};
    bool whiff_punish[2] = {false, false};
};

// An active hitbox placed in the world, e.g. for drawing it or for observations.
struct ActiveHitbox {
    int owner;                 // player index
    float x, y;                // world position of the circle's centre
    int frames_left;           // active frames remaining after this one
    bool spent;                // the move already connected, so this hitbox can't hit again
    float power;               // stale-move multiplier for damage and knockback
    const HitboxDef* def;      // points into GameConfig::moves; valid as long as the config lives
};

// ---------------------------------------------------------------------------
// Functions
// ---------------------------------------------------------------------------

// Reads data/moves/<name>.json for every name in MOVE_NAMES into cfg.moves.
// Returns "" on success, otherwise an error message naming the file and field.
std::string load_moves(GameConfig& cfg, const std::string& dir);
// Same, but from the files' contents, in MOVE_NAMES order. Replays carry their
// own copy of the move files, so they still play back after the files change.
std::string load_moves_from_text(GameConfig& cfg, const std::string texts[NUM_MOVES]);

void reset(GameState& s, const GameConfig& cfg);

// Advances the game by exactly one frame (1/60 s). It is pure: the result
// depends only on its arguments, the way a seeded env.step() is reproducible.
void step(GameState& s, const Input in[2], const GameConfig& cfg, StepEvents& ev);

// Writes up to `max` currently active hitboxes into `out`; returns how many.
int active_hitboxes(const GameState& s, const GameConfig& cfg, ActiveHitbox out[], int max);

// True if hits pass through this fighter right now (respawn, ledge and
// get-up intangibility, or the i-frames of a dodge, roll or tech).
bool intangible(const Fighter& f, const GameConfig& cfg);

// Stale-move multiplier (damage and knockback) for fighter f using move m now.
float stale_power(const Fighter& f, int m, const GameConfig& cfg);

// Who is winning: 0 or 1, or -1 for a draw. More stocks wins; with equal
// stocks (a timeout), less damage wins.
int winner(const GameState& s);

// The combat formulas, exposed so tests can check them directly.
// Smash's knockback: ((((p/10 + p*d/20) * 200/(w+100) * 1.4) + 18) * KBG/100) + BKB,
// with p = the victim's % after the hit and d = the damage dealt.
float knockback(const HitboxDef& hb, float victim_percent, float damage, float weight);
int hitstun_frames(float knockback, const GameConfig& cfg);
int hitlag_frames(float damage, const GameConfig& cfg);

// A 64-bit fingerprint of the state. Two states that are identical field by
// field always give the same hash. The determinism tests compare these.
uint64_t hash_state(const GameState& s);
