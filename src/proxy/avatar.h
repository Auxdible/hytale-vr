// Your character's forearms and hands, built from Hytale's own files: the
// player model in Assets.zip and the clothes on its arms, colored with the skin saved in
// %APPDATA%\Hytale\UserData\CachedPlayerSkins. Loads on a background thread.
#pragma once

#include <cstdint>
#include <vector>

namespace hvr::avatar {

struct Vertex {
    float x, y, z, u, v;
};

// One arm in its hand's frame, in model units (32 per block): the palm at the
// origin, the arm running back along +Z, the back of the hand up (+Y) when the
// thumb points forward.
struct Arm {
    std::vector<Vertex> vertices;  // clothes first, then skin
    // Where the hand and its item point sit in the arm's frame: column-major
    // matrices from each part's own axes (model units) into the arm's.
    bool has_hand = false, has_attachment = false;
    float hand[16] = {}, attachment[16] = {};
};

struct Model {
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> rgba;  // top row first
    Arm arms[2];                // 0 left, 1 right
};

void start_loading();
// Null until loading finished, and for good if it failed.
const Model* ready();

}  // namespace hvr::avatar
