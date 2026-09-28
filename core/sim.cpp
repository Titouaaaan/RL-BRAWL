#include "sim.h"

#include <algorithm>  // std::min, std::max
#include <cstring>    // std::memcpy

void reset(GameState& s, const GameConfig& cfg) {
    s = GameState{};  // put every field back to the default written in sim.h
    s.f[0].x = -cfg.spawn_x;
    s.f[0].facing = 1;
    s.f[1].x = cfg.spawn_x;
    s.f[1].facing = -1;
}

static void set_state(Fighter& f, FighterState st) {
    // `static` on a function here means "only visible inside this .cpp file".
    if (f.state != st) {
        f.state = st;
        f.state_frame = 0;
    }
}

static void step_fighter(Fighter& f, const Input& in, const GameConfig& cfg) {
    bool grounded = (f.state != ST_AIRBORNE);
    bool jump_pressed = (in.button == BTN_JUMP && f.prev_button != BTN_JUMP);

    // Jump: a ground jump, or a double jump if one is left.
    if (jump_pressed && f.jumps_left > 0) {
        f.vy = cfg.jump_velocity;
        f.jumps_left -= 1;
        set_state(f, ST_AIRBORNE);
        grounded = false;
    }

    // Horizontal movement. For now air drift is the same as walking.
    // ponytail: full-speed air control; phase 1 adds air drift and friction from the design doc.
    f.vx = cfg.walk_speed * in.stick_x;
    if (in.stick_x != 0) f.facing = in.stick_x;
    if (grounded) set_state(f, in.stick_x != 0 ? ST_WALK : ST_IDLE);

    // Gravity while airborne, capped at the maximum fall speed.
    if (!grounded) f.vy = std::max(f.vy - cfg.gravity, -cfg.max_fall_speed);

    float prev_y = f.y;
    f.x += f.vx;
    f.y += f.vy;

    // Floor collision. The floor has edges, so a fighter who walks past one
    // falls. The blast zones (KO) are added in phase 1; until then, whoever
    // falls just keeps falling. Landing needs prev_y >= 0 (the fighter was
    // above the floor last frame), so someone below the stage can't snap up.
    bool over_floor = (f.x >= -cfg.stage_half_width && f.x <= cfg.stage_half_width);
    if (over_floor && prev_y >= 0.0f && f.y <= 0.0f) {
        f.y = 0.0f;
        f.vy = 0.0f;
        f.jumps_left = 2;
        if (!grounded) set_state(f, ST_IDLE);  // landing
    } else if (grounded && !over_floor) {
        set_state(f, ST_AIRBORNE);  // walked off the edge
        f.jumps_left = std::min<uint8_t>(f.jumps_left, 1);  // only the double jump is left
    }

    f.prev_button = in.button;
    f.state_frame += 1;
}

void step(GameState& s, const Input in[2], const GameConfig& cfg) {
    step_fighter(s.f[0], in[0], cfg);
    step_fighter(s.f[1], in[1], cfg);
    s.frame += 1;
}

// FNV-1a: a very simple hash. For each byte, XOR it in, then multiply by a
// fixed prime. It is not cryptographic, but fine for "are these two equal?".
static void mix(uint64_t& h, const void* data, size_t n) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < n; ++i) {
        h ^= bytes[i];
        h *= 1099511628211ull;
    }
}

uint64_t hash_state(const GameState& s) {
    // We hash field by field instead of hashing the raw struct in one go,
    // because the compiler inserts unused "padding" bytes between fields to
    // keep them aligned, and those bytes can hold garbage that differs between
    // two otherwise-identical states.
    uint64_t h = 14695981039346656037ull;
    for (const Fighter& f : s.f) {
        mix(h, &f.x, sizeof f.x);
        mix(h, &f.y, sizeof f.y);
        mix(h, &f.vx, sizeof f.vx);
        mix(h, &f.vy, sizeof f.vy);
        mix(h, &f.facing, sizeof f.facing);
        mix(h, &f.state, sizeof f.state);
        mix(h, &f.state_frame, sizeof f.state_frame);
        mix(h, &f.jumps_left, sizeof f.jumps_left);
        mix(h, &f.prev_button, sizeof f.prev_button);
    }
    mix(h, &s.frame, sizeof s.frame);
    return h;
}
