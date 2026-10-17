// Platform-free math for rendering Hytale per eye: building each eye's
// matrices from Hytale's own camera block and the headset's eye frusta.
#pragma once

#include <cmath>

#include "controls.h"
#include "vrmath.h"

namespace hvr {

// General 4x4 inverse (Hytale's projections aren't rigid).
inline bool invert(const Mat4& a, Mat4& out) {
    const float* m = a.m;
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    const float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (std::fabs(det) < 1e-20f) return false;
    for (int i = 0; i < 16; ++i) out.m[i] = inv[i] / det;
    return true;
}

inline Mat4 inverse(const Mat4& a) {
    Mat4 r;
    invert(a, r);
    return r;
}

// True for a perspective matrix (as opposed to a 2D/fullscreen ortho one).
inline bool is_perspective(const Mat4& a) { return std::fabs(a.at(3, 2)) > 1e-4f || std::fabs(a.at(3, 0)) > 1e-4f || std::fabs(a.at(3, 1)) > 1e-4f; }

// An eye's projection: the headset's frustum sides with Hytale's depth rows,
// so depth stays exactly what Hytale's passes expect.
inline Mat4 eye_projection(const Mat4& headset, const Mat4& hytale) {
    Mat4 r = headset;
    for (int column = 0; column < 4; ++column) {
        r.at(2, column) = hytale.at(2, column);
        r.at(3, column) = hytale.at(3, column);
    }
    return r;
}

// Builds a GL projection from tangents of the frustum sides.
inline Mat4 projection_from_tangents(float left, float right, float bottom, float top, float near_z, float far_z) {
    Mat4 r;
    r.at(0, 0) = 2.0f / (right - left);
    r.at(0, 2) = (right + left) / (right - left);
    r.at(1, 1) = 2.0f / (top - bottom);
    r.at(1, 2) = (top + bottom) / (top - bottom);
    r.at(2, 2) = -(far_z + near_z) / (far_z - near_z);
    r.at(2, 3) = -2.0f * far_z * near_z / (far_z - near_z);
    r.at(3, 2) = -1.0f;
    r.at(3, 3) = 0.0f;
    return r;
}

// Hytale's ProjInfos: view-space xy = z * (uv * xy + zw), uv in 0..1.
inline void proj_infos(const Mat4& p, float out[4]) {
    out[0] = -2.0f / p.at(0, 0);
    out[1] = -2.0f / p.at(1, 1);
    out[2] = (1.0f - p.at(0, 2)) / p.at(0, 0);
    out[3] = (1.0f - p.at(1, 2)) / p.at(1, 1);
}

// Fullscreen passes get frustum rays as four "far corners", either in view
// space or in camera-relative world space. Each corner sits at some NDC xy
// and some distance; the eye's corner keeps both and changes the frustum.
struct CornerFit {
    bool ok = false;
    bool world_space = false;
};

inline float ndc_error(float value) {
    // Corners land on -1, 1 or 3 (oversized fullscreen triangles).
    const float candidates[3] = {-1.0f, 1.0f, 3.0f};
    float best = 1e9f;
    for (float c : candidates) best = std::fmin(best, std::fabs(value - c));
    return best;
}

inline bool view_corner_ndc(const Mat4& p, Vec3 c, float& x, float& y, float& distance) {
    if (c.z >= -1e-3f) return false;
    distance = -c.z;
    x = p.at(0, 0) * c.x / distance - p.at(0, 2);
    y = p.at(1, 1) * c.y / distance - p.at(1, 2);
    return true;
}

inline Vec3 view_corner(const Mat4& p, float x, float y, float distance) {
    return {distance * (x + p.at(0, 2)) / p.at(0, 0), distance * (y + p.at(1, 2)) / p.at(1, 1), -distance};
}

// Rewrites mono corners for an eye. view_* are rotation-only view matrices.
inline CornerFit eye_corners(const Vec3 mono[4], const Mat4& mono_p, const Mat4& mono_view, const Mat4& eye_p,
                             const Mat4& eye_view, Vec3 out[4]) {
    CornerFit fit;
    float err_view = 0, err_world = 0;
    float vx[4], vy[4], vd[4], wx[4], wy[4], wd[4];
    bool view_ok = true, world_ok = true;
    for (int i = 0; i < 4; ++i) {
        view_ok = view_ok && view_corner_ndc(mono_p, mono[i], vx[i], vy[i], vd[i]);
        if (view_ok) err_view += ndc_error(vx[i]) + ndc_error(vy[i]);
        const Vec3 as_view = transform_direction(mono_view, mono[i]);
        world_ok = world_ok && view_corner_ndc(mono_p, as_view, wx[i], wy[i], wd[i]);
        if (world_ok) err_world += ndc_error(wx[i]) + ndc_error(wy[i]);
    }
    if (!view_ok && !world_ok) return fit;
    fit.world_space = !view_ok || (world_ok && err_world < err_view);
    const float error = fit.world_space ? err_world : err_view;
    if (error > 0.2f) return fit;  // not a frustum we recognize; leave it
    const Mat4 eye_view_inverse = inverse_rigid(eye_view);
    for (int i = 0; i < 4; ++i) {
        if (fit.world_space) {
            out[i] = transform_direction(eye_view_inverse, view_corner(eye_p, wx[i], wy[i], wd[i]));
        } else {
            out[i] = view_corner(eye_p, vx[i], vy[i], vd[i]);
        }
    }
    fit.ok = true;
    return fit;
}

// Drives Hytale's camera toward the headset's direction with mouse motion,
// learning how far one mouse count turns the camera.
struct MouseFollower {
    float gain_yaw = -0.0025f;    // radians per count, sign included (mouse right turns right)
    float gain_pitch = -0.0025f;  // radians per count (mouse down looks down)
    float last_yaw = 0, last_pitch = 0, last_error_yaw = 0;
    bool have_last = false;
    // Learning: what the camera did over a stretch of frames against what was
    // sent, so the camera answering a frame or two late, or bobbing while you
    // walk, can't teach it the wrong gain (or sign).
    float moved_yaw = 0, moved_pitch = 0;
    int counts_x = 0, counts_y = 0, window = 0;

    static float wrap(float a) {
        const float pi = 3.14159265f;
        while (a > pi) a -= 2 * pi;
        while (a < -pi) a += 2 * pi;
        return a;
    }

    static void learn(float& gain, float moved, int counts) {
        if (std::abs(counts) < 60) return;
        const float observed = moved / static_cast<float>(counts);
        if (!std::isfinite(observed) || std::fabs(observed) < 2e-5f || std::fabs(observed) > 0.05f) return;
        // Opposite sign: the camera clearly went the other way; take it.
        if ((observed > 0) != (gain > 0)) gain = observed;
        else gain += (observed - gain) * 0.4f;
    }

    // Engine and target angles in radians. Returns counts to send now.
    void step(float engine_yaw, float engine_pitch, float target_yaw, float target_pitch, int& out_x, int& out_y) {
        if (have_last) {
            moved_yaw += wrap(engine_yaw - last_yaw);
            moved_pitch += engine_pitch - last_pitch;
            if (++window >= 10) {
                learn(gain_yaw, moved_yaw, counts_x);
                learn(gain_pitch, moved_pitch, counts_y);
                moved_yaw = moved_pitch = 0;
                counts_x = counts_y = window = 0;
            }
        }
        last_yaw = engine_yaw;
        last_pitch = engine_pitch;
        have_last = true;
        float yaw_error = wrap(target_yaw - engine_yaw);
        float pitch_error = target_pitch - engine_pitch;
        // Near half a turn away, keep turning the way we were going instead
        // of flipping back and forth across the far side.
        const float pi = 3.14159265f;
        if (std::fabs(yaw_error) > 2.6f && last_error_yaw * yaw_error < 0)
            yaw_error += yaw_error > 0 ? -2 * pi : 2 * pi;
        last_error_yaw = yaw_error;
        // Tiny errors are left alone, so the character doesn't jitter.
        const float deadband = 0.004f;  // about a quarter of a degree
        if (std::fabs(yaw_error) < deadband) yaw_error = 0;
        if (std::fabs(pitch_error) < deadband) pitch_error = 0;
        // Close part of the gap per frame. The camera answers a frame or more
        // late, and closing too much at once overshoots and sways.
        float x = 0.3f * yaw_error / gain_yaw;
        float y = 0.3f * pitch_error / gain_pitch;
        x = std::fmax(-400.0f, std::fmin(400.0f, x));
        y = std::fmax(-400.0f, std::fmin(400.0f, y));
        out_x = static_cast<int>(std::lround(x));
        out_y = static_cast<int>(std::lround(y));
        counts_x += out_x;
        counts_y += out_y;
    }
};

// Room-scale walking: when your head moves away from where your character
// stands, hold movement keys toward it until the character catches up.
struct RoomWalker {
    float start = 0.22f;  // world units of drift before walking
    float stop = 0.07f;   // close enough
    bool walking = false;

    // offset: head minus character, horizontal, in world units.
    // body_yaw: which way the character faces.
    MoveKeys step(Vec3 offset, float body_yaw) {
        offset.y = 0;
        const float distance = length(offset);
        if (!walking && distance > start) walking = true;
        if (walking && distance < stop) walking = false;
        MoveKeys keys;
        if (!walking) return keys;
        const Vec3 forward{-std::sin(body_yaw), 0, -std::cos(body_yaw)};
        const Vec3 right{std::cos(body_yaw), 0, -std::sin(body_yaw)};
        const float f = dot(offset, forward), r = dot(offset, right);
        const float threshold = 0.38f * distance;
        keys.forward = f > threshold;
        keys.back = f < -threshold;
        keys.right = r > threshold;
        keys.left = r < -threshold;
        return keys;
    }
};

// Yaw (around +Y, 0 = looking down -Z) and pitch of a forward vector.
inline float yaw_of(Vec3 forward) { return std::atan2(-forward.x, -forward.z); }
inline float pitch_of(Vec3 forward) {
    const float l = length(forward);
    return l > 1e-6f ? std::asin(std::fmax(-1.0f, std::fmin(1.0f, forward.y / l))) : 0.0f;
}

}  // namespace hvr
