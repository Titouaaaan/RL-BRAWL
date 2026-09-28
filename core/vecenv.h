// vecenv.h: the RL-facing layer. N games stepped together, with frame skip,
// reaction delay, observations, rewards and auto-reset. This is the C++
// equivalent of a gymnasium VectorEnv. The Python binding only exposes it.
//
// Two-player layout: every per-player output has a player axis, so
// obs[i, p] is game i seen by player p, and reward[i, p] is p's reward.
// One policy can play both sides.
#pragma once

#include <vector>

#include "sim.h"

// Observation layout (all float32, roughly scaled to [-1, 1]):
//   [0, 37)    me:  fighter features (see write_fighter in vecenv.cpp)
//   [37, 74)   opponent: same 37 features
//   [74, 77)   relative dx, dy, and time left
//   [77, 121)  4 hitbox slots x 11 features (the opponent's hitboxes first)
// "Me" is always the observing player, and x is mirrored for player 1, so
// both players see themselves start on the left. That's the egocentric
// canonicalization from the design doc. Actions are mirrored the same way:
// player 1's stick_x is negated before it reaches the game.
const int OBS_FIGHTER = 27 + NUM_STATES;  // 37: 27 features + the state one-hot
const int OBS_HITBOXES = 4;
const int OBS_HITBOX = 11;
const int OBS_DIM = 2 * OBS_FIGHTER + 3 + OBS_HITBOXES * OBS_HITBOX;  // 121

struct EnvParams {
    int frame_skip = 4;          // sim frames per decision; the action repeats for each
    int reaction_delay = 0;      // observe the state from this many frames ago
    float damage_reward = 0.01f; // reward per % dealt (and penalty per % taken)
};

// Writes player `player`'s view of `s` into out[0..OBS_DIM).
void write_obs(const GameState& s, const GameConfig& cfg, int player, float* out);

struct VecEnv {
    GameConfig cfg;
    EnvParams params;
    int num_envs;
    std::vector<GameState> games;
    // Last reaction_delay + 1 states of each game, a ring buffer indexed by frame.
    std::vector<GameState> history;

    // Output buffers, rewritten by every step(). Python sees them as NumPy
    // arrays pointing at this memory (no copying). They're allocated once in
    // the constructor and never resized, so those pointers stay valid.
    std::vector<float> obs;          // [num_envs][2][OBS_DIM]
    std::vector<float> final_obs;    // like obs; for games that just ended, their last obs
    std::vector<float> reward;       // [num_envs][2], summed over the frame-skip frames
    std::vector<uint8_t> terminated; // [num_envs] game ended by stocks (a real ending)
    std::vector<uint8_t> truncated;  // [num_envs] game ended by the time limit (cut short)
    std::vector<int8_t> winner;      // [num_envs] if it ended: 0, 1, or -1 = draw
    std::vector<uint64_t> final_hash; // [num_envs] if it ended: hash_state of the last state (replay check)
    // Player 0's move statistics summed over all games, for logging. They
    // only grow; the training script zeroes them after reading.
    std::vector<int64_t> move_uses;  // [NUM_MOVES] times each move was started
    std::vector<int64_t> move_hits;  // [NUM_MOVES] times each move hit

    VecEnv(int num_envs, const GameConfig& cfg, const EnvParams& params);

    void reset_all();
    // actions: [num_envs][2][3] = (stick_x, stick_y, button), egocentric
    // (player 1's stick_x is mirrored). Games that end
    // are reset right away, and obs then holds the new game's first observation.
    void step(const int8_t* actions);

private:
    void reset_game(int i);
    void write_game_obs(int i, float* dst);
};
