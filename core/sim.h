// sim.h: the whole game's data types and the functions that advance it.
//
// A header file (.h) is what other files #include to learn which types and
// functions exist. The function bodies live in sim.cpp and moves.cpp. The
// compiler turns each .cpp into machine code separately, and the linker then
// joins them together.
#pragma once  // stops this header from being pasted in twice by the same .cpp file

#include <cstdint>  // fixed-width integer types: int8_t, uint8_t, uint64_t
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

// Only one button can be pressed per frame. That matches the RL action space,
// which is button in {none, jump, light, heavy, dodge}.
enum Button : uint8_t { BTN_NONE = 0, BTN_JUMP = 1, BTN_LIGHT = 2, BTN_HEAVY = 3, BTN_DODGE = 4 };

// One player's controller input for a single frame.
struct Input {
    int8_t stick_x = 0;  // -1 left, 0 neutral, 1 right
    int8_t stick_y = 0;  // -1 down (fast-fall), 0 neutral, 1 up
    uint8_t button = BTN_NONE;
};

// ---------------------------------------------------------------------------
// Moves (loaded from data/moves/*.json by load_moves in moves.cpp)
// ---------------------------------------------------------------------------

// Which move comes out is decided by the game, not the agent: ground or air,
// light or heavy, and the stick's up/down (left/right only sets facing).
//
//                  down              neutral / side    up
//   ground light   down_tilt         jab               up_tilt
//   ground heavy   down_heavy        heavy             up_heavy
//   air light      down_air          air               up_air
//   air heavy      heavy_down_air    heavy_air         heavy_up_air
enum MoveId : int8_t {
    MOVE_NONE = -1,
    MOVE_DOWN_TILT, MOVE_JAB, MOVE_UP_TILT,
    MOVE_DOWN_HEAVY, MOVE_HEAVY, MOVE_UP_HEAVY,
    MOVE_DOWN_AIR, MOVE_AIR, MOVE_UP_AIR,
    MOVE_HEAVY_DOWN_AIR, MOVE_HEAVY_AIR, MOVE_HEAVY_UP_AIR,
    NUM_MOVES,  // = 12 (an enum's values count up from 0, so this is the count)
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
};

// "Frame data": the timing of a move. Startup = frames before the first
// hitbox, active = frames with a hitbox, recovery = frames after, during which
// the attacker is still stuck in the move and can be punished.
struct MoveDef {
    int total_frames = 1;  // startup + active + recovery
    int landing_lag = 0;   // aerials only: stuck frames if you land mid-move
    std::vector<HitboxDef> hitboxes;
};

// ---------------------------------------------------------------------------
// Game state
// ---------------------------------------------------------------------------

// A fighter's "state" in the state-machine sense: what it's doing right now.
// Each frame, (state, state_frame, input) decides the next state.
enum FighterState : uint8_t {
    ST_IDLE = 0,       // standing, can act
    ST_RUN = 1,        // moving: a fast dash for the first dash_frames, then a run; can act
    ST_JUMPSQUAT = 2,  // crouching for a few frames before leaving the ground
    ST_AIRBORNE = 3,   // in the air, can act (double jump, aerial, drift)
    ST_LANDING = 4,    // landing lag: stuck for a few frames after touching down
    ST_ATTACK = 5,     // doing a move (see Fighter::move)
    ST_HITSTUN = 6,    // got hit: can't act until `hitstun` runs out
    ST_SPOTDODGE = 7,  // dodge on the ground: briefly intangible, then a short recovery
    ST_AIRDODGE = 8,   // dodge in the air: briefly intangible, then helpless
    ST_HELPLESS = 9,   // after an air dodge: can only drift until touching the ground
    NUM_STATES = 10,
};

struct Fighter {
    float x = 0, y = 0;    // position of the feet; y = 0 is the floor, and y goes up
    float vx = 0, vy = 0;  // the fighter's own velocity (walking, jumping, gravity)
    // Knockback velocity, kept separate from vx/vy because it follows its own
    // rule: it shrinks by a fixed amount every frame (GameConfig::kb_decay).
    float launch_vx = 0, launch_vy = 0;
    float percent = 0;     // damage taken; more % = flies further when hit
    int8_t facing = 1;     // 1 = facing right, -1 = facing left
    uint8_t state = ST_IDLE;
    int32_t state_frame = 0;  // frames spent in `state` so far (1 on the first frame)
    bool grounded = true;
    bool fastfall = false;
    // Consecutive frames the stick has been held down with no attack button.
    // Fast-fall needs GameConfig::fastfall_hold of them, so down + attack
    // does a down move without dropping.
    uint8_t down_frames = 0;
    // Set when an attack is pressed; cleared once the stick leaves "down".
    // A down input that was part of an attack can't turn into a fast-fall:
    // you have to let go and press down again.
    bool fastfall_blocked = false;
    // An air attack that ended without hitting locks out air attacks until
    // you land (or get hit). Hitting keeps them available, so juggles work.
    bool air_attack_locked = false;
    uint8_t jumps_left = 2;   // one ground jump plus one double jump
    uint8_t stocks = 3;       // lives left
    int8_t move = MOVE_NONE;  // current move while state == ST_ATTACK
    bool move_hit = false;    // this use of the move already hit (each use hits at most once)
    int16_t hitstun = 0;      // frames of hitstun left
    int16_t landing_lag = 0;  // length of the current ST_LANDING
    int16_t invuln = 0;       // frames left of invulnerability (after respawning)
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
    // Movement
    // Starting to move (or turning around) begins with a short, fast dash,
    // then settles into a run. Turning during the dash restarts it the other
    // way (a "dash dance").
    float dash_speed = 0.45f;
    int dash_frames = 8;
    float run_speed = 0.34f;
    float ground_friction = 0.04f;  // how fast you stop sliding on the ground
    float air_speed = 0.22f;        // max horizontal speed from drifting
    float air_accel = 0.03f;
    float air_friction = 0.01f;
    float jump_velocity = 0.9f;
    float double_jump_velocity = 0.85f;
    float gravity = 0.045f;        // lower = floatier (longer hang time)
    float max_fall_speed = 1.1f;
    float fastfall_speed = 1.6f;
    int fastfall_hold = 6;  // frames of holding down (no attack) before fast-falling
    int jumpsquat_frames = 3;
    int landing_lag = 3;  // after a normal landing (aerials use their own)
    // Dodges. "Intangible" (i-frames) = hitboxes pass through you. Frames
    // count from 1 like move frames: intangible on frames start..end.
    int spotdodge_frames = 26, spotdodge_iframe_start = 3, spotdodge_iframe_end = 15;
    int airdodge_frames = 30, airdodge_iframe_start = 3, airdodge_iframe_end = 18;
    int dodge_landing_lag = 10;  // landing during/after an air dodge
    // Body
    float hurtbox_w = 2.0f, hurtbox_h = 3.0f;
    // (Two grounded fighters can't overlap; see push_apart in sim.cpp.)
    // Combat
    float hitstun_per_kb = 25.0f;  // hitstun frames = knockback * this
    float kb_decay = 0.05f;        // knockback speed lost per frame
    // Stage and rules
    float stage_half_width = 30.0f;  // the floor runs from -30 to +30
    float blast_x = 55.0f;           // KO if |x| > 55...
    float blast_top = 40.0f;         // ...or y > 40...
    float blast_bottom = -25.0f;     // ...or y < -25
    float spawn_x = 10.0f;
    float respawn_y = 10.0f;
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
};

// An active hitbox placed in the world, e.g. for drawing it or for observations.
struct ActiveHitbox {
    int owner;                 // player index
    float x, y;                // world position of the circle's centre
    int frames_left;           // active frames remaining after this one
    bool spent;                // the move already hit, so this hitbox can't hit again
    const HitboxDef* def;      // points into GameConfig::moves; valid as long as the config lives
};

// ---------------------------------------------------------------------------
// Functions
// ---------------------------------------------------------------------------

// Reads jab.json, heavy.json and aerial.json from `dir` into cfg.moves.
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
// or the i-frames of a dodge).
bool intangible(const Fighter& f, const GameConfig& cfg);

// Who is winning: 0 or 1, or -1 for a draw. More stocks wins; with equal
// stocks (a timeout), less damage wins.
int winner(const GameState& s);

// The combat formulas, exposed so tests can check them directly.
float knockback(const HitboxDef& hb, float victim_percent);
int hitstun_frames(float knockback, const GameConfig& cfg);

// A 64-bit fingerprint of the state. Two states that are identical field by
// field always give the same hash. The determinism tests compare these.
uint64_t hash_state(const GameState& s);
