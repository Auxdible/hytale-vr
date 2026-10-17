// VR settings, stored as key=value lines in HytaleVR\settings.ini.
#pragma once

#include <string>

namespace hvr {

// Key values are SDL3 scancodes, which name physical keys on any layout.
namespace scancode {
constexpr int A = 4, D = 7, E = 8, Q = 20, S = 22, W = 26;
constexpr int Escape = 41, Tab = 43, Space = 44;
constexpr int F1 = 58, F6 = 63, F12 = 69;
constexpr int LCtrl = 224, LShift = 225;
}  // namespace scancode

struct Settings {
    // VR
    bool vr_enabled = false;        // start VR with the game
    bool vsync_off_in_vr = true;    // SteamVR paces frames; vsync would halve them

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
