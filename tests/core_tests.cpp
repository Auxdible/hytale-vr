// Tests for the platform-free parts of the VR runtime.
#include "../src/core/controls.h"
#include "../src/core/settings.h"
#include "../src/core/vrmath.h"

#include <cmath>
#include <cstdio>

namespace {

int g_failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::printf("FAIL: %s\n", what);
        ++g_failures;
    }
}

bool near(float a, float b, float tolerance = 1e-3f) { return std::fabs(a - b) <= tolerance; }

void test_screen_placement_and_hits() {
    using namespace hvr;
    const Screen s = place_screen({0, 1.7f, 0}, {0, -0.3f, -1}, 2.0f, 3.2f, 16.0f / 9.0f, 0.0f);
    check(near(s.center.x, 0) && near(s.center.y, 1.7f) && near(s.center.z, -2.0f), "screen ahead at eye height");
    check(near(s.height, 1.8f), "screen height follows aspect");

    ScreenHit center = hit_screen(s, {0, 1.7f, 0}, {0, 0, -1});
    check(center.hit && near(center.u, 0.5f) && near(center.v, 0.5f) && near(center.distance, 2.0f), "center hit");

    ScreenHit top_right = hit_screen(s, {1.5f, 1.7f + 0.85f, 0}, {0, 0, -1});
    check(top_right.hit && top_right.u > 0.95f && top_right.v < 0.05f, "top right hit is u=1 v=0");

    check(!hit_screen(s, {0, 1.7f, 0}, {0, 0, 1}).hit, "ray pointing away misses");
    check(!hit_screen(s, {5, 1.7f, 0}, {0, 0, -1}).hit, "ray beside the screen misses");

    // Turned to face +X: the screen sits at +X and its right edge is toward +Z.
    const Screen turned = place_screen({0, 1.7f, 0}, {1, 0, 0}, 2.0f, 3.2f, 16.0f / 9.0f, 0.0f);
    check(near(turned.center.x, 2.0f) && near(turned.center.z, 0), "turned screen placed along +X");
    ScreenHit right = hit_screen(turned, {0, 1.7f, 1.0f}, {1, 0, 0});
    check(right.hit && right.u > 0.5f, "turned screen keeps right on the user's right");
}

void test_rigid_inverse() {
    using namespace hvr;
    const Mat4 pose = translation({1, 2, 3}) * rotation_y(0.7f);
    const Mat4 identity = inverse_rigid(pose) * pose;
    bool ok = true;
    for (int i = 0; i < 16; ++i) ok = ok && near(identity.m[i], (i % 5 == 0) ? 1.0f : 0.0f);
    check(ok, "rigid inverse undoes the pose");
}

void test_stick() {
    using namespace hvr;
    MoveKeys idle = stick_to_keys(0.05f, 0.1f, 0.18f);
    check(!idle.forward && !idle.back && !idle.left && !idle.right, "deadzone holds nothing");
    MoveKeys forward = stick_to_keys(0.1f, 0.9f, 0.18f);
    check(forward.forward && !forward.left && !forward.right, "forward is W only");
    MoveKeys diagonal = stick_to_keys(0.7f, 0.7f, 0.18f);
    check(diagonal.forward && diagonal.right, "diagonal holds two keys");
    check(stick_curve(0.1f, 0.18f) == 0.0f && near(stick_curve(1.0f, 0.18f), 1.0f), "stick curve ends");
    check(stick_curve(-1.0f, 0.18f) < 0, "stick curve keeps sign");

    MotionAccumulator motion;
    int x = 0, y = 0, total = 0;
    for (int i = 0; i < 10; ++i) {
        motion.add(0.3f, 0, x, y);
        total += x;
    }
    check(total == 3, "slow motion accumulates whole pixels");
}

void test_settings_round_trip() {
    using namespace hvr;
    Settings s;
    s.vr_enabled = true;
    s.screen_distance = 4.5f;
    s.key_jump = scancode::E;
    Settings back = parse_settings(format_settings(s));
    check(back.vr_enabled && near(back.screen_distance, 4.5f) && back.key_jump == scancode::E, "settings round trip");

    Settings clamped = parse_settings("screen_distance = 99\nmenu_key = -4\n# comment\nnonsense\nunknown = 3\n");
    check(near(clamped.screen_distance, 8.0f), "distance clamped");
    check(clamped.menu_key == scancode::F6, "bad key falls back to default");
}

}  // namespace

int main() {
    test_screen_placement_and_hits();
    test_rigid_inverse();
    test_stick();
    test_settings_round_trip();
    if (g_failures == 0) std::printf("all core tests passed\n");
    return g_failures == 0 ? 0 : 1;
}
