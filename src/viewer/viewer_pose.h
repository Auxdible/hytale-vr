#pragma once

// How the headset's tracking space is placed in the Hytale world, and the
// smoothing that turns the plugin's 20 Hz positions into per-frame ones.
// Both spaces are right-handed and Y-up with -Z forward; Hytale's yaw 0 looks
// along -Z and grows when turning left.

#include <cmath>
#include <deque>

#include "vr_math.h"

namespace hytalevr::viewer {

constexpr float kPi = 3.14159265358979323846f;

inline float wrap_angle(float radians) {
    while (radians > kPi) radians -= 2.0f * kPi;
    while (radians < -kPi) radians += 2.0f * kPi;
    return radians;
}

// Hytale's yaw for a forward direction.
inline float hytale_yaw(float forward_x, float forward_z) {
    return std::atan2(-forward_x, -forward_z);
}

// The yaw convention the dashboard and the old render hook share.
inline float dashboard_yaw(float forward_x, float forward_z) {
    return std::atan2(forward_x, -forward_z);
}

// Rotates (x, z) by yaw; a positive yaw turns -Z towards -X (left).
inline void rotate_xz(float yaw, float x, float z, float& out_x, float& out_z) {
    const float c = std::cos(yaw);
    const float s = std::sin(yaw);
    out_x = c * x + s * z;
    out_z = -s * x + c * z;
}

struct TrackingAnchor {
    float yaw = 0.0f;
    // The tracking-space floor point that sits on the player's feet.
    float origin_x = 0.0f;
    float origin_z = 0.0f;
};

// pose is OpenVR's 3x4 row-major device-to-tracking matrix.
inline void pose_position(const float pose[12], float out[3]) {
    out[0] = pose[3];
    out[1] = pose[7];
    out[2] = pose[11];
}

inline void pose_forward(const float pose[12], float out[3]) {
    out[0] = -pose[2];
    out[1] = -pose[6];
    out[2] = -pose[10];
}

// Puts the headset over the player's feet, facing the way Hytale faces.
inline void recenter(TrackingAnchor& anchor, const float hmd_pose[12], float native_hytale_yaw) {
    float forward[3];
    pose_forward(hmd_pose, forward);
    anchor.origin_x = hmd_pose[3];
    anchor.origin_z = hmd_pose[11];
    anchor.yaw = wrap_angle(native_hytale_yaw - hytale_yaw(forward[0], forward[2]));
}

// Turns the view about the headset rather than the play-space origin, so
// turning never slides the player sideways.
inline void turn_about_head(TrackingAnchor& anchor, float delta, const float hmd_pose[12]) {
    float offset_x = 0.0f;
    float offset_z = 0.0f;
    rotate_xz(anchor.yaw, hmd_pose[3] - anchor.origin_x, hmd_pose[11] - anchor.origin_z, offset_x, offset_z);
    anchor.yaw = wrap_angle(anchor.yaw + delta);
    float back_x = 0.0f;
    float back_z = 0.0f;
    rotate_xz(-anchor.yaw, offset_x, offset_z, back_x, back_z);
    anchor.origin_x = hmd_pose[3] - back_x;
    anchor.origin_z = hmd_pose[11] - back_z;
}

// Maps a tracking-space point to world space relative to `feet`, the
// player's feet already expressed relative to the render origin.
inline Matrix4 world_from_tracking(const TrackingAnchor& anchor, const float feet[3]) {
    const float c = std::cos(anchor.yaw);
    const float s = std::sin(anchor.yaw);
    Matrix4 m = identity_matrix();
    m.value[0] = c;
    m.value[2] = -s;
    m.value[8] = s;
    m.value[10] = c;
    m.value[12] = feet[0] - (c * anchor.origin_x + s * anchor.origin_z);
    m.value[13] = feet[1];
    m.value[14] = feet[2] - (-s * anchor.origin_x + c * anchor.origin_z);
    return m;
}

inline Matrix4 matrix_from_pose(const float pose[12]) {
    Matrix4 m = identity_matrix();
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 4; ++column) {
            m.value[column * 4 + row] = pose[row * 4 + column];
        }
    }
    return m;
}

inline void transform_point(const Matrix4& m, const float in[3], float out[3]) {
    for (int row = 0; row < 3; ++row) {
        out[row] = m.value[row] * in[0] + m.value[4 + row] * in[1] + m.value[8 + row] * in[2] +
                   m.value[12 + row];
    }
}

inline void transform_direction(const Matrix4& m, const float in[3], float out[3]) {
    for (int row = 0; row < 3; ++row) {
        out[row] = m.value[row] * in[0] + m.value[4 + row] * in[1] + m.value[8 + row] * in[2];
    }
}

// Plays the plugin's position samples back a little late so movement is
// smooth between server ticks instead of stepping 20 times a second.
class PositionSmoother {
public:
    explicit PositionSmoother(double delay_ms = 70.0) : delay_ms_(delay_ms) {}

    void reset() { samples_.clear(); }

    void add(double time_ms, const double position[3]) {
        if (!samples_.empty()) {
            const Sample& last = samples_.back();
            const double dx = position[0] - last.position[0];
            const double dy = position[1] - last.position[1];
            const double dz = position[2] - last.position[2];
            // A teleport or respawn snaps instead of gliding across the map.
            if (dx * dx + dy * dy + dz * dz > 64.0) samples_.clear();
        }
        if (!samples_.empty() && time_ms <= samples_.back().time_ms) time_ms = samples_.back().time_ms + 0.001;
        samples_.push_back({time_ms, {position[0], position[1], position[2]}});
        while (samples_.size() > 32) samples_.pop_front();
    }

    bool sample(double now_ms, double out[3]) const {
        if (samples_.empty()) return false;
        const double target = now_ms - delay_ms_;
        if (samples_.size() == 1 || target <= samples_.front().time_ms) {
            for (int i = 0; i < 3; ++i) out[i] = samples_.front().position[i];
            return true;
        }
        for (size_t i = 1; i < samples_.size(); ++i) {
            const Sample& a = samples_[i - 1];
            const Sample& b = samples_[i];
            if (target <= b.time_ms) {
                const double t = (target - a.time_ms) / (b.time_ms - a.time_ms);
                for (int axis = 0; axis < 3; ++axis) {
                    out[axis] = a.position[axis] + (b.position[axis] - a.position[axis]) * t;
                }
                return true;
            }
        }
        // Past the newest sample: hold it rather than guess ahead.
        for (int i = 0; i < 3; ++i) out[i] = samples_.back().position[i];
        return true;
    }

private:
    struct Sample {
        double time_ms;
        double position[3];
    };
    double delay_ms_;
    std::deque<Sample> samples_;
};

} // namespace hytalevr::viewer
