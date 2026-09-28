#include "sim.h"

#include <algorithm>  // std::min, std::max, std::clamp
#include <cmath>      // std::sqrt, std::cos, std::sin, std::lround

static const float DEG_TO_RAD = 3.14159265f / 180.0f;

void reset(GameState& s, const GameConfig& cfg) {
    s = GameState{};  // put every field back to the default written in sim.h
    for (int p = 0; p < 2; ++p) {
        s.f[p].x = (p == 0) ? -cfg.spawn_x : cfg.spawn_x;
        s.f[p].facing = (p == 0) ? 1 : -1;
        s.f[p].stocks = cfg.stocks;
    }
}

float knockback(const HitboxDef& hb, float victim_percent) {
    return hb.base_kb + hb.kb_growth * victim_percent;
}

int hitstun_frames(float kb, const GameConfig& cfg) {
    return static_cast<int>(std::lround(kb * cfg.hitstun_per_kb));
}

// ---------------------------------------------------------------------------
// Small helpers. `static` on a function here means "only visible inside this
// .cpp file".
// ---------------------------------------------------------------------------

static void set_state(Fighter& f, FighterState st) {
    if (f.state != st) {
        f.state = st;
        f.state_frame = 0;
    }
}

// Every caller (Python, Godot) goes through here, so bad input values are
// fixed in one place: a stick of 5 must not make anyone walk 5x faster.
static Input sanitize(Input in) {
    in.stick_x = std::clamp<int8_t>(in.stick_x, -1, 1);
    in.stick_y = std::clamp<int8_t>(in.stick_y, -1, 1);
    if (in.button > BTN_DODGE) in.button = BTN_NONE;
    return in;
}

// Moves v toward target by at most `amount`, without overshooting.
static float approach(float v, float target, float amount) {
    if (v < target) return std::min(v + amount, target);
    return std::max(v - amount, target);
}

// States where the fighter is free to act on input.
static bool actionable(const Fighter& f) {
    return f.state == ST_IDLE || f.state == ST_RUN || f.state == ST_AIRBORNE;
}

static void start_attack(Fighter& f, MoveId m) {
    f.move = m;
    f.move_hit = false;
    set_state(f, ST_ATTACK);
}

// Touching the floor after being in the air.
static void land(Fighter& f, const GameConfig& cfg) {
    f.y = 0;
    f.vy = 0;
    f.launch_vy = 0;
    f.grounded = true;
    f.fastfall = false;
    f.jumps_left = 2;
    if (f.state == ST_ATTACK) {  // landed during an aerial: that move's landing lag
        f.landing_lag = static_cast<int16_t>(cfg.moves[f.move].landing_lag);
        f.move = MOVE_NONE;
        set_state(f, ST_LANDING);
    } else if (f.state == ST_AIRBORNE) {
        f.landing_lag = static_cast<int16_t>(cfg.landing_lag);
        set_state(f, ST_LANDING);
    } else if (f.state == ST_AIRDODGE || f.state == ST_HELPLESS) {
        f.landing_lag = static_cast<int16_t>(cfg.dodge_landing_lag);
        set_state(f, ST_LANDING);  // touching the ground is what ends helpless
    }
    // In hitstun you keep sliding along the floor until the hitstun ends.
}

// ---------------------------------------------------------------------------
// One fighter, one frame: the state machine, then physics.
// ---------------------------------------------------------------------------

static void step_fighter(Fighter& f, const Input& raw_in, const GameConfig& cfg) {
    const Input in = sanitize(raw_in);
    const bool jump_pressed = (in.button == BTN_JUMP && f.prev_button != BTN_JUMP);

    // 1. Leave timed states whose time is up. The fighter can act again on
    //    this same frame (that's what "actionable on frame N" means).
    switch (f.state) {
    case ST_HITSTUN:
        f.hitstun -= 1;
        if (f.hitstun <= 0) set_state(f, f.grounded ? ST_IDLE : ST_AIRBORNE);
        break;
    case ST_ATTACK:
        if (f.state_frame >= cfg.moves[f.move].total_frames) {
            f.move = MOVE_NONE;
            set_state(f, f.grounded ? ST_IDLE : ST_AIRBORNE);
        }
        break;
    case ST_LANDING:
        if (f.state_frame >= f.landing_lag) set_state(f, ST_IDLE);
        break;
    case ST_SPOTDODGE:
        if (f.state_frame >= cfg.spotdodge_frames) set_state(f, f.grounded ? ST_IDLE : ST_AIRBORNE);
        break;
    case ST_AIRDODGE:
        // An air dodge doesn't end in freedom: you stay helpless until you land.
        if (f.state_frame >= cfg.airdodge_frames) set_state(f, ST_HELPLESS);
        break;
    case ST_JUMPSQUAT:
        if (f.state_frame >= cfg.jumpsquat_frames) {
            f.vy = cfg.jump_velocity;
            f.grounded = false;
            set_state(f, ST_AIRBORNE);
        }
        break;
    default:
        break;
    }

    // 2. Read the controller, if the fighter is free to act. Attacks come out
    //    whenever the button is held (holding light = repeated jabs); jumps
    //    only on a fresh press.
    if (actionable(f)) {
        if (f.grounded) {
            if (jump_pressed) {
                f.jumps_left -= 1;
                set_state(f, ST_JUMPSQUAT);
            } else if (in.button == BTN_LIGHT || in.button == BTN_HEAVY) {
                if (in.stick_x != 0) f.facing = in.stick_x;  // attack the way the stick points
                if (in.button == BTN_HEAVY) start_attack(f, MOVE_HEAVY);
                else start_attack(f, in.stick_y > 0 ? MOVE_UTILT : MOVE_JAB);
            } else if (in.button == BTN_DODGE) {
                set_state(f, ST_SPOTDODGE);
            } else if (in.stick_x != 0) {
                // Start moving, or turn around: either way a fresh dash begins.
                bool fresh = (f.state != ST_RUN) || (in.stick_x != f.facing);
                f.facing = in.stick_x;
                if (fresh) {
                    f.state = ST_RUN;
                    f.state_frame = 0;
                }
            } else {
                set_state(f, ST_IDLE);
            }
        } else {
            // You can turn freely in the air, so an aerial can hit either side.
            // Facing only locks once the aerial starts (actionable() is false
            // during ATTACK), so the hitbox never flips mid-move.
            if (in.stick_x != 0) f.facing = in.stick_x;
            if (jump_pressed && f.jumps_left > 0) {
                f.jumps_left -= 1;
                f.vy = cfg.double_jump_velocity;
                f.fastfall = false;
            } else if (in.button == BTN_LIGHT || in.button == BTN_HEAVY) {
                start_attack(f, in.stick_y > 0 ? MOVE_UAIR : in.stick_y < 0 ? MOVE_DAIR : MOVE_AERIAL);
            } else if (in.button == BTN_DODGE) {
                f.fastfall = false;
                set_state(f, ST_AIRDODGE);
            }
        }
    }

    // 3. Horizontal velocity: walking and friction on the ground, drift in the air.
    if (f.grounded) {
        if (f.state == ST_RUN) f.vx = f.facing * (f.state_frame < cfg.dash_frames ? cfg.dash_speed : cfg.run_speed);
        else f.vx = approach(f.vx, 0.0f, cfg.ground_friction);
    } else if (f.state != ST_HITSTUN && f.state != ST_AIRDODGE) {  // no drift in hitstun or mid-dodge
        if (in.stick_x != 0) f.vx = approach(f.vx, cfg.air_speed * in.stick_x, cfg.air_accel);
        else f.vx = approach(f.vx, 0.0f, cfg.air_friction);
    }

    // 4. Vertical velocity: gravity, capped fall speed, fast-fall. Fast-fall
    //    = holding down once you're falling: you drop at a higher fixed speed.
    if (!f.grounded) {
        if (f.state != ST_HITSTUN && f.state != ST_AIRDODGE && in.stick_y < 0 && f.vy <= 0) f.fastfall = true;
        f.vy = f.fastfall ? -cfg.fastfall_speed : std::max(f.vy - cfg.gravity, -cfg.max_fall_speed);
    }

    // 5. Knockback shrinks by a fixed amount each frame, keeping its direction.
    float speed = std::sqrt(f.launch_vx * f.launch_vx + f.launch_vy * f.launch_vy);
    if (speed > 0) {
        float scale = std::max(speed - cfg.kb_decay, 0.0f) / speed;
        f.launch_vx *= scale;
        f.launch_vy *= scale;
    }

    // 6. Move, then collide with the floor.
    float prev_y = f.y;
    f.x += f.vx + f.launch_vx;
    f.y += f.vy + f.launch_vy;
    bool over_floor = (f.x >= -cfg.stage_half_width && f.x <= cfg.stage_half_width);

    if (f.grounded) {
        if (!over_floor || f.y > 0) {
            // Walked off the edge or got launched upward.
            f.grounded = false;
            if (!over_floor) f.jumps_left = std::min<uint8_t>(f.jumps_left, 1);
            if (f.state == ST_IDLE || f.state == ST_RUN || f.state == ST_LANDING) set_state(f, ST_AIRBORNE);
        } else {
            f.y = 0;  // a downward launch on the ground just stops at the floor
            f.launch_vy = std::max(f.launch_vy, 0.0f);
        }
    } else if (over_floor && prev_y >= 0 && f.y <= 0) {
        // Landing needs prev_y >= 0 (above the floor last frame), so someone
        // below the stage can't snap up onto it.
        land(f, cfg);
    }

    if (f.invuln > 0) f.invuln -= 1;
    f.prev_button = in.button;
    f.state_frame += 1;
}

// ---------------------------------------------------------------------------
// Combat
// ---------------------------------------------------------------------------

// Does a circle overlap an axis-aligned box? Find the point of the box closest
// to the circle's centre, then check whether it's within the radius.
static bool circle_hits_box(float cx, float cy, float r, float left, float bottom, float right, float top) {
    float nx = std::clamp(cx, left, right);
    float ny = std::clamp(cy, bottom, top);
    float dx = cx - nx, dy = cy - ny;
    return dx * dx + dy * dy <= r * r;
}

// Returns the hitbox of attacker `a` that hits victim `v` this frame, or
// nullptr. If several overlap, the first one listed in the JSON wins.
static const HitboxDef* find_hit(const Fighter& a, const Fighter& v, const GameConfig& cfg) {
    if (a.state != ST_ATTACK || a.move_hit || intangible(v, cfg)) return nullptr;
    for (const HitboxDef& hb : cfg.moves[a.move].hitboxes) {
        if (a.state_frame < hb.start || a.state_frame > hb.end) continue;
        float cx = a.x + a.facing * hb.x;
        float cy = a.y + hb.y;
        float half_w = cfg.hurtbox_w / 2;
        if (circle_hits_box(cx, cy, hb.radius, v.x - half_w, v.y, v.x + half_w, v.y + cfg.hurtbox_h)) return &hb;
    }
    return nullptr;
}

static void apply_hit(Fighter& a, Fighter& v, const HitboxDef& hb, const GameConfig& cfg) {
    v.percent += hb.damage;
    // Knockback uses the damage *after* this hit, so a strong hit at 0% still sends you a bit.
    float kb = knockback(hb, v.percent);
    float angle = hb.angle * DEG_TO_RAD;
    v.launch_vx = a.facing * kb * std::cos(angle);
    v.launch_vy = kb * std::sin(angle);
    v.vx = 0;
    v.vy = 0;
    v.fastfall = false;
    v.move = MOVE_NONE;  // getting hit cancels whatever you were doing
    v.hitstun = static_cast<int16_t>(hitstun_frames(kb, cfg));
    v.state = ST_HITSTUN;
    v.state_frame = 0;
    a.move_hit = true;
}

// Grounded fighters whose bodies overlap are pushed apart, half each, so
// nobody can stand inside the opponent (where every attack trades). Running
// into a standing opponent therefore shoves both along at half speed.
static void push_apart(Fighter& a, Fighter& b, const GameConfig& cfg) {
    if (!a.grounded || !b.grounded) return;  // you can still jump over someone
    float dx = b.x - a.x;
    float overlap = cfg.hurtbox_w - std::abs(dx);
    if (overlap <= 0) return;
    float dir = (dx > 0) ? 1.0f : (dx < 0) ? -1.0f : -1.0f * a.facing;  // exactly stacked: a steps back
    a.x -= dir * overlap / 2;
    b.x += dir * overlap / 2;
    // Nobody gets shoved off the stage (that would be a free KO for walking
    // into someone): a fighter at the edge stays there and acts as a wall.
    float lim = cfg.stage_half_width;
    if (std::abs(a.x) > lim) {
        a.x = std::clamp(a.x, -lim, lim);
        b.x = a.x + dir * cfg.hurtbox_w;
    } else if (std::abs(b.x) > lim) {
        b.x = std::clamp(b.x, -lim, lim);
        a.x = b.x - dir * cfg.hurtbox_w;
    }
}

static bool outside_blast_zone(const Fighter& f, const GameConfig& cfg) {
    return f.x < -cfg.blast_x || f.x > cfg.blast_x || f.y > cfg.blast_top || f.y < cfg.blast_bottom;
}

static void respawn(Fighter& f, int player, const GameConfig& cfg) {
    Fighter fresh;  // all defaults from sim.h
    fresh.stocks = f.stocks;
    fresh.x = (player == 0) ? -cfg.spawn_x : cfg.spawn_x;
    fresh.y = cfg.respawn_y;
    fresh.facing = (player == 0) ? 1 : -1;
    fresh.grounded = false;
    fresh.state = ST_AIRBORNE;
    fresh.jumps_left = 1;
    fresh.invuln = static_cast<int16_t>(cfg.respawn_invuln);
    fresh.prev_button = f.prev_button;  // so a held jump doesn't fire on respawn
    f = fresh;
}

// ---------------------------------------------------------------------------
// The whole game, one frame.
// ---------------------------------------------------------------------------

void step(GameState& s, const Input in[2], const GameConfig& cfg, StepEvents& ev) {
    ev = StepEvents{};
    if (s.over) return;

    step_fighter(s.f[0], in[0], cfg);
    step_fighter(s.f[1], in[1], cfg);
    push_apart(s.f[0], s.f[1], cfg);

    // Find both players' hits first, then apply them. If both hit each other
    // on the same frame (a "trade"), both hits land, whoever is player 0.
    const HitboxDef* hit[2];
    for (int p = 0; p < 2; ++p) hit[p] = find_hit(s.f[p], s.f[1 - p], cfg);
    for (int p = 0; p < 2; ++p) {
        if (!hit[p]) continue;
        apply_hit(s.f[p], s.f[1 - p], *hit[p], cfg);
        ev.hit[p] = true;
        ev.damage[p] = hit[p]->damage;
    }

    for (int p = 0; p < 2; ++p) {
        Fighter& f = s.f[p];
        if (!outside_blast_zone(f, cfg)) continue;
        ev.ko[p] = true;
        f.stocks -= 1;
        if (f.stocks > 0) respawn(f, p, cfg);
        else s.over = true;
    }

    s.frame += 1;
    if (s.frame >= cfg.time_limit_frames) s.over = true;
}

bool intangible(const Fighter& f, const GameConfig& cfg) {
    if (f.invuln > 0) return true;
    if (f.state == ST_SPOTDODGE)
        return f.state_frame >= cfg.spotdodge_iframe_start && f.state_frame <= cfg.spotdodge_iframe_end;
    if (f.state == ST_AIRDODGE)
        return f.state_frame >= cfg.airdodge_iframe_start && f.state_frame <= cfg.airdodge_iframe_end;
    return false;
}

int winner(const GameState& s) {
    const Fighter &a = s.f[0], &b = s.f[1];
    if (a.stocks != b.stocks) return a.stocks > b.stocks ? 0 : 1;
    if (a.percent != b.percent) return a.percent < b.percent ? 0 : 1;
    return -1;
}

int active_hitboxes(const GameState& s, const GameConfig& cfg, ActiveHitbox out[], int max) {
    int n = 0;
    for (int p = 0; p < 2; ++p) {
        const Fighter& f = s.f[p];
        if (f.state != ST_ATTACK) continue;
        for (const HitboxDef& hb : cfg.moves[f.move].hitboxes) {
            if (f.state_frame < hb.start || f.state_frame > hb.end || n >= max) continue;
            out[n++] = ActiveHitbox{p, f.x + f.facing * hb.x, f.y + hb.y, hb.end - f.state_frame, f.move_hit, &hb};
        }
    }
    return n;
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
    // two otherwise-identical states. When you add a field to Fighter or
    // GameState, add it here too.
    uint64_t h = 14695981039346656037ull;
    for (const Fighter& f : s.f) {
        mix(h, &f.x, sizeof f.x);
        mix(h, &f.y, sizeof f.y);
        mix(h, &f.vx, sizeof f.vx);
        mix(h, &f.vy, sizeof f.vy);
        mix(h, &f.launch_vx, sizeof f.launch_vx);
        mix(h, &f.launch_vy, sizeof f.launch_vy);
        mix(h, &f.percent, sizeof f.percent);
        mix(h, &f.facing, sizeof f.facing);
        mix(h, &f.state, sizeof f.state);
        mix(h, &f.state_frame, sizeof f.state_frame);
        mix(h, &f.grounded, sizeof f.grounded);
        mix(h, &f.fastfall, sizeof f.fastfall);
        mix(h, &f.jumps_left, sizeof f.jumps_left);
        mix(h, &f.stocks, sizeof f.stocks);
        mix(h, &f.move, sizeof f.move);
        mix(h, &f.move_hit, sizeof f.move_hit);
        mix(h, &f.hitstun, sizeof f.hitstun);
        mix(h, &f.landing_lag, sizeof f.landing_lag);
        mix(h, &f.invuln, sizeof f.invuln);
        mix(h, &f.prev_button, sizeof f.prev_button);
    }
    mix(h, &s.frame, sizeof s.frame);
    mix(h, &s.over, sizeof s.over);
    return h;
}
