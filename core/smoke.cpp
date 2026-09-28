// A small native program that runs the sim with random inputs twice and checks
// that both runs end in the same state. Why keep it when pytest checks the
// same thing? Because this one is built in Debug with AddressSanitizer, so any
// memory bug in the core crashes here with a readable report. Getting ASan to
// work inside python.exe is much more painful.
#include "vecenv.h"

#include <cstdio>
#include <random>

static GameConfig cfg;

static uint64_t run(int frames) {
    std::mt19937 rng(1234);  // fixed seed, so both runs see the same inputs
    std::uniform_int_distribution<int> stick(-1, 1), button(0, 3);
    GameState s;
    reset(s, cfg);
    for (int t = 0; t < frames; ++t) {
        Input in[2];
        for (Input& i : in) {
            i.stick_x = static_cast<int8_t>(stick(rng));
            i.stick_y = static_cast<int8_t>(stick(rng));
            i.button = static_cast<uint8_t>(button(rng));
        }
        StepEvents ev;
        step(s, in, cfg, ev);
        if (s.over) reset(s, cfg);  // keep playing new games
    }
    return hash_state(s);
}

// Run from the repo root, or pass the moves folder as the first argument.
int main(int argc, char** argv) {
    std::string err = load_moves(cfg, argc > 1 ? argv[1] : "data/moves");
    if (!err.empty()) {
        std::printf("FAIL: %s\n", err.c_str());
        return 1;
    }
    uint64_t a = run(100000), b = run(100000);
    std::printf("run 1: %016llx\nrun 2: %016llx\n", (unsigned long long)a, (unsigned long long)b);
    if (a != b) {
        std::printf("FAIL: not deterministic\n");
        return 1;
    }
    // Also run the RL layer (observations are the most index-heavy code, so
    // the likeliest place for an out-of-bounds bug) with delay and frame skip.
    VecEnv env(16, cfg, EnvParams{4, 3, 0.01f});
    std::mt19937 rng(7);
    std::uniform_int_distribution<int> stick(-1, 1), button(0, 3);
    std::vector<int8_t> actions(16 * 2 * 3);
    for (int t = 0; t < 20000; ++t) {
        for (size_t k = 0; k < actions.size(); ++k) actions[k] = int8_t(k % 3 == 2 ? button(rng) : stick(rng));
        env.step(actions.data());
    }
    std::printf("OK\n");
    return 0;
}
