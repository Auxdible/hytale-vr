// Hytale's own settings file: switches off what makes VR uncomfortable.
#pragma once

#include <cctype>
#include <string>
#include <vector>

namespace hvr {

// Sets each of Hytale's camera comfort settings that is present in its
// settings JSON to its VR value: view bobbing and camera shake off. Keys that
// aren't there are left alone. Returns the keys it changed.
inline std::vector<std::string> tune_hytale_settings(std::string& json) {
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
    return changed;
}

}  // namespace hvr
