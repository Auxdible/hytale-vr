// Small, platform-free math used by the VR runtime and its tests.
#pragma once

#include <cmath>

namespace hvr {

struct Vec3 {
    float x = 0, y = 0, z = 0;
};

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(Vec3 a) { return std::sqrt(dot(a, a)); }
inline Vec3 normalize(Vec3 a) {
    const float l = length(a);
    return l > 1e-6f ? a * (1.0f / l) : Vec3{0, 0, -1};
}

// Column-major 4x4, as OpenGL expects: m[column * 4 + row].
struct Mat4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    float& at(int row, int column) { return m[column * 4 + row]; }
    float at(int row, int column) const { return m[column * 4 + row]; }
};

inline Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column) {
            float sum = 0;
            for (int k = 0; k < 4; ++k) sum += a.at(row, k) * b.at(k, column);
            r.at(row, column) = sum;
        }
    return r;
}

inline Vec3 transform_point(const Mat4& a, Vec3 p) {
    return {a.at(0, 0) * p.x + a.at(0, 1) * p.y + a.at(0, 2) * p.z + a.at(0, 3),
            a.at(1, 0) * p.x + a.at(1, 1) * p.y + a.at(1, 2) * p.z + a.at(1, 3),
            a.at(2, 0) * p.x + a.at(2, 1) * p.y + a.at(2, 2) * p.z + a.at(2, 3)};
}

inline Vec3 transform_direction(const Mat4& a, Vec3 d) {
    return {a.at(0, 0) * d.x + a.at(0, 1) * d.y + a.at(0, 2) * d.z,
            a.at(1, 0) * d.x + a.at(1, 1) * d.y + a.at(1, 2) * d.z,
            a.at(2, 0) * d.x + a.at(2, 1) * d.y + a.at(2, 2) * d.z};
}

// OpenVR's row-major 3x4 pose.
inline Mat4 from_rows34(const float rows[3][4]) {
    Mat4 r;
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 4; ++column) r.at(row, column) = rows[row][column];
    return r;
}

// OpenVR's row-major 4x4 projection.
inline Mat4 from_rows44(const float rows[4][4]) {
    Mat4 r;
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column) r.at(row, column) = rows[row][column];
    return r;
}

// Inverse of a rotation plus translation.
inline Mat4 inverse_rigid(const Mat4& a) {
    Mat4 r;
    for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 3; ++column) r.at(row, column) = a.at(column, row);
    const Vec3 t{a.at(0, 3), a.at(1, 3), a.at(2, 3)};
    for (int row = 0; row < 3; ++row)
        r.at(row, 3) = -(r.at(row, 0) * t.x + r.at(row, 1) * t.y + r.at(row, 2) * t.z);
    return r;
}

inline Mat4 translation(Vec3 t) {
    Mat4 r;
    r.at(0, 3) = t.x;
    r.at(1, 3) = t.y;
    r.at(2, 3) = t.z;
    return r;
}

inline Mat4 rotation_y(float radians) {
    Mat4 r;
    const float c = std::cos(radians), s = std::sin(radians);
    r.at(0, 0) = c;
    r.at(0, 2) = s;
    r.at(2, 0) = -s;
    r.at(2, 2) = c;
    return r;
}

inline Mat4 rotation_x(float radians) {
    Mat4 r;
    const float c = std::cos(radians), s = std::sin(radians);
    r.at(1, 1) = c;
    r.at(1, 2) = -s;
    r.at(2, 1) = s;
    r.at(2, 2) = c;
    return r;
}

inline Mat4 scale(Vec3 s) {
    Mat4 r;
    r.at(0, 0) = s.x;
    r.at(1, 1) = s.y;
    r.at(2, 2) = s.z;
    return r;
}

inline Vec3 position_of(const Mat4& a) { return {a.at(0, 3), a.at(1, 3), a.at(2, 3)}; }

// The virtual screen: a flat rectangle facing +Z in its own frame, standing
// upright in the room. yaw turns it around the vertical axis.
struct Screen {
    Vec3 center{0, 1.6f, -2.5f};
    float yaw = 0;
    float width = 3.2f;
    float height = 1.8f;

    Mat4 model() const { return translation(center) * rotation_y(yaw) * scale({width, height, 1}); }
};

// Places the screen straight ahead of the head at eye height, ignoring head
// pitch and roll so it never tilts.
inline Screen place_screen(Vec3 head, Vec3 head_forward, float distance, float width, float aspect,
                           float height_offset) {
    Vec3 flat{head_forward.x, 0, head_forward.z};
    if (length(flat) < 1e-3f) flat = {0, 0, -1};
    flat = normalize(flat);
    Screen s;
    s.yaw = std::atan2(-flat.x, -flat.z);
    s.width = width;
    s.height = width / (aspect > 0.1f ? aspect : 16.0f / 9.0f);
    s.center = head + flat * distance;
    s.center.y = head.y + height_offset;
    return s;
}

struct ScreenHit {
    bool hit = false;
    float u = 0, v = 0;  // 0..1, v = 0 at the top edge
    float distance = 0;
};

// Intersects a ray with the screen rectangle.
inline ScreenHit hit_screen(const Screen& s, Vec3 origin, Vec3 direction) {
    ScreenHit result;
    const Mat4 to_screen = inverse_rigid(translation(s.center) * rotation_y(s.yaw));
    const Vec3 o = transform_point(to_screen, origin);
    const Vec3 d = transform_direction(to_screen, direction);
    if (std::fabs(d.z) < 1e-6f) return result;
    const float t = -o.z / d.z;
    if (t <= 0) return result;
    const float x = o.x + d.x * t, y = o.y + d.y * t;
    const float u = x / s.width + 0.5f, v = 0.5f - y / s.height;
    if (u < 0 || u > 1 || v < 0 || v > 1) return result;
    result.hit = true;
    result.u = u;
    result.v = v;
    result.distance = t;
    return result;
}

}  // namespace hvr
