#include "app.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <vector>
#include <string>

#include "../core/hytale_settings.h"
#include "census.h"
#include "gl_api.h"
#include "gl_state.h"
#include "input.h"
#include "internal.h"
#include "log.h"
#include "menu.h"
#include "sdl_hooks.h"
#include "stereo.h"
#include "vr_runtime.h"

namespace hvr::app {
namespace {

bool g_started = false;
bool g_gl_failed = false;
Settings g_settings;
bool g_dirty = false;
ULONGLONG g_dirty_at = 0;
std::atomic<bool> g_menu_open{false};

// Sharpness: while VR runs, Hytale is told its window has more pixels and
// draws into a buffer that big, which is scaled down for the monitor.
struct Sharpness {
    float scale = 1;        // what Hytale has been told
    float tall = 1;         // and how many times taller its window is (Lens shape)
    int width = 0, height = 0;  // the buffer it draws into (0 = off)
    int frames = 0;         // since the last change
    bool checked = false;   // Hytale was seen drawing at the new size
    bool gave_up = false;   // Hytale kept drawing at the window's size
    float gave_up_at = 0;   // the setting then; moving the slider tries again
    std::string status = "Off until VR starts.";
} g_sharp;

int scaled(int size, float scale) { return static_cast<int>(static_cast<float>(size) * scale + 0.5f); }

// sharpness: the Sharpness setting (1 = the window's pixels). lens_aspect: when
// above 0, the same number of pixels is laid out in that width/height instead.
void apply_sharpness(float sharpness, float lens_aspect, int pixels_w, int pixels_h) {
    int w = 0, h = 0;
    float scale = 1.0f, tall = 1.0f;
    if ((sharpness > 1.0f || lens_aspect > 0) && pixels_w > 0 && pixels_h > 0) {
        float fw = static_cast<float>(pixels_w) * sharpness, fh = static_cast<float>(pixels_h) * sharpness;
        if (lens_aspect > 0) {
            fh = std::sqrt(fw * fh / lens_aspect);
            fw = fh * lens_aspect;
        }
        w = static_cast<int>(fw + 0.5f);
        h = static_cast<int>(fh + 0.5f);
        scale = static_cast<float>(w) / static_cast<float>(pixels_w);
        tall = static_cast<float>(h) / (static_cast<float>(pixels_h) * scale);
        if (std::fabs(tall - 1.0f) < 0.01f) tall = 1.0f;
    }
    if (w == g_sharp.width && h == g_sharp.height && (w || g_sharp.scale == 1.0f)) return;
    const bool on = stereo::set_virtual_backbuffer(w, h);
    g_sharp.scale = on ? scale : 1.0f;
    g_sharp.tall = on ? tall : 1.0f;
    g_sharp.width = on ? w : 0;
    g_sharp.height = on ? h : 0;
    g_sharp.frames = 0;
    g_sharp.checked = false;
    sdl::set_render_scale(g_sharp.scale, g_sharp.tall);
}

// Called after the frame is shown: picks the size for the next one.
void update_sharpness(bool vr_active, int pixels_w, int pixels_h) {
    float want = 1.0f, lens = 0.0f;
    if (g_sharp.gave_up && g_settings.render_scale != g_sharp.gave_up_at && !g_dirty) g_sharp.gave_up = false;
    if (vr_active && !g_sharp.gave_up && sdl::installed()) {
        want = g_settings.render_scale;
        if (g_settings.lens_shape) lens = session::lens_aspect();
    }
    if (want < 1.05f) want = 1.0f;
    // Wait for the slider to settle before resizing everything.
    if (g_dirty) return;
    apply_sharpness(want, lens, pixels_w, pixels_h);
    if (!g_sharp.width) {
        g_sharp.status = g_sharp.gave_up ? "Off: Hytale kept drawing at the window's size."
                         : vr_active   ? "Off."
                                       : "Off until VR starts.";
        return;
    }
    ++g_sharp.frames;
    const int seen = stereo::window_viewport_width();
    // Either Hytale's last full-window viewport or its 3D render size shows it.
    const int rendered = stereo::render_width();
    if (!g_sharp.checked && (seen == g_sharp.width || rendered == g_sharp.width)) {
        g_sharp.checked = true;
        logf("Hytale draws at %dx%d (%.2fx the window, %.2fx taller)", g_sharp.width, g_sharp.height, g_sharp.scale,
             g_sharp.tall);
    }
    // Give up only when Hytale is clearly still drawing at the window's own size.
    if (!g_sharp.checked && g_sharp.frames > 300 && (seen == pixels_w || rendered == pixels_w)) {
        logf("Hytale didn't pick up the bigger size (still drawing %d wide, wanted %d); Sharpness is off. %s", seen,
             g_sharp.width, sdl::usage_summary().c_str());
        g_sharp.gave_up = true;
        g_sharp.gave_up_at = g_settings.render_scale;
        apply_sharpness(1.0f, 0.0f, pixels_w, pixels_h);
        g_sharp.status = "Off: Hytale kept drawing at the window's size.";
        return;
    }
    char text[128];
    snprintf(text, sizeof(text), g_sharp.checked ? "Hytale draws at %dx%d." : "Asking Hytale to draw at %dx%d...",
             g_sharp.width, g_sharp.height);
    g_sharp.status = text;
}

std::wstring settings_path() { return data_dir() + L"\\settings.ini"; }

void load_settings() {
    FILE* file = _wfopen(settings_path().c_str(), L"rb");
    if (!file) {
        logf("No settings yet; using defaults");
        return;
    }
    std::string text;
    char buffer[4096];
    size_t got;
    while ((got = std::fread(buffer, 1, sizeof(buffer), file)) > 0) text.append(buffer, got);
    std::fclose(file);
    g_settings = parse_settings(text);
}

void save_settings() {
    ensure_dir(data_dir());
    const std::wstring temp = settings_path() + L".tmp";
    FILE* file = _wfopen(temp.c_str(), L"wb");
    if (!file) return;
    const std::string text = format_settings(g_settings);
    std::fwrite(text.data(), 1, text.size(), file);
    std::fclose(file);
    MoveFileExW(temp.c_str(), settings_path().c_str(), MOVEFILE_REPLACE_EXISTING);
}

// Hytale's settings file, read and written with plain Win32 calls so this is
// safe while the DLL loads and unloads.
bool read_file(const std::wstring& path, std::string& out) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart < (16 << 20);
    if (ok) {
        out.resize(static_cast<size_t>(size.QuadPart));
        DWORD got = 0;
        ok = ReadFile(file, out.data(), static_cast<DWORD>(out.size()), &got, nullptr) && got == out.size();
    }
    CloseHandle(file);
    return ok;
}

bool write_file(const std::wstring& path, const std::string& text) {
    const std::wstring temp = path + L".hytalevr.tmp";
    HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD wrote = 0;
    const bool ok = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &wrote, nullptr) && wrote == text.size();
    CloseHandle(file);
    if (!ok) {
        DeleteFileW(temp.c_str());
        return false;
    }
    return MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

void tune_hytale(const char* when) {
    if (!g_settings.tune_hytale) return;
    wchar_t appdata[MAX_PATH] = {};
    const DWORD n = GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return;
    const std::wstring base = std::wstring(appdata) + L"\\Hytale";
    for (const wchar_t* name : {L"\\UserData\\Settings.json", L"\\Settings.json"}) {
        const std::wstring path = base + name;
        std::string json;
        if (!read_file(path, json)) continue;
        // The field of view only for players who start in VR: it changes how Hytale looks on a monitor too.
        const std::vector<std::string> changed = tune_hytale_settings(json, g_settings.vr_enabled);
        if (changed.empty()) {
            if (std::strcmp(when, "start") == 0) logf("Hytale's settings are already set for VR (%ls)", path.c_str());
            return;
        }
        // Keep the player's original once, before the first change.
        const std::wstring backup = data_dir() + L"\\hytale_settings_before_vr.json";
        if (GetFileAttributesW(backup.c_str()) == INVALID_FILE_ATTRIBUTES) {
            std::string original;
            if (read_file(path, original)) write_file(backup, original);
        }
        std::string list;
        for (const std::string& key : changed) list += (list.empty() ? "" : ", ") + key;
        if (write_file(path, json))
            logf("Set for VR in Hytale's settings at %s (takes effect when Hytale next starts): %s", when, list.c_str());
        else
            logf("Couldn't write Hytale's settings at %ls", path.c_str());
        return;
    }
    if (std::strcmp(when, "start") == 0) logf("Didn't find Hytale's settings file under %ls", base.c_str());
}

void start() {
    g_started = true;
    load_settings();
    sdl::set_menu_key(g_settings.menu_key);
    logf("Menu key: scancode %d. Start in VR: %s", g_settings.menu_key, g_settings.vr_enabled ? "yes" : "no");
    if (g_settings.vr_enabled) session::request_start();
    tune_hytale("start");
}

}  // namespace

Settings& settings() { return g_settings; }

void on_unload() {
    if (g_started) tune_hytale("exit");
}

std::string render_status() { return g_sharp.status; }

void settings_changed() {
    clamp(g_settings);
    g_dirty = true;
    g_dirty_at = GetTickCount64();
}

bool menu_open() { return g_menu_open; }

void set_menu_open(bool open) {
    if (g_menu_open.exchange(open) == open) return;
    sdl::take_menu_input();  // drop anything left from last time
    input::release_all();
    sdl::set_capture(open);
}

void before_swap(HDC dc) {
    if (g_gl_failed) return;
    if (!glr.ready && !gl_api_init()) {
        g_gl_failed = true;
        logf("OpenGL functions are missing; HytaleVR stays off");
        return;
    }
    census::frame_boundary();
    if (!g_started) start();
    {
        float grip[12];
        if (session::take_item_grip(grip)) {
            for (int i = 0; i < 12; ++i) g_settings.item_grip[i] = grip[i];
            g_settings.item_aligned = true;
            settings_changed();
        }
    }

    HWND hwnd = WindowFromDC(dc);
    sdl::install();
    sdl::set_window(hwnd);
    input::set_target(hwnd);

    if (sdl::take_menu_key_press() || session::take_menu_button()) set_menu_open(!g_menu_open);

    RECT client{};
    GetClientRect(hwnd, &client);
    const int pixels_w = client.right - client.left, pixels_h = client.bottom - client.top;
    int points_w = pixels_w, points_h = pixels_h, sdl_pixels_w = 0, sdl_pixels_h = 0;
    sdl::window_size(points_w, points_h, sdl_pixels_w, sdl_pixels_h);
    const float points_to_pixels = points_w > 0 ? static_cast<float>(pixels_w) / static_cast<float>(points_w) : 1.0f;

    const bool vr_active = session::state() == session::State::Running;
    // While Sharpness is on, Hytale drew this frame into a bigger buffer.
    const float sharp = g_sharp.width ? g_sharp.scale : 1.0f;
    const int frame_w = g_sharp.width ? g_sharp.width : pixels_w, frame_h = g_sharp.width ? g_sharp.height : pixels_h;
    if (g_menu_open || vr_active) {
        InternalScope scope;
        GlStateGuard guard;
        session::FrameContext context;
        context.settings = &g_settings;
        context.menu_open = g_menu_open;
        context.game_relative = sdl::game_mouse_look();
        static int last_look = -1;
        if (last_look != static_cast<int>(context.game_relative)) {
            last_look = context.game_relative;
            logf("Mouse look %s (relative mode %s)", context.game_relative ? "on" : "off",
                 sdl::game_wants_relative_mouse() ? "on" : "off");
        }
        context.window_points_w = points_w;
        context.window_points_h = points_h;
        if (frame_h > 0) context.render_aspect = static_cast<float>(frame_w) / static_cast<float>(frame_h);
        session::begin_frame(context);
        if (g_menu_open)
            menu::render(pixels_w, pixels_h, points_to_pixels, stereo::virtual_backbuffer(), sharp,
                         stereo::hud_fbo_this_frame(), g_sharp.width ? g_sharp.tall : 1.0f);
        session::end_frame(frame_w, frame_h, g_settings);
    }
    if (g_sharp.width || vr_active) {
        {
            InternalScope scope;
            GlStateGuard guard;
            stereo::present_virtual_backbuffer(pixels_w, pixels_h);
            update_sharpness(session::state() == session::State::Running, pixels_w, pixels_h);
        }
        // The guard put back the old framebuffers; "the window" may be a new buffer now.
        stereo::rebind_window();
    }

    if (g_dirty && GetTickCount64() - g_dirty_at > 800) {
        g_dirty = false;
        save_settings();
    }
}

void after_swap() {}

}  // namespace hvr::app
