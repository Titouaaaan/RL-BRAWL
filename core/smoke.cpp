// A small native program that runs the sim with random inputs twice and checks
// that both runs end in the same state. Why keep it when pytest checks the
// same thing? Because this one is built in Debug with AddressSanitizer, so any
// memory bug in the core crashes here with a readable report. Getting ASan to
// work inside python.exe is much more painful.
#include "sim.h"

#include <cstdio>
#include <random>

static uint64_t run(int frames) {
    std::mt19937 rng(1234);  // fixed seed, so both runs see the same inputs
    std::uniform_int_distribution<int> stick(-1, 1), button(0, 3);
    GameConfig cfg;
    GameState s;
    reset(s, cfg);
    for (int t = 0; t < frames; ++t) {
        Input in[2];
        for (Input& i : in) {
            i.stick_x = static_cast<int8_t>(stick(rng));
            i.stick_y = static_cast<int8_t>(stick(rng));
            i.button = static_cast<uint8_t>(button(rng));
        }
        step(s, in, cfg);
    }
    return hash_state(s);
}

int main() {
    uint64_t a = run(100000), b = run(100000);
    std::printf("run 1: %016llx\nrun 2: %016llx\n", (unsigned long long)a, (unsigned long long)b);
    if (a != b) {
        std::printf("FAIL: not deterministic\n");
        return 1;
    }
    std::printf("OK\n");
    return 0;
}
