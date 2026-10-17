// Hooks into Hytale's SDL3 so the VR menu can take the mouse and keyboard,
// and so the menu key reaches us before the game.
#pragma once

#include <string>
#include <vector>

namespace hvr::sdl {

// Installs the hooks once SDL3.dll is loaded. Safe to call every frame.
bool install();
bool installed();

// Called at swap on the render thread with the game's window.
void set_window(void* hwnd);
void* hwnd();

// The menu owns input while capture is on: the game sees no keys or mouse,
// the cursor is free, and events go to menu_events() instead.
void set_capture(bool on);
bool capture_active();

struct MenuInput {
    enum Kind { Motion, Button, Wheel, Key } kind;
    float x = 0, y = 0;  // window points for Motion and Button; scroll for Wheel
    int button = 0;
    bool down = false;
    int scancode = 0;
};
std::vector<MenuInput> take_menu_input();

// True once per press of the menu key.
bool take_menu_key_press();
void set_menu_key(int scancode);

// Captures the next key press as a new binding. Escape cancels (-1).
void begin_rebind();
bool rebinding();
int take_rebind_result();  // 0 while waiting

// Window queries; zero when SDL isn't ready.
bool game_wants_relative_mouse();  // true while Hytale is in SDL's relative mouse mode
// True while Hytale is steering its camera with the mouse: relative mode, or
// the cursor hidden (SDL's or Windows').
bool game_mouse_look();
bool window_size(int& points_w, int& points_h, int& pixels_w, int& pixels_h);
// Moves the OS cursor to a point in the window (applied on SDL's thread).
void warp_mouse(float x, float y);
std::string scancode_name(int scancode);
// Tells Hytale its window has scale times the pixels it really has (1 = the
// truth), so it renders sharper; the extra pixels go to a buffer of that size.
void set_render_scale(float scale);
float render_scale();
// Adds relative mouse motion for Hytale's camera, delivered as an SDL event.
void inject_mouse_motion(int dx, int dy);

std::string usage_summary();

}  // namespace hvr::sdl
