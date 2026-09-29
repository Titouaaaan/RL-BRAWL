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
//   [0, 60)    me:  fighter features (see write_fighter in vecenv.cpp)
//   [60, 120)  opponent: same 60 features
//   [120, 123) relative dx, dy, and time left
//   [123, 171) 4 hitbox slots x 12 features (the opponent's hitboxes first)
// "Me" is always the observing player, and x is mirrored for player 1, so
// both players see themselves start on the left. That's the egocentric
// canonicalization from the design doc. Actions are mirrored the same way:
// player 1's stick_x is negated before it reaches the game.
const int OBS_FIGHTER = 36 + NUM_STATES;  // 60: 36 features + the state one-hot
const int OBS_HITBOXES = 4;
const int OBS_HITBOX = 12;
const int OBS_DIM = 2 * OBS_FIGHTER + 3 + OBS_HITBOXES * OBS_HITBOX;  // 171

struct EnvParams {
    int frame_skip = 4;          // sim frames per decision; the action repeats for each
    int reaction_delay = 0;      // observe the state from this many frames ago
    float damage_reward = 0.01f; // reward per % dealt (and penalty per % taken)
    float timeout_reward = 0.0f; // at the time limit: + for the player with less damage, - for the other
    // Combo bonus: the damage reward of the k-th hit in a combo is multiplied
    // by min(combo_growth^(k-1), combo_cap). 1.0 = off. Zero-sum like the
    // damage reward: the victim loses exactly what the attacker gains.
    float combo_growth = 1.0f;
    float combo_cap = 1.0f;
    // Counterattack bonus: a hit that is a counter (right after a successful
    // dodge / parry / shield block) or a whiff punish earns this on top of the
    // damage reward (once, even if both). Zero-sum. 0 = off.
    float counter_reward = 0.0f;
};

// Statistics about player 0 (the learner in training), summed over all games
// and frames, for logging. Names are in STAT_NAMES (same order).
enum Stat {
    STAT_FRAMES, STAT_HITS, STAT_HITS_TAKEN, STAT_DAMAGE_DEALT, STAT_DAMAGE_TAKEN, STAT_KOS, STAT_DEATHS,
    STAT_COMBO_HITS,      // hits that continued a combo (2nd hit or later)
    STAT_COMBOS_TAKEN,    // opponent hits on player 0 that continued the opponent's combo
    STAT_MAX_COMBO,       // longest combo (a max, not a sum)
    STAT_BLOCKS, STAT_GOT_BLOCKED, STAT_SHIELD_BREAKS, STAT_SHIELD_BREAKS_CAUSED,
    STAT_GRABS, STAT_GOT_GRABBED, STAT_TECHS, STAT_KNOCKDOWNS,
    STAT_FULL_JUMPS, STAT_SHORT_HOPS, STAT_DI_USED,
    STAT_PARRIES, STAT_GOT_PARRIED, STAT_CLANKS, STAT_LEDGE_GRABS, STAT_ESCAPES,
    STAT_EVADES,          // attacks player 0 dodged through (i-frames)
    STAT_COUNTERS,        // player 0 hits within counter_window of a successful defense
    STAT_WHIFF_PUNISHES,  // player 0 hits on an opponent recovering from a missed move
    STAT_COUNTERS_TAKEN,  // counters or whiff punishes player 0 suffered
    STAT_FRAMES_AIRBORNE, STAT_FRAMES_ON_PLATFORM, STAT_FRAMES_CLOSE,  // close = within 4 units
    NUM_STATS,
};
extern const char* const STAT_NAMES[NUM_STATS];

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
    std::vector<uint8_t> truncated;  // [num_envs] game ended by the time limit
    std::vector<int8_t> winner;      // [num_envs] if it ended: 0, 1, or -1 = draw
    std::vector<uint64_t> final_hash; // [num_envs] if it ended: hash_state of the last state (replay check)
    // Player 0's move statistics summed over all games, for logging. They
    // only grow; the training script zeroes them after reading.
    std::vector<int64_t> move_uses;  // [NUM_MOVES] times each move was started
    std::vector<int64_t> move_hits;  // [NUM_MOVES] times each move hit
    std::vector<double> stats;       // [NUM_STATS] see Stat; also only grows until zeroed

    VecEnv(int num_envs, const GameConfig& cfg, const EnvParams& params);

    void reset_all();
    // actions: [num_envs][2][3] = (stick_x, stick_y, button), egocentric
    // (player 1's stick_x is mirrored). Games that end
    // are reset right away, and obs then holds the new game's first observation.
    void step(const int8_t* actions);

private:
    void reset_game(int i);
    void record_stats(const GameState& g, const StepEvents& ev);
    void write_game_obs(int i, float* dst);
};
