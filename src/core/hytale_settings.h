// Hytale's own settings file: switches off what makes VR uncomfortable.
#pragma once

#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>

namespace hvr {

// The field of view written for VR: Hytale decides what to draw and load from
// its own camera, which is narrower than a headset's eye unless this is high.
constexpr int kVrFieldOfView = 120;

inline std::string lower(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

// Raises every field-of-view setting (a key named like FieldOfView or Fov, not
// the sprint one) holding degrees below kVrFieldOfView. Values that don't look
// like degrees are left alone.
inline void raise_field_of_view(std::string& json, std::vector<std::string>& changed) {
    size_t at = 0;
    while ((at = json.find('"', at)) != std::string::npos) {
        const size_t close = json.find('"', at + 1);
        if (close == std::string::npos) return;
        const std::string key = json.substr(at + 1, close - at - 1);
        const std::string name = lower(key);
        at = close + 1;
        if (name.find("sprint") != std::string::npos) continue;
        if (name.find("fieldofview") == std::string::npos && name != "fov") continue;
        size_t p = close + 1;
        while (p < json.size() && std::isspace(static_cast<unsigned char>(json[p]))) ++p;
        if (p >= json.size() || json[p] != ':') continue;
        ++p;
        while (p < json.size() && std::isspace(static_cast<unsigned char>(json[p]))) ++p;
        size_t end = p;
        while (end < json.size() && (std::isdigit(static_cast<unsigned char>(json[end])) || json[end] == '.')) ++end;
        if (end == p) continue;
        const double degrees = std::atof(json.substr(p, end - p).c_str());
        if (degrees < 30 || degrees >= kVrFieldOfView) continue;
        json.replace(p, end - p, std::to_string(kVrFieldOfView));
        changed.push_back(key);
        at = p;
    }
}

// Sets each of Hytale's camera comfort settings that is present in its
// settings JSON to its VR value: view bobbing, camera shake and the sprint
// zoom off, and with field_of_view, the field of view raised. Keys that aren't
// there are left alone. Returns the keys it changed.
inline std::vector<std::string> tune_hytale_settings(std::string& json, bool field_of_view = false) {
    struct Want {
        const char* key;
        const char* value;
    };
    static const Want wants[] = {
        {"ViewBobbingEffect", "false"},
        {"ViewBobbingIntensity", "0"},
        {"CameraShakeEffect", "false"},
        {"FirstPersonCameraShakeIntensity", "0"},
        {"ThirdPersonCameraShakeIntensity", "0"},
        {"SprintFovEffect", "false"},
    };
    std::vector<std::string> changed;
    for (const Want& want : wants) {
        const std::string quoted = std::string("\"") + want.key + "\"";
        size_t at = 0;
        while ((at = json.find(quoted, at)) != std::string::npos) {
            size_t p = at + quoted.size();
            while (p < json.size() && std::isspace(static_cast<unsigned char>(json[p]))) ++p;
            if (p >= json.size() || json[p] != ':') {
                at = p;
                continue;
            }
            ++p;
            while (p < json.size() && std::isspace(static_cast<unsigned char>(json[p]))) ++p;
            // The value: a bare word or number, up to the next , } ] or space.
            size_t end = p;
            while (end < json.size() && json[end] != ',' && json[end] != '}' && json[end] != ']' &&
                   !std::isspace(static_cast<unsigned char>(json[end])))
                ++end;
            const std::string value = json.substr(p, end - p);
            const bool is_bool = value == "true" || value == "false";
            const bool want_bool = std::string(want.value) == "false";
            const bool is_number = !value.empty() && (std::isdigit(static_cast<unsigned char>(value[0])) ||
                                                      value[0] == '-' || value[0] == '.');
            if ((want_bool ? is_bool : is_number) && value != want.value) {
                json.replace(p, end - p, want.value);
                changed.push_back(want.key);
            }
            at = p;
        }
    }
    if (field_of_view) raise_field_of_view(json, changed);
    return changed;
}

}  // namespace hvr
