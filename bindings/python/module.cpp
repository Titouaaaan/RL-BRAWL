// The Python module `fightcore`, built with nanobind. nanobind generates the
// glue code that lets Python call C++ functions and share NumPy arrays with
// C++ without copying them. All the logic lives in core/vecenv.cpp; this file
// only describes how Python sees it.
#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/string.h>  // lets Python str convert to std::string

#include <new>        // placement new, see __init__ below
#include <stdexcept>  // std::invalid_argument (nanobind turns it into a Python ValueError)

#include "../../core/vecenv.h"

namespace nb = nanobind;

// nanobind checks dtype, shape and memory layout of incoming arrays, and
// raises TypeError on a mismatch. c_contig = one plain block of memory in
// row-major order, which is NumPy's default layout.
using ActionArray = nb::ndarray<const int8_t, nb::shape<-1, 2, 3>, nb::c_contig, nb::device::cpu>;
using HashArray = nb::ndarray<uint64_t, nb::shape<-1>, nb::c_contig, nb::device::cpu>;

// Wraps one of VecEnv's buffers as a NumPy array *view*: no copy. Returned with
// rv_policy::reference_internal, which tells Python "this array borrows memory
// from the env", so the env can't be garbage-collected while the array lives.
template <typename T, typename... Dims>
static nb::ndarray<nb::numpy, T> view(std::vector<T>& buf, Dims... dims) {
    size_t shape[] = {size_t(dims)...};
    return nb::ndarray<nb::numpy, T>(buf.data(), sizeof...(dims), shape, nb::handle());
}

NB_MODULE(fightcore, m) {
    m.attr("OBS_DIM") = OBS_DIM;
    m.attr("OBS_FIGHTER") = OBS_FIGHTER;
    const GameConfig defaults;  // stage sizes, so Python never hard-codes them
    m.attr("BLAST_X") = defaults.blast_x;
    m.attr("BLAST_TOP") = defaults.blast_top;
    m.attr("STAGE_HALF_WIDTH") = defaults.stage_half_width;
    nb::list names;
    for (const char* n : MOVE_NAMES) names.append(n);
    m.attr("MOVE_NAMES") = nb::tuple(names);
    m.attr("NUM_STATES") = (int)NUM_STATES;
    m.attr("ST_HITSTUN") = (int)ST_HITSTUN;
    m.attr("ST_SPOTDODGE") = (int)ST_SPOTDODGE;
    m.attr("ST_AIRDODGE") = (int)ST_AIRDODGE;
    m.attr("ST_HELPLESS") = (int)ST_HELPLESS;
    m.attr("ST_LANDING") = (int)ST_LANDING;
    nb::list stat_names;
    for (const char* n : STAT_NAMES) stat_names.append(n);
    m.attr("STAT_NAMES") = nb::tuple(stat_names);
    nb::list states;
    for (const char* n : STATE_NAMES) states.append(n);
    m.attr("STATE_NAMES") = nb::tuple(states);

    nb::class_<VecEnv>(m, "VecEnv")
        // A custom __init__: nanobind hands us raw memory for the object and
        // we build the VecEnv inside it with "placement new" (new (ptr) T(...)).
        .def("__init__",
             [](VecEnv* self, int num_envs, const std::string& moves_dir, int frame_skip, int reaction_delay,
                float damage_reward, float timeout_reward, float combo_growth, float combo_cap) {
                 GameConfig cfg;
                 std::string err = load_moves(cfg, moves_dir);
                 if (!err.empty()) throw std::invalid_argument(err);
                 new (self) VecEnv(num_envs, cfg,
                                   EnvParams{frame_skip, reaction_delay, damage_reward, timeout_reward, combo_growth, combo_cap});
             },
             nb::arg("num_envs"), nb::arg("moves_dir"), nb::arg("frame_skip") = 4, nb::arg("reaction_delay") = 0,
             nb::arg("damage_reward") = 0.01f, nb::arg("timeout_reward") = 0.0f, nb::arg("combo_growth") = 1.0f,
             nb::arg("combo_cap") = 1.0f)

        .def("step",
             [](VecEnv& e, ActionArray actions) {
                 if (actions.shape(0) != size_t(e.num_envs))
                     throw std::invalid_argument("actions must have shape (num_envs, 2, 3)");
                 e.step(actions.data());
             },
             nb::arg("actions"), "Advance every game by frame_skip frames. Outputs land in the buffers below.")
        .def("reset", &VecEnv::reset_all)

        // The output buffers. Grab them once; step() updates them in place.
        .def_prop_ro("obs", [](VecEnv& e) { return view(e.obs, e.num_envs, 2, OBS_DIM); },
                     nb::rv_policy::reference_internal)
        .def_prop_ro("final_obs", [](VecEnv& e) { return view(e.final_obs, e.num_envs, 2, OBS_DIM); },
                     nb::rv_policy::reference_internal)
        .def_prop_ro("reward", [](VecEnv& e) { return view(e.reward, e.num_envs, 2); },
                     nb::rv_policy::reference_internal)
        .def_prop_ro("terminated", [](VecEnv& e) { return view(e.terminated, e.num_envs); },
                     nb::rv_policy::reference_internal)
        .def_prop_ro("truncated", [](VecEnv& e) { return view(e.truncated, e.num_envs); },
                     nb::rv_policy::reference_internal)
        .def_prop_ro("winner", [](VecEnv& e) { return view(e.winner, e.num_envs); },
                     nb::rv_policy::reference_internal)
        .def_prop_ro("final_hash", [](VecEnv& e) { return view(e.final_hash, e.num_envs); },
                     nb::rv_policy::reference_internal)
        .def_prop_ro("move_uses", [](VecEnv& e) { return view(e.move_uses, NUM_MOVES); },
                     nb::rv_policy::reference_internal)
        .def_prop_ro("move_hits", [](VecEnv& e) { return view(e.move_hits, NUM_MOVES); },
                     nb::rv_policy::reference_internal)
        .def_prop_ro("stats", [](VecEnv& e) { return view(e.stats, NUM_STATS); },
                     nb::rv_policy::reference_internal)
        .def_prop_ro("num_envs", [](const VecEnv& e) { return e.num_envs; })
        // Settable, so training can anneal it.
        .def_prop_rw("damage_reward", [](const VecEnv& e) { return e.params.damage_reward; },
                     [](VecEnv& e, float v) { e.params.damage_reward = v; })

        // Debug helpers, used by the tests. Not for the training loop.
        .def("hashes",
             [](const VecEnv& e, HashArray out) {
                 if (out.shape(0) != size_t(e.num_envs)) throw std::invalid_argument("out must have shape (num_envs,)");
                 for (int i = 0; i < e.num_envs; ++i) out.data()[i] = hash_state(e.games[i]);
             },
             nb::arg("out"))
        .def("fighter",
             [](const VecEnv& e, int env, int player) {
                 if (env < 0 || env >= e.num_envs || player < 0 || player > 1)
                     throw std::out_of_range("bad env or player index");
                 const Fighter& f = e.games[env].f[player];
                 nb::dict d;
                 d["x"] = f.x;
                 d["y"] = f.y;
                 d["launch_vx"] = f.launch_vx;
                 d["launch_vy"] = f.launch_vy;
                 d["facing"] = (int)f.facing;
                 d["percent"] = f.percent;
                 d["state"] = (int)f.state;
                 d["move"] = (int)f.move;
                 d["hitstun"] = (int)f.hitstun;
                 d["stocks"] = (int)f.stocks;
                 d["invuln"] = (int)f.invuln;
                 d["intangible"] = intangible(f, e.cfg);
                 d["grounded"] = f.grounded;
                 d["fastfall"] = f.fastfall;
                 d["air_attack_locked"] = f.air_whiffs >= e.cfg.air_whiff_limit;
                 d["air_whiffs"] = (int)f.air_whiffs;
                 d["shield_hp"] = f.shield_hp;
                 d["hitlag"] = (int)f.hitlag;
                 d["surface"] = (int)f.surface;
                 d["landing_lag"] = (int)f.landing_lag;
                 d["combo"] = (int)f.combo;
                 d["vx"] = f.vx;
                 d["vy"] = f.vy;
                 d["move_hit"] = f.move_hit;
                 return d;
             },
             nb::arg("env"), nb::arg("player"))
        .def("set_percent",
             [](VecEnv& e, int env, int player, float percent) {
                 if (env < 0 || env >= e.num_envs || player < 0 || player > 1)
                     throw std::out_of_range("bad env or player index");
                 e.games[env].f[player].percent = percent;
             },
             nb::arg("env"), nb::arg("player"), nb::arg("percent"),
             "Test helper: set a fighter's damage % (to set up situations quickly).");
}
