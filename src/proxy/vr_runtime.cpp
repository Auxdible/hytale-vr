#include "vr_runtime.h"

#include <windows.h>

#include <openvr.h>

#include <atomic>
#include <bit>
#include <cmath>
#include <mutex>

#include "../core/controls.h"
#include "../core/stereo_math.h"
#include "gl_api.h"
#include "gl_hooks.h"
#include "input.h"
#include "log.h"
#include "sdl_hooks.h"
#include "stereo.h"
#include "vr_scene.h"

namespace hvr::session {
namespace {

std::atomic<State> g_state{State::Off};
std::atomic<bool> g_stop_requested{false};
std::atomic<bool> g_gl_ready{false};
std::mutex g_text_mutex;
std::string g_status = "VR is off";
std::string g_headset;
HANDLE g_worker = nullptr;

// Render thread
Scene g_scene;
EyeTarget g_eyes[2];
SceneView g_view;
bool g_screen_placed = false;
std::atomic<bool> g_recenter{false};
std::atomic<bool> g_menu_button{false};
::vr::TrackedDevicePose_t g_poses[::vr::k_unMaxTrackedDeviceCount];
Mat4 g_hmd;
bool g_hmd_valid = false;
LARGE_INTEGER g_last_frame{};
MotionAccumulator g_look;
float g_scroll = 0;
bool g_was_hovering = false;
bool g_pointer_was_active = false;
float g_last_warp_x = -1, g_last_warp_y = -1;
FrameContext g_context;  // from begin_frame, used at the end of the frame

// World view
bool g_world_frame = false;     // the headset is showing the world (not the big screen)
// Which of the last 32 frames drew the world. The headset shows the world once
// a third of them did and goes back to the big screen once none did, so
// frames that come and go while chunks load don't flicker.
uint32_t g_world_history = 0;
GLuint g_last_world_texture[2] = {};
Screen g_cinema_screen;
Screen g_hud_panel;
bool g_hud_placed = false;
bool g_world_aligned = false;   // yaw offset and neutral spot are set
float g_yaw_offset = 0;         // Hytale yaw = headset yaw + this
Vec3 g_neutral;                 // headset spot that maps to Hytale's camera
MouseFollower g_follower;
RoomWalker g_walker;
MoveKeys g_room_keys;
bool g_stick_moving = false;
// Room-scale: where the view sits relative to the character (horizontal,
// world units), and the world spot it's locked to while you walk in your room.
Vec3 g_body_offset;
Vec3 g_room_anchor;
bool g_room_locked = false;
bool g_world_latched = false;  // a world is loaded and playing (mouse look seen)
std::atomic<bool> g_force_world{false};
Vec3 g_jump_check;
bool g_have_jump_check = false;
float g_frame_dt = 0.011f;
int g_non_world_frames = 0;
bool g_snap_armed = true;
int g_world_frames = 0;

struct Actions {
    ::vr::VRActionSetHandle_t set = 0;
    ::vr::VRActionHandle_t hand[2] = {}, haptic[2] = {};
    ::vr::VRActionHandle_t move = 0, look = 0, primary = 0, secondary = 0, jump = 0, back = 0, inventory = 0,
                         vr_menu = 0, crouch = 0, sprint = 0, hotbar_next = 0, recenter = 0;
    bool ready = false;
} g_actions;

struct Pressed {
    bool down = false, pressed = false;
};

void set_status(const std::string& text) {
    std::lock_guard lock(g_text_mutex);
    g_status = text;
}

std::string utf8_path(const std::wstring& path) { return narrow(path); }

void load_actions() {
    ::vr::IVRInput* in = ::vr::VRInput();
    if (!in) return;
    const std::string manifest = utf8_path(data_dir() + L"\\actions.json");
    const ::vr::EVRInputError error = in->SetActionManifestPath(manifest.c_str());
    if (error != ::vr::VRInputError_None) {
        logf("SteamVR rejected the action manifest %s (%d)", manifest.c_str(), static_cast<int>(error));
        return;
    }
    auto action = [&](const char* name) {
        ::vr::VRActionHandle_t handle = ::vr::k_ulInvalidActionHandle;
        in->GetActionHandle(name, &handle);
        return handle;
    };
    in->GetActionSetHandle("/actions/main", &g_actions.set);
    g_actions.hand[0] = action("/actions/main/in/hand_left");
    g_actions.hand[1] = action("/actions/main/in/hand_right");
    g_actions.haptic[0] = action("/actions/main/out/haptic_left");
    g_actions.haptic[1] = action("/actions/main/out/haptic_right");
    g_actions.move = action("/actions/main/in/move");
    g_actions.look = action("/actions/main/in/look");
    g_actions.primary = action("/actions/main/in/primary");
    g_actions.secondary = action("/actions/main/in/secondary");
    g_actions.jump = action("/actions/main/in/jump");
    g_actions.back = action("/actions/main/in/back");
    g_actions.inventory = action("/actions/main/in/inventory");
    g_actions.vr_menu = action("/actions/main/in/vr_menu");
    g_actions.crouch = action("/actions/main/in/crouch");
    g_actions.sprint = action("/actions/main/in/sprint");
    g_actions.hotbar_next = action("/actions/main/in/hotbar_next");
    g_actions.recenter = action("/actions/main/in/recenter");
    g_actions.ready = true;
}

DWORD WINAPI start_worker(void*) {
    const std::wstring dll = data_dir() + L"\\openvr_api.dll";
    if (!LoadLibraryExW(dll.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH)) {
        set_status("openvr_api.dll is missing from the HytaleVR folder. Reinstall Hytale VR.");
        logf("Could not load %ls (%lu)", dll.c_str(), GetLastError());
        g_state = State::Failed;
        return 0;
    }
    if (!::vr::VR_IsRuntimeInstalled()) {
        set_status("SteamVR isn't installed. Install it from Steam, then try again.");
        g_state = State::Failed;
        return 0;
    }
    set_status("Starting SteamVR...");
    ::vr::EVRInitError error = ::vr::VRInitError_None;
    ::vr::VR_Init(&error, ::vr::VRApplication_Scene);
    if (error != ::vr::VRInitError_None) {
        const std::string text = ::vr::VR_GetVRInitErrorAsEnglishDescription(error);
        set_status("SteamVR didn't start: " + text);
        logf("VR_Init failed: %s", text.c_str());
        g_state = State::Failed;
        return 0;
    }
    if (g_stop_requested) {
        ::vr::VR_Shutdown();
        g_state = State::Off;
        set_status("VR is off");
        return 0;
    }
    load_actions();
    char model[128] = {};
    ::vr::VRSystem()->GetStringTrackedDeviceProperty(::vr::k_unTrackedDeviceIndex_Hmd, ::vr::Prop_ModelNumber_String, model,
                                                   sizeof(model));
    {
        std::lock_guard lock(g_text_mutex);
        g_headset = model;
    }
    logf("SteamVR started with %s", model);
    set_status("Hytale is on the big screen. Point at it to use the mouse.");
    g_screen_placed = false;
    g_state = State::Running;
    return 0;
}

// Render thread: tears the session down.
void shutdown_now() {
    input::release_all();
    stereo::disable();
    g_world_frame = false;
    g_world_history = 0;
    g_last_world_texture[0] = g_last_world_texture[1] = 0;
    g_world_aligned = false;
    g_hud_placed = false;
    for (EyeTarget& eye : g_eyes) eye.destroy();
    g_scene.destroy();
    g_gl_ready = false;
    g_actions = Actions{};
    ::vr::VR_Shutdown();
    set_vsync_override(false);
    g_state = State::Off;
    g_stop_requested = false;
    set_status("VR is off");
    logf("SteamVR session ended");
}

Pressed boolean(::vr::VRActionHandle_t action) {
    ::vr::InputDigitalActionData_t data{};
    Pressed result;
    if (action && ::vr::VRInput()->GetDigitalActionData(action, &data, sizeof(data), ::vr::k_ulInvalidInputValueHandle) ==
                      ::vr::VRInputError_None && data.bActive) {
        result.down = data.bState;
        result.pressed = data.bState && data.bChanged;
    }
    return result;
}

void stick(::vr::VRActionHandle_t action, float& x, float& y) {
    ::vr::InputAnalogActionData_t data{};
    x = y = 0;
    if (action && ::vr::VRInput()->GetAnalogActionData(action, &data, sizeof(data), ::vr::k_ulInvalidInputValueHandle) ==
                      ::vr::VRInputError_None && data.bActive) {
        x = data.x;
        y = data.y;
    }
}

bool hand_pose(int hand, Mat4& pose) {
    ::vr::InputPoseActionData_t data{};
    if (!g_actions.hand[hand] ||
        ::vr::VRInput()->GetPoseActionDataForNextFrame(g_actions.hand[hand], ::vr::TrackingUniverseStanding, &data,
                                                     sizeof(data), ::vr::k_ulInvalidInputValueHandle) != ::vr::VRInputError_None ||
        !data.bActive || !data.pose.bPoseIsValid)
        return false;
    pose = from_rows34(data.pose.mDeviceToAbsoluteTracking.m);
    return true;
}

Vec3 g_anchor_head{0, 1.6f, 0};
Vec3 g_anchor_forward{0, 0, -1};

void place_screen_now(const Settings& settings) {
    g_anchor_head = position_of(g_hmd);
    g_anchor_forward = transform_direction(g_hmd, {0, 0, -1});
    g_screen_placed = true;
    g_view.screen = place_screen(g_anchor_head, g_anchor_forward, settings.screen_distance, settings.screen_width,
                                 g_scene.screen_aspect(), settings.screen_height);
    g_cinema_screen = g_view.screen;
}

void handle_events() {
    ::vr::VREvent_t event{};
    while (::vr::VRSystem()->PollNextEvent(&event, sizeof(event))) {
        if (event.eventType == ::vr::VREvent_Quit) {
            logf("SteamVR is closing");
            ::vr::VRSystem()->AcknowledgeQuit_Exiting();
            g_stop_requested = true;
        }
    }
}

void controls(const FrameContext& context, float dt) {
    const Settings& s = *context.settings;
    for (HandView& hand : g_view.hands) hand.laser = hand.hovering = false;
    g_view.dot = false;
    if (!g_actions.ready) return;

    ::vr::VRActiveActionSet_t active{};
    active.ulActionSet = g_actions.set;
    ::vr::VRInput()->UpdateActionState(&active, sizeof(active), 1);

    for (int hand = 0; hand < 2; ++hand) g_view.hands[hand].valid = hand_pose(hand, g_view.hands[hand].pose);

    if (boolean(g_actions.vr_menu).pressed) g_menu_button = true;
    if (boolean(g_actions.recenter).pressed) g_recenter = true;

    // Laser pointer: drives the mouse whenever Hytale shows a cursor or our menu is open.
    const bool pointer_mode = context.menu_open || !context.game_relative;
    const int pointer_hand = s.left_handed ? 0 : 1;
    HandView& pointer = g_view.hands[pointer_hand];
    bool hovering = false;
    if (pointer.valid && pointer_mode) {
        const Vec3 origin = position_of(pointer.pose);
        const Vec3 direction = normalize(transform_direction(pointer.pose, {0, 0, -1}));
        const ScreenHit hit = hit_screen(g_view.screen, origin, direction);
        pointer.laser = true;
        pointer.laser_length = hit.hit ? hit.distance : 3.0f;
        if (hit.hit) {
            hovering = pointer.hovering = true;
            g_view.dot = true;
            g_view.dot_u = hit.u;
            g_view.dot_v = hit.v;
            const float x = hit.u * static_cast<float>(context.window_points_w);
            const float y = hit.v * static_cast<float>(context.window_points_h);
            if (s.controller_input && input::target_focused() &&
                (std::fabs(x - g_last_warp_x) > 0.5f || std::fabs(y - g_last_warp_y) > 0.5f)) {
                sdl::warp_mouse(x, y);
                g_last_warp_x = x;
                g_last_warp_y = y;
            }
        }
    }
    if (hovering && !g_was_hovering && g_actions.haptic[pointer_hand])
        ::vr::VRInput()->TriggerHapticVibrationAction(g_actions.haptic[pointer_hand], 0, 0.015f, 150.0f, 0.25f,
                                                    ::vr::k_ulInvalidInputValueHandle);
    g_was_hovering = hovering;
    g_pointer_was_active = pointer_mode;

    if (!s.controller_input) {
        input::release_all();
        return;
    }

    // Triggers are the mouse buttons: the laser hand's trigger is the left click.
    const Pressed right_trigger = boolean(g_actions.primary);
    const Pressed left_trigger = boolean(g_actions.secondary);
    input::mouse_button(1, s.left_handed ? left_trigger.down : right_trigger.down);
    input::mouse_button(3, s.left_handed ? right_trigger.down : left_trigger.down);

    const bool play = !context.menu_open;
    float mx, my;
    stick(g_actions.move, mx, my);
    MoveKeys move = play ? stick_to_keys(mx, my, s.deadzone) : MoveKeys{};
    g_stick_moving = move.forward || move.back || move.left || move.right;
    if (play && !g_stick_moving) move = g_room_keys;
    input::key(s.key_forward, move.forward);
    input::key(s.key_back, move.back);
    input::key(s.key_left, move.left);
    input::key(s.key_right, move.right);
    input::key(s.key_jump, play && boolean(g_actions.jump).down);
    input::key(s.key_sprint, play && boolean(g_actions.sprint).down);
    input::key(s.key_crouch, play && boolean(g_actions.crouch).down);
    input::key(s.key_inventory, play && boolean(g_actions.inventory).down);
    input::key(41 /* Escape */, play && boolean(g_actions.back).down);
    if (play && boolean(g_actions.hotbar_next).pressed) input::wheel(-1);

    float lx, ly;
    stick(g_actions.look, lx, ly);
    if (g_world_frame && !pointer_mode) {
        // In the world your head aims; the stick turns your body.
        const float x = stick_curve(lx, s.deadzone);
        const float pi = 3.14159265f;
        if (s.turn_mode == 1) {
            g_yaw_offset -= x * s.smooth_speed * pi / 180.0f * dt;
        } else if (std::fabs(lx) > 0.7f && g_snap_armed) {
            g_yaw_offset -= (lx > 0 ? 1.0f : -1.0f) * s.snap_degrees * pi / 180.0f;
            g_snap_armed = false;
        } else if (std::fabs(lx) < 0.35f) {
            g_snap_armed = true;
        }
        g_yaw_offset = MouseFollower::wrap(g_yaw_offset);
        g_scroll = 0;
    } else if (!pointer_mode) {
        int dx = 0, dy = 0;
        g_look.add(stick_curve(lx, s.deadzone) * s.look_speed * dt, -stick_curve(ly, s.deadzone) * s.look_speed * dt * 0.6f,
                   dx, dy);
        input::mouse_move(dx, dy);
        g_scroll = 0;
    } else {
        g_scroll += stick_curve(ly, s.deadzone) * 10.0f * dt;
        const int clicks = static_cast<int>(g_scroll);
        g_scroll -= static_cast<float>(clicks);
        input::wheel(clicks);
    }
}

}  // namespace

void request_start() {
    const State current = g_state;
    if (current == State::Starting || current == State::Running) return;
    g_stop_requested = false;
    g_state = State::Starting;
    set_status("Starting SteamVR...");
    if (g_worker) CloseHandle(g_worker);
    g_worker = CreateThread(nullptr, 0, start_worker, nullptr, 0, nullptr);
    if (!g_worker) {
        g_state = State::Failed;
        set_status("Couldn't start the VR thread.");
    }
}

void request_stop() {
    if (g_state == State::Failed) {
        g_state = State::Off;
        set_status("VR is off");
        return;
    }
    g_stop_requested = true;
}

State state() { return g_state; }

std::string status_text() {
    std::lock_guard lock(g_text_mutex);
    return g_status;
}

std::string headset_name() {
    std::lock_guard lock(g_text_mutex);
    return g_headset;
}

void recenter() { g_recenter = true; }
void force_world() { g_force_world = true; }
bool take_menu_button() { return g_menu_button.exchange(false); }

void begin_frame(const FrameContext& context) {
    g_context = context;
    if (g_state != State::Running) {
        stereo::disable();
        return;
    }
    if (g_stop_requested) {
        shutdown_now();
        return;
    }
    if (!g_gl_ready) {
        uint32_t width = 0, height = 0;
        ::vr::VRSystem()->GetRecommendedRenderTargetSize(&width, &height);
        bool ok = g_scene.init();
        for (EyeTarget& eye : g_eyes) ok = ok && eye.resize(static_cast<int>(width), static_cast<int>(height));
        if (!ok) {
            set_status("Couldn't set up VR rendering. See HytaleVR\\logs\\hytalevr.log.");
            shutdown_now();
            g_state = State::Failed;
            return;
        }
        logf("Eye targets %ux%u", width, height);
        g_gl_ready = true;
        QueryPerformanceCounter(&g_last_frame);
    }
    set_vsync_override(context.settings->vsync_off_in_vr);
}

namespace {

Mat4 eye_view(int eye) {
    const ::vr::HmdMatrix34_t e = ::vr::VRSystem()->GetEyeToHeadTransform(eye == 0 ? ::vr::Eye_Left : ::vr::Eye_Right);
    return inverse_rigid(g_hmd * from_rows34(e.m));
}

Mat4 eye_projection_near(int eye) {
    return from_rows44(::vr::VRSystem()->GetProjectionMatrix(eye == 0 ? ::vr::Eye_Left : ::vr::Eye_Right, 0.05f, 200.0f).m);
}

void submit(int eye, GLuint texture) {
    ::vr::Texture_t t{reinterpret_cast<void*>(static_cast<uintptr_t>(texture)), ::vr::TextureType_OpenGL, ::vr::ColorSpace_Gamma};
    ::vr::VRCompositor()->Submit(eye == 0 ? ::vr::Eye_Left : ::vr::Eye_Right, &t);
}

// The HUD floats ahead of you and catches up when you look well away.
void place_hud(const Settings& s, bool force) {
    const Vec3 head = position_of(g_hmd);
    const Vec3 forward = transform_direction(g_hmd, {0, 0, -1});
    const float head_yaw = yaw_of(forward);
    const float off = std::fabs(MouseFollower::wrap(head_yaw - g_hud_panel.yaw));
    if (force || !g_hud_placed || off > 0.6f) {
        g_hud_panel = place_screen(head, forward, s.hud_distance, s.hud_width, g_context.window_points_h > 0
                                       ? static_cast<float>(g_context.window_points_w) / g_context.window_points_h
                                       : 16.0f / 9.0f,
                                   -0.1f);
        g_hud_placed = true;
    } else {
        // Keep size and distance live from the menu.
        const Vec3 flat = normalize({-std::sin(g_hud_panel.yaw), 0, -std::cos(g_hud_panel.yaw)});
        const float aspect = g_hud_panel.width / g_hud_panel.height;
        g_hud_panel.width = s.hud_width;
        g_hud_panel.height = s.hud_width / aspect;
        g_hud_panel.center = {head.x + flat.x * s.hud_distance, g_hud_panel.center.y, head.z + flat.z * s.hud_distance};
    }
}

// After the poses for the next frame arrive: where each eye goes in Hytale.
void prepare_world(const Settings& s, bool showed_world) {
    // 3D only once you're playing in a world: Hytale's main menu draws a 3D
    // scene too, from a fixed camera, and that belongs on the big screen.
    const bool was_latched = g_world_latched;
    if ((g_context.game_relative && !g_context.menu_open) || g_force_world.exchange(false)) g_world_latched = true;
    g_non_world_frames = showed_world || !stereo::wanted() ? 0 : g_non_world_frames + 1;
    // A loading screen with the cursor back: the world is gone. While mouse look
    // stays on (waiting for chunks) the world is still coming, so stay put.
    if (g_non_world_frames > 45 && !g_context.game_relative) g_world_latched = false;
    {
        // Leaving a world for the main menu moves the camera far away at once.
        Mat4 v;
        Vec3 f, position;
        if (stereo::wanted() && stereo::engine_camera(v, f, position)) {
            if (g_have_jump_check && !g_context.game_relative && length(position - g_jump_check) > 40.0f)
                g_world_latched = false;
            g_jump_check = position;
            g_have_jump_check = true;
        } else {
            g_have_jump_check = false;
        }
    }
    if (was_latched != g_world_latched)
        logf(g_world_latched ? "Playing in a world: 3D on" : "No world showing for a while: back to the big screen");
    if (!s.world_view || !g_hmd_valid || !g_world_latched) {
        stereo::disable();
        g_world_aligned = false;
        g_room_keys = {};
        g_room_locked = false;
        return;
    }
    const Vec3 forward = transform_direction(g_hmd, {0, 0, -1});
    Mat4 engine_view;
    Vec3 engine_forward, engine_position;
    const bool have_engine = stereo::engine_camera(engine_view, engine_forward, engine_position);
    if (!g_world_aligned || g_recenter.exchange(false)) {
        g_neutral = position_of(g_hmd);
        g_body_offset = {};
        g_room_locked = false;
        g_yaw_offset = have_engine ? MouseFollower::wrap(yaw_of(engine_forward) - yaw_of(forward)) : 0.0f;
        g_world_aligned = true;
        g_hud_placed = false;
        g_follower = MouseFollower{};
    }

    // Steer Hytale's camera after the head, so aiming, movement and what it
    // loads all follow where you look.
    if (have_engine && g_context.game_relative && !g_context.menu_open && s.controller_input) {
        const float target_yaw = MouseFollower::wrap(yaw_of(forward) + g_yaw_offset);
        int dx = 0, dy = 0;
        g_follower.step(yaw_of(engine_forward), pitch_of(engine_forward), target_yaw, pitch_of(forward), dx, dy);
        sdl::inject_mouse_motion(dx, dy);
    } else {
        g_follower.have_last = false;
    }

    // Room-scale: the view stays fixed in the world while you stand or walk
    // in your room, and the character steps after your head. Stick movement,
    // falling and the like carry the view along with the character.
    const bool walk = s.room_walk && s.controller_input && have_engine && showed_world && g_context.game_relative &&
                      !g_context.menu_open;
    const bool lock = walk && !g_stick_moving;
    if (lock && !g_room_locked) g_room_anchor = engine_position + g_body_offset;
    if (!lock && g_room_locked) g_body_offset = g_room_anchor - engine_position;
    g_room_locked = lock;
    if (lock) {
        g_body_offset = g_room_anchor - engine_position;
        g_body_offset.y = 0;
        // Pushed or stuck far from where you stand: catch the view up.
        if (length(g_body_offset) > 2.0f) {
            g_room_anchor = engine_position;
            g_body_offset = {};
            g_neutral.x = position_of(g_hmd).x;
            g_neutral.z = position_of(g_hmd).z;
        }
    } else if (!walk) {
        // Ease back over the character so the offset doesn't linger.
        g_body_offset = g_body_offset * (1.0f - std::fmin(1.0f, 2.0f * g_frame_dt));
    }
    if (walk) {
        Vec3 offset = transform_direction(rotation_y(g_yaw_offset), position_of(g_hmd) - g_neutral) * s.world_scale;
        offset = offset + g_body_offset;
        g_room_keys = g_walker.step(offset, yaw_of(engine_forward));
    } else {
        g_room_keys = {};
        g_walker.walking = false;
    }

    // Height follows your head 1:1 from where it was when 3D started or you
    // last recentered, kept within half a meter so it can't leave the body.
    {
        const float head_y = position_of(g_hmd).y;
        const float limit = 0.5f;
        if (head_y - g_neutral.y > limit) g_neutral.y = head_y - limit;
        if (head_y - g_neutral.y < -limit) g_neutral.y = head_y + limit;
    }

    stereo::EyeSetup setup;
    for (int e = 0; e < 2; ++e) {
        setup.headset_projection[e] = eye_projection_near(e);
        setup.eye_to_head[e] =
            from_rows34(::vr::VRSystem()->GetEyeToHeadTransform(e == 0 ? ::vr::Eye_Left : ::vr::Eye_Right).m);
    }
    setup.head = rotation_y(g_yaw_offset) * translation(Vec3{} - g_neutral) * g_hmd;
    setup.world_scale = s.world_scale;
    setup.anchor = g_room_locked ? g_room_anchor : g_body_offset;
    setup.world_locked = g_room_locked;
    setup.no_occlusion = s.no_occlusion;
    stereo::set_eyes(setup);
}

}  // namespace

void end_frame(int pixels_w, int pixels_h, const Settings& settings) {
    const stereo::FrameResult world = stereo::end_frame(pixels_w, pixels_h);
    if (g_state != State::Running || !g_gl_ready) return;

    // 1. Show the frame that was just drawn, with the poses it was drawn for.
    const bool drawn = world.stereo && g_hmd_valid;
    g_world_history = (g_world_history << 1) | (drawn ? 1u : 0u);
    const int recent = std::popcount(g_world_history);
    const bool was_world = g_world_frame;
    if (g_world_frame && (recent == 0 || !stereo::wanted())) g_world_frame = false;
    if (g_world_frame && !drawn && g_last_world_texture[0]) {
        // A frame without the world in the middle of playing (chunks loading):
        // keep showing the last world frame rather than flicking to the screen.
        if (g_hmd_valid)
            for (int eye = 0; eye < 2; ++eye) submit(eye, g_last_world_texture[eye]);
    } else if (drawn && (g_world_frame || recent >= 11)) {
        g_world_frame = true;
        if (!was_world) {
            logf("Showing Hytale's world in 3D");
            set_status("You're in the world. Look around; the stick turns you.");
        }
        ++g_world_frames;
        place_hud(settings, false);
        g_view.screen = g_hud_panel;
        g_view.floor = false;
        g_view.eye = position_of(g_hmd);
        for (int eye = 0; eye < 2; ++eye) {
            g_scene.render_overlay(world.eye_fbo[eye], world.width, world.height, eye_projection_near(eye), eye_view(eye),
                                   g_view, world.hud_texture);
            submit(eye, world.eye_texture[eye]);
            g_last_world_texture[eye] = world.eye_texture[eye];
        }
    } else {
        if (was_world) {
            logf("Back to the big screen");
            set_status("Hytale is on the big screen. Point at it to use the mouse.");
        }
        g_world_frame = false;
        g_scene.capture_window(pixels_w, pixels_h, stereo::virtual_backbuffer());
        if (g_hmd_valid) {
            g_cinema_screen = place_screen(g_anchor_head, g_anchor_forward, settings.screen_distance, settings.screen_width,
                                           g_scene.screen_aspect(), settings.screen_height);
            g_view.screen = g_cinema_screen;
            g_view.floor = settings.show_floor;
            g_view.eye = position_of(g_hmd);
            for (int eye = 0; eye < 2; ++eye) {
                g_scene.render(g_eyes[eye], eye_projection_near(eye), eye_view(eye), g_view);
                submit(eye, g_eyes[eye].texture());
            }
        }
    }

    // 2. Wait for the next frame's poses, then read the controllers and get
    // the next frame's eyes ready before Hytale starts drawing it.
    handle_events();
    if (g_stop_requested) {
        shutdown_now();
        return;
    }
    ::vr::VRCompositor()->WaitGetPoses(g_poses, ::vr::k_unMaxTrackedDeviceCount, nullptr, 0);
    const ::vr::TrackedDevicePose_t& hmd = g_poses[::vr::k_unTrackedDeviceIndex_Hmd];
    g_hmd_valid = hmd.bPoseIsValid;
    if (g_hmd_valid) g_hmd = from_rows34(hmd.mDeviceToAbsoluteTracking.m);

    LARGE_INTEGER now, frequency;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    float dt = static_cast<float>(now.QuadPart - g_last_frame.QuadPart) / static_cast<float>(frequency.QuadPart);
    g_last_frame = now;
    if (dt > 0.1f) dt = 0.1f;
    g_frame_dt = dt;

    if (g_hmd_valid && !g_world_frame && (!g_screen_placed || g_recenter.exchange(false))) place_screen_now(settings);
    // The laser aims at whatever is showing: the HUD panel or the big screen.
    g_view.screen = g_world_frame ? g_hud_panel : g_cinema_screen;
    controls(g_context, dt);
    prepare_world(settings, world.stereo);
}

}  // namespace hvr::session
