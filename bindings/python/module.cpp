// The Python module `fightcore`, built with nanobind. nanobind generates the
// glue code that lets Python call C++ functions and hand NumPy arrays to C++
// without copying them.
#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/string.h>  // lets Python str convert to std::string

#include <stdexcept>  // std::invalid_argument (nanobind turns it into a Python ValueError)
#include <vector>

#include "../../core/sim.h"

namespace nb = nanobind;

// These aliases describe which NumPy arrays each function accepts. nanobind
// checks dtype, shape and memory layout at the call, and raises TypeError on
// a mismatch. c_contig means one plain block of memory in row-major order,
// which is NumPy's default layout.
using ActionArray = nb::ndarray<const int8_t, nb::shape<-1, 2, 3>, nb::c_contig, nb::device::cpu>;
using HashArray = nb::ndarray<uint64_t, nb::shape<-1>, nb::c_contig, nb::device::cpu>;

// N independent games, all advanced by one C++ call so that Python never loops
// over environments.
struct VecEnv {
    GameConfig cfg;
    std::vector<GameState> games;

    VecEnv(int n, const std::string& moves_dir) : games(n) {
        std::string err = load_moves(cfg, moves_dir);
        if (!err.empty()) throw std::invalid_argument(err);
        for (GameState& g : games) reset(g, cfg);
    }

    // actions[i, p] = (stick_x, stick_y, button) for game i, player p.
    void step(ActionArray actions) {
        if (actions.shape(0) != games.size())
            throw std::invalid_argument("actions must have shape (num_envs, 2, 3)");
        auto a = actions.view();  // gives fast a(i, p, k) indexing
        for (size_t i = 0; i < games.size(); ++i) {
            Input in[2];
            for (int p = 0; p < 2; ++p) {
                // Out-of-range values are clamped inside step() itself.
                in[p].stick_x = a(i, p, 0);
                in[p].stick_y = a(i, p, 1);
                in[p].button = static_cast<uint8_t>(a(i, p, 2));
            }
            StepEvents ev;  // ponytail: events discarded until phase 2 turns them into rewards
            ::step(games[i], in, cfg, ev);  // `::` = the free function from sim.h, not this method
        }
    }

    // Writes each game's state hash into `out`, a NumPy array the caller
    // allocated beforehand. No new array is created: C++ writes straight into
    // NumPy's memory. Observations will work the same way.
    void hashes(HashArray out) const {
        if (out.shape(0) != games.size())
            throw std::invalid_argument("out must have shape (num_envs,)");
        uint64_t* dst = out.data();
        for (size_t i = 0; i < games.size(); ++i) dst[i] = hash_state(games[i]);
    }

    // One fighter's fields as a dict. For tests and debugging, not for the
    // training loop (building a dict per call is slow).
    nb::dict fighter(size_t env, int player) const {
        if (env >= games.size() || player < 0 || player > 1) throw std::out_of_range("bad env or player index");
        const Fighter& f = games[env].f[player];
        nb::dict d;
        d["x"] = f.x;
        d["y"] = f.y;
        d["launch_vx"] = f.launch_vx;
        d["launch_vy"] = f.launch_vy;
        d["percent"] = f.percent;
        d["state"] = (int)f.state;
        d["hitstun"] = (int)f.hitstun;
        d["stocks"] = (int)f.stocks;
        d["invuln"] = (int)f.invuln;
        d["grounded"] = f.grounded;
        return d;
    }
};

NB_MODULE(fightcore, m) {
    m.attr("ST_HITSTUN") = (int)ST_HITSTUN;
    nb::class_<VecEnv>(m, "VecEnv")
        .def(nb::init<int, const std::string&>(), nb::arg("num_envs"), nb::arg("moves_dir"))
        .def("step", &VecEnv::step, nb::arg("actions"))
        .def("hashes", &VecEnv::hashes, nb::arg("out"))
        .def("fighter", &VecEnv::fighter, nb::arg("env"), nb::arg("player"))
        .def_prop_ro("num_envs", [](const VecEnv& e) { return e.games.size(); });
}
