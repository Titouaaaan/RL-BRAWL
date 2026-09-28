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
    "frames_airborne", "frames_on_platform", "frames_close",
};

// ---------------------------------------------------------------------------
// Observations
// ---------------------------------------------------------------------------

// `m` is the mirror: +1 for player 0, -1 for player 1. Multiplying every
// x-ish quantity by m flips player 1's world left-to-right.
static void write_fighter(const Fighter& f, const GameConfig& cfg, float m, float*& o) {
    // `float*& o` = a reference to the pointer, so advancing it here also
    // advances it for the caller. Each *o++ = v writes v and moves on by one.
    float x = m * f.x;
    *o++ = x / cfg.blast_x;
    *o++ = f.y / cfg.blast_top;
    *o++ = m * f.vx;
    *o++ = f.vy;
    *o++ = m * f.launch_vx;
    *o++ = f.launch_vy;
    *o++ = f.percent / 100.0f;
    *o++ = f.stocks / float(cfg.stocks);
    *o++ = f.grounded;
    *o++ = f.fastfall;
    // How far along the hold-down-to-fast-fall timer is (1 = fast-fall ready).
    *o++ = std::min<float>(f.down_frames, float(cfg.fastfall_hold)) / std::max(cfg.fastfall_hold, 1);
    *o++ = f.jumps_left / 2.0f;
    *o++ = m * f.facing;
    for (int st = 0; st < NUM_STATES; ++st) *o++ = (f.state == st);  // one-hot, NUM_STATES values
    *o++ = std::min(f.state_frame, 120) / 60.0f;
    *o++ = f.hitstun / 60.0f;
    // Frames until the current move / landing lag ends. The move itself is
    // deliberately NOT given as an ID: the agent learns about moves from
    // timing and hitbox properties, so new moves never change the obs layout.
    int move_left = (f.state == ST_ATTACK) ? cfg.moves[f.move].total_frames - f.state_frame : 0;
    int lag_left = (f.state == ST_LANDING) ? f.landing_lag - f.state_frame : 0;
    *o++ = move_left / 60.0f;
    *o++ = lag_left / 60.0f;
    *o++ = f.invuln / float(std::max(cfg.respawn_invuln, 1));
    *o++ = intangible(f, cfg);  // hits would pass through right now
    *o++ = f.fastfall_blocked;
    *o++ = float(f.air_whiffs) / std::max(cfg.air_whiff_limit, 1);  // 1 = no more air attacks until landing
    *o++ = f.shield_hp / cfg.shield_max;
    *o++ = float(f.hitlag) / std::max(cfg.hitlag_max, 1);
    *o++ = std::min(f.combo, uint8_t(5)) / 5.0f;  // my current combo length (capped at 5)
    *o++ = (f.combo > 0) ? std::min<float>(f.combo_timer, float(cfg.combo_window)) / cfg.combo_window : 1.0f;
    // Distances to the blast zones and stage edges. They're linear in x/y,
    // but handing them over directly makes "near the edge" easy to learn.
    *o++ = (cfg.blast_x - x) / cfg.blast_x;
    *o++ = (x + cfg.blast_x) / cfg.blast_x;
    *o++ = (cfg.blast_top - f.y) / cfg.blast_top;
    *o++ = (f.y - cfg.blast_bottom) / -cfg.blast_bottom;
    *o++ = (cfg.stage_half_width - x) / cfg.stage_half_width;
    *o++ = (x + cfg.stage_half_width) / cfg.stage_half_width;
}

void write_obs(const GameState& s, const GameConfig& cfg, int player, float* out) {
    float* o = out;
    const float m = (player == 0) ? 1.0f : -1.0f;
    const Fighter& me = s.f[player];
    const Fighter& opp = s.f[1 - player];

    write_fighter(me, cfg, m, o);
    write_fighter(opp, cfg, m, o);
    *o++ = m * (opp.x - me.x) / cfg.blast_x;
    *o++ = (opp.y - me.y) / cfg.blast_top;
    *o++ = (cfg.time_limit_frames - s.frame) / float(cfg.time_limit_frames);

    // Hitboxes, described by what they do, never by which move made them.
    ActiveHitbox all[2 * MAX_HITBOXES_PER_MOVE];
    int n = active_hitboxes(s, cfg, all, 2 * MAX_HITBOXES_PER_MOVE);
    int written = 0;
    // Two passes: the opponent's hitboxes (threats) first, then mine.
    // ponytail: extra hitboxes past 4 are dropped; fine while moves have at most 2.
    for (int pass = 0; pass < 2; ++pass) {
        int owner = (pass == 0) ? 1 - player : player;
        for (int k = 0; k < n && written < OBS_HITBOXES; ++k) {
            const ActiveHitbox& hb = all[k];
            if (hb.owner != owner) continue;
            const Fighter& victim = s.f[1 - owner];
            // What it would do if it hit now, including stale-move weakening.
            float kb = knockback(*hb.def, victim.percent + hb.def->damage * hb.power) * hb.power;
            float angle = hb.def->angle * 3.14159265f / 180.0f;
            *o++ = m * (hb.x - me.x) / 10.0f;
            *o++ = (hb.y - me.y) / 10.0f;
            *o++ = hb.def->radius / 2.0f;
            *o++ = hb.def->damage * hb.power / 20.0f;
            *o++ = kb / 3.0f;
            *o++ = m * s.f[owner].facing * std::cos(angle);  // launch direction
            *o++ = std::sin(angle);
            *o++ = hb.frames_left / 10.0f;
            *o++ = (owner == player);
            *o++ = hb.spent;
            *o++ = hb.def->grab;  // a grab: ignores shields
            *o++ = 1.0f;  // mask: this slot holds a real hitbox
            ++written;
        }
    }
    for (; written < OBS_HITBOXES; ++written)
        for (int j = 0; j < OBS_HITBOX; ++j) *o++ = 0.0f;  // empty slot, mask = 0

    assert(o - out == OBS_DIM);  // catches a layout mistake in the Debug build
}

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
                    float bonus = params.damage_reward * ev.damage[p] * std::max(mult, 1.0f);
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
