// VR settings, stored as key=value lines in HytaleVR\settings.ini.
#pragma once

#include <string>

namespace hvr {

// Key values are SDL3 scancodes, which name physical keys on any layout.
namespace scancode {
constexpr int A = 4, D = 7, E = 8, F = 9, Q = 20, S = 22, W = 26;
constexpr int Escape = 41, Tab = 43, Space = 44;
constexpr int F1 = 58, F6 = 63, F12 = 69;
constexpr int LCtrl = 224, LShift = 225;
}  // namespace scancode

struct Settings {
    // VR
    bool vr_enabled = false;        // start VR with the game
    bool vsync_off_in_vr = true;    // SteamVR paces frames; vsync would halve them

    // World view: Hytale's own 3D, drawn once per eye
    bool world_view = true;
    bool room_walk = false;         // walking in your room walks your character
    bool no_occlusion = true;       // turn off Hytale's occlusion culling in 3D (stops pop-in)
    float hud_distance = 1.6f;      // meters
    float hud_width = 1.9f;         // meters
    float world_scale = 1.0f;       // Hytale blocks per meter of head movement
    float render_scale = 1.0f;      // Hytale draws at this many times the window's size while in VR (1 = off)
    int turn_mode = 0;              // 0 snap, 1 smooth
    float snap_degrees = 30.0f;
    float smooth_speed = 120.0f;    // degrees per second at full stick
    bool tune_hytale = true;        // turn off view bobbing and camera shake in Hytale's own settings
    bool steady_view = true;        // smooths Hytale's camera sway out of the view while you walk
    int move_with = 0;              // the stick walks where 0 your head or 1 your other hand points
    bool seated = false;            // playing sitting down: no crouching or walking by moving your head
    bool real_crouch = true;        // crouching in your room crouches your character
    float crouch_depth = 0.35f;     // meters your head drops before that counts as crouching
    float vignette = 0.5f;          // comfort: how much the edges darken while the stick moves or turns you (0 = off)

    // Hands
    // Hytale's own first-person arms and held item. They are glued to your
    // head as one piece, so they stay hidden unless you want them back.
    bool show_arms = false;
    // Hytale's own hand and held item, carried by your controller instead.
    bool hytale_hand = true;
    bool hytale_arms = true;        // your character's own arm, in your skin, on each controller
    float item_size = 0.7f;         // of Hytale's first-person size
    float item_turn = 180.0f;       // degrees it turns about the controller's up axis
    float item_tilt = 30.0f;        // degrees the item tips forward from the controller
    bool hotbar_on_hand = true;     // the hotbar floats over the left controller
    float hotbar_width = 0.22f;     // meters
    float hotbar_left = 0.31f;      // where the hotbar is in Hytale's HUD, as fractions of the window
    float hotbar_right = 0.69f;
    float hotbar_top = 0.88f;
    float hotbar_bottom = 0.995f;
    bool aim_with_hand = false;     // Hytale aims (and breaks and places) where the right controller points
    bool swing_to_hit = true;       // a quick swing of the right controller attacks
    float swing_speed = 2.2f;       // meters per second

    // Screen
    float screen_distance = 2.6f;   // meters
    float screen_width = 3.4f;      // meters
    float screen_height = 0.0f;     // meters above eye height
    bool show_floor = true;

    // Controllers
    bool controller_input = true;
    bool left_handed = false;       // laser and click on the left hand
    float look_speed = 900.0f;      // mouse pixels per second at full stick
    float deadzone = 0.18f;
    int key_jump = scancode::Space;
    int key_sprint = scancode::LShift;
    int key_crouch = scancode::LCtrl;
    int key_inventory = scancode::Tab;
    int key_interact = scancode::F;
    int key_forward = scancode::W;
    int key_back = scancode::S;
    int key_left = scancode::A;
    int key_right = scancode::D;

    // Menu
    int menu_key = scancode::F6;
    float menu_scale = 1.0f;
};

void clamp(Settings& s);
Settings parse_settings(const std::string& text);
std::string format_settings(const Settings& s);

}  // namespace hvr
