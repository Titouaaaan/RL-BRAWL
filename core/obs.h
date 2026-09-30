// obs.h: what an agent sees. Split from vecenv so the Godot extension can
// build the exact same observation for a live bot (vecenv.cpp throws C++
// exceptions, which the Godot build turns off).
#pragma once

#include "sim.h"

// Observation layout (all float32, roughly scaled to [-1, 1]):
//   [0, 61)    me:  fighter features (see write_fighter in obs.cpp)
//   [61, 122)  opponent: same 61 features
//   [122, 125) relative dx, dy, and time left
//   [125, 173) 4 hitbox slots x 12 features (the opponent's hitboxes first)
// "Me" is always the observing player, and x is mirrored for player 1, so
// both players see themselves start on the left. That's the egocentric
// canonicalization from the design doc. Actions are mirrored the same way:
// player 1's stick_x is negated before it reaches the game.
const int OBS_FIGHTER = 37 + NUM_STATES;  // 61: 37 features + the state one-hot
const int OBS_HITBOXES = 4;
const int OBS_HITBOX = 12;
const int OBS_DIM = 2 * OBS_FIGHTER + 3 + OBS_HITBOXES * OBS_HITBOX;  // 173

// Writes player `player`'s view of `s` into out[0..OBS_DIM).
void write_obs(const GameState& s, const GameConfig& cfg, int player, float* out);
