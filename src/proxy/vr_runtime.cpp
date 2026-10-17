#include "vr_runtime.h"

#include <windows.h>

#include <openvr.h>

#include <atomic>
#include <cmath>
#include <mutex>

#include "../core/controls.h"
#include "gl_api.h"
#include "gl_hooks.h"
#include "input.h"
#include "log.h"
#include "sdl_hooks.h"
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
    const MoveKeys move = play ? stick_to_keys(mx, my, s.deadzone) : MoveKeys{};
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
    if (!pointer_mode) {
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
bool take_menu_button() { return g_menu_button.exchange(false); }

void begin_frame(const FrameContext& context) {
    if (g_state != State::Running) return;
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

    controls(context, dt);
    if (g_hmd_valid && (!g_screen_placed || g_recenter.exchange(false))) place_screen_now(*context.settings);
}

void end_frame(int pixels_w, int pixels_h, const Settings& settings) {
    if (g_state != State::Running || !g_gl_ready) return;
    g_scene.capture_window(pixels_w, pixels_h);
    if (!g_hmd_valid) return;

    // Screen settings from the menu apply live around where it was placed.
    g_view.screen = place_screen(g_anchor_head, g_anchor_forward, settings.screen_distance, settings.screen_width,
                                 g_scene.screen_aspect(), settings.screen_height);
    g_view.floor = settings.show_floor;
    g_view.eye = position_of(g_hmd);

    for (int eye = 0; eye < 2; ++eye) {
        const ::vr::EVREye which = eye == 0 ? ::vr::Eye_Left : ::vr::Eye_Right;
        const ::vr::HmdMatrix44_t p = ::vr::VRSystem()->GetProjectionMatrix(which, 0.05f, 200.0f);
        const ::vr::HmdMatrix34_t e = ::vr::VRSystem()->GetEyeToHeadTransform(which);
        const Mat4 projection = from_rows44(p.m);
        const Mat4 view = inverse_rigid(g_hmd * from_rows34(e.m));
        g_scene.render(g_eyes[eye], projection, view, g_view);
        ::vr::Texture_t texture{reinterpret_cast<void*>(static_cast<uintptr_t>(g_eyes[eye].texture())), ::vr::TextureType_OpenGL,
                              ::vr::ColorSpace_Gamma};
        ::vr::VRCompositor()->Submit(which, &texture);
    }
}

}  // namespace hvr::session
