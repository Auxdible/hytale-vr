#pragma once

// The VR viewer's copy of the world around the player, built from the lines
// the HytaleVR server plugin streams (see plugin WorldStreamer.java).

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace hytalevr::viewer {

constexpr int kSectionSize = 32;
constexpr int kSectionCells = kSectionSize * kSectionSize * kSectionSize;

inline int cell_index(int x, int y, int z) { return (y << 10) | (z << 5) | x; }

enum class DrawKind { Empty, Cube, Model };
enum class BlockOpacity { Solid, Semitransparent, Cutout, Transparent };
// Matches the order of the BLOCK line's face textures.
enum Face { kFaceUp = 0, kFaceDown, kFaceNorth, kFaceSouth, kFaceEast, kFaceWest, kFaceCount };

struct BlockInfo {
    std::string key;
    DrawKind draw = DrawKind::Empty;
    BlockOpacity opacity = BlockOpacity::Solid;
    uint8_t color[3] = {128, 128, 128};
    std::array<std::string, kFaceCount> faces;
};

struct PlayerSample {
    double feet[3]{};
    double eye[3]{};
    double look[3]{0.0, 0.0, -1.0};
};

struct EntityBox {
    int64_t id = 0;
    double position[3]{};
    double min[3]{};
    double max[3]{};
    float yaw = 0.0f;
    std::string model;
};

struct SectionKey {
    int x = 0;
    int y = 0;
    int z = 0;
    bool operator==(const SectionKey& other) const {
        return x == other.x && y == other.y && z == other.z;
    }
};

struct SectionKeyHash {
    size_t operator()(const SectionKey& key) const {
        return (static_cast<size_t>(static_cast<uint32_t>(key.x)) * 73856093u) ^
               (static_cast<size_t>(static_cast<uint32_t>(key.y)) * 19349663u) ^
               (static_cast<size_t>(static_cast<uint32_t>(key.z)) * 83492791u);
    }
};

inline int floor_div32(int value) { return value >> 5; }

// Decodes the plugin's SECTION payload: Base64 of a varint palette followed
// by varint (run length, palette index) pairs.
bool decode_section(std::string_view base64, std::vector<int32_t>& ids);

class World {
public:
    enum class Change { None, Reset, Player, Entities, Block, Section };

    // Applies one line from the plugin. When it stores a section, its key is
    // written to section.
    Change apply(std::string_view line, SectionKey* section = nullptr);

    const BlockInfo* block(int32_t id) const;
    // Block id at a world cell, or -1 when that section has not arrived.
    int32_t block_at(int x, int y, int z) const;
    const std::vector<int32_t>* section(const SectionKey& key) const;
    std::vector<SectionKey> section_keys() const;
    void drop_sections_outside(const SectionKey& center, int radius, int vertical,
                               std::vector<SectionKey>& dropped);

    bool has_player() const { return has_player_; }
    const PlayerSample& player() const { return player_; }
    const std::vector<EntityBox>& entities() const { return entities_; }
    const std::string& name() const { return name_; }
    size_t section_count() const { return sections_.size(); }

private:
    std::string name_;
    bool has_player_ = false;
    PlayerSample player_;
    std::vector<EntityBox> entities_;
    std::unordered_map<int32_t, BlockInfo> blocks_;
    std::unordered_map<SectionKey, std::vector<int32_t>, SectionKeyHash> sections_;
};

} // namespace hytalevr::viewer
