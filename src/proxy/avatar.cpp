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

struct Body {
    std::string model, texture;
    Image image;
};

// The body the player picked: its model and its texture in their skin color.
bool load_body(Zip& zip, Body& body) {
    std::string choice;  // "<part id>.<color>"
    const std::wstring skin_file = latest_skin_file();
    if (!skin_file.empty()) {
        std::ifstream file(skin_file, std::ios::binary);
        const json skin = json::parse(file, nullptr, false);
        if (!skin.is_discarded()) {
            if (const json* value = find_key(skin, "bodyCharacteristic"); value && value->is_string()) choice = *value;
        }
    }
    logf("Character: skin file %s, body \"%s\"", skin_file.empty() ? "not found" : narrow(skin_file).c_str(),
         choice.c_str());

    const size_t dot = choice.rfind('.');
    const std::string id = choice.substr(0, dot), color = dot == std::string::npos ? "" : choice.substr(dot + 1);
    json catalog;
    const json* part = nullptr;
    if (!id.empty() && load_json(zip, "CharacterCreator/BodyCharacteristics.json", catalog))
        part = find_id(catalog, id);
    body.model = string_at(part, "Model");
    std::string gradient_set = string_at(part, "GradientSet");
    body.texture = string_at(part, "GreyscaleTexture");
    if (body.texture.empty() && part) {
        if (const json* textures = find_key(*part, "Textures")) {
            const json* chosen = find_key(*textures, color);
            body.texture = string_at(chosen, "Texture");
            if (body.texture.empty() && textures->is_object() && !textures->empty())
                body.texture = string_at(&textures->begin().value(), "Texture");
        }
        gradient_set.clear();
    }
    if (body.model.empty()) body.model = "Characters/Player.blockymodel";
    if (body.texture.empty()) body.texture = "Characters/Player_Textures/Player_Greyscale.png", gradient_set.clear();

    std::vector<uint8_t> png;
    if (!zip.load(body.texture, png) || !decode_png(png, body.image)) {
        logf("Character: couldn't read the texture %s", body.texture.c_str());
        return false;
    }
    json gradients;
    if (!gradient_set.empty() && !color.empty() && load_json(zip, "CharacterCreator/GradientSets.json", gradients)) {
        const json* set = find_id(gradients, gradient_set);
        const std::string strip = set ? string_at(find_key(*set, color), "Texture") : std::string();
        Image gradient;
        if (!strip.empty() && zip.load(strip, png) && decode_png(png, gradient)) {
            apply_gradient(body.image, gradient);
        } else {
            logf("Character: no %s color in gradient set %s", color.c_str(), gradient_set.c_str());
        }
    }
    logf("Character: model %s, texture %s %ux%u", body.model.c_str(), body.texture.c_str(), body.image.width,
         body.image.height);
    return true;
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

V3 vec(const json& node, const char* key, V3 fallback = {}) {
    const auto it = node.find(key);
    if (it == node.end() || !it->is_object()) return fallback;
    return {it->value("x", fallback.x), it->value("y", fallback.y), it->value("z", fallback.z)};
}

Frame node_frame(const json& node) {
    Frame f;
    f.p = vec(node, "position");
    double qx = 0, qy = 0, qz = 0, qw = 1;
    if (auto it = node.find("orientation"); it != node.end() && it->is_object()) {
        qx = it->value("x", 0.0), qy = it->value("y", 0.0), qz = it->value("z", 0.0), qw = it->value("w", 1.0);
    }
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
        node.name = item.value("name", std::string());
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
    bool mirror_x = false, mirror_y = false;
    if (auto m = it->find("mirror"); m != it->end() && m->is_object())
        mirror_x = m->value("x", false), mirror_y = m->value("y", false);
    const int angle = ((it->value("angle", 0) % 360) + 360) % 360;
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
    if (!shape.value("visible", true)) return;
    const std::string type = shape.value("type", std::string());
    const json settings = shape.value("settings", json::object());
    const V3 size = vec(settings, "size");
    const V3 stretch = vec(shape, "stretch", {1, 1, 1});
    const V3 o = vec(shape, "offset");
    const json layout = shape.value("textureLayout", json::object());
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
        const std::string normal = settings.value("normal", std::string("+Z"));
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

bool build_arm(const std::vector<Node>& nodes, int side, const Image& tex, Arm& arm) {
    // From the forearm down when the model has one, else the whole arm.
    int root = -1, forearm = -1, hand = -1;
    for (int i = 0; i < static_cast<int>(nodes.size()); ++i) {
        if (arm_side(nodes[i].name) != side) continue;
        const std::string n = lower(nodes[i].name);
        const auto shallower = [&](int best) { return best < 0 || nodes[i].depth < nodes[best].depth; };
        if (shallower(root)) root = i;
        if ((n.find("forearm") != std::string::npos || n.find("lower") != std::string::npos) && shallower(forearm))
            forearm = i;
        if (n.find("hand") != std::string::npos && shallower(hand)) hand = i;
    }
    if (forearm >= 0) root = forearm;
    if (root < 0) return false;
    if (hand >= 0 && !inside(nodes, hand, root)) hand = -1;

    std::vector<V3> points, hand_points;
    std::vector<std::pair<float, float>> uvs, hand_uvs;
    for (int i = 0; i < static_cast<int>(nodes.size()); ++i) {
        if (!nodes[i].shape || !inside(nodes, i, root)) continue;
        emit_shape(*nodes[i].shape, nodes[i].world, tex, points, uvs);
        if (hand >= 0 && inside(nodes, i, hand)) emit_shape(*nodes[i].shape, nodes[i].world, tex, hand_points, hand_uvs);
    }
    if (points.empty()) return false;

    // Palm: the middle of the hand's blocks, or the tip end of the arm.
    const V3 shoulder = nodes[root].world.p;
    V3 lo{1e9, 1e9, 1e9}, hi{-1e9, -1e9, -1e9};
    const auto grow = [&](V3 p) {
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
    };
    V3 palm;
    if (!hand_points.empty()) {
        for (V3 p : hand_points) grow(p);
        palm = (lo + hi) * 0.5;
    } else {
        V3 tip = shoulder;
        for (V3 p : points)
            if (dot(p - shoulder, p - shoulder) > dot(tip - shoulder, tip - shoulder)) tip = p;
        const V3 along = unit(tip - shoulder);
        double reach = 0;
        for (V3 p : points) reach = std::max(reach, dot(p - shoulder, along));
        int count = 0;
        for (V3 p : points)
            if (dot(p - shoulder, along) > reach - 4) palm = palm + p, ++count;
        palm = palm * (1.0 / std::max(count, 1)) - along * 2.0;
    }
    V3 down = unit(palm - shoulder);
    if (dot(down, down) < 0.5) down = {0, -1, 0};
    // Characters face +Z; with the arm hanging the thumb points that way.
    V3 forward = unit(V3{0, 0, 1} - down * down.z);
    if (dot(forward, forward) < 0.5) forward = unit(V3{0, 1, 0} - down * down.y);
    const V3 outward = cross(down, forward);

    // Arm toward the palm becomes -Z, the thumb side +Y.
    arm.vertices.clear();
    for (size_t i = 0; i < points.size(); ++i) {
        const V3 p = points[i] - palm;
        arm.vertices.push_back({static_cast<float>(dot(p, outward)), static_cast<float>(dot(p, forward)),
                                static_cast<float>(-dot(p, down)), uvs[i].first, uvs[i].second});
    }
    logf("Character: %s arm from \"%s\"%s%s%s, %zu triangles", side ? "right" : "left", nodes[root].name.c_str(),
         hand >= 0 ? ", hand \"" : "", hand >= 0 ? nodes[hand].name.c_str() : "", hand >= 0 ? "\"" : "",
         arm.vertices.size() / 3);
    return true;
}

void load() {
    const std::wstring zip_path = find_assets_zip();
    Zip zip;
    if (zip_path.empty() || !zip.open(zip_path)) {
        logf("Character: couldn't open Hytale's Assets.zip (%s); keeping the blocky hands",
             zip_path.empty() ? "not found" : narrow(zip_path).c_str());
        return;
    }
    Body body;
    json model;
    if (!load_body(zip, body)) return;
    if (!load_json(zip, body.model, model) &&
        (body.model == "Characters/Player.blockymodel" || !load_json(zip, "Characters/Player.blockymodel", model))) {
        logf("Character: couldn't read the player model");
        return;
    }
    std::vector<Node> nodes;
    flatten(model.value("nodes", json::array()), -1, Frame{}, nodes);
    std::string names;
    for (const Node& node : nodes) names += (names.empty() ? "" : ", ") + node.name;
    logf("Character: %zu model parts: %s", nodes.size(), names.c_str());

    auto* result = new Model;
    bool any = false;
    for (int side = 0; side < 2; ++side) any |= build_arm(nodes, side, body.image, result->arms[side]);
    result->width = body.image.width;
    result->height = body.image.height;
    result->rgba = std::move(body.image.rgba);
    if (!any) {
        logf("Character: no arm parts found in the model; keeping the blocky hands");
        delete result;
        return;
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
