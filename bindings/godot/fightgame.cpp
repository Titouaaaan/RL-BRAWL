// The Godot extension: one class, FightGame, that GDScript can create with
// FightGame.new(). It wraps the same GameState and step() from core/sim.cpp
// that Python trains on, so what you play in Godot is exactly the training
// game. Godot only draws and reads the keyboard; it never runs game logic.
//
// This file is compiled into a DLL (a library loaded at runtime). Godot finds
// it through godot/fightgame.gdextension and calls fightgame_init() below at
// startup.
#include <gdextension_interface.h>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include <cstdio>  // std::snprintf

#include "../../core/net.h"  // the network bots
#include "../../core/obs.h"  // write_obs: what a live bot sees
#include "../../core/sim.h"

using namespace godot;

// RefCounted = a Godot object that frees itself automatically once no
// script holds a reference to it, like a Python object.
class FightGame : public RefCounted {
    GDCLASS(FightGame, RefCounted)  // Godot macro: registers the class's type info

    GameConfig cfg;
    GameState state;
    Net bot[2];  // each player's network bot, if any

protected:
    // Godot calls this once, to learn which methods scripts may call.
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("load_moves_from_text", "texts"), &FightGame::load_moves_from_text);
        ClassDB::bind_method(D_METHOD("get_move_names"), &FightGame::get_move_names);
        ClassDB::bind_method(D_METHOD("get_state_names"), &FightGame::get_state_names);
        ClassDB::bind_method(D_METHOD("get_hash"), &FightGame::get_hash);
        ClassDB::bind_method(D_METHOD("set_input_buffer", "player", "frames"), &FightGame::set_input_buffer);
        ClassDB::bind_method(D_METHOD("get_obs", "player"), &FightGame::get_obs);
        ClassDB::bind_method(D_METHOD("get_obs_dim"), &FightGame::get_obs_dim);
        ClassDB::bind_method(D_METHOD("load_bot", "player", "data"), &FightGame::load_bot);
        ClassDB::bind_method(D_METHOD("bot_act", "player", "obs", "u"), &FightGame::bot_act);
        ClassDB::bind_method(D_METHOD("reset"), &FightGame::reset);
        ClassDB::bind_method(
            D_METHOD("step", "p1_stick_x", "p1_stick_y", "p1_button", "p2_stick_x", "p2_stick_y", "p2_button"),
            &FightGame::step);
        ClassDB::bind_method(D_METHOD("get_fighter", "index"), &FightGame::get_fighter);
        ClassDB::bind_method(D_METHOD("get_hitboxes"), &FightGame::get_hitboxes);
        ClassDB::bind_method(D_METHOD("get_frame"), &FightGame::get_frame);
        ClassDB::bind_method(D_METHOD("is_over"), &FightGame::is_over);
        ClassDB::bind_method(D_METHOD("get_winner"), &FightGame::get_winner);
        ClassDB::bind_method(D_METHOD("get_config"), &FightGame::get_config);
    }

public:
    FightGame() { ::reset(state, cfg); }

    // Loads the move files' contents, in get_move_names() order, and restarts
    // the game (replays carry their own copy). Returns "" on success, otherwise
    // the error text (Godot shows it on screen instead of crashing).
    String load_moves_from_text(const PackedStringArray& in) {
        if (in.size() != NUM_MOVES) return "expected one text per move in get_move_names()";
        std::string texts[NUM_MOVES];
        for (int m = 0; m < NUM_MOVES; ++m) texts[m] = in[m].utf8().get_data();
        std::string err = ::load_moves_from_text(cfg, texts);
        ::reset(state, cfg);
        return String::utf8(err.c_str());
    }

    // The input buffer per player: humans use it, bots were trained without it
    // (a replay sets the value it was recorded with).
    void set_input_buffer(int64_t player, int64_t frames) {
        if (player == 0 || player == 1) cfg.input_buffer[player] = static_cast<int>(frames);
    }

    // What a trained agent playing `player` sees right now: the same
    // observation as in training (egocentric: mirrored for player 1).
    PackedFloat32Array get_obs(int64_t player) const {
        PackedFloat32Array out;
        out.resize(OBS_DIM);
        write_obs(state, cfg, player == 1 ? 1 : 0, out.ptrw());
        return out;
    }
    int64_t get_obs_dim() const { return OBS_DIM; }

    // Gives `player` a network bot: the bytes of an exported .bin
    // (tools/export_bot.py). Returns "" or the error text.
    String load_bot(int64_t player, const PackedByteArray& data) {
        if (player != 0 && player != 1) return "bad player";
        return String::utf8(load_net(bot[player], data.ptr(), data.size()).c_str());
    }

    // That bot's decision for an observation: [stick x, stick y, button],
    // egocentric like training. u (uniform in [0, 1)) picks the sample.
    PackedInt32Array bot_act(int64_t player, const PackedFloat32Array& obs, double u) const {
        PackedInt32Array out;
        if ((player != 0 && player != 1) || bot[player].hidden == 0 || obs.size() != OBS_DIM) return out;
        int a = net_act(bot[player], obs.ptr(), float(u));
        out.push_back(a / 15 - 1);  // decode the index: ppo.py's ACTIONS order
        out.push_back(a / 5 % 3 - 1);
        out.push_back(a % 5);
        return out;
    }

    // The state hash as 16 hex digits. (A string, because GDScript integers
    // are signed and JSON numbers can't hold 64 bits exactly.)
    String get_hash() const {
        char buf[17];
        std::snprintf(buf, sizeof buf, "%016llx", (unsigned long long)hash_state(state));
        return String(buf);
    }

    PackedStringArray get_move_names() const {
        PackedStringArray out;
        for (const char* n : MOVE_NAMES) out.push_back(n);
        return out;
    }

    PackedStringArray get_state_names() const {
        PackedStringArray out;
        for (const char* n : STATE_NAMES) out.push_back(n);
        return out;
    }

    void reset() { ::reset(state, cfg); }

    // GDScript integers are 64-bit. We narrow them to the sim's small types;
    // step() itself clamps anything out of range.
    void step(int64_t p1x, int64_t p1y, int64_t p1b, int64_t p2x, int64_t p2y, int64_t p2b) {
        Input in[2];
        in[0] = Input{(int8_t)p1x, (int8_t)p1y, (uint8_t)p1b};
        in[1] = Input{(int8_t)p2x, (int8_t)p2y, (uint8_t)p2b};
        StepEvents ev;  // ponytail: unused in Godot until we show hit/KO effects
        ::step(state, in, cfg, ev);
    }

    // Returns one fighter's fields as a Dictionary, which is easy to read
    // from GDScript: f.x, f.state and so on.
    Dictionary get_fighter(int64_t index) const {
        Dictionary d;
        if (index < 0 || index > 1) return d;  // empty Dictionary for a bad index
        const Fighter& f = state.f[index];
        d["x"] = f.x;
        d["y"] = f.y;
        d["vx"] = f.vx;
        d["vy"] = f.vy;
        d["facing"] = f.facing;
        d["state"] = f.state;
        d["state_frame"] = f.state_frame;
        d["jumps_left"] = f.jumps_left;
        d["percent"] = f.percent;
        d["stocks"] = f.stocks;
        d["move"] = f.move;
        d["hitstun"] = f.hitstun;
        d["invuln"] = f.invuln;
        d["respawn_wait"] = f.respawn_wait;
        d["intangible"] = intangible(f, cfg);
        d["air_attack_locked"] = f.air_whiffs >= cfg.air_whiff_limit;
        d["shield_hp"] = f.shield_hp;
        d["hitlag"] = f.hitlag;
        d["combo"] = f.combo;
        d["ledge_side"] = f.ledge_side;
        d["tumble"] = f.tumble;
        d["charge"] = f.charge;
        return d;
    }

    // Active hitboxes this frame, each as {owner, x, y, radius, spent}.
    Array get_hitboxes() const {
        ActiveHitbox boxes[2 * MAX_HITBOXES_PER_MOVE];
        int n = active_hitboxes(state, cfg, boxes, 2 * MAX_HITBOXES_PER_MOVE);
        Array out;
        for (int i = 0; i < n; ++i) {
            Dictionary d;
            d["owner"] = boxes[i].owner;
            d["x"] = boxes[i].x;
            d["y"] = boxes[i].y;
            d["radius"] = boxes[i].def->radius;
            d["spent"] = boxes[i].spent;
            d["grab"] = boxes[i].def->grab;
            out.push_back(d);
        }
        return out;
    }

    int64_t get_frame() const { return state.frame; }
    bool is_over() const { return state.over; }
    int64_t get_winner() const { return winner(state); }  // 0, 1, or -1 = draw

    // The numbers the drawing code needs, so sizes on screen always match the sim.
    Dictionary get_config() const {
        Dictionary d;
        d["stage_half_width"] = cfg.stage_half_width;
        d["blast_x"] = cfg.blast_x;
        d["blast_top"] = cfg.blast_top;
        d["blast_bottom"] = cfg.blast_bottom;
        d["hurtbox_w"] = cfg.hurtbox_w;
        d["hurtbox_h"] = cfg.hurtbox_h;
        d["time_limit_frames"] = cfg.time_limit_frames;
        d["dash_frames"] = cfg.dash_frames;
        d["shield_max"] = cfg.shield_max;
        d["crouch_h"] = cfg.crouch_h;
        d["lying_h"] = cfg.lying_h;
        d["stage_depth"] = cfg.stage_depth;
        d["ledge_hang_max"] = cfg.ledge_hang_max;
        d["charge_max"] = cfg.charge_max;
        Array move_frames;  // total_frames of each move, in MOVE_NAMES order (attack animations stretch over it)
        for (int m = 0; m < NUM_MOVES; ++m) move_frames.push_back(cfg.moves[m].total_frames);
        d["move_frames"] = move_frames;
        Array surfaces;  // [x0, x1, y] per floor; the first is the main stage
        for (const Surface& s : cfg.surfaces) {
            Array one;
            one.push_back(s.x0);
            one.push_back(s.x1);
            one.push_back(s.y);
            surfaces.push_back(one);
        }
        d["surfaces"] = surfaces;
        return d;
    }
};

// --- Registration: tells Godot which classes this DLL provides. ---

static void initialize(ModuleInitializationLevel level) {
    if (level != MODULE_INITIALIZATION_LEVEL_SCENE) return;
    GDREGISTER_CLASS(FightGame);
}

static void uninitialize(ModuleInitializationLevel) {}

// extern "C" keeps this function's name exactly "fightgame_init" in the DLL,
// so Godot can look it up by the name given in fightgame.gdextension.
// (C++ normally encodes argument types into names; C does not.)
extern "C" GDExtensionBool GDE_EXPORT fightgame_init(GDExtensionInterfaceGetProcAddress get_proc_address,
                                                     GDExtensionClassLibraryPtr library,
                                                     GDExtensionInitialization* init) {
    GDExtensionBinding::InitObject init_obj(get_proc_address, library, init);
    init_obj.register_initializer(initialize);
    init_obj.register_terminator(uninitialize);
    init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
    return init_obj.init();
}
