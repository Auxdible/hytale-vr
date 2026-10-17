#include "app.h"

#include <atomic>
#include <cstdio>
#include <string>

#include "census.h"
#include "gl_api.h"
#include "gl_state.h"
#include "input.h"
#include "internal.h"
#include "log.h"
#include "menu.h"
#include "sdl_hooks.h"
#include "vr_runtime.h"

namespace hvr::app {
namespace {

bool g_started = false;
bool g_gl_failed = false;
Settings g_settings;
bool g_dirty = false;
ULONGLONG g_dirty_at = 0;
std::atomic<bool> g_menu_open{false};

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

void start() {
    g_started = true;
    load_settings();
    sdl::set_menu_key(g_settings.menu_key);
    logf("Menu key: scancode %d. Start in VR: %s", g_settings.menu_key, g_settings.vr_enabled ? "yes" : "no");
    if (g_settings.vr_enabled) session::request_start();
}

}  // namespace

Settings& settings() { return g_settings; }

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
    if (g_menu_open || vr_active) {
        InternalScope scope;
        GlStateGuard guard;
        session::FrameContext context;
        context.settings = &g_settings;
        context.menu_open = g_menu_open;
        context.game_relative = sdl::game_wants_relative_mouse();
        context.window_points_w = points_w;
        context.window_points_h = points_h;
        session::begin_frame(context);
        if (g_menu_open) menu::render(pixels_w, pixels_h, points_to_pixels);
        session::end_frame(pixels_w, pixels_h, g_settings);
    }

    if (g_dirty && GetTickCount64() - g_dirty_at > 800) {
        g_dirty = false;
        save_settings();
    }
}

void after_swap() {}

}  // namespace hvr::app
