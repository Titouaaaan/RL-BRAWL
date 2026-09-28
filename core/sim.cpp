#include "sim.h"

#include <algorithm>  // std::min, std::max, std::clamp
#include <cmath>      // std::sqrt, std::cos, std::sin, std::atan2, std::lround

static const float DEG_TO_RAD = 3.14159265f / 180.0f;

const char* const STATE_NAMES[NUM_STATES] = {
    "IDLE",   "RUN",        "JUMPSQUAT", "AIRBORNE", "LANDING",     "ATTACK",  "HITSTUN", "SPOTDODGE", "AIRDODGE",
    "HELPLESS", "SHIELD", "SHIELDSTUN", "ROLL",    "SHIELDBREAK", "HOLDING", "GRABBED", "KNOCKDOWN", "TECH",
};

void reset(GameState& s, const GameConfig& cfg) {
    s = GameState{};  // put every field back to the default written in sim.h
    for (int p = 0; p < 2; ++p) {
        s.f[p].x = (p == 0) ? -cfg.spawn_x : cfg.spawn_x;
        s.f[p].facing = (p == 0) ? 1 : -1;
        s.f[p].stocks = cfg.stocks;
        s.f[p].shield_hp = cfg.shield_max;
    }
}

float knockback(const HitboxDef& hb, float victim_percent) {
    return hb.base_kb + hb.kb_growth * victim_percent;
}

int hitstun_frames(float kb, const GameConfig& cfg) {
    return static_cast<int>(std::lround(kb * cfg.hitstun_per_kb));
}

int hitlag_frames(float damage, const GameConfig& cfg) {
    int frames = static_cast<int>(std::lround(cfg.hitlag_base + damage * cfg.hitlag_per_damage));
    return std::min(frames, cfg.hitlag_max);
}

float stale_power(const Fighter& f, int m, const GameConfig& cfg) {
    float loss = 0;
    bool found = false;
    for (int i = 0; i < 9; ++i) {
        if (f.stale[i] == m) {
            loss += cfg.stale_steps[i];
            found = true;
        }
    }
    return found ? 1.0f - loss : cfg.fresh_bonus;
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
// fixed in one place: a stick of 5 must not make anyone run 5x faster.
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

static bool shielding(const Fighter& f) {
    return f.state == ST_SHIELD || f.state == ST_SHIELDSTUN;
}

// The move table from sim.h: [air?][heavy?][stick down / neutral / up].
static MoveId pick_move(bool grounded, uint8_t button, int8_t stick_y) {
    static const MoveId table[2][2][3] = {
        {{MOVE_DOWN_TILT, MOVE_JAB, MOVE_UP_TILT}, {MOVE_DOWN_HEAVY, MOVE_HEAVY, MOVE_UP_HEAVY}},
        {{MOVE_DOWN_AIR, MOVE_AIR, MOVE_UP_AIR}, {MOVE_HEAVY_DOWN_AIR, MOVE_HEAVY_AIR, MOVE_HEAVY_UP_AIR}},
    };
    return table[grounded ? 0 : 1][button == BTN_HEAVY ? 1 : 0][stick_y + 1];
}

static bool is_aerial(int m) {
    return m >= MOVE_DOWN_AIR && m <= MOVE_HEAVY_UP_AIR;
}

static void start_attack(Fighter& f, MoveId m) {
    f.move = m;
    f.move_hit = false;
    f.state = ST_ATTACK;
    f.state_frame = 0;
}

static void start_jumpsquat(Fighter& f, const Input& in) {
    f.jumps_left -= 1;
    f.short_hop = (in.stick_y < 0);  // down + jump is always a short hop
    set_state(f, ST_JUMPSQUAT);
}

static void start_roll(Fighter& f, int8_t dir) {
    f.roll_dir = (dir > 0) ? 1 : 2;
    f.state = ST_ROLL;
    f.state_frame = 0;
}

// Touching a floor (surface `surf`) after being in the air.
static void land(Fighter& f, int surf, const GameConfig& cfg) {
    f.y = cfg.surfaces[surf].y;
    f.surface = static_cast<int8_t>(surf);
    f.vy = 0;
    f.launch_vy = 0;
    f.grounded = true;
    f.fastfall = false;
    f.jumps_left = 2;
    f.air_whiffs = 0;
    switch (f.state) {
    case ST_ATTACK: {
        // Landing during an aerial costs its landing lag, unless it lands in
        // an auto-cancel window (early or late in the move).
        const MoveDef& m = cfg.moves[f.move];
        bool cancels = f.state_frame < m.autocancel_before || f.state_frame > m.autocancel_after;
        f.landing_lag = static_cast<int16_t>(cancels ? cfg.landing_lag : m.landing_lag);
        f.move = MOVE_NONE;
        set_state(f, ST_LANDING);
        break;
    }
    case ST_AIRBORNE:
        f.landing_lag = static_cast<int16_t>(cfg.landing_lag);
        set_state(f, ST_LANDING);
        break;
    case ST_AIRDODGE:
    case ST_HELPLESS:
        f.landing_lag = static_cast<int16_t>(cfg.dodge_landing_lag);
        set_state(f, ST_LANDING);  // touching the ground is what ends helpless
        break;
    case ST_HITSTUN:
        // Tech: pressed dodge just before impact. Otherwise a hard landing
        // (enough hitstun left) knocks you down; a light one you just slide.
        if (f.dodge_ago <= cfg.tech_window) {
            f.vx = f.launch_vx = 0;
            f.hitstun = 0;
            set_state(f, ST_TECH);
        } else if (f.hitstun >= cfg.knockdown_hitstun) {
            f.vx = f.launch_vx = 0;
            f.hitstun = 0;
            set_state(f, ST_KNOCKDOWN);
        }
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// One fighter, one frame: the state machine, then physics.
// ---------------------------------------------------------------------------

static void step_fighter(Fighter& f, const Input& raw_in, const GameConfig& cfg) {
    const Input in = sanitize(raw_in);
    const bool jump_pressed = (in.button == BTN_JUMP && f.prev_button != BTN_JUMP);
    const bool attacking = (in.button == BTN_LIGHT || in.button == BTN_HEAVY);
    if (in.button == BTN_DODGE && f.prev_button != BTN_DODGE) f.dodge_ago = 0;
    else if (f.dodge_ago < 255) f.dodge_ago += 1;

    // 1. Leave timed states whose time is up. The fighter can act again on
    //    this same frame (that's what "actionable on frame N" means).
    switch (f.state) {
    case ST_HITSTUN:
        f.hitstun -= 1;
        if (f.hitstun <= 0) set_state(f, f.grounded ? ST_IDLE : ST_AIRBORNE);
        break;
    case ST_ATTACK:
        if (f.state_frame >= cfg.moves[f.move].total_frames) {
            if (!f.grounded && !f.move_hit && is_aerial(f.move)) f.air_whiffs += 1;  // a missed air attack
            f.move = MOVE_NONE;
            set_state(f, f.grounded ? ST_IDLE : ST_AIRBORNE);
        }
        break;
    case ST_LANDING:
    case ST_KNOCKDOWN:
        if (f.state == ST_KNOCKDOWN ? f.state_frame >= cfg.knockdown_frames : f.state_frame >= f.landing_lag)
            set_state(f, ST_IDLE);
        break;
    case ST_TECH:
        if (f.state_frame >= cfg.tech_frames) set_state(f, ST_IDLE);
        break;
    case ST_SPOTDODGE:
        if (f.state_frame >= cfg.spotdodge_frames) set_state(f, f.grounded ? ST_IDLE : ST_AIRBORNE);
        break;
    case ST_ROLL:
        if (f.state_frame >= cfg.roll_frames) {
            f.facing = (f.roll_dir == 1) ? -1 : 1;  // end facing back where you came from, like Smash
            set_state(f, f.grounded ? ST_IDLE : ST_AIRBORNE);
        }
        break;
    case ST_AIRDODGE:
        // An air dodge doesn't end in freedom: you stay helpless until you land.
        if (f.state_frame >= cfg.airdodge_frames) set_state(f, ST_HELPLESS);
        break;
    case ST_SHIELDSTUN:
        if (f.state_frame >= f.landing_lag) set_state(f, ST_SHIELD);  // the shield input below decides what's next
        break;
    case ST_SHIELDBREAK:
        if (f.state_frame >= cfg.shieldbreak_frames) {
            f.shield_hp = cfg.shield_max * 0.375f;
            set_state(f, ST_IDLE);
        }
        break;
    case ST_JUMPSQUAT:
        if (in.button != BTN_JUMP) f.short_hop = true;  // let go of jump early: a short hop
        if (f.state_frame >= cfg.jumpsquat_frames) {
            f.vy = f.short_hop ? cfg.short_hop_velocity : cfg.jump_velocity;
            f.grounded = false;
            set_state(f, ST_AIRBORNE);
        }
        break;
    case ST_HOLDING:
        // Throw in the direction pressed (after a few frames), or forward when
        // the hold runs out.
        if ((f.state_frame >= 4 && (in.stick_x != 0 || in.stick_y != 0)) || f.state_frame >= cfg.grab_hold_max) {
            MoveId t = MOVE_THROW_FORWARD;
            if (in.stick_y > 0) t = MOVE_THROW_UP;
            else if (in.stick_y < 0) t = MOVE_THROW_DOWN;
            else if (in.stick_x == -f.facing) t = MOVE_THROW_BACK;
            start_attack(f, t);
        }
        break;
    default:
        break;
    }

    // 2. Read the controller, if the fighter is free to act (or shielding).
    //    Attacks come out whenever the button is held (holding light = repeated
    //    jabs); jumps only on a fresh press.
    if (f.state == ST_SHIELD) {
        if (in.button == BTN_DODGE) {
            if (in.stick_x != 0) start_roll(f, in.stick_x);
            else if (in.stick_y < 0) set_state(f, ST_SPOTDODGE);
            // else: keep holding the shield
        } else if (jump_pressed) {
            start_jumpsquat(f, in);  // jump out of shield
        } else if (in.button == BTN_LIGHT) {
            start_attack(f, MOVE_GRAB);  // grab out of shield (Smash: attack while shielding)
        } else {
            f.landing_lag = static_cast<int16_t>(cfg.shield_drop_lag);  // letting go of the shield
            set_state(f, ST_LANDING);
        }
    } else if (actionable(f)) {
        if (f.grounded) {
            if (jump_pressed) {
                start_jumpsquat(f, in);
            } else if (attacking) {
                if (in.stick_x != 0) f.facing = in.stick_x;  // attack the way the stick points
                start_attack(f, pick_move(true, in.button, in.stick_y));
            } else if (in.button == BTN_DODGE) {
                if (in.stick_x != 0) start_roll(f, in.stick_x);
                else if (in.stick_y < 0) set_state(f, ST_SPOTDODGE);
                else set_state(f, ST_SHIELD);
            } else if (in.stick_y < 0 && f.surface != 0) {
                // Down on a platform: drop through it.
                f.grounded = false;
                f.drop_frames = static_cast<uint8_t>(cfg.drop_through_frames);
                f.jumps_left = std::min<uint8_t>(f.jumps_left, 1);
                set_state(f, ST_AIRBORNE);
            } else if (in.stick_x != 0) {
                // Start moving, or turn around: either way a fresh dash begins,
                // with an instant burst of speed (that's what makes it a dash).
                bool fresh = (f.state != ST_RUN) || (in.stick_x != f.facing);
                f.facing = in.stick_x;
                if (fresh) {
                    f.state = ST_RUN;
                    f.state_frame = 0;
                    float burst = cfg.dash_speed * 0.6f;
                    f.vx = (f.vx * f.facing > burst) ? f.vx : f.facing * burst;
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
            } else if (attacking && f.air_whiffs < cfg.air_whiff_limit) {
                start_attack(f, pick_move(false, in.button, in.stick_y));
            } else if (in.button == BTN_DODGE) {
                f.fastfall = false;
                set_state(f, ST_AIRDODGE);
            }
        }
    }

    // 3. Horizontal velocity.
    if (f.grounded) {
        if (f.state == ST_RUN) {
            float target = f.facing * (f.state_frame < cfg.dash_frames ? cfg.dash_speed : cfg.run_speed);
            f.vx = approach(f.vx, target, cfg.ground_accel);
        } else if (f.state == ST_ROLL) {
            float dir = (f.roll_dir == 1) ? 1.0f : -1.0f;
            f.vx = (f.state_frame < cfg.roll_move_frames) ? dir * cfg.roll_distance / cfg.roll_move_frames : 0.0f;
        } else {
            f.vx = approach(f.vx, 0.0f, cfg.ground_friction);
        }
    } else if (f.state != ST_HITSTUN && f.state != ST_AIRDODGE && f.state != ST_GRABBED) {
        if (in.stick_x != 0) f.vx = approach(f.vx, cfg.air_speed * in.stick_x, cfg.air_accel);
        else f.vx = approach(f.vx, 0.0f, cfg.air_friction);
    }

    // 4. Vertical velocity: gravity, capped fall speed, fast-fall. Fast-fall =
    //    holding down (without attacking) for fastfall_hold frames while
    //    falling: you drop at a higher fixed speed. Pressing an attack resets
    //    the count, so down + attack is a down move, not a drop.
    if (attacking) f.fastfall_blocked = true;
    if (in.stick_y >= 0) f.fastfall_blocked = false;  // stick released: down is "fresh" again
    bool counts = in.stick_y < 0 && !attacking && !f.fastfall_blocked;
    f.down_frames = counts ? uint8_t(std::min(f.down_frames + 1, 255)) : 0;
    if (!f.grounded && f.state != ST_GRABBED) {
        bool can_fastfall = f.state != ST_HITSTUN && f.state != ST_AIRDODGE;
        if (can_fastfall && f.down_frames >= cfg.fastfall_hold && f.vy <= 0) f.fastfall = true;
        f.vy = f.fastfall ? -cfg.fastfall_speed : std::max(f.vy - cfg.gravity, -cfg.max_fall_speed);
    }
    if (f.drop_frames > 0) f.drop_frames -= 1;

    // 5. Knockback shrinks by a fixed amount each frame, keeping its direction.
    float speed = std::sqrt(f.launch_vx * f.launch_vx + f.launch_vy * f.launch_vy);
    if (speed > 0) {
        float scale = std::max(speed - cfg.kb_decay, 0.0f) / speed;
        f.launch_vx *= scale;
        f.launch_vy *= scale;
    }

    // 6. Move, then collide with the floors.
    if (f.state != ST_GRABBED) {  // a held fighter is placed by step()
        float prev_y = f.y;
        f.x += f.vx + f.launch_vx;
        f.y += f.vy + f.launch_vy;
        if (f.grounded) {
            const Surface& s = cfg.surfaces[f.surface];
            bool on = (f.x >= s.x0 && f.x <= s.x1);
            if (f.state == ST_ROLL && !on) {  // rolls stop at the edge instead of rolling off
                f.x = std::clamp(f.x, s.x0, s.x1);
                on = true;
            }
            if (!on || f.y > s.y) {
                // Walked off the edge or got launched upward.
                f.grounded = false;
                if (!on) f.jumps_left = std::min<uint8_t>(f.jumps_left, 1);
                if (f.state == ST_IDLE || f.state == ST_RUN || f.state == ST_LANDING || f.state == ST_SHIELD)
                    set_state(f, ST_AIRBORNE);
            } else {
                f.y = s.y;  // a downward launch on the ground just stops at the floor
                f.launch_vy = std::max(f.launch_vy, 0.0f);
            }
        } else if (f.vy + f.launch_vy <= 0) {
            // Falling: land on the highest floor we crossed this frame. Needing
            // prev_y >= floor (above it last frame) means nobody below a
            // floor can snap up onto it, and platforms can be jumped through.
            int best = -1;
            for (int i = 0; i < NUM_SURFACES; ++i) {
                const Surface& s = cfg.surfaces[i];
                if (i != 0 && f.drop_frames > 0) continue;  // dropping through platforms
                bool crossed = prev_y >= s.y && f.y <= s.y && f.x >= s.x0 && f.x <= s.x1;
                if (crossed && (best < 0 || s.y > cfg.surfaces[best].y)) best = i;
            }
            if (best >= 0) land(f, best, cfg);
        }
    }

    // 7. The shield shrinks while held and slowly grows back otherwise.
    if (f.state == ST_SHIELD) {
        f.shield_hp -= cfg.shield_drain;
        if (f.shield_hp <= 0) {
            f.shield_hp = 0;
            set_state(f, ST_SHIELDBREAK);
        }
    } else if (!shielding(f) && f.state != ST_SHIELDBREAK) {
        f.shield_hp = std::min(f.shield_hp + cfg.shield_regen, cfg.shield_max);
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

enum HitKind { HIT_NONE, HIT_ATTACK, HIT_SHIELD, HIT_GRAB, HIT_THROW };

struct PendingHit {
    HitKind kind = HIT_NONE;
    const HitboxDef* hb = nullptr;
    // Captured before any hit is applied: applying one hit cancels the
    // victim's move, which in a trade is the other hit's attacker.
    int move = MOVE_NONE;
    float power = 1.0f;
};

// What attacker `a` does to victim `v` this frame, if anything. If several
// hitboxes overlap, the first one listed in the JSON wins.
static PendingHit find_hit(const Fighter& a, const Fighter& v, const GameConfig& cfg) {
    PendingHit out;
    if (a.state != ST_ATTACK || a.move_hit) return out;
    const MoveDef& m = cfg.moves[a.move];
    if (m.is_throw) {
        // A throw's hitbox is applied to the held opponent on its start frame.
        if (v.state == ST_GRABBED && !m.hitboxes.empty() && a.state_frame == m.hitboxes[0].start)
            out = {HIT_THROW, &m.hitboxes[0]};
        return out;
    }
    if (intangible(v, cfg) || v.state == ST_GRABBED) return out;
    for (const HitboxDef& hb : m.hitboxes) {
        if (a.state_frame < hb.start || a.state_frame > hb.end) continue;
        float cx = a.x + a.facing * hb.x;
        float cy = a.y + hb.y;
        float half_w = cfg.hurtbox_w / 2;
        if (!circle_hits_box(cx, cy, hb.radius, v.x - half_w, v.y, v.x + half_w, v.y + cfg.hurtbox_h)) continue;
        if (hb.grab) out = {HIT_GRAB, &hb};                       // grabs go through shields
        else if (shielding(v)) out = {HIT_SHIELD, &hb};           // shields stop attacks
        else out = {HIT_ATTACK, &hb};
        return out;
    }
    return out;
}

static void push_stale(Fighter& a, int m) {
    for (int i = 8; i > 0; --i) a.stale[i] = a.stale[i - 1];
    a.stale[0] = static_cast<int8_t>(m);
}

static void apply_hit(Fighter& a, Fighter& v, const PendingHit& h, const GameConfig& cfg) {
    const HitboxDef& hb = *h.hb;
    float power = h.power;
    float damage = hb.damage * power;
    v.percent += damage;
    // Knockback uses the damage *after* this hit, so a strong hit at 0% still sends you a bit.
    float kb = knockback(hb, v.percent) * power;
    float angle = hb.angle * DEG_TO_RAD;
    v.launch_vx = a.facing * kb * std::cos(angle);
    v.launch_vy = kb * std::sin(angle);
    v.vx = 0;
    v.vy = 0;
    v.fastfall = false;
    v.move = MOVE_NONE;  // getting hit cancels whatever you were doing
    v.air_whiffs = 0;
    v.hitstun = static_cast<int16_t>(hitstun_frames(kb, cfg));
    v.state = ST_HITSTUN;
    v.state_frame = 0;
    // Both freeze for a moment (hitlag); the victim's stick meanwhile is DI.
    // max(): in a trade, both freeze for the longer of the two hitlags.
    uint8_t lag = static_cast<uint8_t>(hitlag_frames(damage, cfg));
    v.hitlag = std::max(v.hitlag, lag);
    a.hitlag = std::max(a.hitlag, lag);
    v.di_x = v.di_y = 0;
    a.move_hit = true;
    push_stale(a, h.move);
}

static void apply_shield_hit(Fighter& a, Fighter& v, const PendingHit& h, const GameConfig& cfg) {
    float damage = h.hb->damage * h.power;
    v.shield_hp -= damage * cfg.shield_damage_mult;
    // Shield stun (Smash: roughly damage * 0.8 + 2 frames) and pushback.
    v.landing_lag = static_cast<int16_t>(std::lround(damage * 0.8f + 2.0f));
    v.state = ST_SHIELDSTUN;
    v.state_frame = 0;
    float away = (v.x >= a.x) ? 1.0f : -1.0f;
    v.launch_vx = away * (0.1f + 0.02f * damage);
    uint8_t lag = static_cast<uint8_t>(std::lround(hitlag_frames(damage, cfg) * 0.67f));
    v.hitlag = std::max(v.hitlag, lag);
    a.hitlag = std::max(a.hitlag, lag);
    a.move_hit = true;
    if (v.shield_hp <= 0) {
        v.shield_hp = 0;
        v.launch_vx = 0;
        v.state = ST_SHIELDBREAK;
        v.state_frame = 0;
    }
}

static void apply_grab(Fighter& a, Fighter& v) {
    a.move = MOVE_NONE;
    a.move_hit = true;
    a.state = ST_HOLDING;
    a.state_frame = 0;
    a.vx = 0;
    v.move = MOVE_NONE;
    v.state = ST_GRABBED;
    v.state_frame = 0;
    v.hitstun = 0;
    v.fastfall = false;
    v.vx = v.vy = v.launch_vx = v.launch_vy = 0;
}

// Directional influence: bend the launch angle toward the stick held during
// hitlag. Only the part of the stick perpendicular to the launch counts
// (holding along the launch changes nothing), up to di_max_degrees.
static void apply_di(Fighter& v, const GameConfig& cfg) {
    float speed = std::sqrt(v.launch_vx * v.launch_vx + v.launch_vy * v.launch_vy);
    float sl = std::sqrt(float(v.di_x * v.di_x + v.di_y * v.di_y));
    if (speed <= 0 || sl <= 0) return;
    // Work in a frame where the launch points right (m = mirror), then flip
    // back. Mirror-image situations then do exactly the same float maths,
    // which keeps the two players' observations bit-identical (the RL side
    // relies on that symmetry; trig on mirrored angles isn't bit-exact).
    float m = (v.launch_vx < 0) ? -1.0f : 1.0f;
    float ux = m * v.launch_vx / speed, uy = v.launch_vy / speed;
    float sx = m * v.di_x / sl, sy = v.di_y / sl;
    float perp = ux * sy - uy * sx;  // sine of the angle from the launch to the stick
    float angle = std::atan2(uy, ux) + perp * cfg.di_max_degrees * DEG_TO_RAD;
    v.launch_vx = m * speed * std::cos(angle);
    v.launch_vy = speed * std::sin(angle);
}

// Grounded fighters whose bodies overlap are pushed apart, half each, so
// nobody can stand inside the opponent (where every attack trades). Running
// into a standing opponent therefore shoves both along at half speed.
static void push_apart(Fighter& a, Fighter& b, const GameConfig& cfg) {
    if (!a.grounded || !b.grounded || a.surface != b.surface) return;  // you can still jump over someone
    if (a.state == ST_ROLL || b.state == ST_ROLL) return;                // rolls pass through
    if (a.state == ST_GRABBED || b.state == ST_GRABBED) return;
    float dx = b.x - a.x;
    float overlap = cfg.hurtbox_w - std::abs(dx);
    if (overlap <= 0) return;
    float dir = (dx > 0) ? 1.0f : (dx < 0) ? -1.0f : -1.0f * a.facing;  // exactly stacked: a steps back
    a.x -= dir * overlap / 2;
    b.x += dir * overlap / 2;
    // Nobody gets shoved off the floor they stand on (that would be a free KO
    // for walking into someone): a fighter at the edge acts as a wall.
    const Surface& s = cfg.surfaces[a.surface];
    if (a.x < s.x0 || a.x > s.x1) {
        a.x = std::clamp(a.x, s.x0, s.x1);
        b.x = a.x + dir * cfg.hurtbox_w;
    } else if (b.x < s.x0 || b.x > s.x1) {
        b.x = std::clamp(b.x, s.x0, s.x1);
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
    fresh.shield_hp = cfg.shield_max;
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
    uint8_t before[2] = {s.f[0].state, s.f[1].state};  // to spot state changes for the events
    for (Fighter& f : s.f)
        if (f.combo_timer < 65535) f.combo_timer += 1;

    for (int p = 0; p < 2; ++p) {
        Fighter& f = s.f[p];
        if (f.hitlag > 0) {
            // Frozen in hitlag. The victim's stick is recorded as DI and
            // applied the moment the freeze ends.
            Input i = sanitize(in[p]);
            f.hitlag -= 1;
            if (f.state == ST_HITSTUN) {
                f.di_x = i.stick_x;
                f.di_y = i.stick_y;
                if (f.hitlag == 0) {
                    apply_di(f, cfg);
                    ev.di[p] = (f.di_x != 0 || f.di_y != 0);
                }
            }
            if (i.button == BTN_DODGE && f.prev_button != BTN_DODGE) f.dodge_ago = 0;
            f.prev_button = i.button;
            continue;
        }
        step_fighter(f, in[p], cfg);
        if (f.state == ST_ATTACK && f.state_frame == 1) ev.move_started[p] = f.move;
    }

    // A held fighter sits in front of the holder. If the holder is no longer
    // holding or throwing, the grab is over.
    for (int p = 0; p < 2; ++p) {
        Fighter& h = s.f[p];
        Fighter& v = s.f[1 - p];
        if (v.state != ST_GRABBED) continue;
        bool holding = h.state == ST_HOLDING || (h.state == ST_ATTACK && cfg.moves[h.move].is_throw);
        if (!holding) {
            set_state(v, v.grounded ? ST_IDLE : ST_AIRBORNE);
            continue;
        }
        v.x = h.x + h.facing * cfg.grab_hold_distance;
        v.y = h.y;
        v.grounded = h.grounded;
        v.surface = h.surface;
        v.facing = static_cast<int8_t>(-h.facing);
    }

    push_apart(s.f[0], s.f[1], cfg);

    // Find both players' hits first, then apply them, so a "trade" (both hit
    // on the same frame) treats both players the same. Attacks beat grabs: a
    // grabber who gets hit this frame doesn't get the grab. Two grabs cancel.
    // Was each fighter still stunned just before this frame's hits? (For combos.)
    bool victim_stunned[2];
    for (int p = 0; p < 2; ++p) victim_stunned[p] = s.f[p].state == ST_HITSTUN || s.f[p].state == ST_KNOCKDOWN;
    PendingHit hit[2];
    for (int p = 0; p < 2; ++p) {
        if (s.f[p].hitlag > 0) continue;
        hit[p] = find_hit(s.f[p], s.f[1 - p], cfg);
        hit[p].move = s.f[p].move;
        hit[p].power = stale_power(s.f[p], s.f[p].move, cfg);
    }
    // Decide both cancellations before changing anything, so the result
    // doesn't depend on which player is checked first.
    bool cancel[2];
    for (int p = 0; p < 2; ++p) {
        bool hurt = hit[1 - p].kind == HIT_ATTACK || hit[1 - p].kind == HIT_THROW;
        cancel[p] = hit[p].kind == HIT_GRAB && (hurt || hit[1 - p].kind == HIT_GRAB);
    }
    for (int p = 0; p < 2; ++p) {
        if (!cancel[p]) continue;
        hit[p].kind = HIT_NONE;
        s.f[p].move_hit = true;  // the grab is used up
    }
    for (int p = 0; p < 2; ++p) {
        Fighter& a = s.f[p];
        Fighter& v = s.f[1 - p];
        switch (hit[p].kind) {
        case HIT_ATTACK:
        case HIT_THROW:
            ev.hit[p] = true;
            ev.damage[p] = hit[p].hb->damage * hit[p].power;
            ev.hit_move[p] = static_cast<int8_t>(hit[p].move);
            apply_hit(a, v, hit[p], cfg);
            break;
        case HIT_SHIELD:
            apply_shield_hit(a, v, hit[p], cfg);
            ev.shield_block[1 - p] = true;
            break;
        case HIT_GRAB:
            ev.hit_move[p] = static_cast<int8_t>(hit[p].move);
            apply_grab(a, v);
            ev.grab[p] = true;
            break;
        default:
            break;
        }
    }

    // Combos, for both players at once (so a trade treats them the same).
    // Like Smash's combo counter, only *true* combos count: a hit continues
    // your combo only if the victim was still stunned (in hitstun or lying
    // on the floor) when it landed. Getting hit ends your combo; in a trade,
    // both restart at 1. (A "hit within a second" rule also rewarded
    // interrupting an opponent who had already recovered.)
    bool landed[2], was_hit[2];
    for (int p = 0; p < 2; ++p) landed[p] = hit[p].kind == HIT_ATTACK || hit[p].kind == HIT_THROW;
    for (int p = 0; p < 2; ++p) was_hit[p] = landed[1 - p];
    for (int p = 0; p < 2; ++p) {
        Fighter& f = s.f[p];
        if (landed[p]) {
            bool continues = !was_hit[p] && f.combo > 0 && victim_stunned[1 - p];
            f.combo = continues ? uint8_t(std::min(f.combo + 1, 255)) : 1;
            f.combo_timer = 0;
            ev.combo[p] = f.combo;
        } else if (was_hit[p]) {
            f.combo = 0;
        }
    }
    // Once the opponent can act again, the combo is over.
    for (int p = 0; p < 2; ++p) {
        const Fighter& v = s.f[1 - p];
        if (!landed[p] && v.state != ST_HITSTUN && v.state != ST_KNOCKDOWN) s.f[p].combo = 0;
    }

    // Events that are just state changes.
    for (int p = 0; p < 2; ++p) {
        const Fighter& f = s.f[p];
        if (f.state == before[p]) continue;
        if (f.state == ST_TECH) ev.tech[p] = true;
        if (f.state == ST_KNOCKDOWN) ev.knockdown[p] = true;
        if (f.state == ST_SHIELDBREAK) ev.shield_break[p] = true;
        if (before[p] == ST_JUMPSQUAT && f.state == ST_AIRBORNE) ev.jump[p] = f.short_hop ? 2 : 1;
    }

    for (int p = 0; p < 2; ++p) {
        Fighter& f = s.f[p];
        if (!outside_blast_zone(f, cfg)) continue;
        s.f[0].combo = s.f[1].combo = 0;  // a KO ends any combo
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
    int t = f.state_frame;
    switch (f.state) {
    case ST_SPOTDODGE: return t >= cfg.spotdodge_iframe_start && t <= cfg.spotdodge_iframe_end;
    case ST_AIRDODGE: return t >= cfg.airdodge_iframe_start && t <= cfg.airdodge_iframe_end;
    case ST_ROLL: return t >= cfg.roll_iframe_start && t <= cfg.roll_iframe_end;
    case ST_TECH: return t <= cfg.tech_iframes;
    case ST_KNOCKDOWN: return t > cfg.knockdown_frames - 10;  // the get-up is protected
    default: return false;
    }
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
        if (f.state != ST_ATTACK || cfg.moves[f.move].is_throw) continue;
        float power = stale_power(f, f.move, cfg);
        for (const HitboxDef& hb : cfg.moves[f.move].hitboxes) {
            if (f.state_frame < hb.start || f.state_frame > hb.end || n >= max) continue;
            out[n++] = ActiveHitbox{p, f.x + f.facing * hb.x, f.y + hb.y, hb.end - f.state_frame, f.move_hit, power, &hb};
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
        mix(h, &f.shield_hp, sizeof f.shield_hp);
        mix(h, &f.facing, sizeof f.facing);
        mix(h, &f.state, sizeof f.state);
        mix(h, &f.state_frame, sizeof f.state_frame);
        mix(h, &f.grounded, sizeof f.grounded);
        mix(h, &f.surface, sizeof f.surface);
        mix(h, &f.drop_frames, sizeof f.drop_frames);
        mix(h, &f.fastfall, sizeof f.fastfall);
        mix(h, &f.down_frames, sizeof f.down_frames);
        mix(h, &f.fastfall_blocked, sizeof f.fastfall_blocked);
        mix(h, &f.short_hop, sizeof f.short_hop);
        mix(h, &f.air_whiffs, sizeof f.air_whiffs);
        mix(h, &f.jumps_left, sizeof f.jumps_left);
        mix(h, &f.stocks, sizeof f.stocks);
        mix(h, &f.move, sizeof f.move);
        mix(h, &f.move_hit, sizeof f.move_hit);
        mix(h, &f.hitstun, sizeof f.hitstun);
        mix(h, &f.landing_lag, sizeof f.landing_lag);
        mix(h, &f.invuln, sizeof f.invuln);
        mix(h, &f.hitlag, sizeof f.hitlag);
        mix(h, &f.di_x, sizeof f.di_x);
        mix(h, &f.di_y, sizeof f.di_y);
        mix(h, &f.roll_dir, sizeof f.roll_dir);
        mix(h, &f.dodge_ago, sizeof f.dodge_ago);
        mix(h, f.stale, sizeof f.stale);
        mix(h, &f.combo, sizeof f.combo);
        mix(h, &f.combo_timer, sizeof f.combo_timer);
        mix(h, &f.prev_button, sizeof f.prev_button);
    }
    mix(h, &s.frame, sizeof s.frame);
    mix(h, &s.over, sizeof s.over);
    return h;
}
