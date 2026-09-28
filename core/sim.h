// sim.h: the whole game's data types and the functions that advance it.
//
// A header file (.h) is what other files #include to learn which types and
// functions exist. The function bodies live in sim.cpp. The compiler turns each
// .cpp into machine code separately, and the linker then joins them together.
#pragma once  // stops this header from being pasted in twice by the same .cpp file

#include <cstdint>  // fixed-width integer types: int8_t, uint8_t, uint64_t

// Only one button can be pressed per frame. That matches the RL action space,
// which is button in {none, jump, light, heavy}.
enum Button : uint8_t { BTN_NONE = 0, BTN_JUMP = 1, BTN_LIGHT = 2, BTN_HEAVY = 3 };

// One player's controller input for a single frame.
struct Input {
    int8_t stick_x = 0;  // -1 left, 0 neutral, 1 right
    int8_t stick_y = 0;  // -1 down, 0 neutral, 1 up
    uint8_t button = BTN_NONE;
};

// A fighter's "state" in the state-machine sense: what it is doing right now.
// Phase 0 has only movement. Attacking, Hitstun and the others come in phase 1.
enum FighterState : uint8_t { ST_IDLE = 0, ST_WALK = 1, ST_AIRBORNE = 2 };

struct Fighter {
    float x = 0, y = 0;    // position; y = 0 is the floor, and y goes up
    float vx = 0, vy = 0;  // velocity in units per frame
    int8_t facing = 1;     // 1 = facing right, -1 = facing left
    uint8_t state = ST_IDLE;
    int32_t state_frame = 0;  // how many frames the fighter has been in `state`
    uint8_t jumps_left = 2;   // one ground jump plus one double jump
    // Last frame's button. Needed so that *holding* jump jumps only once:
    // we act on a button only on the frame it goes from released to pressed.
    uint8_t prev_button = BTN_NONE;
};

// Everything that describes one game at one instant. step() reads nothing else,
// so copying this struct gives an exact save point.
struct GameState {
    Fighter f[2];
    int32_t frame = 0;
};

// Tuning numbers. They are kept apart from GameState because they never change
// during a game. Units: distance per frame, at 60 frames per second.
struct GameConfig {
    float walk_speed = 0.25f;
    float jump_velocity = 0.9f;
    float gravity = 0.05f;
    float max_fall_speed = 1.2f;
    float stage_half_width = 30.0f;  // the floor runs from -30 to +30
    float spawn_x = 10.0f;           // the players start at -10 and +10
};

void reset(GameState& s, const GameConfig& cfg);

// Advances the game by exactly one frame (1/60 s). It is pure: the result
// depends only on its arguments, the way a seeded env.step() is reproducible.
void step(GameState& s, const Input in[2], const GameConfig& cfg);

// A 64-bit fingerprint of the state. Two states that are identical field by
// field always give the same hash. The determinism tests compare these.
uint64_t hash_state(const GameState& s);
