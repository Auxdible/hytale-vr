// Tests for the platform-free parts of the VR runtime.
#include "../src/core/controls.h"
#include "../src/core/hytale_settings.h"
#include "../src/core/settings.h"
#include "../src/core/stereo_math.h"
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
    {
        // You look 90 degrees left of where Hytale's camera faces and push forward:
        // that's Hytale's left.
        float x = 0, y = 0;
        rotate_stick(0, 1, 1.5707963f, x, y);
        check(near(x, -1) && near(y, 0), "stick forward turns into the camera's left");
    }
    {
        std::string json = "{\"Gameplay\": {\"ViewBobbingEffect\": true, \"ViewBobbingIntensity\":0.75,\n"
                           "\"CameraShakeEffect\" : false, \"FirstPersonCameraShakeIntensity\": 1}, \"Other\": true}";
        const std::vector<std::string> changed = tune_hytale_settings(json);
        check(changed.size() == 3, "three comfort settings changed");
        check(json == "{\"Gameplay\": {\"ViewBobbingEffect\": false, \"ViewBobbingIntensity\":0,\n"
                      "\"CameraShakeEffect\" : false, \"FirstPersonCameraShakeIntensity\": 0}, \"Other\": true}",
              "comfort settings rewritten in place");
        check(tune_hytale_settings(json).empty(), "already tuned settings stay as they are");
    }
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

void test_stereo_math() {
    using namespace hvr;
    // Hytale's projection from the renderer log (near 0.05, far ~1000).
    Mat4 mono_p;
    mono_p.at(0, 0) = 0.47199f;
    mono_p.at(1, 1) = 0.8391f;
    mono_p.at(2, 2) = -1.0001f;
    mono_p.at(2, 3) = -0.10001f;
    mono_p.at(3, 2) = -1.0f;
    mono_p.at(3, 3) = 0.0f;
    Mat4 inverse_p;
    check(invert(mono_p, inverse_p), "projection inverts");
    const Mat4 round = mono_p * inverse_p;
    bool identity = true;
    for (int i = 0; i < 16; ++i) identity = identity && near(round.m[i], (i % 5 == 0) ? 1.0f : 0.0f, 1e-3f);
    check(identity, "inverse undoes projection");
    check(is_perspective(mono_p) && !is_perspective(Mat4{}), "perspective detection");

    const Mat4 headset = projection_from_tangents(-1.3f, 1.0f, -1.2f, 1.1f, 0.1f, 100.0f);
    const Mat4 eye_p = eye_projection(headset, mono_p);
    check(near(eye_p.at(2, 2), -1.0001f) && near(eye_p.at(2, 3), -0.10001f), "eye keeps Hytale's depth rows");
    check(near(eye_p.at(0, 0), headset.at(0, 0)) && near(eye_p.at(0, 2), headset.at(0, 2)), "eye keeps headset sides");

    float infos[4];
    proj_infos(mono_p, infos);
    check(near(infos[0], -4.23735f) && near(infos[1], -2.38351f, 1e-3f) && near(infos[2], 2.11867f) && near(infos[3], 1.19175f),
          "ProjInfos match the renderer log");

    // View-space corners from the log's SSAO pass: oversized triangle at far 1024.
    const float d = 1024.0f;
    Vec3 mono_corners[4] = {view_corner(mono_p, -1, 3, d), view_corner(mono_p, 3, -1, d), view_corner(mono_p, -1, -1, d),
                            view_corner(mono_p, 1, 1, d)};
    check(near(mono_corners[0].x, -2169.5f, 1.0f) && near(mono_corners[0].y, 3661.0f, 5.0f), "corner math matches log");
    const Mat4 view = rotation_y(0.4f);
    Vec3 eye_view_corners[4];
    CornerFit fit = eye_corners(mono_corners, mono_p, view, eye_p, view, eye_view_corners);
    check(fit.ok && !fit.world_space, "view-space corners recognized");
    float x = 0, y = 0, dist = 0;
    view_corner_ndc(eye_p, eye_view_corners[0], x, y, dist);
    check(near(x, -1) && near(y, 3) && near(dist, d), "eye corner keeps its NDC and distance");

    Vec3 world_corners[4];
    const Mat4 to_world = inverse_rigid(view);
    for (int i = 0; i < 4; ++i) world_corners[i] = transform_direction(to_world, mono_corners[i]);
    Vec3 eye_world_corners[4];
    fit = eye_corners(world_corners, mono_p, view, eye_p, view, eye_world_corners);
    check(fit.ok && fit.world_space, "world-space corners recognized");

    for (int delay = 0; delay <= 2; ++delay) {
        MouseFollower follow;
        float engine_yaw = 0.0f;
        const float true_gain = 0.004f;  // inverted and larger than the guess
        int in_flight[3] = {};
        float worst_late = 0;
        for (int frame = 0; frame < 300; ++frame) {
            int cx = 0, cy = 0;
            // Walking bob: a small sway the follower shouldn't chase into a wobble.
            const float bob = 0.003f * std::sin(static_cast<float>(frame) * 0.4f);
            follow.step(engine_yaw + bob, 0, 1.0f, 0, cx, cy);
            in_flight[delay] += cx;
            engine_yaw += static_cast<float>(in_flight[0]) * true_gain;
            for (int i = 0; i < 2; ++i) in_flight[i] = in_flight[i + 1];
            in_flight[2] = 0;
            if (frame > 200) worst_late = std::fmax(worst_late, std::fabs(MouseFollower::wrap(engine_yaw - 1.0f)));
        }
        check(worst_late < 0.02f, "mouse follower settles with unknown gain and sign, a late camera and bobbing");
    }
    {
        RoomWalker walker;
        const MoveKeys idle = walker.step({0.1f, 0, 0}, 0);
        const MoveKeys ahead = walker.step({0, 0, -0.5f}, 0);         // head went forward
        const MoveKeys turned = walker.step({-0.5f, 0, 0}, 1.5708f);  // facing -X, head went -X
        const MoveKeys right = walker.step({0.5f, 0, 0}, 0);
        const MoveKeys settled = walker.step({0.05f, 0, 0}, 0);
        check(!idle.forward && !idle.right && ahead.forward && !ahead.left && turned.forward && right.right &&
                  !settled.right && !walker.walking,
              "room walker heads toward the headset");
    }
    check(near(yaw_of({0, 0, -1}), 0) && near(yaw_of({-1, 0, 0}), 1.5708f) && near(pitch_of({0, 1, 0}), 1.5708f),
          "yaw and pitch conventions");
}

}  // namespace

int main() {
    test_stereo_math();
    test_screen_placement_and_hits();
    test_rigid_inverse();
    test_stick();
    test_settings_round_trip();
    if (g_failures == 0) std::printf("all core tests passed\n");
    return g_failures == 0 ? 0 : 1;
}
