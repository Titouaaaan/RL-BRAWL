#include "sim.h"

#include <algorithm>  // std::min, std::max, std::clamp
#include <cmath>      // std::sqrt, std::cos, std::sin, std::atan2, std::lround, std::floor

static const float DEG_TO_RAD = 3.14159265f / 180.0f;

const char* const STATE_NAMES[NUM_STATES] = {
    "IDLE",        "RUN",     "JUMPSQUAT", "AIRBORNE", "LANDING", "ATTACK",  "HITSTUN",     "SPOTDODGE",
    "AIRDODGE",    "HELPLESS", "SHIELD",   "SHIELDSTUN", "ROLL",  "SHIELDBREAK", "HOLDING", "GRABBED",
    "KNOCKDOWN",   "TECH",    "CROUCH",    "REBOUND",  "LEDGE",   "LEDGE_CLIMB", "GETUP",   "SHIELDDROP",
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

float knockback(const HitboxDef& hb, float p, float d, float w) {
    // Smash's formula (Brawl onward), SmashWiki "Knockback".
    float kb = (p / 10.0f + p * d / 20.0f) * 200.0f / (w + 100.0f) * 1.4f + 18.0f;
    return kb * hb.kb_growth / 100.0f + hb.base_kb;
}

int hitstun_frames(float kb, const GameConfig& cfg) {
    // Ultimate: 0.4 frames per knockback unit, minus one frame.
    return std::max(0, static_cast<int>(std::floor(kb * cfg.hitstun_per_kb)) - 1);
}

int hitlag_frames(float damage, const GameConfig& cfg) {
    int frames = static_cast<int>(std::floor(damage * cfg.hitlag_per_damage + cfg.hitlag_base));
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
    // Tumbling lasts through hitstun, then free fall and an air dodge (which
    // can still tech), and stops as soon as you do anything else.
    if (st != ST_AIRBORNE && st != ST_AIRDODGE) f.tumble = false;
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
    return f.state == ST_IDLE || f.state == ST_RUN || f.state == ST_AIRBORNE || f.state == ST_CROUCH;
}

static bool shielding(const Fighter& f) {
    return f.state == ST_SHIELD || f.state == ST_SHIELDSTUN;
}

static float hurt_height(const Fighter& f, const GameConfig& cfg) {
    if (f.state == ST_CROUCH) return cfg.crouch_h;
    if (f.state == ST_KNOCKDOWN) return cfg.lying_h;
    return cfg.hurtbox_h;
}

// The move table from sim.h: [air?][heavy?][stick down / neutral / up].
// (Light + side on the ground is the forward tilt; see the caller.)
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

static bool is_smash(int m) {
    return m == MOVE_DOWN_HEAVY || m == MOVE_HEAVY || m == MOVE_UP_HEAVY;
}

static void start_attack(Fighter& f, MoveId m) {
    f.tumble = false;
    f.move = m;
    f.move_hit = false;
    f.move_evaded = false;
    f.state = ST_ATTACK;
    f.state_frame = 0;
}

static void start_jumpsquat(Fighter& f, const Input& in) {
    f.jumps_left -= 1;
    f.short_hop = (in.stick_y < 0);  // down + jump is always a short hop
    set_state(f, ST_JUMPSQUAT);
}

// Starts a spot dodge, roll or air dodge, applying dodge staling: dodging again
// within dodge_stale_reset frames makes it longer and its intangibility later.
static void start_dodge(Fighter& f, FighterState st, int base_len, const GameConfig& cfg) {
    if (f.dodge_timer >= cfg.dodge_stale_reset) f.dodge_stale = 0;
    float extra = std::min(f.dodge_stale * cfg.dodge_stale_step, 0.3f);
    f.dodge_len = static_cast<int16_t>(std::lround(base_len * (1.0f + extra)));
    f.dodge_shift = static_cast<uint8_t>(std::lround(3.0f * f.dodge_stale / 5.0f));
    f.dodge_stale = std::min<uint8_t>(f.dodge_stale + 1, 5);
    f.dodge_timer = 0;
    f.state = st;
    f.state_frame = 0;
}

static void start_roll(Fighter& f, int8_t dir, const GameConfig& cfg, bool stales = true) {
    f.roll_dir = (dir > 0) ? 1 : 2;
    if (stales) {
        start_dodge(f, ST_ROLL, cfg.roll_frames, cfg);
    } else {  // tech rolls and get-up / ledge rolls don't count as dodges
        f.dodge_len = static_cast<int16_t>(cfg.roll_frames);
        f.dodge_shift = 0;
        f.state = ST_ROLL;
        f.state_frame = 0;
    }
}

// Puts a fighter who was hanging from ledge `side` onto the stage, `inset`
// units in from the edge, facing inward.
static void place_on_stage(Fighter& f, int side, float inset, const GameConfig& cfg) {
    const Surface& s = cfg.surfaces[0];
    f.x = (side > 0) ? s.x1 - inset : s.x0 + inset;
    f.y = s.y;
    f.grounded = true;
    f.surface = 0;
    f.vx = f.vy = f.launch_vx = f.launch_vy = 0;
    f.facing = static_cast<int8_t>(-side);
    f.ledge_side = 0;
}

// The main stage is a solid block (its top is surface 0): an airborne fighter
// can't pass through its sides or its underside. Coming from the side, you
// stop against the wall (and can slide up it to the ledge); from below, you
// bonk against the underside.
static void collide_stage_body(Fighter& f, float prev_x, float prev_y, const GameConfig& cfg) {
    const Surface& s = cfg.surfaces[0];
    const float half = cfg.hurtbox_w / 2, h = cfg.hurtbox_h;
    const float top = s.y, bottom = s.y - cfg.stage_depth;
    bool overlap = f.x + half > s.x0 && f.x - half < s.x1 && f.y + h > bottom && f.y < top;
    if (!overlap || prev_y >= top) return;  // not inside, or coming from above (that's a landing)
    if (prev_y + h <= bottom) {  // from below: bonk
        f.y = bottom - h;
        f.vy = std::min(f.vy, 0.0f);
        f.launch_vy = std::min(f.launch_vy, 0.0f);
    } else {  // from the side: stop against the wall
        bool left = prev_x < (s.x0 + s.x1) / 2;
        f.x = left ? s.x0 - half : s.x1 + half;
        if (left) {
            f.vx = std::min(f.vx, 0.0f);
            f.launch_vx = std::min(f.launch_vx, 0.0f);
        } else {
            f.vx = std::max(f.vx, 0.0f);
            f.launch_vx = std::max(f.launch_vx, 0.0f);
        }
    }
}

// Touching a floor (surface `surf`) after being in the air.
static void land(Fighter& f, int surf, const Input& in, const GameConfig& cfg) {
    f.y = cfg.surfaces[surf].y;
    f.surface = static_cast<int8_t>(surf);
    f.vy = 0;
    f.launch_vy = 0;
    f.grounded = true;
    f.fastfall = false;
    f.jumps_left = 2;
    f.air_whiffs = 0;
    f.ledge_grabs = 0;
    // Landing while tumbling (Smash): tech if dodge was pressed within
    // tech_window frames and not mashed (the press before that one was at
    // least tech_lockout frames earlier); otherwise you're knocked down.
    // An air dodge started while tumbling still techs, but a missed tech
    // just lands out of the air dodge.
    bool tumbling = f.tumble && (f.state == ST_HITSTUN || f.state == ST_AIRBORNE || f.state == ST_AIRDODGE);
    bool teched = tumbling && f.dodge_ago <= cfg.tech_window && f.dodge_prev_gap >= cfg.tech_lockout;
    f.tumble = false;
    if (teched || (tumbling && f.state != ST_AIRDODGE)) {
        f.vx = f.launch_vx = 0;
        f.hitstun = 0;
        f.airdodge_dir = false;
        if (!teched) {
            set_state(f, ST_KNOCKDOWN);
        } else if (in.stick_x != 0) {
            start_roll(f, in.stick_x, cfg, false);  // tech roll
            f.invuln = std::max<int16_t>(f.invuln, 4);
        } else {
            set_state(f, ST_TECH);  // tech in place
        }
        return;
    }
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
        f.landing_lag = static_cast<int16_t>(f.airdodge_dir ? cfg.dir_dodge_landing_lag : cfg.dodge_landing_lag);
        f.airdodge_dir = false;
        set_state(f, ST_LANDING);  // touching the ground is what ends helpless
        break;
    default:  // including a weak (non-tumble) hitstun: slide on the floor until it ends

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
    if (in.button == BTN_DODGE && f.prev_button != BTN_DODGE) {
        f.dodge_prev_gap = f.dodge_ago;
        f.dodge_ago = 0;
    } else if (f.dodge_ago < 255) {
        f.dodge_ago += 1;
    }
    if (f.dodge_timer < 65535) f.dodge_timer += 1;
    if (f.ledge_cooldown > 0) f.ledge_cooldown -= 1;
    // Struggling in a grab: every fresh button press counts.
    f.mash = (f.state == ST_GRABBED && in.button != BTN_NONE && in.button != f.prev_button) ? 1 : 0;

    // 1. Leave timed states whose time is up (the fighter can act again on
    //    this same frame), and the options that exist only inside a state.
    switch (f.state) {
    case ST_HITSTUN: {
        f.hitstun -= 1;
        if (f.hitstun <= 0) {
            set_state(f, f.grounded ? ST_IDLE : ST_AIRBORNE);  // still tumbling if airborne
            break;
        }
        // Hitstun cancelling: late in a long hitstun, once the launch has
        // slowed down, a fighter may air dodge or attack out of it.
        if (f.grounded) break;
        float speed = std::sqrt(f.launch_vx * f.launch_vx + f.launch_vy * f.launch_vy);
        bool dodge_out = in.button == BTN_DODGE && f.state_frame >= cfg.hitstun_cancel_dodge &&
                         speed < cfg.hitstun_cancel_dodge_speed;
        bool attack_out = attacking && f.state_frame >= cfg.hitstun_cancel_attack &&
                          speed < cfg.hitstun_cancel_attack_speed;
        if (dodge_out || attack_out) {
            f.hitstun = 0;
            set_state(f, ST_AIRBORNE);  // the input below takes it from here
        }
        break;
    }
    case ST_ATTACK:
        // Jab combo: keep pressing (or holding) light to go jab 1 -> 2 -> 3.
        if (f.move == MOVE_JAB && f.state_frame >= 5 && in.button == BTN_LIGHT) {
            start_attack(f, MOVE_JAB2);
        } else if (f.move == MOVE_JAB2 && f.state_frame >= 6 && in.button == BTN_LIGHT) {
            start_attack(f, MOVE_JAB3);
        } else if (f.state_frame >= cfg.moves[f.move].total_frames) {
            if (!f.grounded && !f.move_hit && is_aerial(f.move)) f.air_whiffs += 1;  // a missed air attack
            f.move = MOVE_NONE;
            set_state(f, f.grounded ? ST_IDLE : ST_AIRBORNE);
        }
        break;
    case ST_LANDING:
        if (f.state_frame >= f.landing_lag) set_state(f, ST_IDLE);
        break;
    case ST_REBOUND:
        if (f.state_frame >= f.landing_lag) set_state(f, f.grounded ? ST_IDLE : ST_AIRBORNE);
        break;
    case ST_KNOCKDOWN:
        // Lying on the floor: after a moment, choose how to get up (or it
        // happens by itself).
        if (f.state_frame >= cfg.knockdown_min) {
            if (attacking) {
                start_attack(f, MOVE_GETUP_ATTACK);
            } else if (in.button == BTN_DODGE && in.stick_x != 0) {
                start_roll(f, in.stick_x, cfg, false);  // get-up roll
                f.invuln = std::max<int16_t>(f.invuln, 4);
            } else if (in.stick_y > 0 || jump_pressed || f.state_frame >= cfg.knockdown_max) {
                set_state(f, ST_GETUP);
            }
        }
        break;
    case ST_GETUP:
        if (f.state_frame >= cfg.getup_frames) set_state(f, ST_IDLE);
        break;
    case ST_TECH:
        if (f.state_frame >= cfg.tech_frames) set_state(f, ST_IDLE);
        break;
    case ST_LEDGE_CLIMB:
        if (f.state_frame >= cfg.ledge_climb_frames) set_state(f, ST_IDLE);
        break;
    case ST_SPOTDODGE:
        if (f.state_frame >= f.dodge_len) set_state(f, f.grounded ? ST_IDLE : ST_AIRBORNE);
        break;
    case ST_ROLL:
        if (f.state_frame >= f.dodge_len) {
            f.facing = (f.roll_dir == 1) ? -1 : 1;  // end facing back where you came from, like Smash
            set_state(f, f.grounded ? ST_IDLE : ST_AIRBORNE);
        }
        break;
    case ST_AIRDODGE:
        // An air dodge doesn't end in freedom: you stay helpless until you land.
        if (f.state_frame >= f.dodge_len) set_state(f, ST_HELPLESS);
        break;
    case ST_SHIELDSTUN:
        if (f.state_frame >= f.landing_lag) set_state(f, ST_SHIELD);  // the shield input below decides what's next
        break;
    case ST_SHIELDDROP:
        if (f.state_frame >= cfg.shield_drop_lag) set_state(f, ST_IDLE);
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
        // Throw in the direction pressed (after a few frames). If you hold on
        // too long, the opponent breaks free (see step()).
        if (f.state_frame >= 4 && (in.stick_x != 0 || in.stick_y != 0)) {
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

    // 2. Read the controller, if the fighter is free to act (or shielding,
    //    or hanging from a ledge). Attacks come out whenever the button is
    //    held (holding light = jab combo); jumps only on a fresh press.
    if (f.state == ST_SHIELD) {
        if (in.button == BTN_DODGE) {
            if (in.stick_x != 0) start_roll(f, in.stick_x, cfg);
            else if (in.stick_y < 0) start_dodge(f, ST_SPOTDODGE, cfg.spotdodge_frames, cfg);
            // else: keep holding the shield
        } else if (jump_pressed) {
            start_jumpsquat(f, in);  // jump out of shield
        } else if (in.button == BTN_LIGHT) {
            start_attack(f, MOVE_GRAB);  // grab out of shield (Smash: attack while shielding)
        } else {
            // Letting go of the shield. Its first frames are the parry window,
            // if the shield had been up for a moment (no parry by mashing).
            f.parry_ok = f.state_frame >= cfg.parry_min_shield;
            set_state(f, ST_SHIELDDROP);
        }
    } else if (f.state == ST_LEDGE) {
        const int side = f.ledge_side;
        const int8_t inward = static_cast<int8_t>(-side);
        if (f.state_frame >= cfg.ledge_min_frames) {
            if (jump_pressed) {  // ledge jump
                f.ledge_side = 0;
                f.grounded = false;
                f.vy = cfg.jump_velocity;
                f.vx = inward * 0.1f;
                f.jumps_left = 1;
                f.ledge_cooldown = 20;
                f.invuln = std::max<int16_t>(f.invuln, 5);
                set_state(f, ST_AIRBORNE);
            } else if (attacking) {  // ledge attack: climb up swinging
                place_on_stage(f, side, 1.3f, cfg);
                start_attack(f, MOVE_LEDGE_ATTACK);
            } else if (in.button == BTN_DODGE) {  // ledge roll onto the stage
                place_on_stage(f, side, 0.5f, cfg);
                start_roll(f, inward, cfg, false);
                f.invuln = std::max<int16_t>(f.invuln, static_cast<int16_t>(cfg.ledge_option_invuln));
            } else if (in.stick_x == inward || in.stick_y > 0) {  // climb up
                place_on_stage(f, side, 1.3f, cfg);
                f.invuln = std::max<int16_t>(f.invuln, static_cast<int16_t>(cfg.ledge_climb_invuln));
                set_state(f, ST_LEDGE_CLIMB);
            } else if (in.stick_x == side || in.stick_y < 0 || f.state_frame >= cfg.ledge_hang_max) {  // let go
                f.ledge_side = 0;
                f.grounded = false;
                f.ledge_cooldown = 30;
                f.jumps_left = std::max<uint8_t>(f.jumps_left, 1);
                set_state(f, ST_AIRBORNE);
            }
        }
    } else if (actionable(f)) {
        if (f.grounded) {
            if (jump_pressed) {
                start_jumpsquat(f, in);
            } else if (attacking) {
                if (f.state == ST_RUN && in.button == BTN_LIGHT && in.stick_y == 0) {
                    start_attack(f, MOVE_DASH_ATTACK);  // keeps the running momentum
                } else {
                    if (in.stick_x != 0) f.facing = in.stick_x;  // attack the way the stick points
                    MoveId m = pick_move(true, in.button, in.stick_y);
                    if (m == MOVE_JAB && in.stick_x != 0) m = MOVE_FORWARD_TILT;
                    start_attack(f, m);
                }
            } else if (in.button == BTN_DODGE) {
                if (in.stick_x != 0) start_roll(f, in.stick_x, cfg);
                else if (in.stick_y < 0) start_dodge(f, ST_SPOTDODGE, cfg.spotdodge_frames, cfg);
                else set_state(f, ST_SHIELD);
            } else if (in.stick_y < 0 && f.surface != 0) {
                // Down on a platform: drop through it.
                f.grounded = false;
                f.drop_frames = static_cast<uint8_t>(cfg.drop_through_frames);
                f.jumps_left = std::min<uint8_t>(f.jumps_left, 1);
                set_state(f, ST_AIRBORNE);
            } else if (in.stick_y < 0) {
                set_state(f, ST_CROUCH);
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
                f.tumble = false;
            } else if (attacking && f.air_whiffs < cfg.air_whiff_limit) {
                start_attack(f, pick_move(false, in.button, in.stick_y));
            } else if (in.button == BTN_DODGE) {
                f.fastfall = false;
                start_dodge(f, ST_AIRDODGE, cfg.airdodge_frames, cfg);
                // Directional air dodge: a burst of speed in the stick's direction.
                f.airdodge_dir = (in.stick_x != 0 || in.stick_y != 0);
                if (f.airdodge_dir) {
                    float len = std::sqrt(float(in.stick_x * in.stick_x + in.stick_y * in.stick_y));
                    f.vx = in.stick_x / len * cfg.airdodge_speed;
                    f.vy = in.stick_y / len * cfg.airdodge_speed;
                    f.launch_vx = f.launch_vy = 0;
                }
            }
        }
    }

    // 3. Horizontal velocity.
    const bool dir_dodging = f.state == ST_AIRDODGE && f.airdodge_dir && f.state_frame < cfg.airdodge_move_frames;
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
    } else if (dir_dodging) {
        f.vx = approach(f.vx, 0.0f, cfg.airdodge_speed / cfg.airdodge_move_frames);
    } else if (f.state != ST_HITSTUN && f.state != ST_AIRDODGE && f.state != ST_GRABBED && f.state != ST_LEDGE) {
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
    if (!f.grounded && f.state != ST_GRABBED && f.state != ST_LEDGE) {
        if (dir_dodging) {
            f.vy = approach(f.vy, 0.0f, cfg.airdodge_speed / cfg.airdodge_move_frames);  // no gravity during the slingshot
        } else {
            bool can_fastfall = f.state != ST_HITSTUN && f.state != ST_AIRDODGE;
            if (can_fastfall && f.down_frames >= cfg.fastfall_hold && f.vy <= 0) f.fastfall = true;
            f.vy = f.fastfall ? -cfg.fastfall_speed : std::max(f.vy - cfg.gravity, -cfg.max_fall_speed);
        }
    }
    if (f.drop_frames > 0) f.drop_frames -= 1;

    // 5. Knockback shrinks by a fixed amount each frame, keeping its direction.
    float speed = std::sqrt(f.launch_vx * f.launch_vx + f.launch_vy * f.launch_vy);
    if (speed > 0) {
        float scale = std::max(speed - cfg.kb_decay, 0.0f) / speed;
        f.launch_vx *= scale;
        f.launch_vy *= scale;
    }

    // 6. Move, then collide with the floors. (Held and hanging fighters are
    //    placed by step().)
    if (f.state != ST_GRABBED && f.state != ST_LEDGE) {
        float prev_x = f.x, prev_y = f.y;
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
                if (f.state == ST_IDLE || f.state == ST_RUN || f.state == ST_LANDING || f.state == ST_SHIELD ||
                    f.state == ST_CROUCH || f.state == ST_SHIELDDROP)
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
            if (best >= 0) land(f, best, in, cfg);
        }
        if (!f.grounded) collide_stage_body(f, prev_x, prev_y, cfg);
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

// Stuck in the recovery of a missed attack or grab (past its last active
// frame without hitting), in an aerial's landing lag, or rebounding from a
// clank: the textbook moments to punish in Smash.
static bool punishable(const Fighter& f, const GameConfig& cfg) {
    if (f.state == ST_REBOUND) return true;
    if (f.state == ST_LANDING) return f.landing_lag > cfg.landing_lag;  // longer than a plain landing
    if (f.state != ST_ATTACK || f.move_hit || f.move < 0) return false;
    const MoveDef& m = cfg.moves[f.move];
    if (m.is_throw || m.hitboxes.empty()) return false;
    int last_active = 0;
    for (const HitboxDef& hb : m.hitboxes) last_active = std::max(last_active, hb.end);
    return f.state_frame > last_active;
}

// HIT_EVADED: the hitbox touched a dodging (intangible) victim. Nothing
// happens to either fighter; it only counts as a successful defense.
enum HitKind { HIT_NONE, HIT_ATTACK, HIT_SHIELD, HIT_GRAB, HIT_THROW, HIT_PARRY, HIT_EVADED };

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
    // Intangible from a dodge (not from respawn / ledge invulnerability or a
    // get-up): the hitbox still "touches" and we report an evade.
    const bool dodging = v.invuln == 0 && intangible(v, cfg) &&
                         (v.state == ST_SPOTDODGE || v.state == ST_ROLL || v.state == ST_AIRDODGE);
    if ((intangible(v, cfg) && !dodging) || v.state == ST_GRABBED) return out;
    const float half_w = cfg.hurtbox_w / 2;
    const float h = hurt_height(v, cfg);
    for (const HitboxDef& hb : m.hitboxes) {
        if (a.state_frame < hb.start || a.state_frame > hb.end) continue;
        float cx = a.x + a.facing * hb.x;
        float cy = a.y + hb.y;
        if (!circle_hits_box(cx, cy, hb.radius, v.x - half_w, v.y, v.x + half_w, v.y + h)) continue;
        bool parrying = v.state == ST_SHIELDDROP && v.parry_ok && v.state_frame <= cfg.parry_window;
        if (dodging) out = {HIT_EVADED, &hb};                     // went through the dodge's i-frames
        else if (hb.grab) out = {HIT_GRAB, &hb};                       // grabs go through shields
        else if (parrying) out = {HIT_PARRY, &hb};                // let go of the shield just in time
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
    const bool crouching = v.state == ST_CROUCH;
    float damage = hb.damage * h.power;  // staleness weakens the damage, and so the knockback
    v.percent += damage;
    float kb = knockback(hb, v.percent, damage, cfg.weight);
    if (crouching) kb *= cfg.crouch_kb_mult;  // crouch cancel
    // Launch angle. 361 = the Sakurai angle: along the ground for weak hits on
    // a grounded opponent, rising to ~38 degrees; always ~38 in the air.
    float deg = hb.angle;
    if (hb.angle == 361.0f) {
        if (!v.grounded || kb >= 88.0f) deg = 38.0f;
        else if (kb < 60.0f) deg = 0.0f;
        else deg = 38.0f * (kb - 60.0f) / 28.0f;
    }
    float angle = deg * DEG_TO_RAD;
    float launch = kb * cfg.launch_speed_scale;
    v.launch_vx = a.facing * launch * std::cos(angle);
    v.launch_vy = launch * std::sin(angle);
    v.vx = 0;
    v.vy = 0;
    v.fastfall = false;
    v.move = MOVE_NONE;  // getting hit cancels whatever you were doing
    v.air_whiffs = 0;
    v.ledge_grabs = 0;   // hitstun resets the ledge-grab count
    v.ledge_side = 0;
    v.airdodge_dir = false;
    v.hitstun = static_cast<int16_t>(hitstun_frames(kb, cfg));
    v.tumble = kb > cfg.tumble_kb;
    v.state = ST_HITSTUN;
    v.state_frame = 0;
    // Both freeze for a moment (hitlag); the victim's stick meanwhile is DI.
    // max(): in a trade, both freeze for the longer of the two hitlags.
    int lag = hitlag_frames(damage, cfg);
    if (crouching) lag = static_cast<int>(lag * 0.67f);
    v.hitlag = std::max<uint8_t>(v.hitlag, static_cast<uint8_t>(lag));
    a.hitlag = std::max<uint8_t>(a.hitlag, static_cast<uint8_t>(lag));
    v.di_x = v.di_y = 0;
    a.move_hit = true;
    push_stale(a, h.move);
}

static void apply_shield_hit(Fighter& a, Fighter& v, const PendingHit& h, const GameConfig& cfg) {
    float damage = h.hb->damage * h.power;
    v.shield_hp -= damage;
    // Shield stun (Ultimate): floor(0.8 * d * t + 2), t = 0.725 for smash
    // attacks, 0.33 for aerials, 1 otherwise. Plus pushback.
    float t = is_smash(h.move) ? 0.725f : is_aerial(h.move) ? 0.33f : 1.0f;
    v.landing_lag = static_cast<int16_t>(std::floor(0.8f * damage * t + 2.0f));
    v.state = ST_SHIELDSTUN;
    v.state_frame = 0;
    float away = (v.x >= a.x) ? 1.0f : -1.0f;
    v.launch_vx = away * (0.04f + 0.01f * damage);
    uint8_t lag = static_cast<uint8_t>(hitlag_frames(damage, cfg) * 0.67f);
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

// A parry: the defender is free at once, the attacker freezes for longer and
// then still has to finish its move: a guaranteed chance to punish.
static void apply_parry(Fighter& a, Fighter& v, const PendingHit& h, const GameConfig& cfg) {
    float damage = h.hb->damage * h.power;
    a.hitlag = static_cast<uint8_t>(std::min(255, hitlag_frames(damage, cfg) + cfg.parry_attacker_lag));
    a.move_hit = true;
    v.state = ST_IDLE;
    v.state_frame = 0;
}

static void apply_grab(Fighter& a, Fighter& v, const GameConfig& cfg) {
    a.move = MOVE_NONE;
    a.move_hit = true;
    a.state = ST_HOLDING;
    a.state_frame = 0;
    a.vx = 0;
    a.grab_timer = static_cast<int16_t>(cfg.grab_hold_base + cfg.grab_hold_per_percent * v.percent);
    v.move = MOVE_NONE;
    v.state = ST_GRABBED;
    v.tumble = false;
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

// The strongest active hitbox damage of a grounded attack that can clank, or 0.
static float clank_damage(const Fighter& f, const GameConfig& cfg) {
    if (f.state != ST_ATTACK || f.move_hit || f.hitlag > 0 || !f.grounded) return 0;
    if (is_aerial(f.move) || cfg.moves[f.move].is_throw || f.move == MOVE_GRAB) return 0;
    float d = 0;
    for (const HitboxDef& hb : cfg.moves[f.move].hitboxes)
        if (f.state_frame >= hb.start && f.state_frame <= hb.end) d = std::max(d, hb.damage);
    return d * stale_power(f, f.move, cfg);
}

// Clanks: when two grounded attacks' active hitboxes touch, the weaker one
// (by damage) rebounds, or both do if within clank_range %. Aerials, grabs
// and throws never clank (as in Smash).
static void resolve_clank(GameState& s, const GameConfig& cfg, StepEvents& ev) {
    float dmg[2] = {clank_damage(s.f[0], cfg), clank_damage(s.f[1], cfg)};
    if (dmg[0] <= 0 || dmg[1] <= 0) return;
    bool touch = false;
    for (const HitboxDef& ha : cfg.moves[s.f[0].move].hitboxes) {
        if (s.f[0].state_frame < ha.start || s.f[0].state_frame > ha.end) continue;
        for (const HitboxDef& hb : cfg.moves[s.f[1].move].hitboxes) {
            if (s.f[1].state_frame < hb.start || s.f[1].state_frame > hb.end) continue;
            float dx = (s.f[0].x + s.f[0].facing * ha.x) - (s.f[1].x + s.f[1].facing * hb.x);
            float dy = (s.f[0].y + ha.y) - (s.f[1].y + hb.y);
            float r = ha.radius + hb.radius;
            if (dx * dx + dy * dy <= r * r) touch = true;
        }
    }
    if (!touch) return;
    // Both rebound, unless one is stronger by more than clank_range: then it
    // keeps going and only the weaker one rebounds.
    bool rebound[2] = {true, true};
    if (dmg[0] > dmg[1] + cfg.clank_range) rebound[0] = false;
    if (dmg[1] > dmg[0] + cfg.clank_range) rebound[1] = false;
    float dmax = std::max(dmg[0], dmg[1]);
    int frames = std::min(cfg.rebound_max, static_cast<int>(std::floor((dmax + 4.0f) * 15.0f / 8.0f)));
    uint8_t lag = static_cast<uint8_t>(hitlag_frames(dmax, cfg));
    for (int p = 0; p < 2; ++p) {
        if (!rebound[p]) continue;
        Fighter& f = s.f[p];
        f.move = MOVE_NONE;
        f.state = ST_REBOUND;
        f.state_frame = 0;
        f.landing_lag = static_cast<int16_t>(frames);
        f.hitlag = std::max(f.hitlag, lag);
        ev.clank[p] = true;
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

// Grabs a ledge if an airborne fighter is falling next to one (not holding
// down, not in hitstun, the ledge free, grab limit not reached).
static void try_ledge_grab(Fighter& f, const Fighter& other, const Input& raw_in, int p, const GameConfig& cfg,
                           StepEvents& ev) {
    Input in = sanitize(raw_in);
    bool can = !f.grounded && f.hitlag == 0 && f.ledge_cooldown == 0 && in.stick_y >= 0 &&
               (f.state == ST_AIRBORNE || f.state == ST_HELPLESS || f.state == ST_AIRDODGE) &&
               f.vy + f.launch_vy <= 0 && f.ledge_grabs < cfg.ledge_max_grabs;
    if (!can) return;
    const Surface& stage = cfg.surfaces[0];
    for (int side = -1; side <= 1; side += 2) {
        if (other.state == ST_LEDGE && other.ledge_side == side) continue;  // taken: edge-hogged
        float ex = (side > 0) ? stage.x1 : stage.x0;
        float out = (f.x - ex) * side;              // how far past the edge (outward) the fighter is
        float top = f.y + cfg.hurtbox_h - stage.y;  // hands' height relative to the ledge
        if (out < -0.5f || out > 2.2f || top < -0.6f || top > 1.4f) continue;
        static const float invuln_mult[4] = {1.0f, 0.8f, 0.5f, 0.0f};
        f.state = ST_LEDGE;
        f.tumble = false;
        f.state_frame = 0;
        f.ledge_side = static_cast<int8_t>(side);
        f.x = ex + side * cfg.ledge_hang_x;
        f.y = stage.y + cfg.ledge_hang_y;
        f.vx = f.vy = f.launch_vx = f.launch_vy = 0;
        f.facing = static_cast<int8_t>(-side);
        f.jumps_left = 2;
        f.air_whiffs = 0;
        f.fastfall = false;
        f.airdodge_dir = false;
        int16_t iv = static_cast<int16_t>(cfg.ledge_invuln * invuln_mult[std::min<int>(f.ledge_grabs, 3)]);
        f.invuln = std::max(f.invuln, iv);
        f.ledge_grabs += 1;
        ev.ledge_grab[p] = true;
        return;
    }
}

// ---------------------------------------------------------------------------
// The whole game, one frame.
// ---------------------------------------------------------------------------

void step(GameState& s, const Input in[2], const GameConfig& cfg, StepEvents& ev) {
    ev = StepEvents{};
    if (s.over) return;
    uint8_t before[2] = {s.f[0].state, s.f[1].state};  // to spot state changes for the events
    for (Fighter& f : s.f) {
        if (f.combo_timer < 65535) f.combo_timer += 1;
        if (f.since_defense < 65535) f.since_defense += 1;
    }

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
            if (i.button == BTN_DODGE && f.prev_button != BTN_DODGE) {
                f.dodge_prev_gap = f.dodge_ago;
                f.dodge_ago = 0;
            }
            f.prev_button = i.button;
            continue;
        }
        step_fighter(f, in[p], cfg);
        if (f.state == ST_ATTACK && f.state_frame == 1) ev.move_started[p] = f.move;
    }

    // Ledges: falling fighters next to one grab it.
    for (int p = 0; p < 2; ++p) try_ledge_grab(s.f[p], s.f[1 - p], in[p], p, cfg, ev);

    // Grabs: a held fighter sits in front of the holder; the hold runs out
    // (faster when the victim mashes) and the victim breaks free.
    for (int p = 0; p < 2; ++p) {
        Fighter& h = s.f[p];
        Fighter& v = s.f[1 - p];
        if (v.state != ST_GRABBED) continue;
        bool throwing = h.state == ST_ATTACK && cfg.moves[h.move].is_throw;
        if (h.state != ST_HOLDING && !throwing) {
            set_state(v, v.grounded ? ST_IDLE : ST_AIRBORNE);
            continue;
        }
        if (h.state == ST_HOLDING && h.hitlag == 0) {
            h.grab_timer -= 1 + v.mash * cfg.grab_mash;
            if (h.grab_timer <= 0) {  // broke free: both stumble apart
                v.x = h.x + h.facing * (cfg.grab_hold_distance + 0.3f);
                v.launch_vx = h.facing * 0.12f;
                v.landing_lag = h.landing_lag = static_cast<int16_t>(cfg.grab_release_lag);
                v.state = h.state = ST_LANDING;
                v.state_frame = h.state_frame = 0;
                ev.escaped[1 - p] = true;
                continue;
            }
        }
        v.x = h.x + h.facing * cfg.grab_hold_distance;
        v.y = h.y;
        v.grounded = h.grounded;
        v.surface = h.surface;
        v.facing = static_cast<int8_t>(-h.facing);
    }

    push_apart(s.f[0], s.f[1], cfg);

    // Clanks first: a rebounding attack can't hit anyone.
    resolve_clank(s, cfg, ev);

    // Was each fighter still stunned just before this frame's hits? (For combos.)
    bool victim_stunned[2];
    for (int p = 0; p < 2; ++p) victim_stunned[p] = s.f[p].state == ST_HITSTUN || s.f[p].state == ST_KNOCKDOWN;
    // ...and stuck recovering from their own mistake? (For whiff punishes.)
    bool victim_punishable[2];
    for (int p = 0; p < 2; ++p) victim_punishable[p] = punishable(s.f[p], cfg);

    // Find both players' hits first, then apply them, so a "trade" (both hit
    // on the same frame) treats both players the same.
    PendingHit hit[2];
    for (int p = 0; p < 2; ++p) {
        if (s.f[p].hitlag > 0) continue;
        hit[p] = find_hit(s.f[p], s.f[1 - p], cfg);
        hit[p].move = s.f[p].move;
        hit[p].power = stale_power(s.f[p], s.f[p].move, cfg);
    }
    // Attacks beat grabs: a grabber who gets hit this frame doesn't get the
    // grab; two grabs cancel. Decide both before changing anything, so the
    // result doesn't depend on which player is checked first.
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
        case HIT_PARRY:
            apply_parry(a, v, hit[p], cfg);
            ev.parry[1 - p] = true;
            break;
        case HIT_EVADED:
            ev.evade[1 - p] = !a.move_evaded;
            a.move_evaded = true;
            break;
        case HIT_GRAB:
            ev.hit_move[p] = static_cast<int8_t>(hit[p].move);
            apply_grab(a, v, cfg);
            ev.grab[p] = true;
            break;
        default:
            break;
        }
    }

    // Counterattacks and punishes, for both players at once. A successful
    // defense opens a counter_window; a hit inside it is a counter (and uses
    // it up). Getting hit closes it.
    for (int p = 0; p < 2; ++p) {
        bool defended = hit[1 - p].kind == HIT_EVADED || hit[1 - p].kind == HIT_PARRY || hit[1 - p].kind == HIT_SHIELD;
        if (defended) s.f[p].since_defense = 0;
    }
    for (int p = 0; p < 2; ++p) {
        Fighter& f = s.f[p];
        bool lands = hit[p].kind == HIT_ATTACK || hit[p].kind == HIT_THROW;
        bool got_hit = hit[1 - p].kind == HIT_ATTACK || hit[1 - p].kind == HIT_THROW;
        if (lands && !got_hit) {
            ev.counter[p] = f.since_defense <= cfg.counter_window;
            ev.whiff_punish[p] = victim_punishable[1 - p];
        }
        if (lands || got_hit) f.since_defense = 65535;
    }

    // Combos, for both players at once (so a trade treats them the same).
    // Like Smash's combo counter, only *true* combos count: a hit continues
    // your combo only if the victim was still stunned (in hitstun or lying
    // on the floor) when it landed. Getting hit ends your combo; in a trade,
    // both restart at 1.
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
        if (f.state == ST_TECH || (f.state == ST_ROLL && before[p] == ST_HITSTUN)) ev.tech[p] = true;
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
    int shift = f.dodge_shift;
    switch (f.state) {
    case ST_SPOTDODGE: return t >= cfg.spotdodge_iframe_start + shift && t <= cfg.spotdodge_iframe_end;
    case ST_AIRDODGE: return t >= cfg.airdodge_iframe_start + shift && t <= cfg.airdodge_iframe_end;
    case ST_ROLL: return t >= cfg.roll_iframe_start + shift && t <= cfg.roll_iframe_end;
    case ST_TECH: return t <= cfg.tech_iframes;
    case ST_GETUP: return t <= cfg.getup_iframes;
    case ST_ATTACK: return f.move >= 0 && t <= cfg.moves[f.move].intangible_until;
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
        mix(h, &f.tumble, sizeof f.tumble);
        mix(h, &f.landing_lag, sizeof f.landing_lag);
        mix(h, &f.invuln, sizeof f.invuln);
        mix(h, &f.hitlag, sizeof f.hitlag);
        mix(h, &f.di_x, sizeof f.di_x);
        mix(h, &f.di_y, sizeof f.di_y);
        mix(h, &f.roll_dir, sizeof f.roll_dir);
        mix(h, &f.dodge_stale, sizeof f.dodge_stale);
        mix(h, &f.dodge_timer, sizeof f.dodge_timer);
        mix(h, &f.dodge_len, sizeof f.dodge_len);
        mix(h, &f.dodge_shift, sizeof f.dodge_shift);
        mix(h, &f.airdodge_dir, sizeof f.airdodge_dir);
        mix(h, &f.dodge_ago, sizeof f.dodge_ago);
        mix(h, &f.dodge_prev_gap, sizeof f.dodge_prev_gap);
        mix(h, &f.ledge_grabs, sizeof f.ledge_grabs);
        mix(h, &f.ledge_side, sizeof f.ledge_side);
        mix(h, &f.ledge_cooldown, sizeof f.ledge_cooldown);
        mix(h, &f.grab_timer, sizeof f.grab_timer);
        mix(h, &f.mash, sizeof f.mash);
        mix(h, &f.parry_ok, sizeof f.parry_ok);
        mix(h, f.stale, sizeof f.stale);
        mix(h, &f.combo, sizeof f.combo);
        mix(h, &f.combo_timer, sizeof f.combo_timer);
        mix(h, &f.since_defense, sizeof f.since_defense);
        mix(h, &f.move_evaded, sizeof f.move_evaded);
        mix(h, &f.prev_button, sizeof f.prev_button);
    }
    mix(h, &s.frame, sizeof s.frame);
    mix(h, &s.over, sizeof s.over);
    return h;
}
