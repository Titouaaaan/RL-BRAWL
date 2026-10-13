#include "net.h"

#include <cmath>
#include <cstring>  // std::memcpy

#include "obs.h"  // OBS_DIM

// One layer: y = W x + b, then tanh if `act`. Returns where the next layer's weights start.
static const float* layer(const float* p, const float* x, int in, int out, float* y, bool act) {
    for (int o = 0; o < out; ++o) {
        float s = p[out * in + o];  // the bias, stored after the weights
        for (int i = 0; i < in; ++i) s += p[o * in + i] * x[i];
        y[o] = act ? std::tanh(s) : s;
    }
    return p + out * in + out;
}

std::string load_net(Net& net, const uint8_t* data, size_t size) {
    int32_t head[3];  // obs size, hidden size, actions (little-endian, like every platform we run on)
    if (size < sizeof head) return "not a bot file";
    std::memcpy(head, data, sizeof head);  // memcpy: the bytes may not be aligned for an int32 read
    if (head[0] != OBS_DIM || head[2] != NET_ACTIONS)
        return "this bot sees " + std::to_string(head[0]) + " inputs, the game gives " + std::to_string(OBS_DIM) +
               ": it was trained on another game version";
    size_t h = head[1], n = h * OBS_DIM + h + h * h + h + NET_ACTIONS * h + NET_ACTIONS;
    if (head[1] <= 0 || size != sizeof head + n * sizeof(float)) return "bot file has the wrong size";
    net.hidden = head[1];
    net.w.resize(n);
    std::memcpy(net.w.data(), data + sizeof head, n * sizeof(float));
    return "";
}

void net_logits(const Net& net, const float* obs, float* logits) {
    std::vector<float> a(net.hidden), b(net.hidden);
    const float* p = layer(net.w.data(), obs, OBS_DIM, net.hidden, a.data(), true);
    p = layer(p, a.data(), net.hidden, net.hidden, b.data(), true);
    layer(p, b.data(), net.hidden, NET_ACTIONS, logits, false);
}

int net_act(const Net& net, const float* obs, float u) {
    float l[NET_ACTIONS], top = -INFINITY, sum = 0;
    net_logits(net, obs, l);
    for (float v : l) top = std::fmax(top, v);
    for (float& v : l) sum += v = std::exp(v - top);  // softmax, unnormalized (minus the max: no overflow)
    float t = u * sum;
    for (int k = 0; k < NET_ACTIONS; ++k)
        if ((t -= l[k]) < 0) return k;
    return NET_ACTIONS - 1;  // u rounded up to the very end
}
