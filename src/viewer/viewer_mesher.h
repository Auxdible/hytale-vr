#pragma once

// Turns one 32x32x32 section into triangles: only faces that border
// something see-through, with per-corner ambient occlusion and fixed
// per-direction shading so edges and corners read clearly in a headset.

#include <cstdint>
#include <functional>
#include <vector>

#include "viewer/viewer_world.h"

namespace hytalevr::viewer {

struct MeshVertex {
    float position[3];  // relative to the section's lowest corner
    float uv[2];
    float layer;         // texture-array layer
    uint8_t color[4];    // tint * shade
};

struct FaceMaterial {
    float layer = 0.0f;
    float tint[3] = {1.0f, 1.0f, 1.0f};
};

using MaterialFn = std::function<FaceMaterial(int32_t id, const BlockInfo& info, int face)>;

struct SectionMesh {
    std::vector<MeshVertex> opaque;       // solid and cutout (alpha-tested)
    std::vector<MeshVertex> translucent;  // blended, drawn after opaque
};

// Whether a block hides the faces of the blocks touching it.
bool block_occludes(const BlockInfo* info);

void build_section_mesh(const World& world, const SectionKey& key,
                        const MaterialFn& material, SectionMesh& mesh);

} // namespace hytalevr::viewer
