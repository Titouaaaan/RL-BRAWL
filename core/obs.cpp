#include "obs.h"

#include <algorithm>
#include <cassert>  // assert(): checked in Debug builds (the ASan smoke test), removed in Release
#include <cmath>

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
    // Protected time left: > 1 while still waiting off the field after a KO (keeps OBS_DIM, so old checkpoints load).
    *o++ = (f.invuln + f.respawn_wait) / float(std::max(cfg.respawn_invuln, 1));
    *o++ = intangible(f, cfg);  // hits would pass through right now
    *o++ = f.fastfall_blocked;
    *o++ = float(f.air_whiffs) / std::max(cfg.air_whiff_limit, 1);  // 1 = no more air attacks until landing
    *o++ = f.shield_hp / cfg.shield_max;
    *o++ = float(f.hitlag) / std::max(cfg.hitlag_max, 1);
    *o++ = std::min(f.combo, uint8_t(5)) / 5.0f;  // my current combo length (capped at 5)
    *o++ = (f.combo > 0) ? std::min<float>(f.combo_timer, float(cfg.combo_window)) / cfg.combo_window : 1.0f;
    *o++ = f.tumble;  // tumbling: techable, knockdown if the landing isn't teched
    *o++ = float(f.ledge_grabs) / std::max(cfg.ledge_max_grabs, 1);  // ledge regrabs used (less intangibility)
    *o++ = f.dodge_stale / 5.0f;  // dodge staleness: 1 = fully stale (longer, later i-frames)
    // How much of the counter window is left (1 = just defended, 0 = none):
    // the counter bonus depends on it, so the agent must be able to see it.
    *o++ = (f.since_defense <= cfg.counter_window) ? 1.0f - float(f.since_defense) / cfg.counter_window : 0.0f;
    *o++ = float(f.charge) / std::max(cfg.charge_max, 1);  // smash charge (1 = full: 40% stronger)
    *o++ = std::min(f.streak, uint8_t(10)) / 10.0f;  // hit streak (the streak bonus grows with it)
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
            // What it would do if it hit now, including stale-move weakening
            // (knockback in Smash units, ~0-250; the Sakurai angle 361 shown as 38).
            float dmg = hb.def->damage * hb.power;
            float kb = knockback(*hb.def, victim.percent + dmg, dmg, cfg.weight);
            float deg = (hb.def->angle == 361.0f) ? 38.0f : hb.def->angle;
            float angle = deg * 3.14159265f / 180.0f;
            *o++ = m * (hb.x - me.x) / 10.0f;
            *o++ = (hb.y - me.y) / 10.0f;
            *o++ = hb.def->radius / 2.0f;
            *o++ = hb.def->damage * hb.power / 20.0f;
            *o++ = kb / 200.0f;
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
