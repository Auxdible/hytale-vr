#include "viewer/viewer_world.h"

#include <charconv>
#include <cstdlib>

namespace hytalevr::viewer {
namespace {

int base64_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

bool base64_decode(std::string_view text, std::vector<uint8_t>& out) {
    out.clear();
    out.reserve(text.size() * 3 / 4);
    uint32_t buffer = 0;
    int bits = 0;
    for (char c : text) {
        if (c == '=') break;
        const int value = base64_value(c);
        if (value < 0) return false;
        buffer = (buffer << 6) | static_cast<uint32_t>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((buffer >> bits) & 0xFF));
        }
    }
    return true;
}

bool read_varint(const std::vector<uint8_t>& bytes, size_t& cursor, uint32_t& value) {
    value = 0;
    for (int shift = 0; shift < 35; shift += 7) {
        if (cursor >= bytes.size()) return false;
        const uint8_t b = bytes[cursor++];
        value |= static_cast<uint32_t>(b & 0x7F) << shift;
        if ((b & 0x80) == 0) return true;
    }
    return false;
}

std::string_view next_token(std::string_view& text, char separator) {
    const size_t at = text.find(separator);
    std::string_view token = text.substr(0, at);
    text = at == std::string_view::npos ? std::string_view() : text.substr(at + 1);
    return token;
}

bool parse_double(std::string_view text, double& value) {
    // std::from_chars for double is not available on every toolchain this
    // builds with, so go through strtod on a bounded copy.
    char buffer[64];
    if (text.empty() || text.size() >= sizeof(buffer)) return false;
    text.copy(buffer, text.size());
    buffer[text.size()] = '\0';
    char* end = nullptr;
    value = std::strtod(buffer, &end);
    return end == buffer + text.size();
}

template <typename T>
bool parse_int(std::string_view text, T& value) {
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc() && result.ptr == text.data() + text.size();
}

bool parse_doubles(std::string_view& text, char separator, double* values, int count) {
    for (int i = 0; i < count; ++i) {
        if (!parse_double(next_token(text, separator), values[i])) return false;
    }
    return true;
}

} // namespace

bool decode_section(std::string_view base64, std::vector<int32_t>& ids) {
    std::vector<uint8_t> bytes;
    if (!base64_decode(base64, bytes)) return false;
    size_t cursor = 0;
    uint32_t palette_size = 0;
    if (!read_varint(bytes, cursor, palette_size) || palette_size == 0 ||
        palette_size > static_cast<uint32_t>(kSectionCells)) {
        return false;
    }
    std::vector<int32_t> palette(palette_size);
    for (uint32_t i = 0; i < palette_size; ++i) {
        uint32_t id = 0;
        if (!read_varint(bytes, cursor, id)) return false;
        palette[i] = static_cast<int32_t>(id);
    }
    ids.assign(kSectionCells, 0);
    size_t written = 0;
    while (written < static_cast<size_t>(kSectionCells)) {
        uint32_t run = 0;
        uint32_t index = 0;
        if (!read_varint(bytes, cursor, run) || !read_varint(bytes, cursor, index) ||
            run == 0 || index >= palette_size ||
            written + run > static_cast<size_t>(kSectionCells)) {
            return false;
        }
        std::fill(ids.begin() + static_cast<std::ptrdiff_t>(written),
                  ids.begin() + static_cast<std::ptrdiff_t>(written + run), palette[index]);
        written += run;
    }
    return true;
}

World::Change World::apply(std::string_view line, SectionKey* section_out) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.remove_suffix(1);
    std::string_view rest = line;
    const std::string_view kind = next_token(rest, ' ');

    if (kind == "WORLD") {
        name_ = std::string(rest);
        sections_.clear();
        entities_.clear();
        has_player_ = false;
        return Change::Reset;
    }
    if (kind == "PLAYER") {
        PlayerSample sample;
        if (!parse_doubles(rest, ' ', sample.feet, 3) || !parse_doubles(rest, ' ', sample.eye, 3) ||
            !parse_doubles(rest, ' ', sample.look, 3)) {
            return Change::None;
        }
        player_ = sample;
        has_player_ = true;
        return Change::Player;
    }
    if (kind == "ENTITIES") {
        std::vector<EntityBox> entities;
        while (!rest.empty()) {
            std::string_view entry = next_token(rest, ';');
            EntityBox box;
            double values[10]{};
            if (!parse_int(next_token(entry, ','), box.id) || !parse_doubles(entry, ',', values, 10)) {
                continue;
            }
            for (int i = 0; i < 3; ++i) {
                box.position[i] = values[i];
                box.min[i] = values[3 + i];
                box.max[i] = values[6 + i];
            }
            box.yaw = static_cast<float>(values[9]);
            box.model = std::string(entry);
            entities.push_back(std::move(box));
        }
        entities_ = std::move(entities);
        return Change::Entities;
    }
    if (kind == "BLOCK") {
        int32_t id = 0;
        if (!parse_int(next_token(rest, '|'), id)) return Change::None;
        BlockInfo info;
        info.key = std::string(next_token(rest, '|'));
        const std::string_view draw = next_token(rest, '|');
        info.draw = draw == "Cube" || draw == "CubeWithModel" ? DrawKind::Cube
                  : draw == "Model" ? DrawKind::Model
                  : DrawKind::Empty;
        const std::string_view opacity = next_token(rest, '|');
        info.opacity = opacity == "Semitransparent" ? BlockOpacity::Semitransparent
                     : opacity == "Cutout" ? BlockOpacity::Cutout
                     : opacity == "Transparent" ? BlockOpacity::Transparent
                     : BlockOpacity::Solid;
        std::string_view color = next_token(rest, '|');
        for (int i = 0; i < 3; ++i) {
            int channel = 128;
            parse_int(next_token(color, ','), channel);
            info.color[i] = static_cast<uint8_t>(channel < 0 ? 0 : channel > 255 ? 255 : channel);
        }
        for (int face = 0; face < kFaceCount; ++face) info.faces[face] = std::string(next_token(rest, '|'));
        blocks_[id] = std::move(info);
        return Change::Block;
    }
    if (kind == "SECTION") {
        SectionKey key;
        if (!parse_int(next_token(rest, ' '), key.x) || !parse_int(next_token(rest, ' '), key.y) ||
            !parse_int(next_token(rest, ' '), key.z)) {
            return Change::None;
        }
        std::vector<int32_t> ids;
        if (!decode_section(rest, ids)) return Change::None;
        sections_[key] = std::move(ids);
        if (section_out) *section_out = key;
        return Change::Section;
    }
    return Change::None;
}

const BlockInfo* World::block(int32_t id) const {
    const auto it = blocks_.find(id);
    return it == blocks_.end() ? nullptr : &it->second;
}

int32_t World::block_at(int x, int y, int z) const {
    const SectionKey key{floor_div32(x), floor_div32(y), floor_div32(z)};
    const auto it = sections_.find(key);
    if (it == sections_.end()) return -1;
    return it->second[static_cast<size_t>(cell_index(x & 31, y & 31, z & 31))];
}

const std::vector<int32_t>* World::section(const SectionKey& key) const {
    const auto it = sections_.find(key);
    return it == sections_.end() ? nullptr : &it->second;
}

std::vector<SectionKey> World::section_keys() const {
    std::vector<SectionKey> keys;
    keys.reserve(sections_.size());
    for (const auto& entry : sections_) keys.push_back(entry.first);
    return keys;
}

void World::drop_sections_outside(const SectionKey& center, int radius, int vertical,
                                  std::vector<SectionKey>& dropped) {
    for (auto it = sections_.begin(); it != sections_.end();) {
        const SectionKey& key = it->first;
        if (std::abs(key.x - center.x) > radius || std::abs(key.z - center.z) > radius ||
            std::abs(key.y - center.y) > vertical) {
            dropped.push_back(key);
            it = sections_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace hytalevr::viewer
