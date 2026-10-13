// net.h: a trained agent's policy network (the actor) run in C++, so the game
// plays against network bots without Python or PyTorch (and can run in a
// browser). The weights come from tools/export_bot.py (ppo.py save_bot).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The actor MLP of train/ppo.py: OBS_DIM -> hidden -> hidden -> NET_ACTIONS,
// tanh between layers. Action index a = (stick_x+1)*15 + (stick_y+1)*5 + button,
// the order of ppo.py's ACTIONS table.
const int NET_ACTIONS = 45;

struct Net {
    int hidden = 0;
    std::vector<float> w;  // per layer: weight (out x in, row-major like PyTorch), then bias
};

// Reads an exported .bin from memory (Godot reads the file itself, also inside
// a web export). Returns "" on success, otherwise the error text.
std::string load_net(Net& net, const uint8_t* data, size_t size);
void net_logits(const Net& net, const float* obs, float* logits);  // logits[NET_ACTIONS]
// Samples an action index like torch's Categorical(logits=...): u is uniform in [0, 1).
int net_act(const Net& net, const float* obs, float u);
