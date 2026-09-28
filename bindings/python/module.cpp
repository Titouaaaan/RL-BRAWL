// The Python module `fightcore`, built with nanobind. nanobind generates the
// glue code that lets Python call C++ functions and hand NumPy arrays to C++
// without copying them.
#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>

#include <algorithm>  // std::clamp
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

    explicit VecEnv(int n) : games(n) {
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
                // Clamp at the Python/C++ boundary so a bad action can't make
                // anyone walk 100x faster.
                in[p].stick_x = std::clamp<int8_t>(a(i, p, 0), -1, 1);
                in[p].stick_y = std::clamp<int8_t>(a(i, p, 1), -1, 1);
                in[p].button = static_cast<uint8_t>(std::clamp<int8_t>(a(i, p, 2), 0, 3));
            }
            ::step(games[i], in, cfg);  // `::` = the free function from sim.h, not this method
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
};

NB_MODULE(fightcore, m) {
    nb::class_<VecEnv>(m, "VecEnv")
        .def(nb::init<int>(), nb::arg("num_envs"))
        .def("step", &VecEnv::step, nb::arg("actions"))
        .def("hashes", &VecEnv::hashes, nb::arg("out"))
        .def_prop_ro("num_envs", [](const VecEnv& e) { return e.games.size(); });
}
