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
    int width = 0, height = 0;  // the buffer it draws into (0 = off)
    int frames = 0;         // since the last change
    bool checked = false;   // Hytale was seen drawing at the new size
    bool gave_up = false;   // Hytale kept drawing at the window's size
    std::string status = "Off until VR starts.";
} g_sharp;

int scaled(int size, float scale) { return static_cast<int>(static_cast<float>(size) * scale + 0.5f); }

void apply_sharpness(float scale, int pixels_w, int pixels_h) {
    const int w = scale > 1.0f ? scaled(pixels_w, scale) : 0, h = scale > 1.0f ? scaled(pixels_h, scale) : 0;
    if (scale == g_sharp.scale && w == g_sharp.width && h == g_sharp.height) return;
    const bool on = stereo::set_virtual_backbuffer(w, h);
    g_sharp.scale = on ? scale : 1.0f;
    g_sharp.width = on ? w : 0;
    g_sharp.height = on ? h : 0;
    g_sharp.frames = 0;
    g_sharp.checked = false;
    sdl::set_render_scale(g_sharp.scale);
}

// Called after the frame is shown: picks the size for the next one.
void update_sharpness(bool vr_active, int pixels_w, int pixels_h) {
    float want = 1.0f;
    if (vr_active && !g_sharp.gave_up && sdl::installed()) want = g_settings.render_scale;
    if (want < 1.05f) want = 1.0f;
    // Wait for the slider to settle before resizing everything.
    if (want != g_sharp.scale && g_dirty) return;
    apply_sharpness(want, pixels_w, pixels_h);
    if (g_sharp.scale == 1.0f) {
        g_sharp.status = g_sharp.gave_up ? "Off: Hytale kept drawing at the window's size."
                         : vr_active   ? "Off."
                                       : "Off until VR starts.";
        return;
    }
    ++g_sharp.frames;
    const int seen = stereo::window_viewport_width();
    if (!g_sharp.checked && seen == g_sharp.width) {
        g_sharp.checked = true;
        logf("Hytale draws at %dx%d (%.2fx the window)", g_sharp.width, g_sharp.height, g_sharp.scale);
    }
    if (!g_sharp.checked && g_sharp.frames > 300 && seen > 0) {
        logf("Hytale didn't pick up the bigger size (still drawing %d wide, wanted %d); Sharpness is off. %s", seen,
             g_sharp.width, sdl::usage_summary().c_str());
        g_sharp.gave_up = true;
        apply_sharpness(1.0f, pixels_w, pixels_h);
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

void start() {
    g_started = true;
    load_settings();
    sdl::set_menu_key(g_settings.menu_key);
    logf("Menu key: scancode %d. Start in VR: %s", g_settings.menu_key, g_settings.vr_enabled ? "yes" : "no");
    if (g_settings.vr_enabled) session::request_start();
}

}  // namespace

Settings& settings() { return g_settings; }

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
        session::begin_frame(context);
        if (g_menu_open)
            menu::render(pixels_w, pixels_h, points_to_pixels, stereo::virtual_backbuffer(), sharp,
                         stereo::hud_fbo_this_frame());
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
