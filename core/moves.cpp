// Loads move frame data from data/moves/*.json into GameConfig::moves.
//
// This is kept out of sim.cpp because the JSON library is one ~25,000-line
// header: only this file pays its compile time.
//
// We never let the JSON library throw an exception. The Godot build turns C++
// exceptions off, and there a throw would crash the game, so a typo in a JSON
// file would kill Godot. Instead every field is checked by hand and problems
// come back as an error message.
#include <fstream>
#include <iterator>  // std::istreambuf_iterator

#include "../extern/nlohmann/json.hpp"
#include "sim.h"

using json = nlohmann::json;

static const char* MOVE_FILES[NUM_MOVES] = {"jab.json", "heavy.json", "aerial.json"};

static bool read_float(const json& obj, const char* key, float& out, std::string& err) {
    auto it = obj.find(key);  // find() returns end() if missing (or if obj isn't an object)
    if (it == obj.end() || !it->is_number()) {
        err = std::string("missing field \"") + key + "\" (must be a number)";
        return false;
    }
    out = it->get<float>();
    return true;
}

static bool read_int(const json& obj, const char* key, int& out, std::string& err) {
    auto it = obj.find(key);
    if (it == obj.end() || !it->is_number_integer()) {
        err = std::string("missing field \"") + key + "\" (must be a whole number)";
        return false;
    }
    out = it->get<int>();
    return true;
}

// Parses one move from the text of its JSON file.
static std::string parse_move(const std::string& text, MoveDef& out) {
    // allow_exceptions = false: on a syntax error we get a "discarded" value
    // instead of an exception. ignore_comments = true: // comments are allowed.
    json j = json::parse(text, nullptr, /*allow_exceptions=*/false, /*ignore_comments=*/true);
    if (j.is_discarded()) return "not valid JSON (check commas and brackets)";

    MoveDef m;
    std::string err;
    if (!read_int(j, "total_frames", m.total_frames, err)) return err;
    if (!read_int(j, "landing_lag", m.landing_lag, err)) return err;
    if (m.total_frames < 1) return "total_frames must be at least 1";

    auto list = j.find("hitboxes");
    if (list == j.end() || !list->is_array()) return "missing \"hitboxes\" list";
    if (list->size() > MAX_HITBOXES_PER_MOVE) return "too many hitboxes (max 4)";

    for (size_t i = 0; i < list->size(); ++i) {
        const json& h = (*list)[i];
        HitboxDef hb;
        bool ok = read_int(h, "start", hb.start, err) && read_int(h, "end", hb.end, err) &&
                  read_float(h, "x", hb.x, err) && read_float(h, "y", hb.y, err) &&
                  read_float(h, "radius", hb.radius, err) && read_float(h, "damage", hb.damage, err) &&
                  read_float(h, "base_kb", hb.base_kb, err) && read_float(h, "kb_growth", hb.kb_growth, err) &&
                  read_float(h, "angle", hb.angle, err);
        std::string where = "hitbox " + std::to_string(i + 1) + ": ";
        if (!ok) return where + err;
        if (hb.start < 1 || hb.end < hb.start || hb.end > m.total_frames)
            return where + "needs 1 <= start <= end <= total_frames";
        if (hb.radius <= 0) return where + "radius must be positive";
        m.hitboxes.push_back(hb);
    }
    out = m;
    return "";
}

std::string load_moves_from_text(GameConfig& cfg, const std::string texts[NUM_MOVES]) {
    for (int m = 0; m < NUM_MOVES; ++m) {
        std::string err = parse_move(texts[m], cfg.moves[m]);
        if (!err.empty()) return std::string(MOVE_FILES[m]) + ": " + err;
    }
    return "";
}

std::string load_moves(GameConfig& cfg, const std::string& dir) {
    std::string texts[NUM_MOVES];
    for (int m = 0; m < NUM_MOVES; ++m) {
        std::string path = dir + "/" + MOVE_FILES[m];
        std::ifstream file(path);
        if (!file) return path + ": cannot open file";
        // Read the whole file into a string: istreambuf_iterator walks it char by char.
        texts[m].assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }
    std::string err = load_moves_from_text(cfg, texts);
    return err.empty() ? "" : dir + "/" + err;
}
