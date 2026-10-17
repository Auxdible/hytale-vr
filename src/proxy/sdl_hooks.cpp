#include "sdl_hooks.h"

#include <windows.h>

#include <MinHook.h>

#include <atomic>
#include <cstring>
#include <mutex>

#include "log.h"
#include "sdl3_min.h"

namespace hvr::sdl {
namespace {

using sdl3::Event;
using sdl3::Window;

// Hooked
bool (*real_PollEvent)(Event*) = nullptr;
int (*real_PeepEvents)(Event*, int, int, uint32_t, uint32_t) = nullptr;
bool (*real_WaitEventTimeout)(Event*, int32_t) = nullptr;
bool (*real_SetWindowRelativeMouseMode)(Window*, bool) = nullptr;
const bool* (*real_GetKeyboardState)(int*) = nullptr;
uint32_t (*real_GetMouseState)(float*, float*) = nullptr;
uint32_t (*real_GetRelativeMouseState)(float*, float*) = nullptr;
bool (*real_HideCursor)() = nullptr;
bool (*real_GetWindowSizeInPixels)(Window*, int*, int*) = nullptr;
bool (*real_GetWindowSize)(Window*, int*, int*) = nullptr;
float (*real_GetWindowPixelDensity)(Window*) = nullptr;
float (*real_GetWindowDisplayScale)(Window*) = nullptr;
void (*real_WarpMouseInWindow)(Window*, float, float) = nullptr;

// Called directly
Window* (*fn_GetKeyboardFocus)() = nullptr;
bool (*fn_GetWindowRelativeMouseMode)(Window*) = nullptr;
void (*fn_WarpMouseInWindow)(Window*, float, float) = nullptr;
bool (*fn_GetWindowSize)(Window*, int*, int*) = nullptr;
bool (*fn_GetWindowSizeInPixels)(Window*, int*, int*) = nullptr;
const char* (*fn_GetScancodeName)(int) = nullptr;
bool (*fn_ShowCursor)() = nullptr;
bool (*fn_CursorVisible)() = nullptr;
bool (*fn_PushEvent)(Event*) = nullptr;
uint32_t (*fn_GetWindowID)(Window*) = nullptr;
Window* (*fn_GL_GetCurrentWindow)() = nullptr;
uint64_t (*fn_GetTicksNS)() = nullptr;

std::atomic<bool> g_installed{false};
std::atomic<void*> g_hwnd{nullptr};
std::atomic<Window*> g_window{nullptr};

std::atomic<bool> g_capture_wanted{false};
std::atomic<bool> g_capture_applied{false};
bool g_saved_relative = false;
bool g_saved_cursor_hidden = false;

std::atomic<int> g_inject_x{0}, g_inject_y{0};
std::atomic<bool> g_warp_pending{false};
std::atomic<float> g_warp_x{0}, g_warp_y{0};

std::atomic<int> g_menu_key{63};
std::atomic<bool> g_menu_key_pressed{false};
std::atomic<bool> g_rebinding{false};
std::atomic<int> g_rebind_result{0};

std::mutex g_input_mutex;
std::vector<MenuInput> g_menu_input;

bool g_zero_keys[sdl3::SCANCODE_COUNT] = {};

// Render scale: Hytale is told its window has this many times the pixels.
std::atomic<float> g_render_scale{1.0f};
// Lens shape: Hytale is also told its window is this many times taller (in
// points and pixels), so it draws in the headset lens's shape.
std::atomic<float> g_tall{1.0f};
std::atomic<bool> g_size_event_pending{false};
std::atomic<long long> g_size_queries{0}, g_pixel_queries{0}, g_density_queries{0}, g_display_scale_queries{0};

std::atomic<long long> g_polls{0}, g_peeps{0}, g_waits{0}, g_key_state{0}, g_mouse_state{0}, g_relative_state{0};

Window* window() {
    Window* w = g_window.load();
    if (!w && fn_GetKeyboardFocus) w = fn_GetKeyboardFocus();
    return w;
}

void push_release(uint32_t type, int code) {
    if (!fn_PushEvent) return;
    Event e{};
    e.common.type = type;
    e.common.reserved = sdl3::kOurEvent;
    Window* w = window();
    const uint32_t id = (w && fn_GetWindowID) ? fn_GetWindowID(w) : 0;
    if (type == sdl3::KEY_UP) {
        e.key.windowID = id;
        e.key.scancode = code;
        e.key.down = false;
    } else {
        e.button.windowID = id;
        e.button.button = static_cast<uint8_t>(code);
        e.button.down = false;
    }
    fn_PushEvent(&e);
}

int scaled(int size) { return static_cast<int>(static_cast<float>(size) * g_render_scale.load() + 0.5f); }
int scaled_h(int size) {
    return static_cast<int>(static_cast<float>(size) * g_render_scale.load() * g_tall.load() + 0.5f);
}
int tall(int size) { return static_cast<int>(static_cast<float>(size) * g_tall.load() + 0.5f); }
bool spoofing() { return g_render_scale.load() != 1.0f || g_tall.load() != 1.0f; }

// Mouse positions in Hytale's taller window.
void tall_mouse(Event& e) {
    const float t = g_tall.load();
    if (t == 1.0f) return;
    switch (e.type) {
        case sdl3::MOUSE_MOTION: e.motion.y *= t; break;  // not yrel: that turns the camera
        case sdl3::MOUSE_BUTTON_DOWN:
        case sdl3::MOUSE_BUTTON_UP: e.button.y *= t; break;
        case sdl3::MOUSE_WHEEL: e.wheel.mouse_y *= t; break;
        default: break;
    }
}

// Tells Hytale its window changed size, so it picks up the scaled pixel size.
void push_size_events() {
    Window* w = window();
    if (!w || !fn_PushEvent || !real_GetWindowSizeInPixels || !fn_GetWindowSize) return;
    int points_w = 0, points_h = 0, pixels_w = 0, pixels_h = 0;
    if (!fn_GetWindowSize(w, &points_w, &points_h) || !real_GetWindowSizeInPixels(w, &pixels_w, &pixels_h)) return;
    const uint32_t id = fn_GetWindowID ? fn_GetWindowID(w) : 0;
    Event e{};
    e.window.type = sdl3::WINDOW_RESIZED;
    e.window.reserved = sdl3::kOurEvent;
    e.window.timestamp = fn_GetTicksNS ? fn_GetTicksNS() : 0;
    e.window.windowID = id;
    e.window.data1 = points_w;
    e.window.data2 = tall(points_h);
    fn_PushEvent(&e);
    e.window.type = sdl3::WINDOW_PIXEL_SIZE_CHANGED;
    e.window.data1 = scaled(pixels_w);
    e.window.data2 = scaled_h(pixels_h);
    fn_PushEvent(&e);
    logf("Told Hytale its window is %dx%d pixels (really %dx%d)", e.window.data1, e.window.data2, pixels_w, pixels_h);
}

// Runs on SDL's thread from inside the event hooks.
void apply_pending() {
    const bool wanted = g_capture_wanted.load();
    if (wanted != g_capture_applied.load()) {
        Window* w = window();
        if (wanted) {
            g_saved_relative = w && fn_GetWindowRelativeMouseMode && fn_GetWindowRelativeMouseMode(w);
            g_saved_cursor_hidden = fn_CursorVisible && !fn_CursorVisible();
            // Let go of anything the game thinks is held, before the menu swallows the key-up.
            int count = 0;
            const bool* keys = real_GetKeyboardState ? real_GetKeyboardState(&count) : nullptr;
            for (int i = 0; keys && i < count && i < sdl3::SCANCODE_COUNT; ++i)
                if (keys[i]) push_release(sdl3::KEY_UP, i);
            push_release(sdl3::MOUSE_BUTTON_UP, sdl3::BUTTON_LEFT);
            push_release(sdl3::MOUSE_BUTTON_UP, sdl3::BUTTON_RIGHT);
            if (w && real_SetWindowRelativeMouseMode) real_SetWindowRelativeMouseMode(w, false);
            if (fn_ShowCursor) fn_ShowCursor();
            g_capture_applied = true;
        } else {
            g_capture_applied = false;
            if (w && real_SetWindowRelativeMouseMode) real_SetWindowRelativeMouseMode(w, g_saved_relative);
            if (g_saved_cursor_hidden && !g_saved_relative && real_HideCursor) real_HideCursor();
        }
    }
    if (g_size_event_pending.exchange(false)) push_size_events();
    if (g_warp_pending.exchange(false)) {
        Window* w = window();
        if (w && fn_WarpMouseInWindow) fn_WarpMouseInWindow(w, g_warp_x.load(), g_warp_y.load());
    }
}

void record(const MenuInput& input) {
    std::lock_guard lock(g_input_mutex);
    if (g_menu_input.size() < 512) g_menu_input.push_back(input);
}

// Returns true when the game should see the event.
bool pass(Event& e) {
    if (e.common.reserved == sdl3::kOurEvent) return true;
    const uint32_t type = e.type;
    if (type == sdl3::WINDOW_PIXEL_SIZE_CHANGED && spoofing()) {
        e.window.data1 = scaled(e.window.data1);
        e.window.data2 = scaled_h(e.window.data2);
        return true;
    }
    if (type == sdl3::WINDOW_RESIZED && g_tall.load() != 1.0f) {
        e.window.data2 = tall(e.window.data2);
        return true;
    }
    if (type == sdl3::KEY_DOWN || type == sdl3::KEY_UP) {
        const int scancode = e.key.scancode;
        if (g_rebinding && type == sdl3::KEY_DOWN && !e.key.repeat) {
            g_rebind_result = scancode == 41 ? -1 : scancode;
            g_rebinding = false;
            return false;
        }
        if (scancode == g_menu_key) {
            if (type == sdl3::KEY_DOWN && !e.key.repeat) g_menu_key_pressed = true;
            return false;
        }
    }
    if (!g_capture_applied) {
        tall_mouse(e);
        return true;
    }
    switch (type) {
        case sdl3::MOUSE_MOTION:
            record({MenuInput::Motion, e.motion.x, e.motion.y});
            return false;
        case sdl3::MOUSE_BUTTON_DOWN:
        case sdl3::MOUSE_BUTTON_UP: {
            MenuInput input{MenuInput::Button, e.button.x, e.button.y};
            input.button = e.button.button;
            input.down = type == sdl3::MOUSE_BUTTON_DOWN;
            record(input);
            return false;
        }
        case sdl3::MOUSE_WHEEL: {
            const float flip = e.wheel.direction == 1 ? -1.0f : 1.0f;
            record({MenuInput::Wheel, e.wheel.x * flip, e.wheel.y * flip});
            return false;
        }
        case sdl3::KEY_DOWN:
        case sdl3::KEY_UP: {
            MenuInput input{MenuInput::Key};
            input.scancode = e.key.scancode;
            input.down = type == sdl3::KEY_DOWN;
            record(input);
            return false;
        }
        case sdl3::TEXT_EDITING:
        case sdl3::TEXT_INPUT:
            return false;
        default:
            tall_mouse(e);
            return true;
    }
}

// Motion we owe Hytale's camera, as one relative mouse event.
bool take_injected_motion(Event* e) {
    if (g_capture_applied) return false;
    const int x = g_inject_x.exchange(0), y = g_inject_y.exchange(0);
    if (x == 0 && y == 0) return false;
    std::memset(e, 0, sizeof(*e));
    e->motion.type = sdl3::MOUSE_MOTION;
    e->motion.reserved = sdl3::kOurEvent;
    Window* w = window();
    e->motion.windowID = (w && fn_GetWindowID) ? fn_GetWindowID(w) : 0;
    e->motion.timestamp = fn_GetTicksNS ? fn_GetTicksNS() : 0;
    // Absolute position too, for games that measure motion from the cursor.
    float mx = 0, my = 0;
    if (real_GetMouseState) real_GetMouseState(&mx, &my);
    e->motion.x = mx + static_cast<float>(x);
    e->motion.y = (my + static_cast<float>(y)) * g_tall.load();
    e->motion.xrel = static_cast<float>(x);
    e->motion.yrel = static_cast<float>(y);
    return true;
}

bool hook_PollEvent(Event* e) {
    ++g_polls;
    apply_pending();
    if (!e) return real_PollEvent(e) || g_inject_x.load() != 0 || g_inject_y.load() != 0;
    while (real_PollEvent(e)) {
        if (pass(*e)) return true;
    }
    return take_injected_motion(e);
}

int hook_PeepEvents(Event* events, int count, int action, uint32_t min_type, uint32_t max_type) {
    ++g_peeps;
    apply_pending();
    const int got = real_PeepEvents(events, count, action, min_type, max_type);
    if (action != sdl3::GETEVENT || !events || got <= 0) return got;
    int kept = 0;
    for (int i = 0; i < got; ++i)
        if (pass(events[i])) events[kept++] = events[i];
    return kept;
}

bool hook_WaitEventTimeout(Event* e, int32_t timeout) {
    ++g_waits;
    apply_pending();
    if (!e) return real_WaitEventTimeout(e, timeout);
    if (!real_WaitEventTimeout(e, timeout)) return false;
    if (pass(*e)) return true;
    // Swallowed: report any other pending event, or a timeout.
    return hook_PollEvent(e);
}

bool hook_SetWindowRelativeMouseMode(Window* w, bool enabled) {
    if (g_capture_applied) {
        g_saved_relative = enabled;
        return true;
    }
    return real_SetWindowRelativeMouseMode(w, enabled);
}

const bool* hook_GetKeyboardState(int* count) {
    ++g_key_state;
    const bool* keys = real_GetKeyboardState(count);
    return g_capture_applied ? g_zero_keys : keys;
}

uint32_t hook_GetMouseState(float* x, float* y) {
    ++g_mouse_state;
    const uint32_t buttons = real_GetMouseState(x, y);
    if (y) *y *= g_tall.load();
    return g_capture_applied ? 0 : buttons;
}

uint32_t hook_GetRelativeMouseState(float* x, float* y) {
    ++g_relative_state;
    const uint32_t buttons = real_GetRelativeMouseState(x, y);
    if (!g_capture_applied) return buttons;
    if (x) *x = 0;
    if (y) *y = 0;
    return 0;
}

bool hook_HideCursor() {
    if (g_capture_applied) {
        g_saved_cursor_hidden = true;
        return true;
    }
    return real_HideCursor();
}

bool hook_GetWindowSizeInPixels(Window* w, int* width, int* height) {
    ++g_pixel_queries;
    const bool ok = real_GetWindowSizeInPixels(w, width, height);
    if (ok && spoofing()) {
        if (width) *width = scaled(*width);
        if (height) *height = scaled_h(*height);
    }
    return ok;
}

bool hook_GetWindowSize(Window* w, int* width, int* height) {
    ++g_size_queries;
    const bool ok = real_GetWindowSize(w, width, height);
    if (ok && height && g_tall.load() != 1.0f) *height = tall(*height);
    return ok;
}

void hook_WarpMouseInWindow(Window* w, float x, float y) { real_WarpMouseInWindow(w, x, y / g_tall.load()); }

float hook_GetWindowPixelDensity(Window* w) {
    ++g_density_queries;
    return real_GetWindowPixelDensity(w) * g_render_scale.load();
}

float hook_GetWindowDisplayScale(Window* w) {
    ++g_display_scale_queries;
    return real_GetWindowDisplayScale(w) * g_render_scale.load();
}

template <typename F>
bool find(HMODULE module, const char* name, F& out) {
    out = reinterpret_cast<F>(GetProcAddress(module, name));
    if (!out) logf("SDL3 has no %s", name);
    return out != nullptr;
}

template <typename F>
bool hook(HMODULE module, const char* name, F detour, F& real) {
    void* target = reinterpret_cast<void*>(GetProcAddress(module, name));
    if (!target) {
        logf("SDL3 has no %s", name);
        return false;
    }
    if (MH_CreateHook(target, reinterpret_cast<void*>(detour), reinterpret_cast<void**>(&real)) != MH_OK) {
        logf("Could not hook %s", name);
        return false;
    }
    return true;
}

}  // namespace

bool install() {
    static std::atomic<bool> attempted{false};
    if (g_installed) return true;
    HMODULE module = GetModuleHandleW(L"SDL3.dll");
    if (!module || attempted.exchange(true)) return false;
    bool ok = true;
    ok &= find(module, "SDL_GetKeyboardFocus", fn_GetKeyboardFocus);
    ok &= find(module, "SDL_GetWindowRelativeMouseMode", fn_GetWindowRelativeMouseMode);
    ok &= find(module, "SDL_WarpMouseInWindow", fn_WarpMouseInWindow);
    ok &= find(module, "SDL_GetWindowSize", fn_GetWindowSize);
    ok &= find(module, "SDL_GetWindowSizeInPixels", fn_GetWindowSizeInPixels);
    ok &= find(module, "SDL_GetScancodeName", fn_GetScancodeName);
    ok &= find(module, "SDL_ShowCursor", fn_ShowCursor);
    ok &= find(module, "SDL_CursorVisible", fn_CursorVisible);
    ok &= find(module, "SDL_PushEvent", fn_PushEvent);
    ok &= find(module, "SDL_GetWindowID", fn_GetWindowID);
    ok &= find(module, "SDL_GL_GetCurrentWindow", fn_GL_GetCurrentWindow);
    fn_GetTicksNS = reinterpret_cast<uint64_t (*)()>(GetProcAddress(module, "SDL_GetTicksNS"));
    if (!ok) {
        logf("SDL3 is missing functions; the VR menu can't take input");
        return false;
    }
    const MH_STATUS init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
        logf("MinHook failed to start (%d)", static_cast<int>(init));
        return false;
    }
    ok &= hook(module, "SDL_PollEvent", &hook_PollEvent, real_PollEvent);
    ok &= hook(module, "SDL_PeepEvents", &hook_PeepEvents, real_PeepEvents);
    ok &= hook(module, "SDL_WaitEventTimeout", &hook_WaitEventTimeout, real_WaitEventTimeout);
    ok &= hook(module, "SDL_SetWindowRelativeMouseMode", &hook_SetWindowRelativeMouseMode, real_SetWindowRelativeMouseMode);
    ok &= hook(module, "SDL_GetKeyboardState", &hook_GetKeyboardState, real_GetKeyboardState);
    ok &= hook(module, "SDL_GetMouseState", &hook_GetMouseState, real_GetMouseState);
    ok &= hook(module, "SDL_GetRelativeMouseState", &hook_GetRelativeMouseState, real_GetRelativeMouseState);
    ok &= hook(module, "SDL_HideCursor", &hook_HideCursor, real_HideCursor);
    ok &= hook(module, "SDL_GetWindowSizeInPixels", &hook_GetWindowSizeInPixels, real_GetWindowSizeInPixels);
    ok &= hook(module, "SDL_GetWindowSize", &hook_GetWindowSize, real_GetWindowSize);
    // Only for the render scale; fine without them.
    if (!hook(module, "SDL_GetWindowPixelDensity", &hook_GetWindowPixelDensity, real_GetWindowPixelDensity))
        real_GetWindowPixelDensity = nullptr;
    if (!hook(module, "SDL_GetWindowDisplayScale", &hook_GetWindowDisplayScale, real_GetWindowDisplayScale))
        real_GetWindowDisplayScale = nullptr;
    ok &= hook(module, "SDL_WarpMouseInWindow", &hook_WarpMouseInWindow, real_WarpMouseInWindow);
    if (!ok) {
        MH_RemoveHook(MH_ALL_HOOKS);
        logf("SDL3 hooks were not installed; the VR menu can't take input");
        return false;
    }
    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        logf("SDL3 hooks could not be enabled");
        return false;
    }
    // Our own size queries skip the scaling.
    fn_GetWindowSize = real_GetWindowSize;
    fn_GetWindowSizeInPixels = real_GetWindowSizeInPixels;
    fn_WarpMouseInWindow = real_WarpMouseInWindow;
    g_installed = true;
    logf("SDL3 input hooks installed");
    return true;
}

bool installed() { return g_installed; }

void set_window(void* hwnd) {
    g_hwnd = hwnd;
    if (!g_installed || g_window.load()) return;
    // The window whose GL context is presenting right now.
    Window* w = fn_GL_GetCurrentWindow ? fn_GL_GetCurrentWindow() : nullptr;
    if (w) g_window = w;
}

void* hwnd() { return g_hwnd; }

void set_capture(bool on) { g_capture_wanted = on; }
bool capture_active() { return g_capture_applied; }

std::vector<MenuInput> take_menu_input() {
    std::lock_guard lock(g_input_mutex);
    std::vector<MenuInput> out;
    out.swap(g_menu_input);
    return out;
}

bool take_menu_key_press() { return g_menu_key_pressed.exchange(false); }
void set_menu_key(int scancode) { g_menu_key = scancode; }

void begin_rebind() {
    g_rebind_result = 0;
    g_rebinding = true;
}
bool rebinding() { return g_rebinding; }
int take_rebind_result() { return g_rebind_result.exchange(0); }

bool game_wants_relative_mouse() {
    if (g_capture_applied) return g_saved_relative;
    Window* w = window();
    return w && fn_GetWindowRelativeMouseMode && fn_GetWindowRelativeMouseMode(w);
}

bool game_mouse_look() {
    if (game_wants_relative_mouse()) return true;
    if (g_capture_applied) return g_saved_cursor_hidden;
    if (fn_CursorVisible && !fn_CursorVisible()) return true;
    CURSORINFO info{};
    info.cbSize = sizeof(info);
    if (GetCursorInfo(&info) && !(info.flags & CURSOR_SHOWING) && GetForegroundWindow() == static_cast<HWND>(g_hwnd.load()))
        return true;
    return false;
}

bool window_size(int& points_w, int& points_h, int& pixels_w, int& pixels_h) {
    Window* w = window();
    if (!w || !fn_GetWindowSize || !fn_GetWindowSizeInPixels) return false;
    return fn_GetWindowSize(w, &points_w, &points_h) && fn_GetWindowSizeInPixels(w, &pixels_w, &pixels_h) &&
           points_w > 0 && points_h > 0;
}

void set_render_scale(float scale, float taller) {
    if (!g_installed) return;
    const bool same = g_render_scale.exchange(scale) == scale;
    if (g_tall.exchange(taller) == taller && same) return;
    g_size_event_pending = true;
}

float render_scale() { return g_render_scale; }
float render_taller() { return g_tall; }

void inject_mouse_motion(int dx, int dy) {
    if (dx) g_inject_x += dx;
    if (dy) g_inject_y += dy;
}

void warp_mouse(float x, float y) {
    g_warp_x = x;
    g_warp_y = y;
    g_warp_pending = true;
}

std::string scancode_name(int scancode) {
    const char* name = fn_GetScancodeName ? fn_GetScancodeName(scancode) : nullptr;
    if (name && *name) return name;
    return "Key " + std::to_string(scancode);
}

std::string usage_summary() {
    char text[384];
    snprintf(text, sizeof(text), "PollEvent %lld, PeepEvents %lld, WaitEventTimeout %lld, GetKeyboardState %lld, "
             "GetMouseState %lld, GetRelativeMouseState %lld, GetWindowSize %lld, GetWindowSizeInPixels %lld, "
             "GetWindowPixelDensity %lld, GetWindowDisplayScale %lld", g_polls.load(), g_peeps.load(), g_waits.load(),
             g_key_state.load(), g_mouse_state.load(), g_relative_state.load(), g_size_queries.load(),
             g_pixel_queries.load(), g_density_queries.load(), g_display_scale_queries.load());
    return text;
}

}  // namespace hvr::sdl
