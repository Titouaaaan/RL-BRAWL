// sim.h: the whole game's data types and the functions that advance it.
//
// A header file (.h) is what other files #include to learn which types and
// functions exist. The function bodies live in sim.cpp and moves.cpp. The
// compiler turns each .cpp into machine code separately, and the linker then
// joins them together.
//
// Most mechanics follow Super Smash Bros. (see SmashWiki): hitstun, hitlag,
// directional influence, stale-move negation, shields, grabs, rolls, techs.
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
//           light / heavy: attacks (see the move table below)
//           dodge: shield; + left/right = roll; + down = spot dodge
//           light while shielding = grab; then any direction = throw
//   air     jump: double jump;  light / heavy: aerials;  dodge: air dodge
//           hold down (no attack): fast-fall; on a platform: drop through
//   hit     hold the stick while being hit: directional influence (DI)
//           press dodge just before hitting the ground: tech
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
// light or heavy, and the stick's up/down (left/right only sets facing).
//
//                  down              neutral / side    up
//   ground light   down_tilt         jab               up_tilt
//   ground heavy   down_heavy        heavy             up_heavy
//   air light      down_air          air               up_air
//   air heavy      heavy_down_air    heavy_air         heavy_up_air
//
// Plus the grab (light while shielding) and its four throws.
enum MoveId : int8_t {
    MOVE_NONE = -1,
    MOVE_DOWN_TILT, MOVE_JAB, MOVE_UP_TILT,
    MOVE_DOWN_HEAVY, MOVE_HEAVY, MOVE_UP_HEAVY,
    MOVE_DOWN_AIR, MOVE_AIR, MOVE_UP_AIR,
    MOVE_HEAVY_DOWN_AIR, MOVE_HEAVY_AIR, MOVE_HEAVY_UP_AIR,
    MOVE_GRAB, MOVE_THROW_FORWARD, MOVE_THROW_BACK, MOVE_THROW_UP, MOVE_THROW_DOWN,
    NUM_MOVES,  // = 17 (an enum's values count up from 0, so this is the count)
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
    float base_kb = 0;        // knockback (launch speed, units/frame) at 0%...
    float kb_growth = 0;      // ...plus this much per % of damage the victim has
    float angle = 45;         // launch angle in degrees: 0 = straight forward, 90 = straight up
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
    std::vector<HitboxDef> hitboxes;
};

// ---------------------------------------------------------------------------
// Game state
// ---------------------------------------------------------------------------

// A fighter's "state" in the state-machine sense: what it's doing right now.
// Each frame, (state, state_frame, input) decides the next state.
enum FighterState : uint8_t {
    ST_IDLE = 0,        // standing, can act
    ST_RUN = 1,         // moving: a fast dash for the first dash_frames, then a run; can act
    ST_JUMPSQUAT = 2,   // crouching for a few frames before leaving the ground
    ST_AIRBORNE = 3,    // in the air, can act (double jump, aerial, drift)
    ST_LANDING = 4,     // landing lag (or dropping a shield): stuck for a few frames
    ST_ATTACK = 5,      // doing a move (see Fighter::move), including grabs and throws
    ST_HITSTUN = 6,     // got hit: can't act until `hitstun` runs out
    ST_SPOTDODGE = 7,   // dodge in place: briefly intangible, then a short recovery
    ST_AIRDODGE = 8,    // dodge in the air: briefly intangible, then helpless
    ST_HELPLESS = 9,    // after an air dodge: can only drift until touching the ground
    ST_SHIELD = 10,     // holding the shield: attacks bounce off, grabs don't
    ST_SHIELDSTUN = 11, // an attack hit the shield: stuck for a moment
    ST_ROLL = 12,       // rolling left or right: intangible for most of it
    ST_SHIELDBREAK = 13,// the shield ran out: dazed and defenceless for a while
    ST_HOLDING = 14,    // holding a grabbed opponent; any direction throws
    ST_GRABBED = 15,    // being held: can't act until thrown
    ST_KNOCKDOWN = 16,  // hit the ground in hitstun without teching: lying down, then getting up
    ST_TECH = 17,       // teched a landing: a quick, intangible recovery
    NUM_STATES = 18,
};
// Names for the states above, for display (Godot) and debugging.
extern const char* const STATE_NAMES[NUM_STATES];

// Floors you can stand on. Surface 0 is the main stage (solid); the others are
// platforms you can jump through from below and drop through with down.
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
    int16_t hitstun = 0;      // frames of hitstun left
    int16_t landing_lag = 0;  // length of the current ST_LANDING / ST_SHIELDSTUN / ST_KNOCKDOWN
    int16_t invuln = 0;       // frames left of invulnerability (after respawning)
    // Hitlag ("freeze frames"): when a hit connects, both fighters freeze for
    // a few frames. The victim's stick during the freeze is their DI.
    uint8_t hitlag = 0;
    int8_t di_x = 0, di_y = 0;  // the stick held during hitlag, applied when it ends
    uint8_t roll_dir = 0;       // ST_ROLL: 1 = rolling right, 2 = rolling left
    // Frames since dodge was last pressed (capped): a tech needs a recent press.
    uint8_t dodge_ago = 255;
    // The last 9 moves that connected, newest first (MOVE_NONE = empty).
    // Repeating a move that's in here weakens it (stale-move negation).
    int8_t stale[9] = {MOVE_NONE, MOVE_NONE, MOVE_NONE, MOVE_NONE, MOVE_NONE,
                       MOVE_NONE, MOVE_NONE, MOVE_NONE, MOVE_NONE};
    // Combo counter (like Smash's training mode): hits in this fighter's
    // current string. A hit within GameConfig::combo_window frames of the
    // previous one continues it; getting hit, or waiting too long, ends it.
    uint8_t combo = 0;
    uint16_t combo_timer = 0;  // frames since the string's last hit
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
// during a game. Units: distance per frame, at 60 frames per second.
struct GameConfig {
    // Movement. Starting to move (or turning around) begins with a short, fast
    // dash, then settles into a run; speed changes smoothly (ground_accel)
    // rather than instantly. Turning during the dash is a "dash dance".
    float dash_speed = 0.48f;
    int dash_frames = 11;
    float run_speed = 0.36f;
    float ground_accel = 0.09f;     // how fast running speed changes
    float ground_friction = 0.035f; // how fast you stop sliding on the ground
    float air_speed = 0.26f;        // max horizontal speed from drifting
    float air_accel = 0.035f;
    float air_friction = 0.006f;    // low: you keep your momentum in the air (floaty)
    float jump_velocity = 0.85f;
    float short_hop_velocity = 0.58f;
    float double_jump_velocity = 0.8f;
    float gravity = 0.038f;         // lower = floatier (longer hang time)
    float max_fall_speed = 0.95f;
    float fastfall_speed = 1.4f;
    int fastfall_hold = 6;  // frames of holding down (no attack) before fast-falling
    int jumpsquat_frames = 3;
    int landing_lag = 2;    // after a normal landing (aerials use their own)
    int air_whiff_limit = 2;  // missed air attacks allowed per trip through the air
    // Dodges. "Intangible" (i-frames) = hitboxes pass through you. Frames
    // count from 1 like move frames: intangible on frames start..end.
    int spotdodge_frames = 24, spotdodge_iframe_start = 3, spotdodge_iframe_end = 15;
    int airdodge_frames = 30, airdodge_iframe_start = 3, airdodge_iframe_end = 18;
    int dodge_landing_lag = 10;  // landing during/after an air dodge
    int roll_frames = 26, roll_iframe_start = 4, roll_iframe_end = 15;
    float roll_distance = 7.0f;  // covered during the first roll_move_frames frames
    int roll_move_frames = 18;
    // Shield. It shrinks while held and when hit; at 0 it breaks.
    float shield_max = 50.0f;
    float shield_drain = 0.15f;     // per frame while shielding
    float shield_regen = 0.08f;     // per frame while not shielding
    float shield_damage_mult = 1.0f;// shield lost per % of damage blocked
    int shield_drop_lag = 7;        // frames stuck after letting go of the shield
    int shieldbreak_frames = 150;   // dazed after a shield break (2.5 s)
    // Grabs: hold the opponent up to grab_hold_max frames, then throw.
    int grab_hold_max = 45;
    float grab_hold_distance = 1.9f;  // the held opponent sits this far in front
    // Tech: press dodge within tech_window frames before hitting the ground in
    // hitstun. Otherwise, with at least knockdown_hitstun frames of hitstun
    // left, you're knocked down.
    int tech_window = 10, tech_frames = 20, tech_iframes = 14;
    int knockdown_hitstun = 8, knockdown_frames = 32;
    // Body
    float hurtbox_w = 2.0f, hurtbox_h = 3.0f;
    // (Two grounded fighters can't overlap; see push_apart in sim.cpp.)
    // Combat
    float hitstun_per_kb = 25.0f;  // hitstun frames = knockback * this
    float kb_decay = 0.05f;        // knockback speed lost per frame
    // Hitlag = hitlag_base + damage * hitlag_per_damage frames (capped).
    float hitlag_base = 3.0f, hitlag_per_damage = 0.35f;
    int hitlag_max = 12;
    float di_max_degrees = 15.0f;  // most a victim can bend their launch angle
    // Stale-move negation: a move found at position i (0 = newest) of the
    // last-9 queue loses stale_steps[i] of its power, summed over every time it
    // appears. A move not in the queue at all gets fresh_bonus.
    float stale_steps[9] = {0.08f, 0.07594f, 0.06782f, 0.06028f, 0.05274f, 0.0452f, 0.03766f, 0.03012f, 0.02258f};
    float fresh_bonus = 1.05f;
    int combo_window = 60;  // a hit within this many frames of your last one continues your combo
    // Stage: surface 0 is the main floor; 1-3 are pass-through platforms
    // (the "Battlefield" layout). Dropping through ignores platforms this long.
    Surface surfaces[NUM_SURFACES] = {{-30, 30, 0}, {-17, -7, 8}, {7, 17, 8}, {-5, 5, 15}};
    int drop_through_frames = 12;
    float blast_x = 55.0f;           // KO if |x| > 55...
    float blast_top = 45.0f;         // ...or y > 45...
    float blast_bottom = -25.0f;     // ...or y < -25
    float stage_half_width = 30.0f;  // = surfaces[0] extent, used by observations
    float spawn_x = 10.0f;
    float respawn_y = 22.0f;
    int respawn_invuln = 120;
    uint8_t stocks = 3;
    int time_limit_frames = 3 * 60 * 60;  // 3 minutes
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

// True if hits pass through this fighter right now (respawn invulnerability
// or the i-frames of a dodge, roll or tech).
bool intangible(const Fighter& f, const GameConfig& cfg);

// Stale-move multiplier (damage and knockback) for fighter f using move m now.
float stale_power(const Fighter& f, int m, const GameConfig& cfg);

// Who is winning: 0 or 1, or -1 for a draw. More stocks wins; with equal
// stocks (a timeout), less damage wins.
int winner(const GameState& s);

// The combat formulas, exposed so tests can check them directly.
float knockback(const HitboxDef& hb, float victim_percent);
int hitstun_frames(float knockback, const GameConfig& cfg);
int hitlag_frames(float damage, const GameConfig& cfg);

// A 64-bit fingerprint of the state. Two states that are identical field by
// field always give the same hash. The determinism tests compare these.
uint64_t hash_state(const GameState& s);
