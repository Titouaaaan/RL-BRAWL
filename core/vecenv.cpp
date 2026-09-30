#include "vecenv.h"

#include <algorithm>
#include <cassert>  // assert(): checked in Debug builds (the ASan smoke test), removed in Release
#include <cmath>
#include <stdexcept>

const char* const STAT_NAMES[NUM_STATS] = {
    "frames", "hits", "hits_taken", "damage_dealt", "damage_taken", "kos", "deaths",
    "combo_hits", "combo_hits_taken", "max_combo",
    "blocks", "got_blocked", "shield_breaks", "shield_breaks_caused",
    "grabs", "got_grabbed", "techs", "knockdowns",
    "full_jumps", "short_hops", "di_used",
    "parries", "got_parried", "clanks", "ledge_grabs", "escapes",
    "streak_hits", "max_streak", "evades", "counters", "whiff_punishes", "counters_taken",
    "frames_airborne", "frames_on_platform", "frames_close",
};

// ---------------------------------------------------------------------------
// VecEnv
// ---------------------------------------------------------------------------

VecEnv::VecEnv(int n, const GameConfig& c, const EnvParams& p)
    : cfg(c), params(p), num_envs(n),
      games(n), history(size_t(n) * (p.reaction_delay + 1)),
      obs(size_t(n) * 2 * OBS_DIM), final_obs(size_t(n) * 2 * OBS_DIM),
      reward(size_t(n) * 2), terminated(n), truncated(n), winner(n), final_hash(n),
      move_uses(NUM_MOVES), move_hits(NUM_MOVES), stats(NUM_STATS) {
    if (n < 1 || p.frame_skip < 1 || p.reaction_delay < 0)
        throw std::invalid_argument("need num_envs >= 1, frame_skip >= 1, reaction_delay >= 0");
    reset_all();
}

void VecEnv::reset_game(int i) {
    reset(games[i], cfg);
    // Fill the whole ring buffer with the start state, so a delayed
    // observation early in the game just shows the start.
    const int hist = params.reaction_delay + 1;
    for (int k = 0; k < hist; ++k) history[size_t(i) * hist + k] = games[i];
}

void VecEnv::reset_all() {
    for (int i = 0; i < num_envs; ++i) {
        reset_game(i);
        write_game_obs(i, &obs[size_t(i) * 2 * OBS_DIM]);
    }
}

// Writes both players' observations of game i (as of reaction_delay frames ago).
void VecEnv::write_game_obs(int i, float* dst) {
    const int hist = params.reaction_delay + 1;
    int frame = games[i].frame - params.reaction_delay;
    int slot = ((frame % hist) + hist) % hist;  // modulo that stays positive for negative frames
    const GameState& seen = history[size_t(i) * hist + slot];
    write_obs(seen, cfg, 0, dst);
    write_obs(seen, cfg, 1, dst + OBS_DIM);
}

void VecEnv::record_stats(const GameState& g, const StepEvents& ev) {
    const Fighter& me = g.f[0];
    double* st = stats.data();
    st[STAT_FRAMES] += 1;
    st[STAT_HITS] += ev.hit[0];
    st[STAT_HITS_TAKEN] += ev.hit[1];
    st[STAT_DAMAGE_DEALT] += ev.damage[0];
    st[STAT_DAMAGE_TAKEN] += ev.damage[1];
    st[STAT_KOS] += ev.ko[1];
    st[STAT_DEATHS] += ev.ko[0];
    st[STAT_COMBO_HITS] += (ev.combo[0] >= 2);
    st[STAT_COMBOS_TAKEN] += (ev.combo[1] >= 2);
    st[STAT_MAX_COMBO] = std::max(st[STAT_MAX_COMBO], double(ev.combo[0]));
    st[STAT_BLOCKS] += ev.shield_block[0];
    st[STAT_GOT_BLOCKED] += ev.shield_block[1];
    st[STAT_SHIELD_BREAKS] += ev.shield_break[0];
    st[STAT_SHIELD_BREAKS_CAUSED] += ev.shield_break[1];
    st[STAT_GRABS] += ev.grab[0];
    st[STAT_GOT_GRABBED] += ev.grab[1];
    st[STAT_TECHS] += ev.tech[0];
    st[STAT_KNOCKDOWNS] += ev.knockdown[0];
    st[STAT_FULL_JUMPS] += (ev.jump[0] == 1);
    st[STAT_SHORT_HOPS] += (ev.jump[0] == 2);
    st[STAT_DI_USED] += ev.di[0];
    st[STAT_FRAMES_AIRBORNE] += !me.grounded;
    st[STAT_FRAMES_ON_PLATFORM] += (me.grounded && me.surface != 0);
    st[STAT_PARRIES] += ev.parry[0];
    st[STAT_GOT_PARRIED] += ev.parry[1];
    st[STAT_CLANKS] += ev.clank[0];
    st[STAT_LEDGE_GRABS] += ev.ledge_grab[0];
    st[STAT_ESCAPES] += ev.escaped[0];
    st[STAT_STREAK_HITS] += (ev.streak[0] >= 2);
    st[STAT_MAX_STREAK] = std::max(st[STAT_MAX_STREAK], double(ev.streak[0]));
    st[STAT_EVADES] += ev.evade[0];
    st[STAT_COUNTERS] += ev.counter[0];
    st[STAT_WHIFF_PUNISHES] += ev.whiff_punish[0];
    st[STAT_COUNTERS_TAKEN] += (ev.counter[1] || ev.whiff_punish[1]);
    st[STAT_FRAMES_CLOSE] += (std::abs(g.f[1].x - me.x) < 4.0f && std::abs(g.f[1].y - me.y) < 4.0f);
}

void VecEnv::step(const int8_t* actions) {
    const int hist = params.reaction_delay + 1;
    for (int i = 0; i < num_envs; ++i) {
        GameState& g = games[i];
        Input in[2];
        for (int p = 0; p < 2; ++p) {
            const int8_t* a = actions + (size_t(i) * 2 + p) * 3;
            // Actions are egocentric like observations: player 1 lives in a
            // mirrored world, so its "right" is the real game's left. Clamp
            // first so negating can't overflow (-(-128) doesn't fit in int8).
            int8_t sx = std::clamp<int8_t>(a[0], -1, 1);
            if (p == 1) sx = -sx;
            in[p] = Input{sx, a[1], static_cast<uint8_t>(a[2])};
        }

        float r[2] = {0, 0};
        for (int k = 0; k < params.frame_skip && !g.over; ++k) {
            StepEvents ev;
            ::step(g, in, cfg, ev);  // `::step` = the sim's step from sim.h, not this method
            for (int p = 0; p < 2; ++p) {
                r[p] += float(ev.ko[1 - p]) - float(ev.ko[p]);
                if (ev.hit[p]) {
                    // Damage reward, grown for hits deep in a combo; zero-sum.
                    float mult = std::min(std::pow(params.combo_growth, float(ev.combo[p]) - 1.0f), params.combo_cap);
                    float streak = std::min(std::pow(params.streak_growth, float(ev.streak[p]) - 1.0f), params.streak_cap);
                    float bonus = params.damage_reward * ev.damage[p] * std::max(mult, 1.0f) * std::max(streak, 1.0f);
                    // Counterattack / whiff-punish bonus, zero-sum.
                    if (ev.counter[p] || ev.whiff_punish[p]) bonus += params.counter_reward;
                    r[p] += bonus;
                    r[1 - p] -= bonus;
                }
            }
            record_stats(g, ev);
            history[size_t(i) * hist + (g.frame % hist)] = g;
            if (ev.move_started[0] >= 0) move_uses[ev.move_started[0]] += 1;
            if (ev.hit_move[0] >= 0) move_hits[ev.hit_move[0]] += 1;
        }
        float* dst = &obs[size_t(i) * 2 * OBS_DIM];
        if (g.over) {
            // terminated = someone ran out of stocks, truncated = time ran out.
            // Both are real endings of the game (the time left is in the
            // observation), the flags just say which kind.
            terminated[i] = (g.f[0].stocks == 0 || g.f[1].stocks == 0);
            truncated[i] = !terminated[i];
            winner[i] = static_cast<int8_t>(::winner(g));
            // Tie-breaker: at the time limit, whoever is ahead (see winner():
            // more stocks, then less damage) gets a small bonus, so
            // stalling until time runs out is worse than dealing damage.
            if (truncated[i] && winner[i] >= 0) {
                r[winner[i]] += params.timeout_reward;
                r[1 - winner[i]] -= params.timeout_reward;
            }
            final_hash[i] = hash_state(g);
            write_game_obs(i, &final_obs[size_t(i) * 2 * OBS_DIM]);
            reset_game(i);
        } else {
            terminated[i] = 0;
            truncated[i] = 0;
        }
        reward[i * 2] = r[0];
        reward[i * 2 + 1] = r[1];
        write_game_obs(i, dst);
    }
}
