// Platform-free controller-to-keyboard/mouse logic.
#pragma once

#include <cmath>

namespace hvr {

struct MoveKeys {
    bool forward = false, back = false, left = false, right = false;
};

// Turns the movement stick into WASD. Diagonals press two keys.
inline MoveKeys stick_to_keys(float x, float y, float deadzone) {
    MoveKeys keys;
    const float magnitude = std::sqrt(x * x + y * y);
    if (magnitude < deadzone) return keys;
    // 8-way: a key is held when its axis carries at least ~38% of the push.
    const float threshold = 0.38f * magnitude;
    keys.forward = y > threshold;
    keys.back = y < -threshold;
    keys.right = x > threshold;
    keys.left = x < -threshold;
    return keys;
}

// Re-expresses a stick push (x right, y forward) meant relative to one facing
// relative to another facing, where turn is how far the first facing is to
// the left of the second (yaw angles grow to the left).
inline void rotate_stick(float x, float y, float turn, float& out_x, float& out_y) {
    const float c = std::cos(turn), s = std::sin(turn);
    out_x = x * c - y * s;
    out_y = x * s + y * c;
}

// Stick deflection to a smooth 0..1 response past the deadzone.
inline float stick_curve(float value, float deadzone) {
    const float a = std::fabs(value);
    if (a <= deadzone) return 0;
    const float t = (a - deadzone) / (1 - deadzone);
    const float shaped = t * t * (0.5f + 0.5f * t);
    return value < 0 ? -shaped : shaped;
}

// Accumulates fractional mouse motion so slow stick turns aren't lost.
struct MotionAccumulator {
    float x = 0, y = 0;

    // Returns whole pixels to send this frame and keeps the remainder.
    void add(float dx, float dy, int& out_x, int& out_y) {
        x += dx;
        y += dy;
        out_x = static_cast<int>(x);
        out_y = static_cast<int>(y);
        x -= static_cast<float>(out_x);
        y -= static_cast<float>(out_y);
    }
};

}  // namespace hvr
