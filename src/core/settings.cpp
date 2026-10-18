#include "settings.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace hvr {
namespace {

template <typename Visit>
void visit_fields(Settings& s, Visit&& visit) {
    visit("vr_enabled", s.vr_enabled);
    visit("vsync_off_in_vr", s.vsync_off_in_vr);
    visit("world_view", s.world_view);
    visit("room_walk", s.room_walk);
    visit("no_occlusion", s.no_occlusion);
    visit("hud_distance", s.hud_distance);
    visit("hud_width", s.hud_width);
    visit("world_scale", s.world_scale);
    visit("sharpness", s.render_scale);
    visit("item_in_hand", s.item_in_hand);
    visit("item_pitch", s.item_pitch);
    visit("item_x", s.item_x);
    visit("item_y", s.item_y);
    visit("item_z", s.item_z);
    visit("item_aligned", s.item_aligned);
    static const char* const grip_names[12] = {"item_grip_0", "item_grip_1", "item_grip_2",  "item_grip_3",
                                               "item_grip_4", "item_grip_5", "item_grip_6",  "item_grip_7",
                                               "item_grip_8", "item_grip_9", "item_grip_10", "item_grip_11"};
    for (int i = 0; i < 12; ++i) visit(grip_names[i], s.item_grip[i]);
    visit("hotbar_on_hand", s.hotbar_on_hand);
    visit("hotbar_width", s.hotbar_width);
    visit("hotbar_left", s.hotbar_left);
    visit("hotbar_right", s.hotbar_right);
    visit("hotbar_top", s.hotbar_top);
    visit("hotbar_bottom", s.hotbar_bottom);
    visit("swing_to_hit", s.swing_to_hit);
    visit("swing_speed", s.swing_speed);
    visit("vignette", s.vignette);
    visit("steady_view", s.steady_view);
    visit("tune_hytale", s.tune_hytale);
    visit("move_with", s.move_with);
    visit("seated", s.seated);
    visit("real_crouch", s.real_crouch);
    visit("crouch_depth", s.crouch_depth);
    visit("aim_with_hand", s.aim_with_hand);
    visit("turn_mode", s.turn_mode);
    visit("snap_degrees", s.snap_degrees);
    visit("smooth_speed", s.smooth_speed);
    visit("screen_distance", s.screen_distance);
    visit("screen_width", s.screen_width);
    visit("screen_height", s.screen_height);
    visit("show_floor", s.show_floor);
    visit("controller_input", s.controller_input);
    visit("left_handed", s.left_handed);
    visit("look_speed", s.look_speed);
    visit("deadzone", s.deadzone);
    visit("key_jump", s.key_jump);
    visit("key_sprint", s.key_sprint);
    visit("key_crouch", s.key_crouch);
    visit("key_inventory", s.key_inventory);
    visit("key_interact", s.key_interact);
    visit("key_forward", s.key_forward);
    visit("key_back", s.key_back);
    visit("key_left", s.key_left);
    visit("key_right", s.key_right);
    visit("menu_key", s.menu_key);
    visit("menu_scale", s.menu_scale);
}

std::string trim(const std::string& text) {
    const auto begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    const auto end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

void assign(bool& field, const std::string& value) {
    field = value == "1" || value == "true" || value == "yes" || value == "on";
}
void assign(int& field, const std::string& value) {
    char* end = nullptr;
    const long parsed = std::strtol(value.c_str(), &end, 10);
    if (end != value.c_str()) field = static_cast<int>(parsed);
}
void assign(float& field, const std::string& value) {
    char* end = nullptr;
    const float parsed = std::strtof(value.c_str(), &end);
    if (end != value.c_str()) field = parsed;
}

std::string text_of(bool value) { return value ? "true" : "false"; }
std::string text_of(int value) { return std::to_string(value); }
std::string text_of(float value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.5g", value);
    return buffer;
}

int clamp_key(int key, int fallback) { return key > 0 && key < 512 ? key : fallback; }

}  // namespace

void clamp(Settings& s) {
    const Settings defaults;
    s.hud_distance = std::clamp(s.hud_distance, 0.6f, 5.0f);
    s.hud_width = std::clamp(s.hud_width, 0.6f, 6.0f);
    s.world_scale = std::clamp(s.world_scale, 0.25f, 4.0f);
    s.render_scale = std::clamp(s.render_scale, 1.0f, 2.5f);
    s.item_pitch = std::clamp(s.item_pitch, -90.0f, 90.0f);
    s.item_x = std::clamp(s.item_x, -1.0f, 1.0f);
    s.item_y = std::clamp(s.item_y, -1.0f, 1.0f);
    s.item_z = std::clamp(s.item_z, -1.5f, 0.5f);
    s.hotbar_width = std::clamp(s.hotbar_width, 0.08f, 0.6f);
    s.hotbar_left = std::clamp(s.hotbar_left, 0.0f, 0.95f);
    s.hotbar_right = std::clamp(s.hotbar_right, s.hotbar_left + 0.05f, 1.0f);
    s.hotbar_top = std::clamp(s.hotbar_top, 0.0f, 0.95f);
    s.hotbar_bottom = std::clamp(s.hotbar_bottom, s.hotbar_top + 0.02f, 1.0f);
    s.swing_speed = std::clamp(s.swing_speed, 0.8f, 6.0f);
    s.vignette = std::clamp(s.vignette, 0.0f, 1.0f);
    s.move_with = std::clamp(s.move_with, 0, 1);
    s.crouch_depth = std::clamp(s.crouch_depth, 0.15f, 0.7f);
    s.turn_mode = std::clamp(s.turn_mode, 0, 1);
    s.snap_degrees = std::clamp(s.snap_degrees, 10.0f, 90.0f);
    s.smooth_speed = std::clamp(s.smooth_speed, 30.0f, 360.0f);
    s.screen_distance = std::clamp(s.screen_distance, 0.8f, 8.0f);
    s.screen_width = std::clamp(s.screen_width, 0.8f, 12.0f);
    s.screen_height = std::clamp(s.screen_height, -1.5f, 2.0f);
    s.look_speed = std::clamp(s.look_speed, 100.0f, 4000.0f);
    s.deadzone = std::clamp(s.deadzone, 0.0f, 0.6f);
    s.menu_scale = std::clamp(s.menu_scale, 0.6f, 2.0f);
    s.key_jump = clamp_key(s.key_jump, defaults.key_jump);
    s.key_sprint = clamp_key(s.key_sprint, defaults.key_sprint);
    s.key_crouch = clamp_key(s.key_crouch, defaults.key_crouch);
    s.key_inventory = clamp_key(s.key_inventory, defaults.key_inventory);
    s.key_interact = clamp_key(s.key_interact, defaults.key_interact);
    s.key_forward = clamp_key(s.key_forward, defaults.key_forward);
    s.key_back = clamp_key(s.key_back, defaults.key_back);
    s.key_left = clamp_key(s.key_left, defaults.key_left);
    s.key_right = clamp_key(s.key_right, defaults.key_right);
    s.menu_key = clamp_key(s.menu_key, defaults.menu_key);
}

Settings parse_settings(const std::string& text) {
    Settings s;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        const auto equals = line.find('=');
        if (equals == std::string::npos) continue;
        const std::string key = trim(line.substr(0, equals));
        const std::string value = trim(line.substr(equals + 1));
        visit_fields(s, [&](const char* name, auto& field) {
            if (key == name) assign(field, value);
        });
    }
    clamp(s);
    return s;
}

std::string format_settings(const Settings& settings) {
    Settings s = settings;
    std::string out = "# Hytale VR settings. Change these in game from the VR menu.\n";
    visit_fields(s, [&](const char* name, auto& field) {
        out += name;
        out += " = ";
        out += text_of(field);
        out += "\n";
    });
    return out;
}

}  // namespace hvr
