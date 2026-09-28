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
#include <godot_cpp/variant/dictionary.hpp>

#include "../../core/sim.h"

using namespace godot;

// RefCounted = a Godot object that frees itself automatically once no
// script holds a reference to it, like a Python object.
class FightGame : public RefCounted {
    GDCLASS(FightGame, RefCounted)  // Godot macro: registers the class's type info

    GameConfig cfg;
    GameState state;

protected:
    // Godot calls this once, to learn which methods scripts may call.
    static void _bind_methods() {
        ClassDB::bind_method(D_METHOD("reset"), &FightGame::reset);
        ClassDB::bind_method(
            D_METHOD("step", "p1_stick_x", "p1_stick_y", "p1_button", "p2_stick_x", "p2_stick_y", "p2_button"),
            &FightGame::step);
        ClassDB::bind_method(D_METHOD("get_fighter", "index"), &FightGame::get_fighter);
        ClassDB::bind_method(D_METHOD("get_frame"), &FightGame::get_frame);
        ClassDB::bind_method(D_METHOD("get_stage_half_width"), &FightGame::get_stage_half_width);
    }

public:
    FightGame() { ::reset(state, cfg); }

    void reset() { ::reset(state, cfg); }

    // GDScript integers are 64-bit. We narrow them to the sim's small types;
    // step() itself clamps anything out of range.
    void step(int64_t p1x, int64_t p1y, int64_t p1b, int64_t p2x, int64_t p2y, int64_t p2b) {
        Input in[2];
        in[0] = Input{(int8_t)p1x, (int8_t)p1y, (uint8_t)p1b};
        in[1] = Input{(int8_t)p2x, (int8_t)p2y, (uint8_t)p2b};
        ::step(state, in, cfg);
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
        return d;
    }

    int64_t get_frame() const { return state.frame; }
    double get_stage_half_width() const { return cfg.stage_half_width; }
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
