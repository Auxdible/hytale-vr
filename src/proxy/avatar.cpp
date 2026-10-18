#include "avatar.h"

#include <windows.h>
#include <objidl.h>

#include <algorithm>
namespace Gdiplus {
using std::max;
using std::min;
}  // namespace Gdiplus
#include <gdiplus.h>

#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <unordered_map>

#include "../../third_party/nlohmann/json.hpp"
#include "log.h"

namespace hvr::avatar {
namespace {

using json = nlohmann::json;

std::atomic<Model*> g_model{nullptr};
std::atomic<bool> g_started{false};

// ---- Assets.zip ------------------------------------------------------------

struct ZipEntry {
    uint64_t header = 0, size = 0, packed = 0;
    uint16_t method = 0;
};

uint16_t u16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t u32(const uint8_t* p) { return u16(p) | (static_cast<uint32_t>(u16(p + 2)) << 16); }
uint64_t u64(const uint8_t* p) { return u32(p) | (static_cast<uint64_t>(u32(p + 4)) << 32); }

class Zip {
public:
    bool open(const std::wstring& path) {
        file_.open(path, std::ios::binary);
        if (!file_) return false;
        file_.seekg(0, std::ios::end);
        const uint64_t file_size = static_cast<uint64_t>(file_.tellg());
        const uint64_t tail_size = std::min<uint64_t>(file_size, 65536 + 22);
        std::vector<uint8_t> tail(static_cast<size_t>(tail_size));
        if (!read(file_size - tail_size, tail.data(), tail.size())) return false;
        size_t end = std::string::npos;
        for (size_t i = tail.size() - 22 + 1; i-- > 0;)
            if (u32(&tail[i]) == 0x06054b50u) {
                end = i;
                break;
            }
        if (end == std::string::npos) return false;
        uint64_t count = u16(&tail[end + 10]);
        uint64_t directory_size = u32(&tail[end + 12]);
        uint64_t directory = u32(&tail[end + 16]);
        if ((count == 0xffff || directory == 0xffffffffu) && end >= 20 && u32(&tail[end - 20]) == 0x07064b50u) {
            // ZIP64: the real numbers are in a second end record.
            uint8_t record[56];
            if (!read(u64(&tail[end - 20 + 8]), record, sizeof(record)) || u32(record) != 0x06064b50u) return false;
            count = u64(record + 32);
            directory_size = u64(record + 40);
            directory = u64(record + 48);
        }
        std::vector<uint8_t> dir(static_cast<size_t>(directory_size));
        if (!read(directory, dir.data(), dir.size())) return false;
        for (size_t at = 0, n = 0; n < count && at + 46 <= dir.size(); ++n) {
            const uint8_t* p = &dir[at];
            if (u32(p) != 0x02014b50u) return false;
            ZipEntry entry;
            entry.method = u16(p + 10);
            entry.packed = u32(p + 20);
            entry.size = u32(p + 24);
            entry.header = u32(p + 42);
            const size_t name_length = u16(p + 28), extra_length = u16(p + 30), comment_length = u16(p + 32);
            if (at + 46 + name_length + extra_length > dir.size()) return false;
            const uint8_t* extra = p + 46 + name_length;
            for (size_t e = 0; e + 4 <= extra_length;) {
                const uint16_t id = u16(extra + e), length = u16(extra + e + 2);
                if (id == 0x0001) {
                    const uint8_t* field = extra + e + 4;
                    if (entry.size == 0xffffffffu) entry.size = u64(field), field += 8;
                    if (entry.packed == 0xffffffffu) entry.packed = u64(field), field += 8;
                    if (entry.header == 0xffffffffu) entry.header = u64(field);
                }
                e += 4u + length;
            }
            std::string name(reinterpret_cast<const char*>(p + 46), name_length);
            std::replace(name.begin(), name.end(), '\\', '/');
            entries_[name] = entry;
            at += 46 + name_length + extra_length + comment_length;
        }
        return !entries_.empty();
    }

    // The shortest entry whose path is `path` or ends with "/" + path.
    std::string find(std::string path) const {
        std::replace(path.begin(), path.end(), '\\', '/');
        while (!path.empty() && path.front() == '/') path.erase(path.begin());
        const std::string suffix = "/" + path;
        std::string best;
        for (const auto& [name, entry] : entries_) {
            const bool match = name == path || (name.size() > suffix.size() &&
                                                name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0);
            if (match && (best.empty() || name.size() < best.size())) best = name;
        }
        return best;
    }

    bool load(const std::string& path, std::vector<uint8_t>& out) {
        const std::string name = find(path);
        const auto it = entries_.find(name);
        if (name.empty() || it == entries_.end()) {
            logf("Character: %s is not in Assets.zip", path.c_str());
            return false;
        }
        const ZipEntry& entry = it->second;
        if (entry.method != 0) {
            logf("Character: %s is compressed (method %u), which isn't supported", name.c_str(), entry.method);
            return false;
        }
        uint8_t header[30];
        if (!read(entry.header, header, sizeof(header)) || u32(header) != 0x04034b50u) return false;
        out.resize(static_cast<size_t>(entry.size));
        return read(entry.header + 30 + u16(header + 26) + u16(header + 28), out.data(), out.size());
    }

private:
    bool read(uint64_t offset, void* out, size_t size) {
        file_.clear();
        file_.seekg(static_cast<std::streamoff>(offset));
        file_.read(static_cast<char*>(out), static_cast<std::streamsize>(size));
        return file_.gcount() == static_cast<std::streamsize>(size);
    }

    std::ifstream file_;
    std::unordered_map<std::string, ZipEntry> entries_;
};

std::wstring find_assets_zip() {
    namespace fs = std::filesystem;
    std::error_code error;
    const fs::path beside = fs::path(client_dir()).parent_path() / L"Assets.zip";
    if (fs::exists(beside, error)) return beside.wstring();
    wchar_t appdata[MAX_PATH] = {};
    if (!GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH)) return {};
    for (const wchar_t* channel : {L"release", L"pre-release"}) {
        const fs::path path = fs::path(appdata) / L"Hytale" / L"install" / channel / L"package" / L"game" / L"latest" /
                              L"Assets.zip";
        if (fs::exists(path, error)) return path.wstring();
    }
    return {};
}

// ---- Images ----------------------------------------------------------------

struct Image {
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> rgba;
    const uint8_t* at(uint32_t x, uint32_t y) const { return &rgba[(static_cast<size_t>(y) * width + x) * 4]; }
};

bool decode_png(const std::vector<uint8_t>& bytes, Image& image) {
    static ULONG_PTR token = 0;
    if (!token) {
        Gdiplus::GdiplusStartupInput input;
        if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok) return false;
    }
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
    if (!memory) return false;
    std::memcpy(GlobalLock(memory), bytes.data(), bytes.size());
    GlobalUnlock(memory);
    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(memory, TRUE, &stream) != S_OK) {
        GlobalFree(memory);
        return false;
    }
    bool ok = false;
    {
        Gdiplus::Bitmap bitmap(stream);
        Gdiplus::BitmapData data{};
        Gdiplus::Rect rect(0, 0, static_cast<INT>(bitmap.GetWidth()), static_cast<INT>(bitmap.GetHeight()));
        if (bitmap.GetLastStatus() == Gdiplus::Ok && rect.Width > 0 && rect.Height > 0 &&
            bitmap.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data) == Gdiplus::Ok) {
            image.width = static_cast<uint32_t>(rect.Width);
            image.height = static_cast<uint32_t>(rect.Height);
            image.rgba.resize(static_cast<size_t>(image.width) * image.height * 4);
            for (uint32_t y = 0; y < image.height; ++y) {
                const uint8_t* row = static_cast<const uint8_t*>(data.Scan0) + static_cast<ptrdiff_t>(y) * data.Stride;
                for (uint32_t x = 0; x < image.width; ++x) {
                    uint8_t* out = &image.rgba[(static_cast<size_t>(y) * image.width + x) * 4];
                    out[0] = row[x * 4 + 2], out[1] = row[x * 4 + 1], out[2] = row[x * 4 + 0], out[3] = row[x * 4 + 3];
                }
            }
            bitmap.UnlockBits(&data);
            ok = true;
        }
    }
    stream->Release();
    return ok;
}

// Saves an image as a PNG, for checking what the arms were built from.
bool save_png(const std::wstring& path, uint32_t width, uint32_t height, const std::vector<uint8_t>& rgba) {
    Gdiplus::Bitmap bitmap(static_cast<INT>(width), static_cast<INT>(height), PixelFormat32bppARGB);
    Gdiplus::BitmapData data{};
    Gdiplus::Rect rect(0, 0, static_cast<INT>(width), static_cast<INT>(height));
    if (bitmap.LockBits(&rect, Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &data) != Gdiplus::Ok) return false;
    for (uint32_t y = 0; y < height; ++y) {
        uint8_t* row = static_cast<uint8_t*>(data.Scan0) + static_cast<ptrdiff_t>(y) * data.Stride;
        for (uint32_t x = 0; x < width; ++x) {
            const uint8_t* in = &rgba[(static_cast<size_t>(y) * width + x) * 4];
            row[x * 4 + 0] = in[2], row[x * 4 + 1] = in[1], row[x * 4 + 2] = in[0], row[x * 4 + 3] = in[3];
        }
    }
    bitmap.UnlockBits(&data);
    const CLSID png = {0x557cf406, 0x1a04, 0x11d3, {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}};
    return bitmap.Save(path.c_str(), &png, nullptr) == Gdiplus::Ok;
}

// Character Creator textures are greyscale; the gradient (a strip of colors)
// maps each grey level to the chosen color.
void apply_gradient(Image& image, const Image& gradient) {
    if (!gradient.width || !gradient.height) return;
    for (size_t i = 0; i + 3 < image.rgba.size(); i += 4) {
        const uint32_t grey = (image.rgba[i] + image.rgba[i + 1] + image.rgba[i + 2]) / 3;
        const uint8_t* color = gradient.at(grey * (gradient.width - 1) / 255, gradient.height / 2);
        image.rgba[i] = color[0], image.rgba[i + 1] = color[1], image.rgba[i + 2] = color[2];
    }
}

// ---- Skin --------------------------------------------------------------------

std::wstring latest_skin_file() {
    namespace fs = std::filesystem;
    wchar_t appdata[MAX_PATH] = {};
    if (!GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH)) return {};
    std::error_code error;
    fs::path best;
    fs::file_time_type best_time{};
    for (const auto& item :
         fs::directory_iterator(fs::path(appdata) / L"Hytale" / L"UserData" / L"CachedPlayerSkins", error)) {
        if (!item.is_regular_file(error) || item.path().extension() != L".json") continue;
        const auto time = item.last_write_time(error);
        if (best.empty() || time > best_time) best = item.path(), best_time = time;
    }
    return best.wstring();
}

// First value under `key` anywhere in the document.
const json* find_key(const json& value, const std::string& key) {
    if (value.is_object()) {
        if (auto it = value.find(key); it != value.end()) return &*it;
        for (const auto& item : value) {
            if (const json* found = find_key(item, key)) return found;
        }
    } else if (value.is_array()) {
        for (const auto& item : value) {
            if (const json* found = find_key(item, key)) return found;
        }
    }
    return nullptr;
}

// First object anywhere whose "Id" is `id`.
const json* find_id(const json& value, const std::string& id) {
    if (value.is_object()) {
        if (auto it = value.find("Id"); it != value.end() && it->is_string() && *it == id) return &value;
    }
    if (value.is_structured()) {
        for (const auto& item : value) {
            if (const json* found = find_id(item, id)) return found;
        }
    }
    return nullptr;
}

std::string string_at(const json* value, const char* key) {
    if (!value || !value->is_object()) return {};
    auto it = value->find(key);
    return it != value->end() && it->is_string() ? it->get<std::string>() : std::string();
}

bool load_json(Zip& zip, const std::string& path, json& out) {
    std::vector<uint8_t> bytes;
    if (!zip.load(path, bytes)) return false;
    out = json::parse(bytes.begin(), bytes.end(), nullptr, false);
    return !out.is_discarded();
}

// One piece of the character: a model and its texture in the colors picked.
struct Part {
    std::string key, model, texture;
    Image image;
};

// "<id>.<color>" or "<id>.<color>.<variant>": the ways an id and its color
// can be split, most likely first.
std::vector<std::array<std::string, 3>> split_choice(const std::string& choice) {
    std::vector<std::array<std::string, 3>> ways;
    std::vector<std::string> bits;
    for (size_t at = 0;;) {
        const size_t dot = choice.find('.', at);
        bits.push_back(choice.substr(at, dot == std::string::npos ? std::string::npos : dot - at));
        if (dot == std::string::npos) break;
        at = dot + 1;
    }
    if (bits.size() >= 3) ways.push_back({bits[0], bits[1], bits[2]});
    const size_t last = choice.rfind('.');
    if (last != std::string::npos) ways.push_back({choice.substr(0, last), choice.substr(last + 1), ""});
    ways.push_back({choice, "", ""});
    return ways;
}

// Looks the choice up in a Character Creator catalog and loads its texture,
// colored through its gradient.
bool load_part(Zip& zip, const json& catalog, const std::string& choice, Part& out) {
    const json* part = nullptr;
    std::array<std::string, 3> way;
    for (const auto& w : split_choice(choice)) {
        if ((part = find_id(catalog, w[0]))) {
            way = w;
            break;
        }
    }
    if (!part) return false;
    const std::string& color = way[1];
    // A variant (a sleeve length, say) can carry its own model and textures.
    const json* variant = way[2].empty() ? nullptr : find_key(*part, way[2]);
    const auto field = [&](const char* key) {
        std::string value = variant && variant->is_object() ? string_at(variant, key) : std::string();
        return value.empty() ? string_at(part, key) : value;
    };
    out.model = field("Model");
    std::string gradient_set = field("GradientSet");
    out.texture = field("GreyscaleTexture");
    if (out.texture.empty()) {
        gradient_set.clear();
        const json* textures = variant ? find_key(*variant, "Textures") : nullptr;
        if (!textures) textures = find_key(*part, "Textures");
        if (textures) {
            out.texture = string_at(find_key(*textures, color), "Texture");
            if (out.texture.empty() && textures->is_object() && !textures->empty())
                out.texture = string_at(&textures->begin().value(), "Texture");
        }
    }
    if (out.texture.empty()) return false;
    std::vector<uint8_t> png;
    if (!zip.load(out.texture, png) || !decode_png(png, out.image)) {
        logf("Character: couldn't read the texture %s", out.texture.c_str());
        return false;
    }
    json gradients;
    if (!gradient_set.empty() && !color.empty() && load_json(zip, "CharacterCreator/GradientSets.json", gradients)) {
        const json* set = find_id(gradients, gradient_set);
        const std::string strip = set ? string_at(find_key(*set, color), "Texture") : std::string();
        Image gradient;
        if (!strip.empty() && zip.load(strip, png) && decode_png(png, gradient)) {
            apply_gradient(out.image, gradient);
        } else {
            logf("Character: no %s color in gradient set %s", color.c_str(), gradient_set.c_str());
        }
    }
    return true;
}

// The skin Hytale saved for the player: each slot ("bodyCharacteristic",
// "undertop", "gloves", ...) and what was picked for it.
std::vector<std::pair<std::string, std::string>> read_skin() {
    std::vector<std::pair<std::string, std::string>> slots;
    const std::wstring skin_file = latest_skin_file();
    if (skin_file.empty()) {
        logf("Character: no saved skin found");
        return slots;
    }
    std::ifstream file(skin_file, std::ios::binary);
    const json skin = json::parse(file, nullptr, false);
    if (skin.is_discarded()) return slots;
    // The slots sit in the object that holds bodyCharacteristic.
    const json* holder = &skin;
    std::vector<const json*> stack{&skin};
    while (!stack.empty()) {
        const json* at = stack.back();
        stack.pop_back();
        if (at->is_object() && at->contains("bodyCharacteristic")) {
            holder = at;
            break;
        }
        if (at->is_structured())
            for (const auto& item : *at) stack.push_back(&item);
    }
    if (holder->is_object()) {
        for (const auto& [key, value] : holder->items()) {
            if (value.is_string() && !value.get<std::string>().empty()) slots.push_back({key, value.get<std::string>()});
        }
    }
    std::string list;
    for (const auto& [key, value] : slots) list += (list.empty() ? "" : ", ") + key + "=" + value;
    logf("Character: skin file %s: %s", narrow(skin_file).c_str(), list.c_str());
    return slots;
}

// The catalog for a slot: "undertop" is CharacterCreator/Undertops.json.
bool load_catalog(Zip& zip, const std::string& slot, json& out) {
    if (slot.empty()) return false;
    std::string name = slot;
    name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
    for (const std::string& file : {name + "s", name, name + "es"}) {
        if (!zip.find("CharacterCreator/" + file + ".json").empty())
            return load_json(zip, "CharacterCreator/" + file + ".json", out);
    }
    return false;
}

// ---- BlockyModel -------------------------------------------------------------

struct V3 {
    double x = 0, y = 0, z = 0;
};
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(V3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
double dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
V3 unit(V3 a) {
    const double l = std::sqrt(dot(a, a));
    return l > 1e-9 ? a * (1.0 / l) : V3{0, 0, 0};
}

// Rotation (as three column vectors) plus position.
struct Frame {
    V3 x{1, 0, 0}, y{0, 1, 0}, z{0, 0, 1}, p{};
    V3 apply(V3 v) const { return x * v.x + y * v.y + z * v.z + p; }
    Frame operator*(const Frame& b) const { return {apply(b.x) - p, apply(b.y) - p, apply(b.z) - p, apply(b.p)}; }
};

// Lenient reads: a missing or null field (models have both) gives the fallback.
const json& member(const json& object, const char* key) {
    static const json none;
    if (!object.is_object()) return none;
    const auto it = object.find(key);
    return it == object.end() ? none : *it;
}
double number(const json& object, const char* key, double fallback) {
    const json& value = member(object, key);
    return value.is_number() ? value.get<double>() : fallback;
}
bool flag(const json& object, const char* key, bool fallback) {
    const json& value = member(object, key);
    return value.is_boolean() ? value.get<bool>() : fallback;
}
std::string text(const json& object, const char* key, const char* fallback = "") {
    const json& value = member(object, key);
    return value.is_string() ? value.get<std::string>() : std::string(fallback);
}

V3 vec(const json& node, const char* key, V3 fallback = {}) {
    const json& v = member(node, key);
    return {number(v, "x", fallback.x), number(v, "y", fallback.y), number(v, "z", fallback.z)};
}

Frame node_frame(const json& node) {
    Frame f;
    f.p = vec(node, "position");
    const json& q = member(node, "orientation");
    double qx = number(q, "x", 0), qy = number(q, "y", 0), qz = number(q, "z", 0), qw = number(q, "w", 1);
    const double l = std::sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
    if (l < 1e-9) return f;
    qx /= l, qy /= l, qz /= l, qw /= l;
    f.x = {1 - 2 * (qy * qy + qz * qz), 2 * (qx * qy + qz * qw), 2 * (qx * qz - qy * qw)};
    f.y = {2 * (qx * qy - qz * qw), 1 - 2 * (qx * qx + qz * qz), 2 * (qy * qz + qx * qw)};
    f.z = {2 * (qx * qz + qy * qw), 2 * (qy * qz - qx * qw), 1 - 2 * (qx * qx + qy * qy)};
    return f;
}

struct Node {
    std::string name;
    int parent = -1;
    int depth = 0;
    Frame world;
    const json* shape = nullptr;
};

void flatten(const json& nodes, int parent, const Frame& parent_world, std::vector<Node>& out) {
    if (!nodes.is_array()) return;
    for (const json& item : nodes) {
        if (!item.is_object()) continue;
        Node node;
        node.name = text(item, "name");
        node.parent = parent;
        node.depth = parent < 0 ? 0 : out[parent].depth + 1;
        node.world = parent_world * node_frame(item);
        if (auto it = item.find("shape"); it != item.end() && it->is_object()) node.shape = &*it;
        out.push_back(node);
        const int index = static_cast<int>(out.size()) - 1;
        if (auto it = item.find("children"); it != item.end()) flatten(*it, index, node.world, out);
    }
}

// Texture corners for one face, in the order emit_face uses: bottom left,
// bottom right, top right, top left. The UV rules (offset, mirror, angle)
// follow BlockyModel's texture layout as heurazy's HytaleVRInjector-mod reads it.
bool face_uv(const json& layout, const char* face, double w, double h, const Image& tex,
             std::array<std::pair<float, float>, 4>& uv) {
    const auto it = layout.find(face);
    if (it == layout.end() || !it->is_object()) return false;
    const V3 offset = vec(*it, "offset");
    const json& mirror = member(*it, "mirror");
    const bool mirror_x = flag(mirror, "x", false), mirror_y = flag(mirror, "y", false);
    const int angle = ((static_cast<int>(number(*it, "angle", 0)) % 360) + 360) % 360;
    double sx = mirror_x ? -1 : 1, sy = mirror_y ? -1 : 1;
    const double ox = offset.x, oy = offset.y;
    std::array<double, 4> r{};  // x0, y0, x1, y1
    if (angle == 90) {
        std::swap(w, h), std::swap(sx, sy), sx = -sx;
        r = {ox, oy + h * sy, ox + w * sx, oy};
    } else if (angle == 180) {
        sx = -sx, sy = -sy;
        r = {ox + w * sx, oy + h * sy, ox, oy};
    } else if (angle == 270) {
        std::swap(w, h), std::swap(sx, sy), sy = -sy;
        r = {ox + w * sx, oy, ox, oy + h * sy};
    } else {
        r = {ox, oy, ox + w * sx, oy + h * sy};
    }
    // Top left, top right, bottom left, bottom right; turned with the angle.
    std::array<std::pair<double, double>, 4> c{{{r[0], r[1]}, {r[2], r[1]}, {r[0], r[3]}, {r[2], r[3]}}};
    for (int left = angle; left > 0; left -= 90) {
        const auto first = c[0];
        c[0] = c[2], c[2] = c[3], c[3] = c[1], c[1] = first;
    }
    const int order[4] = {2, 3, 1, 0};
    for (int i = 0; i < 4; ++i)
        uv[i] = {static_cast<float>(c[order[i]].first / tex.width), static_cast<float>(c[order[i]].second / tex.height)};
    return true;
}

void emit_face(std::vector<V3>& points, std::vector<std::pair<float, float>>& uvs, const Frame& f,
               const std::array<V3, 4>& q, const std::array<std::pair<float, float>, 4>& uv) {
    const int order[6] = {0, 1, 2, 0, 2, 3};
    for (int i : order) {
        points.push_back(f.apply(q[i]));
        uvs.push_back(uv[i]);
    }
}

void emit_shape(const json& shape, const Frame& f, const Image& tex, std::vector<V3>& points,
                std::vector<std::pair<float, float>>& uvs) {
    if (!flag(shape, "visible", true)) return;
    const std::string type = text(shape, "type");
    const json& settings = member(shape, "settings");
    const V3 size = vec(settings, "size");
    const V3 stretch = vec(shape, "stretch", {1, 1, 1});
    const V3 o = vec(shape, "offset");
    const json& layout = member(shape, "textureLayout");
    const double hx = size.x * stretch.x / 2, hy = size.y * stretch.y / 2, hz = size.z * stretch.z / 2;
    std::array<std::pair<float, float>, 4> uv;
    if (type == "box") {
        const double x0 = o.x - hx, x1 = o.x + hx, y0 = o.y - hy, y1 = o.y + hy, z0 = o.z - hz, z1 = o.z + hz;
        if (face_uv(layout, "front", size.x, size.y, tex, uv))
            emit_face(points, uvs, f, {{{x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}}}, uv);
        if (face_uv(layout, "back", size.x, size.y, tex, uv))
            emit_face(points, uvs, f, {{{x1, y0, z0}, {x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}}}, uv);
        if (face_uv(layout, "left", size.z, size.y, tex, uv))
            emit_face(points, uvs, f, {{{x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}}}, uv);
        if (face_uv(layout, "right", size.z, size.y, tex, uv))
            emit_face(points, uvs, f, {{{x1, y0, z1}, {x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}}}, uv);
        if (face_uv(layout, "top", size.x, size.z, tex, uv))
            emit_face(points, uvs, f, {{{x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}, {x0, y1, z0}}}, uv);
        if (face_uv(layout, "bottom", size.x, size.z, tex, uv))
            emit_face(points, uvs, f, {{{x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}}}, uv);
    } else if (type == "quad" && face_uv(layout, "front", size.x, size.y, tex, uv)) {
        std::string normal = text(settings, "normal", "+Z");
        if (normal.size() < 2) normal = "+Z";
        std::array<V3, 4> q;
        if (normal[1] == 'X')
            q = {{{o.x, o.y - hy, o.z - hx}, {o.x, o.y - hy, o.z + hx}, {o.x, o.y + hy, o.z + hx}, {o.x, o.y + hy, o.z - hx}}};
        else if (normal[1] == 'Y')
            q = {{{o.x - hx, o.y, o.z - hy}, {o.x + hx, o.y, o.z - hy}, {o.x + hx, o.y, o.z + hy}, {o.x - hx, o.y, o.z + hy}}};
        else
            q = {{{o.x - hx, o.y - hy, o.z}, {o.x + hx, o.y - hy, o.z}, {o.x + hx, o.y + hy, o.z}, {o.x - hx, o.y + hy, o.z}}};
        if (normal[0] == '-') std::swap(q[1], q[3]);
        emit_face(points, uvs, f, q, uv);
    }
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// -1 none, 0 left, 1 right: "R-Arm", "Right_Hand", "L_Forearm", ...
int arm_side(const std::string& name) {
    const std::string n = lower(name);
    const bool arm = n.find("arm") != std::string::npos || n.find("hand") != std::string::npos;
    if (!arm) return -1;
    if (n.find("right") != std::string::npos) return 1;
    if (n.find("left") != std::string::npos) return 0;
    if (n.size() > 1 && (n[1] == '-' || n[1] == '_' || n[1] == ' ' || n[1] == '.')) {
        if (n[0] == 'r') return 1;
        if (n[0] == 'l') return 0;
    }
    return -1;
}

bool inside(const std::vector<Node>& nodes, int node, int root) {
    for (; node >= 0; node = nodes[node].parent)
        if (node == root) return true;
    return false;
}

// The arm's frame: the palm at the origin, the arm toward the palm along -Z,
// the thumb side +Y.
struct ArmFrame {
    V3 palm, outward, forward, down;
    V3 point(V3 p) const { return direction(p - palm); }
    V3 direction(V3 v) const { return {dot(v, outward), dot(v, forward), -dot(v, down)}; }
};

struct Geometry {
    std::vector<V3> points;
    std::vector<std::pair<float, float>> uvs;
    size_t layer_start = 0;
};

// The base model's arm: which part starts it and where its hand is.
struct ArmBones {
    int root = -1, hand = -1, attachment = -1;
};

ArmBones find_arm(const std::vector<Node>& nodes, int side) {
    ArmBones bones;
    int forearm = -1;
    for (int i = 0; i < static_cast<int>(nodes.size()); ++i) {
        const std::string n = lower(nodes[i].name);
        const auto shallower = [&](int best) { return best < 0 || nodes[i].depth < nodes[best].depth; };
        const bool left = n.rfind("l-", 0) == 0 || n.rfind("l_", 0) == 0 || n.find("left") != std::string::npos;
        const bool right = n.rfind("r-", 0) == 0 || n.rfind("r_", 0) == 0 || n.find("right") != std::string::npos;
        if (n.find("attachment") != std::string::npos && (side ? right : left) && n.find("eye") == std::string::npos &&
            n.find("ear") == std::string::npos && n.find("eyebrow") == std::string::npos)
            bones.attachment = i;
        if (arm_side(nodes[i].name) != side) continue;
        if (shallower(bones.root)) bones.root = i;
        if ((n.find("forearm") != std::string::npos || n.find("lower") != std::string::npos) && shallower(forearm))
            forearm = i;
        if (n.find("hand") != std::string::npos && shallower(bones.hand)) bones.hand = i;
    }
    if (forearm >= 0) bones.root = forearm;
    if (bones.hand >= 0 && !inside(nodes, bones.hand, bones.root)) bones.hand = -1;
    return bones;
}

ArmFrame arm_frame(const std::vector<Node>& nodes, const ArmBones& bones, const Image& tex) {
    std::vector<V3> points, hand_points;
    std::vector<std::pair<float, float>> uvs;
    for (int i = 0; i < static_cast<int>(nodes.size()); ++i) {
        if (!nodes[i].shape || !inside(nodes, i, bones.root)) continue;
        const size_t before = points.size();
        emit_shape(*nodes[i].shape, nodes[i].world, tex, points, uvs);
        if (bones.hand >= 0 && inside(nodes, i, bones.hand))
            hand_points.insert(hand_points.end(), points.begin() + static_cast<ptrdiff_t>(before), points.end());
    }
    // Palm: the middle of the hand's blocks, or the far end of the arm.
    const V3 shoulder = nodes[bones.root].world.p;
    ArmFrame f;
    if (!hand_points.empty()) {
        V3 lo{1e9, 1e9, 1e9}, hi{-1e9, -1e9, -1e9};
        for (V3 p : hand_points) {
            lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
            hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
        }
        f.palm = (lo + hi) * 0.5;
    } else if (!points.empty()) {
        V3 tip = shoulder;
        for (V3 p : points)
            if (dot(p - shoulder, p - shoulder) > dot(tip - shoulder, tip - shoulder)) tip = p;
        const V3 along = unit(tip - shoulder);
        double reach = 0;
        for (V3 p : points) reach = std::max(reach, dot(p - shoulder, along));
        int count = 0;
        for (V3 p : points)
            if (dot(p - shoulder, along) > reach - 4) f.palm = f.palm + p, ++count;
        f.palm = f.palm * (1.0 / std::max(count, 1)) - along * 2.0;
    }
    f.down = unit(f.palm - shoulder);
    if (dot(f.down, f.down) < 0.5) f.down = {0, -1, 0};
    // Characters face +Z; with the arm hanging the thumb points that way.
    f.forward = unit(V3{0, 0, 1} - f.down * f.down.z);
    if (dot(f.forward, f.forward) < 0.5) f.forward = unit(V3{0, 1, 0} - f.down * f.down.y);
    f.outward = cross(f.down, f.forward);
    return f;
}

// A part's matrix in the arm's frame, column-major: from the part's own axes
// (model units) into the arm's.
void export_frame(const ArmFrame& f, const Frame& world, float out[16]) {
    const V3 columns[4] = {f.direction(world.x), f.direction(world.y), f.direction(world.z), f.point(world.p)};
    for (int c = 0; c < 4; ++c) {
        out[c * 4 + 0] = static_cast<float>(columns[c].x);
        out[c * 4 + 1] = static_cast<float>(columns[c].y);
        out[c * 4 + 2] = static_cast<float>(columns[c].z);
        out[c * 4 + 3] = c == 3 ? 1.0f : 0.0f;
    }
}

Frame inverse(const Frame& f) {
    Frame r;
    r.x = {f.x.x, f.y.x, f.z.x};
    r.y = {f.x.y, f.y.y, f.z.y};
    r.z = {f.x.z, f.y.z, f.z.z};
    r.p = V3{0, 0, 0} - r.apply(f.p);
    return r;
}

// Adds a model's pieces that sit on the arm: the base model's own, or a
// cosmetic's pieces under a part of the same name, moved onto that part.
void add_arm_pieces(const std::vector<Node>& base, const ArmBones& bones, const std::vector<Node>& model,
                    const Image& tex, Geometry& out) {
    for (int i = 0; i < static_cast<int>(model.size()); ++i) {
        if (!model[i].shape) continue;
        // The nearest part (itself or above) that the base arm also has.
        int bone = -1, own = -1;
        for (int at = i; at >= 0 && bone < 0; at = model[at].parent) {
            for (int b = 0; b < static_cast<int>(base.size()); ++b) {
                if (base[b].name == model[at].name && inside(base, b, bones.root)) {
                    bone = b, own = at;
                    break;
                }
            }
        }
        if (bone < 0) continue;
        const Frame onto = base[bone].world * inverse(model[own].world);
        emit_shape(*model[i].shape, onto * model[i].world, tex, out.points, out.uvs);
    }
}

void load() {
    const std::wstring zip_path = find_assets_zip();
    Zip zip;
    if (zip_path.empty() || !zip.open(zip_path)) {
        logf("Character: couldn't open Hytale's Assets.zip (%s); keeping the blocky hands",
             zip_path.empty() ? "not found" : narrow(zip_path).c_str());
        return;
    }
    const auto slots = read_skin();

    // The body first: its model is the skeleton everything else goes on.
    Part body;
    {
        std::string choice;
        for (const auto& [key, value] : slots)
            if (key == "bodyCharacteristic") choice = value;
        json catalog;
        if (choice.empty() || !load_json(zip, "CharacterCreator/BodyCharacteristics.json", catalog) ||
            !load_part(zip, catalog, choice, body)) {
            body = Part{};
            body.texture = "Characters/Player_Textures/Player_Greyscale.png";
            std::vector<uint8_t> png;
            if (!zip.load(body.texture, png) || !decode_png(png, body.image)) return;
        }
        if (body.model.empty()) body.model = "Characters/Player.blockymodel";
    }
    json model;
    if (!load_json(zip, body.model, model) && !load_json(zip, "Characters/Player.blockymodel", model)) {
        logf("Character: couldn't read the player model");
        return;
    }
    std::vector<Node> nodes;
    flatten(member(model, "nodes"), -1, Frame{}, nodes);
    logf("Character: body %s with %s, %ux%u, %zu parts", body.model.c_str(), body.texture.c_str(), body.image.width,
         body.image.height, nodes.size());

    // Clothes and gloves, outermost first so they win over the skin under them.
    std::vector<Part> clothes;
    std::vector<json> cloth_models;
    for (const auto& [key, value] : slots) {
        if (key == "bodyCharacteristic") continue;
        json catalog;
        Part part;
        part.key = key;
        if (!load_catalog(zip, key, catalog) || !load_part(zip, catalog, value, part) || part.model.empty()) continue;
        json cloth;
        if (!load_json(zip, part.model, cloth)) continue;
        clothes.push_back(std::move(part));
        cloth_models.push_back(std::move(cloth));
    }
    const auto rank = [](const std::string& key) {
        const std::string k = lower(key);
        if (k.find("glove") != std::string::npos) return 0;
        if (k.find("over") != std::string::npos) return 1;
        if (k.find("under") != std::string::npos) return 2;
        return 3;
    };

    auto* result = new Model;
    std::vector<const Image*> layers;
    for (int side = 0; side < 2; ++side) {
        const ArmBones bones = find_arm(nodes, side);
        Arm& arm = result->arms[side];
        if (bones.root < 0) continue;
        const ArmFrame frame = arm_frame(nodes, bones, body.image);
        std::vector<Geometry> pieces;
        std::vector<size_t> order(clothes.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::stable_sort(order.begin(), order.end(),
                         [&](size_t a, size_t b) { return rank(clothes[a].key) < rank(clothes[b].key); });
        std::string worn;
        std::vector<const Image*> piece_images;
        for (size_t i : order) {
            std::vector<Node> cloth_nodes;
            flatten(member(cloth_models[i], "nodes"), -1, Frame{}, cloth_nodes);
            Geometry g;
            add_arm_pieces(nodes, bones, cloth_nodes, clothes[i].image, g);
            if (g.points.empty()) continue;
            worn += (worn.empty() ? "" : ", ") + clothes[i].key;
            pieces.push_back(std::move(g));
            piece_images.push_back(&clothes[i].image);
        }
        Geometry skin;
        add_arm_pieces(nodes, bones, nodes, body.image, skin);
        pieces.push_back(std::move(skin));
        piece_images.push_back(&body.image);

        // Every texture goes in one tall image: each piece's UVs move to its strip.
        for (size_t p = 0; p < pieces.size(); ++p) {
            size_t layer = 0;
            while (layer < layers.size() && layers[layer] != piece_images[p]) ++layer;
            if (layer == layers.size()) layers.push_back(piece_images[p]);
            pieces[p].layer_start = layer;
            for (size_t v = 0; v < pieces[p].points.size(); ++v) {
                const V3 q = frame.point(pieces[p].points[v]);
                arm.vertices.push_back({static_cast<float>(q.x), static_cast<float>(q.y), static_cast<float>(q.z),
                                        pieces[p].uvs[v].first, pieces[p].uvs[v].second});
            }
        }
        // Remember each vertex's layer to remap after the atlas is laid out.
        size_t at = 0;
        for (const Geometry& g : pieces) {
            for (size_t v = 0; v < g.points.size(); ++v) arm.vertices[at + v].u += static_cast<float>(g.layer_start) * 4.0f;
            at += g.points.size();
        }
        if (bones.hand >= 0) {
            export_frame(frame, nodes[bones.hand].world, arm.hand);
            arm.has_hand = true;
        }
        if (bones.attachment >= 0) {
            export_frame(frame, nodes[bones.attachment].world, arm.attachment);
            arm.has_attachment = true;
        }
        logf("Character: %s arm from \"%s\"%s, %zu triangles, wearing %s", side ? "right" : "left",
             nodes[bones.root].name.c_str(), bones.attachment >= 0 ? " with an item point" : "",
             arm.vertices.size() / 3, worn.empty() ? "nothing on it" : worn.c_str());
    }

    // Lay the textures out top to bottom and point each vertex at its strip.
    uint32_t width = 1, height = 0;
    std::vector<uint32_t> tops;
    for (const Image* image : layers) {
        tops.push_back(height);
        width = std::max(width, image->width);
        height += image->height;
    }
    if (layers.empty() || !height) {
        logf("Character: no arm parts found in the model; keeping the blocky hands");
        delete result;
        return;
    }
    result->width = width;
    result->height = height;
    result->rgba.assign(static_cast<size_t>(width) * height * 4, 0);
    for (size_t l = 0; l < layers.size(); ++l) {
        for (uint32_t y = 0; y < layers[l]->height; ++y)
            std::memcpy(&result->rgba[(static_cast<size_t>(tops[l] + y) * width) * 4], layers[l]->at(0, y),
                        static_cast<size_t>(layers[l]->width) * 4);
    }
    for (Arm& arm : result->arms) {
        for (Vertex& v : arm.vertices) {
            // The integer part of u (in fours) is the layer, the rest the UV inside it.
            const int layer = static_cast<int>(std::floor((v.u + 1.0f) / 4.0f));
            const float u = v.u - static_cast<float>(layer) * 4.0f;
            const Image& image = *layers[static_cast<size_t>(layer)];
            v.u = u * static_cast<float>(image.width) / static_cast<float>(width);
            v.v = (static_cast<float>(tops[static_cast<size_t>(layer)]) + v.v * static_cast<float>(image.height)) /
                  static_cast<float>(height);
        }
    }
    // What the arms were built from, next to the log: the texture and each arm as a model.
    save_png(log_dir() + L"\\arm_texture.png", width, height, result->rgba);
    for (int side = 0; side < 2; ++side) {
        std::ofstream obj(log_dir() + (side ? L"\\arm_right.obj" : L"\\arm_left.obj"));
        const auto& vertices = result->arms[side].vertices;
        for (const Vertex& v : vertices) obj << "v " << v.x << ' ' << v.y << ' ' << v.z << '\n';
        for (const Vertex& v : vertices) obj << "vt " << v.u << ' ' << 1.0f - v.v << '\n';
        for (size_t i = 0; i + 2 < vertices.size(); i += 3)
            obj << "f " << i + 1 << '/' << i + 1 << ' ' << i + 2 << '/' << i + 2 << ' ' << i + 3 << '/' << i + 3 << '\n';
    }
    g_model.store(result, std::memory_order_release);
}

}  // namespace

void start_loading() {
    if (g_started.exchange(true)) return;
    std::thread([] {
        try {
            load();
        } catch (const std::exception& e) {
            logf("Character: loading failed: %s", e.what());
        }
    }).detach();
}

const Model* ready() { return g_model.load(std::memory_order_acquire); }

}  // namespace hvr::avatar
