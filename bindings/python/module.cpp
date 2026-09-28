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
    m.attr("ST_HITSTUN") = (int)ST_HITSTUN;

    nb::class_<VecEnv>(m, "VecEnv")
        // A custom __init__: nanobind hands us raw memory for the object and
        // we build the VecEnv inside it with "placement new" (new (ptr) T(...)).
        .def("__init__",
             [](VecEnv* self, int num_envs, const std::string& moves_dir, int frame_skip, int reaction_delay,
                float damage_reward) {
                 GameConfig cfg;
                 std::string err = load_moves(cfg, moves_dir);
                 if (!err.empty()) throw std::invalid_argument(err);
                 new (self) VecEnv(num_envs, cfg, EnvParams{frame_skip, reaction_delay, damage_reward});
             },
             nb::arg("num_envs"), nb::arg("moves_dir"), nb::arg("frame_skip") = 4, nb::arg("reaction_delay") = 0,
             nb::arg("damage_reward") = 0.01f)

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
        .def_prop_ro("num_envs", [](const VecEnv& e) { return e.num_envs; })

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
                 d["hitstun"] = (int)f.hitstun;
                 d["stocks"] = (int)f.stocks;
                 d["invuln"] = (int)f.invuln;
                 d["grounded"] = f.grounded;
                 return d;
             },
             nb::arg("env"), nb::arg("player"));
}
