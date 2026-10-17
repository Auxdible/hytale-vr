// hytale_vr_viewer: draws the world around the player in real stereo from the
// data the HytaleVR server plugin streams, while Hytale itself keeps running
// underneath for gameplay. The dashboard still turns controller input into
// Hytale keys and mouse; this program supplies that input through the same
// shared memory the old render hook used.

#include <windows.h>
#include <shellapi.h>
#include <GL/gl.h>
#include <openvr.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "plugin_bridge.h"
#include "vr_camera_shared.h"
#include "vr_item_assets.h"
#include "vr_math.h"
#include "vr_physical_interactions.h"
#include "viewer/viewer_gl.h"
#include "viewer/viewer_mesher.h"
#include "viewer/viewer_pose.h"
#include "viewer/viewer_world.h"

using namespace hytalevr::viewer;
using hytalevr::Matrix4;
using hytalevr::identity_matrix;
using hytalevr::multiply;
namespace glx = hytalevr::viewer::gl;

namespace {

constexpr int kTile = 32;
constexpr int kMaxLayers = 2048;
// The plugin streams 3 sections around the player; one more is kept so
// walking back and forth over a border does not reload terrain.
constexpr int kKeepRadius = 4;
constexpr int kKeepVertical = 3;
constexpr float kNearClip = 0.05f;
constexpr float kFarClip = 400.0f;
constexpr float kFogStart = 70.0f;
constexpr float kFogEnd = 108.0f;
constexpr float kReach = 4.5f;
constexpr float kSnapTurnRadians = 30.0f * kPi / 180.0f;
constexpr int kMsaaSamples = 4;
constexpr float kSkyColor[3] = {0.53f, 0.71f, 0.92f};

std::atomic<bool> g_quit{false};

// ---------------------------------------------------------------- logging

std::mutex g_log_mutex;
std::wstring g_log_path;

void log_line(const std::string& text) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (g_log_path.empty()) return;
    std::ofstream out(g_log_path, std::ios::app);
    SYSTEMTIME time{};
    GetLocalTime(&time);
    char stamp[32];
    std::snprintf(stamp, sizeof(stamp), "%02d:%02d:%02d.%03d ", time.wHour, time.wMinute, time.wSecond,
                  time.wMilliseconds);
    out << stamp << text << "\n";
}

void open_log() {
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    std::wstring directory = std::wstring(temp) + L"HytaleVR";
    CreateDirectoryW(directory.c_str(), nullptr);
    g_log_path = directory + L"\\viewer.log";
    std::ofstream(g_log_path, std::ios::trunc) << "HytaleVR viewer log\n";
}

double now_ms() {
    static LARGE_INTEGER frequency = [] {
        LARGE_INTEGER value{};
        QueryPerformanceFrequency(&value);
        return value;
    }();
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return static_cast<double>(counter.QuadPart) * 1000.0 / static_cast<double>(frequency.QuadPart);
}

std::string narrow(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(std::max(size, 1)) - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, result.data(), size, nullptr, nullptr);
    return result;
}

// ---------------------------------------------------------------- options

struct Options {
    DWORD pid = 0;
    std::wstring assets_zip;
};

bool file_exists(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::wstring parent_directory(std::wstring path) {
    while (!path.empty() && (path.back() == L'\\' || path.back() == L'/')) path.pop_back();
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

// Same places the old render hook looked: beside the running client, then the
// launcher's install folders.
std::wstring find_assets_zip(DWORD pid) {
    if (pid != 0) {
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (process) {
            wchar_t image[MAX_PATH * 2]{};
            DWORD size = static_cast<DWORD>(std::size(image));
            const bool ok = QueryFullProcessImageNameW(process, 0, image, &size) != FALSE;
            CloseHandle(process);
            if (ok) {
                const std::wstring candidate = parent_directory(parent_directory(image)) + L"\\Assets.zip";
                if (file_exists(candidate)) return candidate;
            }
        }
    }
    wchar_t appdata[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH) == 0) return {};
    for (const wchar_t* channel : {L"release", L"pre-release"}) {
        const std::wstring candidate = std::wstring(appdata) + L"\\Hytale\\install\\" + channel +
                                       L"\\package\\game\\latest\\Assets.zip";
        if (file_exists(candidate)) return candidate;
    }
    return {};
}

Options parse_options() {
    Options options;
    int count = 0;
    wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    for (int i = 1; arguments && i + 1 < count; ++i) {
        const std::wstring name = arguments[i];
        if (name == L"--pid") {
            options.pid = static_cast<DWORD>(_wtoi(arguments[++i]));
        } else if (name == L"--assets") {
            options.assets_zip = arguments[++i];
        }
    }
    if (arguments) LocalFree(arguments);
    if (options.assets_zip.empty()) options.assets_zip = find_assets_zip(options.pid);
    return options;
}

bool process_alive(DWORD pid) {
    if (pid == 0) return true;
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!process) return false;
    const bool alive = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    CloseHandle(process);
    return alive;
}

// ---------------------------------------------------------------- shared memory

VrCameraShared* g_shared = nullptr;

bool open_shared_memory() {
    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                        sizeof(VrCameraShared), kVrCameraMappingName);
    if (!mapping) return false;
    const bool existed = GetLastError() == ERROR_ALREADY_EXISTS;
    g_shared = static_cast<VrCameraShared*>(
        MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(VrCameraShared)));
    if (!g_shared) return false;
    if (!existed || g_shared->magic != kVrCameraMagic || g_shared->version != kVrCameraVersion) {
        if (existed) log_line("shared memory from a different dashboard version; resetting");
        *g_shared = VrCameraShared{};
    }
    return true;
}

// ---------------------------------------------------------------- textures

// One slot per block face texture. The world thread assigns slots, the
// loader thread decodes them, and the render thread uploads the pixels.
struct LayerSlot {
    std::atomic<int> state{0};  // 0 pending, 1 loaded, 2 failed
    float average[3] = {1.0f, 1.0f, 1.0f};
    float saturation = 0.0f;
};

LayerSlot g_layers[kMaxLayers];
std::atomic<int> g_layers_finished{0};

struct LayerPixels {
    int layer = 0;
    std::vector<uint8_t> rgba;
};

std::mutex g_layer_mutex;
std::condition_variable g_layer_signal;
std::deque<std::pair<int, std::string>> g_layer_requests;
std::vector<LayerPixels> g_layer_ready;

float saturation_of(const float rgb[3]) {
    const float high = std::max({rgb[0], rgb[1], rgb[2]});
    const float low = std::min({rgb[0], rgb[1], rgb[2]});
    return high <= 0.001f ? 0.0f : (high - low) / high;
}

// Scales the texture's top square (animated textures are vertical strips) to
// one 32x32 tile, and paints fully clear pixels with the average colour so
// mipmaps of leaves and glass do not grow dark fringes.
void make_tile(const std::vector<uint8_t>& rgba, uint32_t width, uint32_t height, LayerSlot& slot,
               std::vector<uint8_t>& tile) {
    const uint32_t size = std::min(width, height);
    tile.assign(static_cast<size_t>(kTile) * kTile * 4, 0);
    double sum[3] = {0, 0, 0};
    double weight = 0;
    for (int ty = 0; ty < kTile; ++ty) {
        for (int tx = 0; tx < kTile; ++tx) {
            const uint32_t x0 = static_cast<uint32_t>(tx) * size / kTile;
            const uint32_t y0 = static_cast<uint32_t>(ty) * size / kTile;
            const uint32_t x1 = std::max(x0 + 1, static_cast<uint32_t>(tx + 1) * size / kTile);
            const uint32_t y1 = std::max(y0 + 1, static_cast<uint32_t>(ty + 1) * size / kTile);
            uint32_t total[4] = {0, 0, 0, 0};
            uint32_t count = 0;
            for (uint32_t y = y0; y < y1; ++y) {
                for (uint32_t x = x0; x < x1; ++x) {
                    const uint8_t* p = &rgba[(static_cast<size_t>(y) * width + x) * 4];
                    for (int c = 0; c < 4; ++c) total[c] += p[c];
                    ++count;
                }
            }
            uint8_t* out = &tile[(static_cast<size_t>(ty) * kTile + tx) * 4];
            for (int c = 0; c < 4; ++c) out[c] = static_cast<uint8_t>(total[c] / count);
            if (out[3] > 128) {
                for (int c = 0; c < 3; ++c) sum[c] += out[c];
                weight += 1.0;
            }
        }
    }
    float average[3] = {1.0f, 1.0f, 1.0f};
    if (weight > 0) {
        for (int c = 0; c < 3; ++c) average[c] = static_cast<float>(sum[c] / weight / 255.0);
    }
    for (size_t i = 0; i < tile.size(); i += 4) {
        if (tile[i + 3] == 0) {
            for (int c = 0; c < 3; ++c) tile[i + c] = static_cast<uint8_t>(average[c] * 255.0f);
        }
    }
    for (int c = 0; c < 3; ++c) slot.average[c] = average[c];
    slot.saturation = saturation_of(average);
}

void texture_loader_main(std::wstring assets_zip) {
    int failures_logged = 0;
    while (!g_quit) {
        std::pair<int, std::string> request;
        {
            std::unique_lock<std::mutex> lock(g_layer_mutex);
            g_layer_signal.wait_for(lock, std::chrono::milliseconds(200),
                                    [] { return g_quit || !g_layer_requests.empty(); });
            if (g_layer_requests.empty()) continue;
            request = std::move(g_layer_requests.front());
            g_layer_requests.pop_front();
        }
        LayerSlot& slot = g_layers[request.first];
        std::vector<uint8_t> png;
        std::vector<uint8_t> rgba;
        std::string error;
        uint32_t width = 0;
        uint32_t height = 0;
        const bool ok = !assets_zip.empty() &&
                        hytalevr::read_vr_asset_by_suffix(assets_zip, request.second, png, error) &&
                        hytalevr::decode_vr_png(png, width, height, rgba) && width > 0 && height > 0;
        if (!ok) {
            if (failures_logged++ < 20) log_line("texture " + request.second + " failed: " + error);
            slot.state.store(2, std::memory_order_release);
            ++g_layers_finished;
            continue;
        }
        LayerPixels pixels;
        pixels.layer = request.first;
        make_tile(rgba, width, height, slot, pixels.rgba);
        {
            std::lock_guard<std::mutex> lock(g_layer_mutex);
            g_layer_ready.push_back(std::move(pixels));
        }
        slot.state.store(1, std::memory_order_release);
        ++g_layers_finished;
    }
}

// ---------------------------------------------------------------- world thread

struct MeshUpload {
    enum class Kind { Mesh, Remove, ClearAll } kind = Kind::Mesh;
    SectionKey key;
    SectionMesh mesh;
};

struct EntityTrack {
    EntityBox box;
    double previous[3]{};
    double current[3]{};
    double updated_ms = 0;
    double interval_ms = 150;
};

// Everything below is guarded by g_state_mutex.
std::mutex g_state_mutex;
bool g_has_player = false;
PlayerSample g_latest_player;
PositionSmoother g_feet_smoother(70.0);
std::unordered_map<int64_t, EntityTrack> g_entity_tracks;
std::deque<MeshUpload> g_uploads;
bool g_plugin_connected = false;
size_t g_section_count = 0;
// The right hand's reach ray in absolute world coordinates, from the render
// thread, and the block it touches, from the world thread.
bool g_reach_valid = false;
double g_reach_origin[3]{};
float g_reach_direction[3]{};
bool g_highlight_valid = false;
int g_highlight[3]{};

std::unordered_map<std::string, int> g_layer_by_path;  // world thread only
int g_next_layer = 1;

int layer_for_path(const std::string& path) {
    if (path.empty()) return 0;
    const auto found = g_layer_by_path.find(path);
    if (found != g_layer_by_path.end()) return found->second;
    if (g_next_layer >= kMaxLayers) return 0;
    const int layer = g_next_layer++;
    g_layer_by_path.emplace(path, layer);
    {
        std::lock_guard<std::mutex> lock(g_layer_mutex);
        g_layer_requests.emplace_back(layer, path);
    }
    g_layer_signal.notify_one();
    return layer;
}

FaceMaterial material_for(int32_t, const BlockInfo& info, int face) {
    const std::string* path = &info.faces[static_cast<size_t>(face)];
    for (int i = 0; path->empty() && i < kFaceCount; ++i) path = &info.faces[static_cast<size_t>(i)];
    const int layer = layer_for_path(*path);
    const float block[3] = {info.color[0] / 255.0f, info.color[1] / 255.0f, info.color[2] / 255.0f};
    FaceMaterial material;
    if (layer > 0 && g_layers[layer].state.load(std::memory_order_acquire) == 1) {
        const LayerSlot& slot = g_layers[layer];
        material.layer = static_cast<float>(layer);
        // Grass tops and leaves ship grey and are tinted by the biome in game;
        // the block's computed colour is the closest thing the plugin sends.
        if (slot.saturation < 0.10f && saturation_of(block) > 0.15f) {
            const float luma = std::max(0.05f, 0.30f * slot.average[0] + 0.59f * slot.average[1] +
                                                   0.11f * slot.average[2]);
            for (int c = 0; c < 3; ++c) material.tint[c] = std::clamp(block[c] / luma, 0.0f, 1.6f);
        }
    } else {
        material.layer = 0.0f;
        for (int c = 0; c < 3; ++c) material.tint[c] = block[c];
    }
    return material;
}

bool raycast_block(const World& world, const double origin[3], const float direction[3], float reach,
                   int hit[3]) {
    int cell[3];
    int step[3];
    double next[3];
    double delta[3];
    for (int axis = 0; axis < 3; ++axis) {
        cell[axis] = static_cast<int>(std::floor(origin[axis]));
        const double d = direction[axis];
        step[axis] = d > 0 ? 1 : -1;
        delta[axis] = std::fabs(d) < 1e-9 ? 1e30 : std::fabs(1.0 / d);
        const double boundary = d > 0 ? (cell[axis] + 1.0 - origin[axis]) : (origin[axis] - cell[axis]);
        next[axis] = std::fabs(d) < 1e-9 ? 1e30 : boundary * delta[axis];
    }
    double travelled = 0.0;
    for (int i = 0; i < 64 && travelled <= reach; ++i) {
        const int32_t id = world.block_at(cell[0], cell[1], cell[2]);
        if (id > 0) {
            const BlockInfo* info = world.block(id);
            if (info && info->draw != DrawKind::Empty) {
                for (int axis = 0; axis < 3; ++axis) hit[axis] = cell[axis];
                return true;
            }
        }
        const int axis = next[0] < next[1] ? (next[0] < next[2] ? 0 : 2) : (next[1] < next[2] ? 1 : 2);
        travelled = next[axis];
        next[axis] += delta[axis];
        cell[axis] += step[axis];
    }
    return false;
}

void world_thread_main() {
    hytalevr::PluginBridge bridge("hytale-vr-viewer", 64u * 1024u * 1024u);
    World world;
    std::unordered_set<SectionKey, SectionKeyHash> dirty;
    int textures_seen = 0;
    uint64_t last_full_remesh = 0;
    uint64_t last_drop = 0;
    bool was_connected = false;

    while (!g_quit) {
        const uint64_t tick = GetTickCount64();
        const std::vector<std::string> lines = bridge.poll(tick);
        const bool connected = bridge.connected();
        if (connected != was_connected) {
            log_line(connected ? "connected to the HytaleVR plugin" : "plugin link lost");
            was_connected = connected;
        }

        for (const std::string& line : lines) {
            SectionKey key;
            const World::Change change = world.apply(line, &key);
            if (change == World::Change::Reset) {
                dirty.clear();
                std::lock_guard<std::mutex> lock(g_state_mutex);
                g_uploads.clear();
                g_uploads.push_back({MeshUpload::Kind::ClearAll, {}, {}});
                g_entity_tracks.clear();
                g_feet_smoother.reset();
                log_line("world " + world.name());
            } else if (change == World::Change::Player) {
                std::lock_guard<std::mutex> lock(g_state_mutex);
                g_latest_player = world.player();
                g_has_player = true;
                g_feet_smoother.add(now_ms(), g_latest_player.feet);
            } else if (change == World::Change::Entities) {
                const double now = now_ms();
                std::lock_guard<std::mutex> lock(g_state_mutex);
                std::unordered_set<int64_t> present;
                for (const EntityBox& box : world.entities()) {
                    present.insert(box.id);
                    auto found = g_entity_tracks.find(box.id);
                    if (found == g_entity_tracks.end()) {
                        EntityTrack track;
                        track.box = box;
                        std::copy(box.position, box.position + 3, track.previous);
                        std::copy(box.position, box.position + 3, track.current);
                        track.updated_ms = now;
                        g_entity_tracks.emplace(box.id, track);
                    } else {
                        EntityTrack& track = found->second;
                        std::copy(track.current, track.current + 3, track.previous);
                        std::copy(box.position, box.position + 3, track.current);
                        track.interval_ms = std::clamp(now - track.updated_ms, 50.0, 400.0);
                        track.updated_ms = now;
                        track.box = box;
                    }
                }
                for (auto it = g_entity_tracks.begin(); it != g_entity_tracks.end();) {
                    it = present.count(it->first) ? std::next(it) : g_entity_tracks.erase(it);
                }
            } else if (change == World::Change::Section) {
                dirty.insert(key);
                // Faces on the shared border may now be hidden or exposed.
                const int offsets[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
                for (const auto& offset : offsets) {
                    const SectionKey neighbour{key.x + offset[0], key.y + offset[1], key.z + offset[2]};
                    if (world.section(neighbour)) dirty.insert(neighbour);
                }
            }
        }

        if (world.has_player() && tick - last_drop > 1000) {
            last_drop = tick;
            const PlayerSample& player = world.player();
            const SectionKey centre{floor_div32(static_cast<int>(std::floor(player.feet[0]))),
                                    floor_div32(static_cast<int>(std::floor(player.feet[1]))),
                                    floor_div32(static_cast<int>(std::floor(player.feet[2])))};
            std::vector<SectionKey> dropped;
            world.drop_sections_outside(centre, kKeepRadius, kKeepVertical, dropped);
            if (!dropped.empty()) {
                std::lock_guard<std::mutex> lock(g_state_mutex);
                for (const SectionKey& key : dropped) {
                    dirty.erase(key);
                    g_uploads.push_back({MeshUpload::Kind::Remove, key, {}});
                }
            }
        }

        // Textures arrive after the first meshes were built with flat colours;
        // rebuild everything once a batch has landed.
        const int finished = g_layers_finished.load();
        if (finished != textures_seen && tick - last_full_remesh > 1000) {
            textures_seen = finished;
            last_full_remesh = tick;
            for (const SectionKey& key : world.section_keys()) dirty.insert(key);
        }

        if (!dirty.empty()) {
            // Nearest sections first so the ground under the player appears first.
            std::vector<SectionKey> order(dirty.begin(), dirty.end());
            if (world.has_player()) {
                const double* feet = world.player().feet;
                auto distance = [&](const SectionKey& key) {
                    const double dx = key.x * 32.0 + 16.0 - feet[0];
                    const double dy = key.y * 32.0 + 16.0 - feet[1];
                    const double dz = key.z * 32.0 + 16.0 - feet[2];
                    return dx * dx + dy * dy * 4.0 + dz * dz;
                };
                std::sort(order.begin(), order.end(),
                          [&](const SectionKey& a, const SectionKey& b) { return distance(a) < distance(b); });
            }
            const double started = now_ms();
            for (const SectionKey& key : order) {
                MeshUpload upload;
                upload.key = key;
                build_section_mesh(world, key, material_for, upload.mesh);
                dirty.erase(key);
                std::lock_guard<std::mutex> lock(g_state_mutex);
                g_uploads.push_back(std::move(upload));
                if (now_ms() - started > 6.0) break;
            }
        }

        {
            std::lock_guard<std::mutex> lock(g_state_mutex);
            g_plugin_connected = connected;
            g_section_count = world.section_count();
            g_highlight_valid = g_reach_valid &&
                                raycast_block(world, g_reach_origin, g_reach_direction, kReach, g_highlight);
        }

        if (lines.empty() && dirty.empty()) Sleep(2);
    }
}

// ---------------------------------------------------------------- Hytale screen capture

// Copies Hytale's window off the desktop a few times a second so menus, the
// inventory and the hotbar can be shown on a panel in VR.
struct PanelFrame {
    std::vector<uint8_t> bgra;
    int width = 0;
    int height = 0;
    uint64_t serial = 0;
};

std::mutex g_panel_mutex;
PanelFrame g_panel_frame;
std::atomic<int> g_panel_interval_ms{200};

struct WindowSearch {
    DWORD pid = 0;
    HWND window = nullptr;
    LONG area = 0;
};

BOOL CALLBACK find_window_callback(HWND window, LPARAM parameter) {
    auto* search = reinterpret_cast<WindowSearch*>(parameter);
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid != search->pid || !IsWindowVisible(window) || GetWindow(window, GW_OWNER)) return TRUE;
    RECT rect{};
    GetClientRect(window, &rect);
    const LONG area = (rect.right - rect.left) * (rect.bottom - rect.top);
    if (area > search->area) {
        search->area = area;
        search->window = window;
    }
    return TRUE;
}

void capture_thread_main(DWORD pid) {
    HWND window = nullptr;
    HDC memory = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = nullptr;
    void* bits = nullptr;
    int bitmap_width = 0;
    int bitmap_height = 0;
    while (!g_quit) {
        Sleep(static_cast<DWORD>(g_panel_interval_ms.load()));
        if (pid == 0) continue;
        if (!window || !IsWindow(window)) {
            WindowSearch search;
            search.pid = pid;
            EnumWindows(find_window_callback, reinterpret_cast<LPARAM>(&search));
            window = search.window;
            if (!window) continue;
        }
        RECT client{};
        if (IsIconic(window) || !GetClientRect(window, &client)) continue;
        const int width = client.right - client.left;
        const int height = client.bottom - client.top;
        if (width < 16 || height < 16) continue;
        POINT top_left{0, 0};
        ClientToScreen(window, &top_left);
        const int target_width = std::min(width, 1280);
        const int target_height = std::max(1, height * target_width / width);
        if (target_width != bitmap_width || target_height != bitmap_height) {
            if (bitmap) DeleteObject(bitmap);
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(info.bmiHeader);
            info.bmiHeader.biWidth = target_width;
            info.bmiHeader.biHeight = -target_height;  // top-down rows
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            bitmap = CreateDIBSection(memory, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
            if (!bitmap) {
                bitmap_width = bitmap_height = 0;
                continue;
            }
            bitmap_width = target_width;
            bitmap_height = target_height;
        }
        HGDIOBJ previous = SelectObject(memory, bitmap);
        HDC screen = GetDC(nullptr);
        SetStretchBltMode(memory, HALFTONE);
        SetBrushOrgEx(memory, 0, 0, nullptr);
        StretchBlt(memory, 0, 0, target_width, target_height, screen, top_left.x, top_left.y, width, height,
                   SRCCOPY);
        ReleaseDC(nullptr, screen);
        GdiFlush();
        SelectObject(memory, previous);
        std::lock_guard<std::mutex> lock(g_panel_mutex);
        const size_t bytes = static_cast<size_t>(target_width) * target_height * 4;
        g_panel_frame.bgra.assign(static_cast<const uint8_t*>(bits), static_cast<const uint8_t*>(bits) + bytes);
        g_panel_frame.width = target_width;
        g_panel_frame.height = target_height;
        ++g_panel_frame.serial;
    }
    if (bitmap) DeleteObject(bitmap);
    DeleteDC(memory);
}

// ---------------------------------------------------------------- GL helpers

GLuint compile_shader(GLenum type, const char* source) {
    const GLuint shader = glx::CreateShader(type);
    glx::ShaderSource(shader, 1, &source, nullptr);
    glx::CompileShader(shader);
    GLint ok = 0;
    glx::GetShaderiv(shader, glx::kCompileStatus, &ok);
    if (!ok) {
        char text[2048]{};
        glx::GetShaderInfoLog(shader, sizeof(text), nullptr, text);
        log_line(std::string("shader compile failed: ") + text);
    }
    return shader;
}

GLuint link_program(const char* vertex, const char* fragment) {
    const GLuint program = glx::CreateProgram();
    const GLuint vs = compile_shader(glx::kVertexShader, vertex);
    const GLuint fs = compile_shader(glx::kFragmentShader, fragment);
    glx::AttachShader(program, vs);
    glx::AttachShader(program, fs);
    glx::LinkProgram(program);
    GLint ok = 0;
    glx::GetProgramiv(program, glx::kLinkStatus, &ok);
    if (!ok) {
        char text[2048]{};
        glx::GetProgramInfoLog(program, sizeof(text), nullptr, text);
        log_line(std::string("shader link failed: ") + text);
    }
    glx::DeleteShader(vs);
    glx::DeleteShader(fs);
    return program;
}

const char* kWorldVertexShader = R"(#version 330 core
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in float a_layer;
layout(location = 3) in vec4 a_color;
uniform mat4 u_view_projection;
uniform vec3 u_offset;
uniform vec3 u_camera;
out vec3 v_uv;
out vec4 v_color;
out float v_distance;
void main() {
    vec3 world = a_position + u_offset;
    v_uv = vec3(a_uv, a_layer);
    v_color = a_color;
    v_distance = length(world - u_camera);
    gl_Position = u_view_projection * vec4(world, 1.0);
}
)";

const char* kWorldFragmentShader = R"(#version 330 core
in vec3 v_uv;
in vec4 v_color;
in float v_distance;
uniform sampler2DArray u_texture;
uniform vec3 u_fog_color;
uniform vec2 u_fog;
uniform float u_alpha_cutoff;
out vec4 o_color;
void main() {
    vec4 color = texture(u_texture, v_uv) * v_color;
    if (color.a < u_alpha_cutoff) discard;
    float fog = clamp((v_distance - u_fog.x) / (u_fog.y - u_fog.x), 0.0, 1.0);
    o_color = vec4(mix(color.rgb, u_fog_color, fog), color.a);
}
)";

const char* kPanelVertexShader = R"(#version 330 core
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec2 a_uv;
uniform mat4 u_mvp;
out vec2 v_uv;
void main() {
    v_uv = a_uv;
    gl_Position = u_mvp * vec4(a_position, 1.0);
}
)";

const char* kPanelFragmentShader = R"(#version 330 core
in vec2 v_uv;
uniform sampler2D u_texture;
out vec4 o_color;
void main() {
    o_color = vec4(texture(u_texture, v_uv).rgb, 1.0);
}
)";

struct GpuMesh {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLsizei count = 0;
};

void setup_mesh_attributes() {
    const GLsizei stride = sizeof(MeshVertex);
    glx::EnableVertexAttribArray(0);
    glx::VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(0));
    glx::EnableVertexAttribArray(1);
    glx::VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(12));
    glx::EnableVertexAttribArray(2);
    glx::VertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(20));
    glx::EnableVertexAttribArray(3);
    glx::VertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, reinterpret_cast<const void*>(24));
}

void release_mesh(GpuMesh& mesh) {
    if (mesh.vbo) glx::DeleteBuffers(1, &mesh.vbo);
    if (mesh.vao) glx::DeleteVertexArrays(1, &mesh.vao);
    mesh = GpuMesh{};
}

void upload_mesh(GpuMesh& mesh, const std::vector<MeshVertex>& vertices, GLenum usage) {
    if (vertices.empty()) {
        if (usage == glx::kStaticDraw) release_mesh(mesh);
        mesh.count = 0;
        return;
    }
    if (!mesh.vao) {
        glx::GenVertexArrays(1, &mesh.vao);
        glx::GenBuffers(1, &mesh.vbo);
        glx::BindVertexArray(mesh.vao);
        glx::BindBuffer(glx::kArrayBuffer, mesh.vbo);
        setup_mesh_attributes();
    } else {
        glx::BindBuffer(glx::kArrayBuffer, mesh.vbo);
    }
    glx::BufferData(glx::kArrayBuffer, static_cast<glx::GLsizeiptr>(vertices.size() * sizeof(MeshVertex)),
                    vertices.data(), usage);
    mesh.count = static_cast<GLsizei>(vertices.size());
}

struct GpuSection {
    GpuMesh opaque;
    GpuMesh translucent;
};

struct EyeTarget {
    GLuint msaa_framebuffer = 0;
    GLuint msaa_color = 0;
    GLuint msaa_depth = 0;
    GLuint resolve_framebuffer = 0;
    GLuint resolve_texture = 0;
};

bool create_eye_target(EyeTarget& target, uint32_t width, uint32_t height, int samples) {
    glx::GenFramebuffers(1, &target.msaa_framebuffer);
    glx::BindFramebuffer(glx::kFramebuffer, target.msaa_framebuffer);
    glx::GenRenderbuffers(1, &target.msaa_color);
    glx::BindRenderbuffer(glx::kRenderbuffer, target.msaa_color);
    glx::RenderbufferStorageMultisample(glx::kRenderbuffer, samples, glx::kRgba8, static_cast<GLsizei>(width),
                                        static_cast<GLsizei>(height));
    glx::FramebufferRenderbuffer(glx::kFramebuffer, glx::kColorAttachment0, glx::kRenderbuffer, target.msaa_color);
    glx::GenRenderbuffers(1, &target.msaa_depth);
    glx::BindRenderbuffer(glx::kRenderbuffer, target.msaa_depth);
    glx::RenderbufferStorageMultisample(glx::kRenderbuffer, samples, glx::kDepthComponent24,
                                        static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    glx::FramebufferRenderbuffer(glx::kFramebuffer, glx::kDepthAttachment, glx::kRenderbuffer, target.msaa_depth);
    const bool msaa_ok = glx::CheckFramebufferStatus(glx::kFramebuffer) == glx::kFramebufferComplete;

    glGenTextures(1, &target.resolve_texture);
    glBindTexture(GL_TEXTURE_2D, target.resolve_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, glx::kTextureMaxLevel, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, glx::kRgba8, static_cast<GLsizei>(width), static_cast<GLsizei>(height), 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glx::GenFramebuffers(1, &target.resolve_framebuffer);
    glx::BindFramebuffer(glx::kFramebuffer, target.resolve_framebuffer);
    glx::FramebufferTexture2D(glx::kFramebuffer, glx::kColorAttachment0, GL_TEXTURE_2D, target.resolve_texture, 0);
    const bool resolve_ok = glx::CheckFramebufferStatus(glx::kFramebuffer) == glx::kFramebufferComplete;
    glx::BindFramebuffer(glx::kFramebuffer, 0);
    return msaa_ok && resolve_ok;
}

Matrix4 from_openvr(const vr::HmdMatrix44_t& m) {
    Matrix4 result{};
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) result.value[column * 4 + row] = m.m[row][column];
    }
    return result;
}

void pose_to_array(const vr::HmdMatrix34_t& m, float out[12]) {
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 4; ++column) out[row * 4 + column] = m.m[row][column];
    }
}

// True when the box is at least partly inside the view.
bool box_visible(const Matrix4& view_projection, const float low[3], const float high[3]) {
    const float* m = view_projection.value;
    for (int plane = 0; plane < 6; ++plane) {
        const int row = plane / 2;
        const float sign = (plane % 2 == 0) ? 1.0f : -1.0f;
        float n[4];
        for (int c = 0; c < 4; ++c) n[c] = m[c * 4 + 3] + sign * m[c * 4 + row];
        const float px = n[0] > 0 ? high[0] : low[0];
        const float py = n[1] > 0 ? high[1] : low[1];
        const float pz = n[2] > 0 ? high[2] : low[2];
        if (n[0] * px + n[1] * py + n[2] * pz + n[3] < 0.0f) return false;
    }
    return true;
}

// Appends a box: `m` maps the unit cube (-0.5..0.5) into render space.
void emit_box(std::vector<MeshVertex>& out, const Matrix4& m, const float color[4]) {
    static const int corners[6][4][3] = {
        {{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0}},  // up
        {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}},  // down
        {{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}},  // north
        {{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}},  // south
        {{1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}},  // east
        {{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}},  // west
    };
    static const float shades[6] = {1.0f, 0.55f, 0.8f, 0.8f, 0.68f, 0.68f};
    for (int face = 0; face < 6; ++face) {
        MeshVertex quad[4];
        for (int corner = 0; corner < 4; ++corner) {
            const float local[3] = {corners[face][corner][0] - 0.5f, corners[face][corner][1] - 0.5f,
                                    corners[face][corner][2] - 0.5f};
            transform_point(m, local, quad[corner].position);
            quad[corner].uv[0] = 0.5f;
            quad[corner].uv[1] = 0.5f;
            quad[corner].layer = 0.0f;
            for (int c = 0; c < 3; ++c) {
                quad[corner].color[c] = static_cast<uint8_t>(std::clamp(color[c] * shades[face], 0.0f, 1.0f) * 255);
            }
            quad[corner].color[3] = static_cast<uint8_t>(std::clamp(color[3], 0.0f, 1.0f) * 255);
        }
        const int order[6] = {0, 1, 2, 0, 2, 3};
        for (int i : order) out.push_back(quad[i]);
    }
}

Matrix4 box_matrix(const float centre[3], const float size[3], float yaw) {
    Matrix4 m = identity_matrix();
    const float c = std::cos(yaw);
    const float s = std::sin(yaw);
    // Columns are the box's scaled local axes.
    m.value[0] = c * size[0];
    m.value[2] = -s * size[0];
    m.value[5] = size[1];
    m.value[8] = s * size[2];
    m.value[10] = c * size[2];
    m.value[12] = centre[0];
    m.value[13] = centre[1];
    m.value[14] = centre[2];
    return m;
}

Matrix4 scale_matrix(float x, float y, float z) {
    Matrix4 m = identity_matrix();
    m.value[0] = x;
    m.value[5] = y;
    m.value[10] = z;
    return m;
}

Matrix4 translation_matrix(float x, float y, float z) {
    Matrix4 m = identity_matrix();
    m.value[12] = x;
    m.value[13] = y;
    m.value[14] = z;
    return m;
}

void color_from_name(const std::string& name, float out[4]) {
    uint32_t hash = 2166136261u;
    for (char c : name) hash = (hash ^ static_cast<uint8_t>(c)) * 16777619u;
    out[0] = 0.45f + 0.45f * static_cast<float>(hash & 0xFF) / 255.0f;
    out[1] = 0.40f + 0.45f * static_cast<float>((hash >> 8) & 0xFF) / 255.0f;
    out[2] = 0.35f + 0.45f * static_cast<float>((hash >> 16) & 0xFF) / 255.0f;
    out[3] = 1.0f;
}

// ---------------------------------------------------------------- VR input

struct Actions {
    bool ready = false;
    vr::VRActionSetHandle_t set = vr::k_ulInvalidActionSetHandle;
    vr::VRActionHandle_t move = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t turn = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t jump = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t sprint = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t left_pose = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t right_pose = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t right_trigger = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t left_trigger = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t button_x = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t button_y = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t button_b = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t left_grip = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t right_grip = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t right_stick_click = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t haptic_left = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t haptic_right = vr::k_ulInvalidActionHandle;
};

Actions g_actions;

bool initialize_actions() {
    if (g_actions.ready) return true;
    auto* input = vr::VRInput();
    if (!input) return false;
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    const std::string manifest = narrow(parent_directory(path) + L"\\hytalevr_actions.json");
    vr::EVRInputError error = input->SetActionManifestPath(manifest.c_str());
    auto handle = [&](const char* name, vr::VRActionHandle_t& out) {
        if (error == vr::VRInputError_None) error = input->GetActionHandle(name, &out);
    };
    if (error == vr::VRInputError_None) error = input->GetActionSetHandle("/actions/hytale", &g_actions.set);
    handle("/actions/hytale/in/move", g_actions.move);
    handle("/actions/hytale/in/turn", g_actions.turn);
    handle("/actions/hytale/in/jump", g_actions.jump);
    handle("/actions/hytale/in/sprint", g_actions.sprint);
    handle("/actions/hytale/in/left_pose", g_actions.left_pose);
    handle("/actions/hytale/in/right_pose", g_actions.right_pose);
    handle("/actions/hytale/in/right_trigger", g_actions.right_trigger);
    handle("/actions/hytale/in/left_trigger", g_actions.left_trigger);
    handle("/actions/hytale/in/button_x", g_actions.button_x);
    handle("/actions/hytale/in/button_y", g_actions.button_y);
    handle("/actions/hytale/in/button_b", g_actions.button_b);
    handle("/actions/hytale/in/left_grip", g_actions.left_grip);
    handle("/actions/hytale/in/right_grip", g_actions.right_grip);
    handle("/actions/hytale/in/right_stick_click", g_actions.right_stick_click);
    if (error == vr::VRInputError_None) {
        input->GetActionHandle("/actions/hytale/out/haptic_left", &g_actions.haptic_left);
        input->GetActionHandle("/actions/hytale/out/haptic_right", &g_actions.haptic_right);
    }
    g_actions.ready = error == vr::VRInputError_None;
    if (!g_actions.ready) log_line("SteamVR input setup failed: " + std::to_string(static_cast<int>(error)));
    if (g_shared) g_shared->controller_input_error = static_cast<int32_t>(error);
    return g_actions.ready;
}

void pulse(int hand, float seconds, float frequency, float amplitude) {
    const vr::VRActionHandle_t action = hand == 0 ? g_actions.haptic_left : g_actions.haptic_right;
    if (action == vr::k_ulInvalidActionHandle || !vr::VRInput()) return;
    vr::VRInput()->TriggerHapticVibrationAction(action, 0.0f, seconds, frequency, amplitude,
                                                vr::k_ulInvalidInputValueHandle);
}

struct ControllerState {
    bool valid = false;
    float pose[12]{};
};

struct InputFrame {
    float move[2]{};
    float turn[2]{};
    bool jump = false;
    bool sprint = false;
    float right_trigger = 0.0f;
    float left_trigger = 0.0f;
    float left_grip = 0.0f;
    float right_grip = 0.0f;
    bool button_x = false;
    bool button_y = false;
    bool button_b = false;
    bool right_stick_click = false;
    ControllerState hands[2];
};

bool read_input(InputFrame& frame) {
    if (!initialize_actions()) return false;
    auto* input = vr::VRInput();
    vr::VRActiveActionSet_t active{};
    active.ulActionSet = g_actions.set;
    active.ulRestrictedToDevice = vr::k_ulInvalidInputValueHandle;
    active.ulSecondaryActionSet = vr::k_ulInvalidActionSetHandle;
    if (input->UpdateActionState(&active, sizeof(active), 1) != vr::VRInputError_None) return false;

    auto analog = [&](vr::VRActionHandle_t action, float* out, int count) {
        vr::InputAnalogActionData_t data{};
        if (input->GetAnalogActionData(action, &data, sizeof(data), vr::k_ulInvalidInputValueHandle) ==
                vr::VRInputError_None &&
            data.bActive) {
            out[0] = data.x;
            if (count > 1) out[1] = data.y;
        }
    };
    auto digital = [&](vr::VRActionHandle_t action) {
        vr::InputDigitalActionData_t data{};
        return input->GetDigitalActionData(action, &data, sizeof(data), vr::k_ulInvalidInputValueHandle) ==
                   vr::VRInputError_None &&
               data.bActive && data.bState;
    };
    auto pose = [&](vr::VRActionHandle_t action, ControllerState& out) {
        vr::InputPoseActionData_t data{};
        out.valid = input->GetPoseActionDataForNextFrame(action, vr::TrackingUniverseStanding, &data, sizeof(data),
                                                         vr::k_ulInvalidInputValueHandle) ==
                        vr::VRInputError_None &&
                    data.bActive && data.pose.bDeviceIsConnected && data.pose.bPoseIsValid;
        if (out.valid) pose_to_array(data.pose.mDeviceToAbsoluteTracking, out.pose);
    };
    analog(g_actions.move, frame.move, 2);
    analog(g_actions.turn, frame.turn, 2);
    analog(g_actions.right_trigger, &frame.right_trigger, 1);
    analog(g_actions.left_trigger, &frame.left_trigger, 1);
    analog(g_actions.left_grip, &frame.left_grip, 1);
    analog(g_actions.right_grip, &frame.right_grip, 1);
    frame.jump = digital(g_actions.jump);
    frame.sprint = digital(g_actions.sprint);
    frame.button_x = digital(g_actions.button_x);
    frame.button_y = digital(g_actions.button_y);
    frame.button_b = digital(g_actions.button_b);
    frame.right_stick_click = digital(g_actions.right_stick_click);
    pose(g_actions.left_pose, frame.hands[0]);
    pose(g_actions.right_pose, frame.hands[1]);
    return true;
}

// A trigger or grip counts as pressed past 0.65 and released below 0.45, so
// a resting finger does not chatter.
bool hysteresis(bool pressed, float value) { return pressed ? value > 0.45f : value > 0.65f; }

// ---------------------------------------------------------------- the viewer

struct Viewer {
    Options options;
    HWND window = nullptr;
    HDC dc = nullptr;
    HGLRC context = nullptr;
    vr::IVRSystem* system = nullptr;
    uint32_t eye_width = 0;
    uint32_t eye_height = 0;
    EyeTarget eyes[2];
    Matrix4 projection[2];
    Matrix4 eye_to_head[2];

    GLuint world_program = 0;
    GLint world_view_projection = -1;
    GLint world_offset = -1;
    GLint world_camera = -1;
    GLint world_texture = -1;
    GLint world_fog_color = -1;
    GLint world_fog = -1;
    GLint world_alpha_cutoff = -1;
    GLuint panel_program = 0;
    GLint panel_mvp = -1;
    GLint panel_texture_uniform = -1;
    GLuint block_textures = 0;
    bool block_mipmaps_dirty = false;
    GLuint panel_texture = 0;
    int panel_texture_width = 0;
    int panel_texture_height = 0;
    uint64_t panel_serial = 0;
    GLuint panel_vao = 0;
    GLuint panel_vbo = 0;

    std::unordered_map<SectionKey, GpuSection, SectionKeyHash> sections;
    GpuMesh dynamic_opaque;
    GpuMesh dynamic_translucent;

    TrackingAnchor anchor;
    bool anchored = false;
    uint32_t recenter_sequence = 0;
    bool snap_armed = true;
    double last_frame_ms = 0;

    bool right_trigger_pressed = false;
    bool left_trigger_pressed = false;
    bool left_grip_pressed = false;
    bool right_grip_pressed = false;
    bool previous_button_y = false;
    hytalevr::PhysicalMotionHistory tip_history[2];
    bool swing_armed[2] = {true, true};
    uint64_t swing_cooldown_until[2] = {0, 0};

    bool panel_open = false;
    float panel_pose[12]{};  // tracking space, row-major 3x4
    float panel_width = 1.4f;
    bool pointer_on_panel = false;
    float pointer_hit[3]{};  // tracking space

    double enabled_lost_since = 0;
    bool seen_enabled = false;
};

bool create_window_and_context(Viewer& viewer) {
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = DefWindowProcW;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = L"HytaleVrViewer";
    window_class.style = CS_OWNDC;
    RegisterClassW(&window_class);
    // Never shown or activated: Hytale must keep keyboard focus for the
    // dashboard's input to reach it.
    viewer.window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, window_class.lpszClassName,
                                    L"HytaleVR Viewer", WS_POPUP, 0, 0, 64, 64, nullptr, nullptr,
                                    window_class.hInstance, nullptr);
    if (!viewer.window) return false;
    viewer.dc = GetDC(viewer.window);
    PIXELFORMATDESCRIPTOR format{};
    format.nSize = sizeof(format);
    format.nVersion = 1;
    format.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    format.iPixelType = PFD_TYPE_RGBA;
    format.cColorBits = 32;
    format.cDepthBits = 24;
    const int index = ChoosePixelFormat(viewer.dc, &format);
    if (index == 0 || !SetPixelFormat(viewer.dc, index, &format)) return false;
    viewer.context = wglCreateContext(viewer.dc);
    if (!viewer.context || !wglMakeCurrent(viewer.dc, viewer.context)) return false;
    if (const char* missing = glx::load()) {
        log_line(std::string("OpenGL 3.3 function missing: ") + missing);
        return false;
    }
    log_line(std::string("OpenGL ") + reinterpret_cast<const char*>(glGetString(GL_VERSION)) + " on " +
             reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
    return true;
}

void create_gl_resources(Viewer& viewer) {
    viewer.world_program = link_program(kWorldVertexShader, kWorldFragmentShader);
    viewer.world_view_projection = glx::GetUniformLocation(viewer.world_program, "u_view_projection");
    viewer.world_offset = glx::GetUniformLocation(viewer.world_program, "u_offset");
    viewer.world_camera = glx::GetUniformLocation(viewer.world_program, "u_camera");
    viewer.world_texture = glx::GetUniformLocation(viewer.world_program, "u_texture");
    viewer.world_fog_color = glx::GetUniformLocation(viewer.world_program, "u_fog_color");
    viewer.world_fog = glx::GetUniformLocation(viewer.world_program, "u_fog");
    viewer.world_alpha_cutoff = glx::GetUniformLocation(viewer.world_program, "u_alpha_cutoff");
    viewer.panel_program = link_program(kPanelVertexShader, kPanelFragmentShader);
    viewer.panel_mvp = glx::GetUniformLocation(viewer.panel_program, "u_mvp");
    viewer.panel_texture_uniform = glx::GetUniformLocation(viewer.panel_program, "u_texture");

    glGenTextures(1, &viewer.block_textures);
    glBindTexture(glx::kTexture2DArray, viewer.block_textures);
    glx::TexImage3D(glx::kTexture2DArray, 0, glx::kRgba8, kTile, kTile, kMaxLayers, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                    nullptr);
    // Layer 0 is plain white: flat-coloured blocks, mobs and hands use it.
    std::vector<uint8_t> white(static_cast<size_t>(kTile) * kTile * 4, 255);
    glx::TexSubImage3D(glx::kTexture2DArray, 0, 0, 0, 0, kTile, kTile, 1, GL_RGBA, GL_UNSIGNED_BYTE, white.data());
    glTexParameteri(glx::kTexture2DArray, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(glx::kTexture2DArray, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
    glTexParameteri(glx::kTexture2DArray, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(glx::kTexture2DArray, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameterf(glx::kTexture2DArray, glx::kTextureMaxAnisotropy, 8.0f);
    glGetError();  // anisotropy is optional
    glx::GenerateMipmap(glx::kTexture2DArray);

    glGenTextures(1, &viewer.panel_texture);
    glBindTexture(GL_TEXTURE_2D, viewer.panel_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, glx::kClampToEdge);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, glx::kClampToEdge);
    const uint8_t grey[4] = {40, 40, 40, 255};
    glTexImage2D(GL_TEXTURE_2D, 0, glx::kRgba8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, grey);

    // A unit quad (-0.5..0.5 in X and Y, facing +Z); the image's top row is v=0.
    const float quad[] = {
        -0.5f, -0.5f, 0.0f, 0.0f, 1.0f, 0.5f, -0.5f, 0.0f, 1.0f, 1.0f, 0.5f, 0.5f, 0.0f, 1.0f, 0.0f,
        -0.5f, -0.5f, 0.0f, 0.0f, 1.0f, 0.5f, 0.5f,  0.0f, 1.0f, 0.0f, -0.5f, 0.5f, 0.0f, 0.0f, 0.0f,
    };
    glx::GenVertexArrays(1, &viewer.panel_vao);
    glx::GenBuffers(1, &viewer.panel_vbo);
    glx::BindVertexArray(viewer.panel_vao);
    glx::BindBuffer(glx::kArrayBuffer, viewer.panel_vbo);
    glx::BufferData(glx::kArrayBuffer, sizeof(quad), quad, glx::kStaticDraw);
    glx::EnableVertexAttribArray(0);
    glx::VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 20, reinterpret_cast<const void*>(0));
    glx::EnableVertexAttribArray(1);
    glx::VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 20, reinterpret_cast<const void*>(12));
    glx::BindVertexArray(0);
}

bool initialize_vr(Viewer& viewer) {
    vr::EVRInitError error = vr::VRInitError_None;
    viewer.system = vr::VR_Init(&error, vr::VRApplication_Scene);
    if (error != vr::VRInitError_None || !viewer.system) {
        viewer.system = nullptr;
        log_line(std::string("SteamVR not ready: ") + vr::VR_GetVRInitErrorAsEnglishDescription(error));
        return false;
    }
    viewer.system->GetRecommendedRenderTargetSize(&viewer.eye_width, &viewer.eye_height);
    GLint max_samples = 0;
    glGetIntegerv(glx::kMaxSamples, &max_samples);
    const int samples = std::clamp(max_samples, 1, kMsaaSamples);
    for (int eye = 0; eye < 2; ++eye) {
        const vr::EVREye which = eye == 0 ? vr::Eye_Left : vr::Eye_Right;
        if (!create_eye_target(viewer.eyes[eye], viewer.eye_width, viewer.eye_height, samples)) {
            log_line("could not create the eye render targets");
            return false;
        }
        viewer.projection[eye] = from_openvr(viewer.system->GetProjectionMatrix(which, kNearClip, kFarClip));
        float eye_pose[12];
        pose_to_array(viewer.system->GetEyeToHeadTransform(which), eye_pose);
        viewer.eye_to_head[eye] = matrix_from_pose(eye_pose);
    }
    log_line("SteamVR ready, " + std::to_string(viewer.eye_width) + "x" + std::to_string(viewer.eye_height) +
             " per eye, " + std::to_string(samples) + "x MSAA");
    return true;
}

void apply_uploads(Viewer& viewer) {
    std::deque<MeshUpload> uploads;
    {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        // A bounded batch per frame keeps a big terrain load from causing a
        // dropped frame.
        size_t take = std::min<size_t>(g_uploads.size(), 12);
        for (size_t i = 0; i < take; ++i) {
            uploads.push_back(std::move(g_uploads.front()));
            g_uploads.pop_front();
        }
    }
    for (MeshUpload& upload : uploads) {
        if (upload.kind == MeshUpload::Kind::ClearAll) {
            for (auto& entry : viewer.sections) {
                release_mesh(entry.second.opaque);
                release_mesh(entry.second.translucent);
            }
            viewer.sections.clear();
        } else if (upload.kind == MeshUpload::Kind::Remove) {
            auto found = viewer.sections.find(upload.key);
            if (found != viewer.sections.end()) {
                release_mesh(found->second.opaque);
                release_mesh(found->second.translucent);
                viewer.sections.erase(found);
            }
        } else {
            GpuSection& section = viewer.sections[upload.key];
            upload_mesh(section.opaque, upload.mesh.opaque, glx::kStaticDraw);
            upload_mesh(section.translucent, upload.mesh.translucent, glx::kStaticDraw);
        }
    }

    std::vector<LayerPixels> layers;
    {
        std::lock_guard<std::mutex> lock(g_layer_mutex);
        const size_t take = std::min<size_t>(g_layer_ready.size(), 64);
        layers.assign(std::make_move_iterator(g_layer_ready.begin()),
                      std::make_move_iterator(g_layer_ready.begin() + static_cast<std::ptrdiff_t>(take)));
        g_layer_ready.erase(g_layer_ready.begin(), g_layer_ready.begin() + static_cast<std::ptrdiff_t>(take));
    }
    if (!layers.empty()) {
        glBindTexture(glx::kTexture2DArray, viewer.block_textures);
        for (const LayerPixels& layer : layers) {
            glx::TexSubImage3D(glx::kTexture2DArray, 0, 0, 0, layer.layer, kTile, kTile, 1, GL_RGBA,
                               GL_UNSIGNED_BYTE, layer.rgba.data());
        }
        viewer.block_mipmaps_dirty = true;
    } else if (viewer.block_mipmaps_dirty) {
        // Regenerate once a batch has finished rather than per upload.
        glBindTexture(glx::kTexture2DArray, viewer.block_textures);
        glx::GenerateMipmap(glx::kTexture2DArray);
        viewer.block_mipmaps_dirty = false;
    }

    std::lock_guard<std::mutex> lock(g_panel_mutex);
    if (g_panel_frame.serial != viewer.panel_serial && g_panel_frame.width > 0) {
        viewer.panel_serial = g_panel_frame.serial;
        glBindTexture(GL_TEXTURE_2D, viewer.panel_texture);
        if (g_panel_frame.width != viewer.panel_texture_width || g_panel_frame.height != viewer.panel_texture_height) {
            viewer.panel_texture_width = g_panel_frame.width;
            viewer.panel_texture_height = g_panel_frame.height;
            glTexImage2D(GL_TEXTURE_2D, 0, glx::kRgba8, g_panel_frame.width, g_panel_frame.height, 0, glx::kBgra,
                         GL_UNSIGNED_BYTE, g_panel_frame.bgra.data());
        } else {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, g_panel_frame.width, g_panel_frame.height, glx::kBgra,
                            GL_UNSIGNED_BYTE, g_panel_frame.bgra.data());
        }
    }
}

float panel_aspect(const Viewer& viewer) {
    return viewer.panel_texture_width > 0
               ? static_cast<float>(viewer.panel_texture_height) / static_cast<float>(viewer.panel_texture_width)
               : 9.0f / 16.0f;
}

// Places the big panel 1.2 m ahead at eye level, facing the player.
void open_panel(Viewer& viewer, const float hmd[12]) {
    float forward[3];
    pose_forward(hmd, forward);
    float length = std::sqrt(forward[0] * forward[0] + forward[2] * forward[2]);
    if (length < 0.01f) {
        forward[0] = 0.0f;
        forward[2] = -1.0f;
        length = 1.0f;
    }
    const float fx = forward[0] / length;
    const float fz = forward[2] / length;
    // Columns: right, up, back (the panel faces +Z towards the player).
    const float back[3] = {-fx, 0.0f, -fz};
    const float right[3] = {-fz, 0.0f, fx};
    float* p = viewer.panel_pose;
    p[0] = right[0];  p[1] = 0.0f; p[2] = back[0];  p[3] = hmd[3] + fx * 1.2f;
    p[4] = right[1];  p[5] = 1.0f; p[6] = back[1];  p[7] = hmd[7] - 0.10f;
    p[8] = right[2];  p[9] = 0.0f; p[10] = back[2]; p[11] = hmd[11] + fz * 1.2f;
    viewer.panel_open = true;
}

// Intersects a tracking-space ray with the big panel; returns the hit in
// panel texture coordinates (0..1, v from the top).
bool panel_hit(const Viewer& viewer, const float origin[3], const float direction[3], float& u, float& v,
               float hit[3]) {
    const float* p = viewer.panel_pose;
    const float normal[3] = {p[2], p[6], p[10]};
    const float centre[3] = {p[3], p[7], p[11]};
    const float denominator = normal[0] * direction[0] + normal[1] * direction[1] + normal[2] * direction[2];
    if (denominator > -1e-4f) return false;  // pointing away or edge-on
    const float t = ((centre[0] - origin[0]) * normal[0] + (centre[1] - origin[1]) * normal[1] +
                     (centre[2] - origin[2]) * normal[2]) /
                    denominator;
    if (t < 0.0f || t > 10.0f) return false;
    for (int i = 0; i < 3; ++i) hit[i] = origin[i] + direction[i] * t;
    const float relative[3] = {hit[0] - centre[0], hit[1] - centre[1], hit[2] - centre[2]};
    const float x = relative[0] * p[0] + relative[1] * p[4] + relative[2] * p[8];
    const float y = relative[0] * p[1] + relative[1] * p[5] + relative[2] * p[9];
    const float height = viewer.panel_width * panel_aspect(viewer);
    u = x / viewer.panel_width + 0.5f;
    v = 0.5f - y / height;
    return u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f;
}

struct FrameState {
    bool has_player = false;
    PlayerSample latest;
    double feet[3]{};          // smoothed, absolute
    double origin[3]{};        // render origin, absolute
    float feet_relative[3]{};  // smoothed feet relative to the render origin
    Matrix4 world_from_tracking = identity_matrix();
    float hmd[12]{};
    bool hmd_valid = false;
    InputFrame input;
    bool input_valid = false;
    bool plugin_connected = false;
    size_t sections = 0;
};

void update_controls(Viewer& viewer, FrameState& frame) {
    VrCameraShared& shared = *g_shared;
    const double now = now_ms();
    const float elapsed = viewer.last_frame_ms == 0
                              ? 0.0f
                              : static_cast<float>(std::clamp((now - viewer.last_frame_ms) * 0.001, 0.0, 0.05));
    viewer.last_frame_ms = now;
    const uint64_t tick = GetTickCount64();
    InputFrame& input = frame.input;

    // Recenter on the first position, on the dashboard's recenter, and
    // whenever both grips are held (the dashboard bumps the sequence).
    const float native_yaw = hytale_yaw(static_cast<float>(frame.latest.look[0]),
                                        static_cast<float>(frame.latest.look[2]));
    if (frame.hmd_valid && frame.has_player &&
        (!viewer.anchored || viewer.recenter_sequence != shared.recenter_sequence)) {
        recenter(viewer.anchor, frame.hmd, native_yaw);
        viewer.anchored = true;
        viewer.recenter_sequence = shared.recenter_sequence;
        viewer.panel_open = false;
        pulse(0, 0.10f, 120.0f, 0.5f);
        pulse(1, 0.10f, 120.0f, 0.5f);
        log_line("recentered");
    }

    // Turning happens here, instantly, instead of through Hytale's mouse; the
    // dashboard then turns Hytale's camera to follow the headset.
    if (frame.input_valid && frame.hmd_valid && viewer.anchored) {
        const float turn = input.turn[0];
        if (shared.viewer_snap_turn != 0) {
            if (std::fabs(turn) < 0.35f) {
                viewer.snap_armed = true;
            } else if (viewer.snap_armed && std::fabs(turn) > 0.75f) {
                viewer.snap_armed = false;
                turn_about_head(viewer.anchor, turn > 0 ? -kSnapTurnRadians : kSnapTurnRadians, frame.hmd);
            }
        } else if (std::fabs(turn) > 0.2f) {
            const float axis = std::copysign((std::fabs(turn) - 0.2f) / 0.8f, turn);
            const float speed = std::clamp(shared.viewer_turn_speed_degrees, 20.0f, 360.0f) * kPi / 180.0f;
            turn_about_head(viewer.anchor, -axis * speed * elapsed, frame.hmd);
        }
    }

    frame.world_from_tracking = world_from_tracking(viewer.anchor, frame.feet_relative);

    viewer.right_trigger_pressed = hysteresis(viewer.right_trigger_pressed, input.right_trigger);
    viewer.left_trigger_pressed = hysteresis(viewer.left_trigger_pressed, input.left_trigger);
    viewer.left_grip_pressed = hysteresis(viewer.left_grip_pressed, input.left_grip);
    viewer.right_grip_pressed = hysteresis(viewer.right_grip_pressed, input.right_grip);

    // Y opens Hytale's inventory (the dashboard presses Tab) and the panel
    // with it. With no world from the plugin, the panel stays up as a screen.
    const bool no_world = !frame.plugin_connected || !frame.has_player;
    if (input.button_y && !viewer.previous_button_y && frame.hmd_valid) {
        if (viewer.panel_open) {
            viewer.panel_open = false;
        } else {
            open_panel(viewer, frame.hmd);
        }
    }
    viewer.previous_button_y = input.button_y;
    if (no_world && !viewer.panel_open && frame.hmd_valid) open_panel(viewer, frame.hmd);
    g_panel_interval_ms = viewer.panel_open ? 66 : 200;

    // Pointer on the panel drives the dashboard's menu mouse.
    viewer.pointer_on_panel = false;
    const ControllerState& right = input.hands[1];
    if (viewer.panel_open && right.valid) {
        float origin[3];
        float direction[3];
        pose_position(right.pose, origin);
        pose_forward(right.pose, direction);
        float u = 0.0f;
        float v = 0.0f;
        if (panel_hit(viewer, origin, direction, u, v, viewer.pointer_hit)) {
            viewer.pointer_on_panel = true;
            const int width = std::max(viewer.panel_texture_width, 1);
            const int height = std::max(viewer.panel_texture_height, 1);
            shared.pointer_surface_width = static_cast<uint32_t>(width);
            shared.pointer_surface_height = static_cast<uint32_t>(height);
            shared.pointer_x = static_cast<int32_t>(u * static_cast<float>(width));
            shared.pointer_y = static_cast<int32_t>((1.0f - v) * static_cast<float>(height));
        }
    }
    shared.pointer_visible = viewer.pointer_on_panel ? 1u : 0u;
    shared.pointer_menu_mode = viewer.pointer_on_panel ? 1u : 0u;
    shared.ui_overlay_active = viewer.pointer_on_panel ? 1u : 0u;

    // Swings: the same tip-speed rule the render hook used.
    const bool swings_enabled = shared.physical_swing_enabled != 0;
    for (int hand = 0; hand < 2; ++hand) {
        const ControllerState& controller = input.hands[hand];
        float speed = 0.0f;
        if (!controller.valid || !swings_enabled) {
            viewer.tip_history[hand].clear();
            viewer.swing_armed[hand] = true;
        } else {
            const float* m = controller.pose;
            const float tip[3] = {m[3] - m[2] * hytalevr::kPhysicalSwingTipOffset,
                                  m[7] - m[6] * hytalevr::kPhysicalSwingTipOffset,
                                  m[11] - m[10] * hytalevr::kPhysicalSwingTipOffset};
            viewer.tip_history[hand].add(tip[0], tip[1], tip[2], tick);
            speed = viewer.tip_history[hand].average_speed(tick, hytalevr::kPhysicalSwingWindowMs);
            if (speed < hytalevr::kPhysicalSwingRearmSpeed) viewer.swing_armed[hand] = true;
            if (viewer.swing_armed[hand] && tick >= viewer.swing_cooldown_until[hand] &&
                hytalevr::physical_swing_requested(speed) && !viewer.pointer_on_panel) {
                viewer.swing_armed[hand] = false;
                viewer.swing_cooldown_until[hand] = tick + 350;
                pulse(hand, 0.06f, 160.0f, 0.8f);
                shared.physical_attack_hand = static_cast<uint32_t>(hand);
                MemoryBarrier();
                InterlockedIncrement(reinterpret_cast<volatile LONG*>(&shared.physical_attack_sequence));
            }
        }
        (hand == 0 ? shared.physical_left_swing_speed : shared.physical_right_swing_speed) = speed;
    }

    // Hand rays in world axes, origin relative to Hytale's eye, for the
    // plugin to resolve swings along.
    for (int hand = 0; hand < 2; ++hand) {
        const ControllerState& controller = input.hands[hand];
        shared.hand_ray_valid[hand] = 0;
        if (!controller.valid || !frame.has_player) continue;
        float position[3];
        float forward[3];
        pose_position(controller.pose, position);
        pose_forward(controller.pose, forward);
        float world_position[3];
        float world_direction[3];
        transform_point(frame.world_from_tracking, position, world_position);
        transform_direction(frame.world_from_tracking, forward, world_direction);
        for (int axis = 0; axis < 3; ++axis) {
            // Position relative to the smoothed feet, then onto the latest
            // eye, so the ray starts where the hand is drawn.
            const double relative_to_feet = world_position[axis] - frame.feet_relative[axis];
            const double eye_above_feet = frame.latest.eye[axis] - frame.latest.feet[axis];
            shared.hand_ray[hand][axis] = static_cast<float>(relative_to_feet - eye_above_feet);
            shared.hand_ray[hand][3 + axis] = world_direction[axis];
        }
        shared.hand_ray_valid[hand] = 1;
        if (hand == 1) {
            std::lock_guard<std::mutex> lock(g_state_mutex);
            g_reach_valid = true;
            for (int axis = 0; axis < 3; ++axis) {
                g_reach_origin[axis] = frame.origin[axis] + world_position[axis];
                g_reach_direction[axis] = world_direction[axis];
            }
        }
    }
    if (!shared.hand_ray_valid[1]) {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        g_reach_valid = false;
    }
    shared.hand_ray_frame = shared.hand_ray_frame + 1;
    for (int axis = 0; axis < 3; ++axis) shared.native_look[axis] = static_cast<float>(frame.latest.look[axis]);

    // Controller state for the dashboard, as the render hook wrote it.
    shared.controller_active = frame.input_valid ? 1u : 0u;
    shared.controller_move_x = input.move[0];
    shared.controller_move_y = input.move[1];
    shared.controller_turn_x = 0.0f;  // turning is done here
    shared.controller_turn_y = 0.0f;
    shared.controller_jump = input.jump ? 1u : 0u;
    shared.controller_sprint = input.sprint ? 1u : 0u;
    shared.controller_right_trigger = input.right_trigger;
    shared.controller_right_trigger_pressed = viewer.right_trigger_pressed ? 1u : 0u;
    shared.controller_left_trigger = input.left_trigger;
    shared.controller_left_trigger_pressed = viewer.left_trigger_pressed ? 1u : 0u;
    shared.controller_button_x = input.button_x ? 1u : 0u;
    shared.controller_button_y = input.button_y ? 1u : 0u;
    shared.controller_button_b = input.button_b ? 1u : 0u;
    shared.controller_left_grip = viewer.left_grip_pressed ? 1u : 0u;
    shared.controller_right_grip = viewer.right_grip_pressed ? 1u : 0u;
    shared.controller_right_stick_click = input.right_stick_click ? 1u : 0u;
    shared.controller_right_pose_active = right.valid ? 1u : 0u;
    shared.controller_ray_active = right.valid ? 1u : 0u;
    shared.physical_jump_active = 0;
    shared.physical_sneak_active = 0;
    if (frame.hmd_valid) shared.physical_hmd_height = frame.hmd[7];

    if (frame.has_player) {
        shared.native_camera_yaw = dashboard_yaw(static_cast<float>(frame.latest.look[0]),
                                                 static_cast<float>(frame.latest.look[2]));
    }
    if (frame.hmd_valid && viewer.anchored) {
        float forward[3];
        pose_forward(frame.hmd, forward);
        float world_forward[3];
        transform_direction(frame.world_from_tracking, forward, world_forward);
        const float target = dashboard_yaw(world_forward[0], world_forward[2]);
        shared.viewer_target_yaw = target;
        shared.body_camera_yaw = target;
    }
    shared.camera_yaw_valid = frame.has_player && viewer.anchored ? 1u : 0u;
    shared.viewer_plugin_connected = frame.plugin_connected ? 1u : 0u;
    shared.viewer_sections = static_cast<uint32_t>(frame.sections);
    shared.viewer_active = 1;
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(&shared.viewer_heartbeat));
}

void build_dynamic_geometry(Viewer& viewer, const FrameState& frame, std::vector<MeshVertex>& opaque,
                            std::vector<MeshVertex>& translucent) {
    const double now = now_ms();
    if (frame.has_player) {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        for (const auto& entry : g_entity_tracks) {
            const EntityTrack& track = entry.second;
            const double t = std::clamp((now - track.updated_ms) / track.interval_ms, 0.0, 1.0);
            float base[3];
            for (int axis = 0; axis < 3; ++axis) {
                base[axis] = static_cast<float>(track.previous[axis] + (track.current[axis] - track.previous[axis]) * t -
                                                frame.origin[axis]);
            }
            const EntityBox& box = track.box;
            float size[3];
            float centre[3];
            for (int axis = 0; axis < 3; ++axis) {
                size[axis] = std::max(0.1f, static_cast<float>(box.max[axis] - box.min[axis]));
                centre[axis] = base[axis] + static_cast<float>(box.min[axis] + box.max[axis]) * 0.5f;
            }
            float color[4];
            color_from_name(box.model, color);
            const float yaw = box.yaw;  // radians, Hytale's convention
            emit_box(opaque, box_matrix(centre, size, yaw), color);
            // A darker block on the front shows which way the mob faces.
            float fx = 0.0f;
            float fz = 0.0f;
            rotate_xz(yaw, 0.0f, -1.0f, fx, fz);
            const float reach = size[2] * 0.5f;
            const float face[3] = {centre[0] + fx * reach, base[1] + size[1] * 0.8f, centre[2] + fz * reach};
            const float face_size[3] = {size[0] * 0.5f, size[1] * 0.25f, 0.08f};
            const float face_color[4] = {color[0] * 0.4f, color[1] * 0.4f, color[2] * 0.4f, 1.0f};
            emit_box(opaque, box_matrix(face, face_size, yaw), face_color);
        }
        if (g_highlight_valid) {
            const float centre[3] = {static_cast<float>(g_highlight[0] + 0.5 - frame.origin[0]),
                                     static_cast<float>(g_highlight[1] + 0.5 - frame.origin[1]),
                                     static_cast<float>(g_highlight[2] + 0.5 - frame.origin[2])};
            const float size[3] = {1.01f, 1.01f, 1.01f};
            const float color[4] = {1.0f, 1.0f, 1.0f, 0.22f};
            emit_box(translucent, box_matrix(centre, size, 0.0f), color);
        }
    }

    // Controllers as small blocks, with a laser to the panel when pointing at it.
    for (int hand = 0; hand < 2; ++hand) {
        const ControllerState& controller = frame.input.hands[hand];
        if (!controller.valid) continue;
        const Matrix4 pose = multiply(frame.world_from_tracking, matrix_from_pose(controller.pose));
        const float grip_color[4] = {0.18f, 0.18f, 0.20f, 1.0f};
        emit_box(opaque, multiply(pose, multiply(translation_matrix(0.0f, -0.01f, 0.03f), scale_matrix(0.045f, 0.04f, 0.12f))),
                 grip_color);
        const float tip_color[4] = {0.85f, 0.70f, 0.50f, 1.0f};
        emit_box(opaque, multiply(pose, multiply(translation_matrix(0.0f, 0.0f, -0.05f), scale_matrix(0.03f, 0.03f, 0.04f))),
                 tip_color);
        if (hand == 1 && viewer.pointer_on_panel) {
            float origin[3];
            pose_position(controller.pose, origin);
            const float* hit = viewer.pointer_hit;
            const float length = std::sqrt((hit[0] - origin[0]) * (hit[0] - origin[0]) +
                                           (hit[1] - origin[1]) * (hit[1] - origin[1]) +
                                           (hit[2] - origin[2]) * (hit[2] - origin[2]));
            const float laser_color[4] = {0.4f, 0.8f, 1.0f, 1.0f};
            emit_box(opaque,
                     multiply(pose, multiply(translation_matrix(0.0f, 0.0f, -length * 0.5f),
                                             scale_matrix(0.004f, 0.004f, length))),
                     laser_color);
            const Matrix4 dot = multiply(frame.world_from_tracking,
                                         multiply(translation_matrix(hit[0], hit[1], hit[2]),
                                                  scale_matrix(0.015f, 0.015f, 0.015f)));
            const float dot_color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
            emit_box(opaque, dot, dot_color);
        }
    }
}

void draw_panels(Viewer& viewer, const FrameState& frame, const Matrix4& view_projection) {
    glx::UseProgram(viewer.panel_program);
    glx::Uniform1i(viewer.panel_texture_uniform, 0);
    glx::ActiveTexture(glx::kTexture0);
    glBindTexture(GL_TEXTURE_2D, viewer.panel_texture);
    glx::BindVertexArray(viewer.panel_vao);
    glDisable(GL_CULL_FACE);
    const float aspect = panel_aspect(viewer);
    if (viewer.panel_open) {
        const Matrix4 model = multiply(frame.world_from_tracking,
                                       multiply(matrix_from_pose(viewer.panel_pose),
                                                scale_matrix(viewer.panel_width, viewer.panel_width * aspect, 1.0f)));
        const Matrix4 mvp = multiply(view_projection, model);
        glx::UniformMatrix4fv(viewer.panel_mvp, 1, GL_FALSE, mvp.value);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    } else if (frame.input.hands[0].valid) {
        // A small copy of Hytale's screen over the left wrist, for the hotbar
        // and health at a glance: lying flat on the controller, facing up.
        Matrix4 lay_flat = identity_matrix();
        lay_flat.value[5] = 0.0f;
        lay_flat.value[6] = -1.0f;
        lay_flat.value[9] = 1.0f;
        lay_flat.value[10] = 0.0f;
        const float width = 0.20f;
        const Matrix4 model =
            multiply(frame.world_from_tracking,
                     multiply(matrix_from_pose(frame.input.hands[0].pose),
                              multiply(translation_matrix(0.0f, 0.03f, 0.10f),
                                       multiply(lay_flat, scale_matrix(width, width * aspect, 1.0f)))));
        const Matrix4 mvp = multiply(view_projection, model);
        glx::UniformMatrix4fv(viewer.panel_mvp, 1, GL_FALSE, mvp.value);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }
    glEnable(GL_CULL_FACE);
    glx::BindVertexArray(0);
}

void render_eye(Viewer& viewer, const FrameState& frame, int eye) {
    const Matrix4 head = multiply(frame.world_from_tracking, matrix_from_pose(frame.hmd));
    const Matrix4 eye_world = multiply(head, viewer.eye_to_head[eye]);
    Matrix4 view{};
    hytalevr::inverse(eye_world, view);
    const Matrix4 view_projection = multiply(viewer.projection[eye], view);
    const float camera[3] = {eye_world.value[12], eye_world.value[13], eye_world.value[14]};

    const EyeTarget& target = viewer.eyes[eye];
    glx::BindFramebuffer(glx::kFramebuffer, target.msaa_framebuffer);
    glViewport(0, 0, static_cast<GLsizei>(viewer.eye_width), static_cast<GLsizei>(viewer.eye_height));
    glClearColor(kSkyColor[0], kSkyColor[1], kSkyColor[2], 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glDisable(GL_BLEND);
    glEnable(glx::kMultisample);

    glx::UseProgram(viewer.world_program);
    glx::UniformMatrix4fv(viewer.world_view_projection, 1, GL_FALSE, view_projection.value);
    glx::Uniform3f(viewer.world_camera, camera[0], camera[1], camera[2]);
    glx::Uniform1i(viewer.world_texture, 0);
    glx::Uniform3f(viewer.world_fog_color, kSkyColor[0], kSkyColor[1], kSkyColor[2]);
    glx::Uniform2f(viewer.world_fog, kFogStart, kFogEnd);
    glx::ActiveTexture(glx::kTexture0);
    glBindTexture(glx::kTexture2DArray, viewer.block_textures);

    struct Visible {
        const GpuSection* section;
        float offset[3];
        float distance;
    };
    std::vector<Visible> visible;
    visible.reserve(viewer.sections.size());
    for (const auto& entry : viewer.sections) {
        const SectionKey& key = entry.first;
        Visible item;
        item.section = &entry.second;
        item.offset[0] = static_cast<float>(key.x * 32.0 - frame.origin[0]);
        item.offset[1] = static_cast<float>(key.y * 32.0 - frame.origin[1]);
        item.offset[2] = static_cast<float>(key.z * 32.0 - frame.origin[2]);
        const float high[3] = {item.offset[0] + 32.0f, item.offset[1] + 32.0f, item.offset[2] + 32.0f};
        if (!box_visible(view_projection, item.offset, high)) continue;
        const float dx = item.offset[0] + 16.0f - camera[0];
        const float dy = item.offset[1] + 16.0f - camera[1];
        const float dz = item.offset[2] + 16.0f - camera[2];
        item.distance = dx * dx + dy * dy + dz * dz;
        visible.push_back(item);
    }

    glx::Uniform1f(viewer.world_alpha_cutoff, 0.5f);
    for (const Visible& item : visible) {
        if (item.section->opaque.count == 0) continue;
        glx::Uniform3f(viewer.world_offset, item.offset[0], item.offset[1], item.offset[2]);
        glx::BindVertexArray(item.section->opaque.vao);
        glDrawArrays(GL_TRIANGLES, 0, item.section->opaque.count);
    }
    glx::Uniform3f(viewer.world_offset, 0.0f, 0.0f, 0.0f);
    if (viewer.dynamic_opaque.count > 0) {
        glx::BindVertexArray(viewer.dynamic_opaque.vao);
        glDrawArrays(GL_TRIANGLES, 0, viewer.dynamic_opaque.count);
    }

    draw_panels(viewer, frame, view_projection);

    // Water, glass and the block highlight, far to near.
    glx::UseProgram(viewer.world_program);
    glx::ActiveTexture(glx::kTexture0);
    glBindTexture(glx::kTexture2DArray, viewer.block_textures);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glx::Uniform1f(viewer.world_alpha_cutoff, 0.02f);
    std::sort(visible.begin(), visible.end(), [](const Visible& a, const Visible& b) { return a.distance > b.distance; });
    for (const Visible& item : visible) {
        if (item.section->translucent.count == 0) continue;
        glx::Uniform3f(viewer.world_offset, item.offset[0], item.offset[1], item.offset[2]);
        glx::BindVertexArray(item.section->translucent.vao);
        glDrawArrays(GL_TRIANGLES, 0, item.section->translucent.count);
    }
    glx::Uniform3f(viewer.world_offset, 0.0f, 0.0f, 0.0f);
    if (viewer.dynamic_translucent.count > 0) {
        glx::BindVertexArray(viewer.dynamic_translucent.vao);
        glDrawArrays(GL_TRIANGLES, 0, viewer.dynamic_translucent.count);
    }
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glx::BindVertexArray(0);

    glx::BindFramebuffer(glx::kReadFramebuffer, target.msaa_framebuffer);
    glx::BindFramebuffer(glx::kDrawFramebuffer, target.resolve_framebuffer);
    glx::BlitFramebuffer(0, 0, static_cast<GLint>(viewer.eye_width), static_cast<GLint>(viewer.eye_height), 0, 0,
                         static_cast<GLint>(viewer.eye_width), static_cast<GLint>(viewer.eye_height),
                         GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glx::BindFramebuffer(glx::kFramebuffer, 0);
}

bool should_quit(Viewer& viewer) {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) return true;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (g_shared->shutdown_requested != 0) {
        log_line("dashboard asked the viewer to close");
        return true;
    }
    // The dashboard clears `enabled` when VR stops; give it a moment while
    // starting up before treating that as a stop.
    const double now = now_ms();
    if (g_shared->enabled != 0) {
        viewer.seen_enabled = true;
        viewer.enabled_lost_since = 0;
    } else if (viewer.seen_enabled) {
        if (viewer.enabled_lost_since == 0) viewer.enabled_lost_since = now;
        if (now - viewer.enabled_lost_since > 2000) {
            log_line("VR was stopped in the dashboard");
            return true;
        }
    }
    static double last_process_check = 0;
    if (now - last_process_check > 1000) {
        last_process_check = now;
        if (!process_alive(viewer.options.pid)) {
            log_line("Hytale closed");
            return true;
        }
    }
    if (viewer.system) {
        vr::VREvent_t event{};
        while (viewer.system->PollNextEvent(&event, sizeof(event))) {
            if (event.eventType == vr::VREvent_Quit) {
                viewer.system->AcknowledgeQuit_Exiting();
                log_line("SteamVR asked the viewer to quit");
                return true;
            }
        }
    }
    return false;
}

int run() {
    open_log();
    Viewer viewer;
    viewer.options = parse_options();
    log_line("Hytale pid " + std::to_string(viewer.options.pid) + ", assets " + narrow(viewer.options.assets_zip));
    if (!open_shared_memory()) {
        log_line("could not open the dashboard's shared memory");
        return 1;
    }
    if (!create_window_and_context(viewer)) {
        log_line("could not create an OpenGL 3.3 context");
        return 1;
    }
    create_gl_resources(viewer);

    std::thread world_thread(world_thread_main);
    std::thread loader_thread(texture_loader_main, viewer.options.assets_zip);
    std::thread capture_thread(capture_thread_main, viewer.options.pid);

    std::vector<MeshVertex> dynamic_opaque;
    std::vector<MeshVertex> dynamic_translucent;
    while (!should_quit(viewer)) {
        if (!viewer.system) {
            if (!initialize_vr(viewer)) {
                Sleep(2000);
                continue;
            }
        }
        vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount]{};
        if (vr::VRCompositor()->WaitGetPoses(poses, vr::k_unMaxTrackedDeviceCount, nullptr, 0) !=
            vr::VRCompositorError_None) {
            Sleep(10);
            continue;
        }

        FrameState frame;
        const vr::TrackedDevicePose_t& hmd = poses[vr::k_unTrackedDeviceIndex_Hmd];
        frame.hmd_valid = hmd.bDeviceIsConnected && hmd.bPoseIsValid;
        if (frame.hmd_valid) pose_to_array(hmd.mDeviceToAbsoluteTracking, frame.hmd);
        frame.input_valid = read_input(frame.input);
        {
            std::lock_guard<std::mutex> lock(g_state_mutex);
            frame.has_player = g_has_player;
            frame.latest = g_latest_player;
            frame.plugin_connected = g_plugin_connected;
            frame.sections = g_section_count;
            if (!g_feet_smoother.sample(now_ms(), frame.feet)) {
                std::copy(frame.latest.feet, frame.latest.feet + 3, frame.feet);
            }
        }
        for (int axis = 0; axis < 3; ++axis) {
            frame.origin[axis] = std::floor(frame.feet[axis]);
            frame.feet_relative[axis] = static_cast<float>(frame.feet[axis] - frame.origin[axis]);
        }

        apply_uploads(viewer);
        update_controls(viewer, frame);
        if (!frame.hmd_valid) continue;

        dynamic_opaque.clear();
        dynamic_translucent.clear();
        build_dynamic_geometry(viewer, frame, dynamic_opaque, dynamic_translucent);
        upload_mesh(viewer.dynamic_opaque, dynamic_opaque, glx::kStreamDraw);
        upload_mesh(viewer.dynamic_translucent, dynamic_translucent, glx::kStreamDraw);

        for (int eye = 0; eye < 2; ++eye) render_eye(viewer, frame, eye);
        for (int eye = 0; eye < 2; ++eye) {
            vr::Texture_t texture{reinterpret_cast<void*>(static_cast<uintptr_t>(viewer.eyes[eye].resolve_texture)),
                                  vr::TextureType_OpenGL, vr::ColorSpace_Gamma};
            vr::VRCompositor()->Submit(eye == 0 ? vr::Eye_Left : vr::Eye_Right, &texture);
        }
    }

    g_quit = true;
    g_layer_signal.notify_all();
    g_shared->viewer_active = 0;
    g_shared->camera_yaw_valid = 0;
    g_shared->controller_active = 0;
    g_shared->pointer_visible = 0;
    g_shared->ui_overlay_active = 0;
    world_thread.join();
    loader_thread.join();
    capture_thread.join();
    if (viewer.system) vr::VR_Shutdown();
    log_line("viewer closed");
    return 0;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    return run();
}
