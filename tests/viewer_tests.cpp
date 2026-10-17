#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "viewer/viewer_mesher.h"
#include "viewer/viewer_pose.h"
#include "viewer/viewer_world.h"

using namespace hytalevr::viewer;
using hytalevr::Matrix4;

namespace {
int g_failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}

bool near(float a, float b, float epsilon = 0.001f) { return std::fabs(a - b) < epsilon; }

// Mirrors plugin SectionEncoder.encode so the two stay in step.
std::string encode_section(const std::vector<int32_t>& ids) {
    std::vector<int32_t> palette;
    std::vector<uint32_t> indexes(ids.size());
    for (size_t i = 0; i < ids.size(); ++i) {
        size_t p = 0;
        while (p < palette.size() && palette[p] != ids[i]) ++p;
        if (p == palette.size()) palette.push_back(ids[i]);
        indexes[i] = static_cast<uint32_t>(p);
    }
    std::vector<uint8_t> bytes;
    auto varint = [&](uint32_t v) {
        while (v & ~0x7Fu) {
            bytes.push_back(static_cast<uint8_t>((v & 0x7F) | 0x80));
            v >>= 7;
        }
        bytes.push_back(static_cast<uint8_t>(v));
    };
    varint(static_cast<uint32_t>(palette.size()));
    for (int32_t id : palette) varint(static_cast<uint32_t>(id));
    size_t start = 0;
    while (start < indexes.size()) {
        size_t end = start + 1;
        while (end < indexes.size() && indexes[end] == indexes[start]) ++end;
        varint(static_cast<uint32_t>(end - start));
        varint(indexes[start]);
        start = end;
    }
    static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    size_t i = 0;
    for (; i + 2 < bytes.size(); i += 3) {
        const uint32_t n = (bytes[i] << 16) | (bytes[i + 1] << 8) | bytes[i + 2];
        out += table[(n >> 18) & 63];
        out += table[(n >> 12) & 63];
        out += table[(n >> 6) & 63];
        out += table[n & 63];
    }
    if (i + 1 == bytes.size()) {
        const uint32_t n = bytes[i] << 16;
        out += table[(n >> 18) & 63];
        out += table[(n >> 12) & 63];
        out += "==";
    } else if (i + 2 == bytes.size()) {
        const uint32_t n = (bytes[i] << 16) | (bytes[i + 1] << 8);
        out += table[(n >> 18) & 63];
        out += table[(n >> 12) & 63];
        out += table[(n >> 6) & 63];
        out += '=';
    }
    return out;
}

void test_protocol() {
    World world;
    check(world.apply("WORLD default") == World::Change::Reset, "WORLD resets");
    check(world.apply("PLAYER 1.5 64 -2.5 1.5 65.6 -2.5 0 0 -1") == World::Change::Player, "PLAYER parses");
    check(near(static_cast<float>(world.player().eye[1]), 65.6f), "eye height");
    check(world.apply("BLOCK 7|Rock_Stone|Cube|Solid|120,120,120|a.png|b.png|c.png|d.png|e.png|f.png") ==
              World::Change::Block, "BLOCK parses");
    const BlockInfo* stone = world.block(7);
    check(stone && stone->draw == DrawKind::Cube && stone->faces[kFaceWest] == "f.png", "BLOCK fields");
    check(world.apply("ENTITIES 12,1,2,3,-0.3,0,-0.3,0.3,1.8,0.3,90,Sheep;-4,5,6,7,-1,0,-1,1,1,1,0,") ==
              World::Change::Entities, "ENTITIES parses");
    check(world.entities().size() == 2 && world.entities()[0].model == "Sheep" &&
              world.entities()[1].id == -4, "ENTITIES fields");

    std::vector<int32_t> ids(kSectionCells, 0);
    for (int x = 0; x < 32; ++x)
        for (int z = 0; z < 32; ++z)
            for (int y = 0; y < 4; ++y) ids[cell_index(x, y, z)] = 7;
    ids[cell_index(5, 10, 5)] = 70000;  // multi-byte varint id
    SectionKey key;
    check(world.apply("SECTION 0 -1 2 " + encode_section(ids), &key) == World::Change::Section, "SECTION parses");
    check(key.x == 0 && key.y == -1 && key.z == 2, "SECTION key");
    check(world.block_at(3, -32 + 2, 64 + 3) == 7, "block_at inside ground");
    check(world.block_at(3, -32 + 5, 64 + 3) == 0, "block_at above ground");
    check(world.block_at(5, -32 + 10, 64 + 5) == 70000, "block_at large id");
    check(world.block_at(0, 0, 0) == -1, "block_at missing section");
    check(world.apply("SECTION 0 0 0 !!!") == World::Change::None, "bad SECTION rejected");
}

void test_mesher() {
    World world;
    world.apply("BLOCK 1|Stone|Cube|Solid|100,100,100||||||");
    world.apply("BLOCK 2|Glass|Cube|Transparent|200,200,200||||||");
    std::vector<int32_t> ids(kSectionCells, 0);
    ids[cell_index(1, 1, 1)] = 1;
    world.apply("SECTION 0 0 0 " + encode_section(ids));
    MaterialFn material = [](int32_t, const BlockInfo&, int) { return FaceMaterial{}; };
    SectionMesh mesh;
    build_section_mesh(world, {0, 0, 0}, material, mesh);
    check(mesh.opaque.size() == 36, "lone block has 6 faces");

    // Two stacked blocks hide the faces between them.
    ids[cell_index(1, 2, 1)] = 1;
    world.apply("SECTION 0 0 0 " + encode_section(ids));
    build_section_mesh(world, {0, 0, 0}, material, mesh);
    check(mesh.opaque.size() == 60, "stacked blocks share a hidden face");

    // Glass next to glass hides the shared face; glass never hides stone.
    ids[cell_index(10, 1, 1)] = 2;
    ids[cell_index(11, 1, 1)] = 2;
    world.apply("SECTION 0 0 0 " + encode_section(ids));
    build_section_mesh(world, {0, 0, 0}, material, mesh);
    check(mesh.translucent.size() == 60, "glass pair");
    check(mesh.opaque.size() == 60, "stone unchanged");

    // Every face winds counter-clockwise seen from outside.
    for (size_t i = 0; i + 2 < mesh.opaque.size(); i += 3) {
        const float* a = mesh.opaque[i].position;
        const float* b = mesh.opaque[i + 1].position;
        const float* c = mesh.opaque[i + 2].position;
        const float u[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
        const float v[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        const float n[3] = {u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
        const float centre[3] = {(a[0] + b[0] + c[0]) / 3 - 1.5f, (a[1] + b[1] + c[1]) / 3,
                                 (a[2] + b[2] + c[2]) / 3 - 1.5f};
        // Stone column spans y 1..3; its centre is (1.5, 2, 1.5).
        const float outward[3] = {centre[0], centre[1] - 2.0f, centre[2]};
        const float dot = n[0] * outward[0] + n[1] * outward[1] + n[2] * outward[2];
        if (dot <= 0.0f) {
            check(false, "face winding faces outward");
            break;
        }
    }

    // A block in a corner gets darker corners on the floor beside it.
    std::vector<int32_t> floor(kSectionCells, 0);
    for (int x = 0; x < 4; ++x)
        for (int z = 0; z < 4; ++z) floor[cell_index(x, 0, z)] = 1;
    floor[cell_index(1, 1, 1)] = 1;
    world.apply("SECTION 0 0 0 " + encode_section(floor));
    build_section_mesh(world, {0, 0, 0}, material, mesh);
    bool found_dark = false;
    for (const MeshVertex& v : mesh.opaque) {
        if (v.position[1] == 1.0f && v.color[0] < 255 && v.position[0] == 1.0f && v.position[2] == 1.0f) {
            found_dark = true;
        }
    }
    check(found_dark, "ambient occlusion darkens corner");
}

void test_pose() {
    // Headset at (0.5, 1.7, 0.2) looking along -X (Hytale yaw +90 deg).
    float hmd[12] = {0, 0, 1, 0.5f,
                     0, 1, 0, 1.7f,
                     -1, 0, 0, 0.2f};
    TrackingAnchor anchor;
    recenter(anchor, hmd, 0.0f);  // Hytale faces -Z
    const float feet[3] = {10.0f, 64.0f, -3.0f};
    Matrix4 world = world_from_tracking(anchor, feet);
    const float head_tracking[3] = {0.5f, 1.7f, 0.2f};
    float head_world[3];
    transform_point(world, head_tracking, head_world);
    check(near(head_world[0], 10.0f) && near(head_world[1], 65.7f) && near(head_world[2], -3.0f),
          "recenter puts head over feet at real height");
    float forward[3];
    pose_forward(hmd, forward);
    float forward_world[3];
    transform_direction(world, forward, forward_world);
    check(near(forward_world[0], 0.0f) && near(forward_world[2], -1.0f), "recenter faces Hytale's way");

    // Snap turning right keeps the head in place.
    turn_about_head(anchor, -kPi / 6.0f, hmd);
    world = world_from_tracking(anchor, feet);
    transform_point(world, head_tracking, head_world);
    check(near(head_world[0], 10.0f) && near(head_world[2], -3.0f), "turn pivots on head");
    transform_direction(world, forward, forward_world);
    check(near(hytale_yaw(forward_world[0], forward_world[2]), -kPi / 6.0f), "turn right lowers yaw");

    // Leaning 0.3 m forward moves the view 0.3 m forward in the world.
    float lean[12];
    std::copy(hmd, hmd + 12, lean);
    lean[3] -= 0.3f;  // -X is forward for this headset
    float lean_position[3];
    pose_position(lean, lean_position);
    float lean_world[3];
    transform_point(world, lean_position, lean_world);
    const float moved = std::sqrt((lean_world[0] - head_world[0]) * (lean_world[0] - head_world[0]) +
                                  (lean_world[2] - head_world[2]) * (lean_world[2] - head_world[2]));
    check(near(moved, 0.3f), "room-scale lean is 1:1");
    check(near(dashboard_yaw(0, -1), 0.0f) && near(dashboard_yaw(1, 0), kPi / 2.0f), "dashboard yaw");

    PositionSmoother smoother(50.0);
    const double a[3] = {0, 0, 0};
    const double b[3] = {1, 0, 0};
    smoother.add(0.0, a);
    smoother.add(50.0, b);
    double out[3];
    smoother.sample(75.0, out);
    check(std::fabs(out[0] - 0.5) < 1e-6, "smoother interpolates");
    const double far[3] = {100, 0, 0};
    smoother.add(100.0, far);
    smoother.sample(100.0, out);
    check(out[0] == 100.0, "smoother snaps on teleport");
}
} // namespace

int main() {
    test_protocol();
    test_mesher();
    test_pose();
    if (g_failures == 0) std::printf("viewer tests passed\n");
    return g_failures == 0 ? 0 : 1;
}
