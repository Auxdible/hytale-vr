#include "viewer/viewer_mesher.h"

#include <algorithm>

namespace hytalevr::viewer {
namespace {

struct FaceShape {
    int normal[3];
    // Corners in counter-clockwise order seen from outside, as 0/1 offsets.
    int corners[4][3];
    float uv[4][2];
    float shade;
};

constexpr FaceShape kFaces[kFaceCount] = {
    // Up (+Y)
    {{0, 1, 0}, {{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0}},
     {{0, 1}, {1, 1}, {1, 0}, {0, 0}}, 1.00f},
    // Down (-Y)
    {{0, -1, 0}, {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}},
     {{0, 0}, {1, 0}, {1, 1}, {0, 1}}, 0.55f},
    // North (-Z)
    {{0, 0, -1}, {{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}},
     {{0, 1}, {1, 1}, {1, 0}, {0, 0}}, 0.80f},
    // South (+Z)
    {{0, 0, 1}, {{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}},
     {{0, 1}, {1, 1}, {1, 0}, {0, 0}}, 0.80f},
    // East (+X)
    {{1, 0, 0}, {{1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}},
     {{0, 1}, {1, 1}, {1, 0}, {0, 0}}, 0.68f},
    // West (-X)
    {{-1, 0, 0}, {{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}},
     {{0, 1}, {1, 1}, {1, 0}, {0, 0}}, 0.68f},
};

constexpr float kAoBrightness[4] = {0.50f, 0.68f, 0.84f, 1.00f};

bool is_translucent(const BlockInfo& info) {
    return info.opacity == BlockOpacity::Semitransparent ||
           info.opacity == BlockOpacity::Transparent;
}

const BlockInfo* drawable(const World& world, int32_t id) {
    if (id <= 0) return nullptr;
    const BlockInfo* info = world.block(id);
    return info && info->draw == DrawKind::Cube ? info : nullptr;
}

} // namespace

bool block_occludes(const BlockInfo* info) {
    return info && info->draw == DrawKind::Cube && info->opacity == BlockOpacity::Solid;
}

void build_section_mesh(const World& world, const SectionKey& key,
                        const MaterialFn& material, SectionMesh& mesh) {
    mesh.opaque.clear();
    mesh.translucent.clear();
    const std::vector<int32_t>* cells = world.section(key);
    if (!cells) return;

    const int base_x = key.x * kSectionSize;
    const int base_y = key.y * kSectionSize;
    const int base_z = key.z * kSectionSize;
    // Neighbours inside the section are read directly; only the border needs
    // the world lookup.
    auto id_at = [&](int x, int y, int z) -> int32_t {
        if (x >= 0 && x < kSectionSize && y >= 0 && y < kSectionSize && z >= 0 && z < kSectionSize) {
            return (*cells)[static_cast<size_t>(cell_index(x, y, z))];
        }
        return world.block_at(base_x + x, base_y + y, base_z + z);
    };
    auto occludes_at = [&](int x, int y, int z) {
        const int32_t id = id_at(x, y, z);
        return id > 0 && block_occludes(world.block(id));
    };

    for (int y = 0; y < kSectionSize; ++y) {
        for (int z = 0; z < kSectionSize; ++z) {
            for (int x = 0; x < kSectionSize; ++x) {
                const int32_t id = (*cells)[static_cast<size_t>(cell_index(x, y, z))];
                const BlockInfo* info = drawable(world, id);
                if (!info) continue;
                const bool translucent = is_translucent(*info);
                std::vector<MeshVertex>& out = translucent ? mesh.translucent : mesh.opaque;

                for (int face = 0; face < kFaceCount; ++face) {
                    const FaceShape& shape = kFaces[face];
                    const int nx = x + shape.normal[0];
                    const int ny = y + shape.normal[1];
                    const int nz = z + shape.normal[2];
                    const int32_t neighbour_id = id_at(nx, ny, nz);
                    const BlockInfo* neighbour = drawable(world, neighbour_id);
                    if (block_occludes(neighbour)) continue;
                    if (neighbour && translucent && neighbour_id == id) continue;

                    const FaceMaterial mat = material(id, *info, face);
                    float ao[4];
                    for (int corner = 0; corner < 4; ++corner) {
                        // The two in-plane directions pointing away from the
                        // block's centre at this corner.
                        int side_a[3] = {0, 0, 0};
                        int side_b[3] = {0, 0, 0};
                        int filled = 0;
                        for (int axis = 0; axis < 3; ++axis) {
                            if (shape.normal[axis] != 0) continue;
                            const int direction = shape.corners[corner][axis] == 1 ? 1 : -1;
                            (filled == 0 ? side_a : side_b)[axis] = direction;
                            ++filled;
                        }
                        const bool a = occludes_at(nx + side_a[0], ny + side_a[1], nz + side_a[2]);
                        const bool b = occludes_at(nx + side_b[0], ny + side_b[1], nz + side_b[2]);
                        const bool c = occludes_at(nx + side_a[0] + side_b[0], ny + side_a[1] + side_b[1],
                                                   nz + side_a[2] + side_b[2]);
                        const int level = a && b ? 0 : 3 - (static_cast<int>(a) + static_cast<int>(b) +
                                                            static_cast<int>(c));
                        ao[corner] = kAoBrightness[level];
                    }

                    MeshVertex vertices[4];
                    for (int corner = 0; corner < 4; ++corner) {
                        MeshVertex& v = vertices[corner];
                        v.position[0] = static_cast<float>(x + shape.corners[corner][0]);
                        v.position[1] = static_cast<float>(y + shape.corners[corner][1]);
                        v.position[2] = static_cast<float>(z + shape.corners[corner][2]);
                        v.uv[0] = shape.uv[corner][0];
                        v.uv[1] = shape.uv[corner][1];
                        v.layer = mat.layer;
                        const float light = shape.shade * ao[corner];
                        for (int channel = 0; channel < 3; ++channel) {
                            v.color[channel] = static_cast<uint8_t>(
                                std::clamp(mat.tint[channel] * light * 255.0f, 0.0f, 255.0f));
                        }
                        v.color[3] = 255;
                    }
                    // Split along the brighter diagonal so AO gradients do not
                    // show a visible seam.
                    if (ao[0] + ao[2] >= ao[1] + ao[3]) {
                        const int order[6] = {0, 1, 2, 0, 2, 3};
                        for (int i : order) out.push_back(vertices[i]);
                    } else {
                        const int order[6] = {1, 2, 3, 1, 3, 0};
                        for (int i : order) out.push_back(vertices[i]);
                    }
                }
            }
        }
    }
}

} // namespace hytalevr::viewer
