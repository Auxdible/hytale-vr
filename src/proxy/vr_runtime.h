// SteamVR session: starts and stops VR, reads the headset and controllers,
// and shows Hytale in the headset.
#pragma once

#include <string>

#include "../core/settings.h"

namespace hvr::session {

enum class State { Off, Starting, Running, Failed };

void request_start();
void request_stop();
State state();
std::string status_text();
std::string headset_name();

// Each frame on the render thread, before the menu draws.
struct FrameContext {
    const Settings* settings = nullptr;
    bool menu_open = false;
    bool game_relative = false;   // Hytale is in mouse-look mode
    int window_points_w = 0, window_points_h = 0;
    float render_aspect = 16.0f / 9.0f;  // width / height of what Hytale draws
};
void begin_frame(const FrameContext& context);
// After the menu has drawn into the window: show the frame in the headset.
void end_frame(int pixels_w, int pixels_h, const Settings& settings);

void recenter();
void force_world();  // treat the current scene as a world now
bool take_menu_button();  // the controller's menu button was pressed
bool plugin_connected();  // the Hytale VR server plugin is answering


}  // namespace hvr::session
