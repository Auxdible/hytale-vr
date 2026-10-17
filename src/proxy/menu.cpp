#include "menu.h"

#include <windows.h>
#include <shellapi.h>

#include <imgui.h>
#include <backends/imgui_impl_opengl3.h>

#include <string>

#include "app.h"
#include "census.h"
#include "gl_api.h"
#include "log.h"
#include "sdl_hooks.h"
#include "stereo.h"
#include "vr_runtime.h"

namespace hvr::menu {
namespace {

bool g_ready = false;
ImFont* g_body = nullptr;
ImFont* g_title = nullptr;
float g_scale = 1.0f;
LARGE_INTEGER g_last{};
bool g_capture_waiting = false;

const ImVec4 kAccent{0.98f, 0.74f, 0.28f, 1.0f};
const ImVec4 kAccentHover{1.0f, 0.82f, 0.42f, 1.0f};
const ImVec4 kAccentActive{0.90f, 0.64f, 0.20f, 1.0f};
const ImVec4 kText{0.93f, 0.94f, 0.96f, 1.0f};
const ImVec4 kDim{0.58f, 0.61f, 0.67f, 1.0f};
const ImVec4 kPanel{0.075f, 0.085f, 0.105f, 0.97f};
const ImVec4 kField{0.14f, 0.15f, 0.18f, 1.0f};
const ImVec4 kFieldHover{0.18f, 0.19f, 0.23f, 1.0f};

void apply_style() {
    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle();
    style.WindowRounding = 14;
    style.ChildRounding = 10;
    style.FrameRounding = 8;
    style.GrabRounding = 8;
    style.PopupRounding = 8;
    style.TabRounding = 8;
    style.WindowBorderSize = 1;
    style.FrameBorderSize = 0;
    style.WindowPadding = {22, 20};
    style.FramePadding = {12, 8};
    style.ItemSpacing = {10, 10};
    style.ItemInnerSpacing = {8, 6};
    style.GrabMinSize = 14;
    style.ScrollbarSize = 12;
    style.TabBarBorderSize = 0;
    style.CellPadding = {8, 5};
    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = kText;
    c[ImGuiCol_TextDisabled] = kDim;
    c[ImGuiCol_WindowBg] = kPanel;
    c[ImGuiCol_PopupBg] = kPanel;
    c[ImGuiCol_Border] = {1, 1, 1, 0.07f};
    c[ImGuiCol_FrameBg] = kField;
    c[ImGuiCol_FrameBgHovered] = kFieldHover;
    c[ImGuiCol_FrameBgActive] = kFieldHover;
    c[ImGuiCol_SliderGrab] = kAccent;
    c[ImGuiCol_SliderGrabActive] = kAccentHover;
    c[ImGuiCol_CheckMark] = kAccent;
    c[ImGuiCol_Button] = kField;
    c[ImGuiCol_ButtonHovered] = kFieldHover;
    c[ImGuiCol_ButtonActive] = {0.22f, 0.23f, 0.28f, 1.0f};
    c[ImGuiCol_Header] = kField;
    c[ImGuiCol_HeaderHovered] = kFieldHover;
    c[ImGuiCol_HeaderActive] = kFieldHover;
    c[ImGuiCol_Tab] = {0, 0, 0, 0};
    c[ImGuiCol_TabHovered] = kFieldHover;
    c[ImGuiCol_TabSelected] = kField;
    c[ImGuiCol_TabSelectedOverline] = kAccent;
    c[ImGuiCol_Separator] = {1, 1, 1, 0.08f};
    c[ImGuiCol_TableBorderLight] = {1, 1, 1, 0.06f};
    c[ImGuiCol_TableRowBgAlt] = {1, 1, 1, 0.025f};
    c[ImGuiCol_NavCursor] = kAccent;
    style.ScaleAllSizes(g_scale);
}

bool file_exists(const char* path) { return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES; }

void init(int pixels_h) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    float scale = static_cast<float>(pixels_h) / 1080.0f;
    scale = scale < 0.8f ? 0.8f : (scale > 2.2f ? 2.2f : scale);
    g_scale = scale * app::settings().menu_scale;
    const char* regular = "C:\\Windows\\Fonts\\segoeui.ttf";
    const char* semibold = "C:\\Windows\\Fonts\\seguisb.ttf";
    if (file_exists(regular)) g_body = io.Fonts->AddFontFromFileTTF(regular, 18.0f * g_scale);
    if (!g_body) {
        ImFontConfig config;
        config.SizePixels = 15.0f * g_scale;
        g_body = io.Fonts->AddFontDefault(&config);
    }
    if (file_exists(semibold)) g_title = io.Fonts->AddFontFromFileTTF(semibold, 28.0f * g_scale);
    if (!g_title) g_title = g_body;
    apply_style();
    ImGui_ImplOpenGL3_Init("#version 330 core");
    QueryPerformanceCounter(&g_last);
    g_ready = true;
    logf("VR menu ready at scale %.2f", g_scale);
}

ImGuiKey imgui_key(int scancode) {
    if (scancode >= 4 && scancode <= 29) return static_cast<ImGuiKey>(ImGuiKey_A + (scancode - 4));
    switch (scancode) {
        case 40: return ImGuiKey_Enter;
        case 41: return ImGuiKey_Escape;
        case 42: return ImGuiKey_Backspace;
        case 43: return ImGuiKey_Tab;
        case 44: return ImGuiKey_Space;
        case 74: return ImGuiKey_Home;
        case 75: return ImGuiKey_PageUp;
        case 76: return ImGuiKey_Delete;
        case 77: return ImGuiKey_End;
        case 78: return ImGuiKey_PageDown;
        case 79: return ImGuiKey_RightArrow;
        case 80: return ImGuiKey_LeftArrow;
        case 81: return ImGuiKey_DownArrow;
        case 82: return ImGuiKey_UpArrow;
        case 224: return ImGuiKey_LeftCtrl;
        case 225: return ImGuiKey_LeftShift;
        case 226: return ImGuiKey_LeftAlt;
        case 228: return ImGuiKey_RightCtrl;
        case 229: return ImGuiKey_RightShift;
        case 230: return ImGuiKey_RightAlt;
        default: return ImGuiKey_None;
    }
}

void feed_input(float points_to_pixels) {
    ImGuiIO& io = ImGui::GetIO();
    for (const sdl::MenuInput& in : sdl::take_menu_input()) {
        switch (in.kind) {
            case sdl::MenuInput::Motion:
                io.AddMousePosEvent(in.x * points_to_pixels, in.y * points_to_pixels);
                break;
            case sdl::MenuInput::Button: {
                io.AddMousePosEvent(in.x * points_to_pixels, in.y * points_to_pixels);
                const int button = in.button == 1 ? 0 : (in.button == 3 ? 1 : (in.button == 2 ? 2 : -1));
                if (button >= 0) io.AddMouseButtonEvent(button, in.down);
                break;
            }
            case sdl::MenuInput::Wheel:
                io.AddMouseWheelEvent(in.x, in.y);
                break;
            case sdl::MenuInput::Key: {
                if (in.down && in.scancode == 41 && !sdl::rebinding()) {
                    app::set_menu_open(false);
                    break;
                }
                const ImGuiKey key = imgui_key(in.scancode);
                if (key == ImGuiKey_None) break;
                io.AddKeyEvent(key, in.down);
                if (in.scancode == 224 || in.scancode == 228) io.AddKeyEvent(ImGuiMod_Ctrl, in.down);
                if (in.scancode == 225 || in.scancode == 229) io.AddKeyEvent(ImGuiMod_Shift, in.down);
                if (in.scancode == 226 || in.scancode == 230) io.AddKeyEvent(ImGuiMod_Alt, in.down);
                break;
            }
        }
    }
}

// A filled pill with a colored dot, right-aligned on the current line.
void status_pill(const char* label, ImVec4 dot) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 text = ImGui::CalcTextSize(label);
    const float pad_x = 12 * g_scale, pad_y = 5 * g_scale, dot_size = 8 * g_scale;
    const ImVec2 size{text.x + pad_x * 2 + dot_size + 8 * g_scale, text.y + pad_y * 2};
    const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    const ImVec2 min{right - size.x, ImGui::GetCursorScreenPos().y + 6 * g_scale};
    const ImVec2 max{min.x + size.x, min.y + size.y};
    draw->AddRectFilled(min, max, ImGui::GetColorU32(ImVec4{1, 1, 1, 0.06f}), size.y * 0.5f);
    draw->AddCircleFilled({min.x + pad_x + dot_size * 0.5f, (min.y + max.y) * 0.5f}, dot_size * 0.5f, ImGui::GetColorU32(dot));
    draw->AddText({min.x + pad_x + dot_size + 8 * g_scale, min.y + pad_y}, ImGui::GetColorU32(kText), label);
}

bool accent_button(const char* label, ImVec2 size) {
    ImGui::PushStyleColor(ImGuiCol_Button, kAccent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccentHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, kAccentActive);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0.08f, 0.07f, 0.05f, 1.0f});
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return pressed;
}

void hint(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, kDim);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

bool slider(const char* label, float* value, float min, float max, const char* format) {
    ImGui::TextUnformatted(label);
    ImGui::SameLine(150 * g_scale);
    ImGui::SetNextItemWidth(-1);
    const std::string id = std::string("##") + label;
    return ImGui::SliderFloat(id.c_str(), value, min, max, format, ImGuiSliderFlags_AlwaysClamp);
}

void tab_world(Settings& s) {
    bool changed = false;
    changed |= ImGui::Checkbox("See the world in 3D", &s.world_view);
    hint("In a world, Hytale draws every frame once per eye so you stand in it. The main menu and loading "
         "screens use the big screen. Set Hytale's field of view to its maximum so nothing at the edge of your "
         "view gets left out.");
    if (ImGui::Button("Switch to 3D now")) session::force_world();
    ImGui::SameLine();
    hint("if you're in a world and still see the big screen");
    changed |= ImGui::Checkbox("Walk when you move in your room", &s.room_walk);
    hint("Your character steps to where your head is, so leaning and walking around work.");
    changed |= ImGui::Checkbox("Stop pop-in (turn off Hytale's occlusion culling)", &s.no_occlusion);
    hint("Hytale hides what its own camera can't see; your eyes see a little more. Costs some speed.");
    ImGui::Spacing();
    ImGui::TextUnformatted("Turning");
    ImGui::SameLine(150 * g_scale);
    changed |= ImGui::RadioButton("Snap", &s.turn_mode, 0);
    ImGui::SameLine();
    changed |= ImGui::RadioButton("Smooth", &s.turn_mode, 1);
    if (s.turn_mode == 0)
        changed |= slider("Snap angle", &s.snap_degrees, 10.0f, 90.0f, "%.0f deg");
    else
        changed |= slider("Turn speed", &s.smooth_speed, 30.0f, 360.0f, "%.0f deg/s");
    changed |= slider("HUD distance", &s.hud_distance, 0.6f, 5.0f, "%.1f m");
    changed |= slider("HUD width", &s.hud_width, 0.6f, 6.0f, "%.1f m");
    changed |= slider("World scale", &s.world_scale, 0.25f, 4.0f, "%.2fx");
    changed |= slider("Sharpness", &s.render_scale, 1.0f, 2.5f, "%.2fx window");
    hint(app::render_status().c_str());
    if (changed) app::settings_changed();
    ImGui::Spacing();
    if (ImGui::Button("Recenter")) session::recenter();
    ImGui::SameLine();
    hint("or click the right stick");
    ImGui::Spacing();
    hint(("Renderer: " + stereo::summary()).c_str());
}

void tab_screen(Settings& s) {
    bool changed = false;
    changed |= slider("Distance", &s.screen_distance, 0.8f, 8.0f, "%.1f m");
    changed |= slider("Width", &s.screen_width, 0.8f, 12.0f, "%.1f m");
    changed |= slider("Height", &s.screen_height, -1.5f, 2.0f, "%+.2f m");
    changed |= ImGui::Checkbox("Show the floor grid", &s.show_floor);
    if (changed) app::settings_changed();
    ImGui::Spacing();
    if (ImGui::Button("Recenter screen")) session::recenter();
    ImGui::SameLine();
    hint("or click the right stick");
}

void tab_controls(Settings& s) {
    bool changed = false;
    changed |= ImGui::Checkbox("Play with controllers", &s.controller_input);
    changed |= ImGui::Checkbox("Left-handed (laser on the left hand)", &s.left_handed);
    changed |= slider("Look speed", &s.look_speed, 100.0f, 4000.0f, "%.0f");
    changed |= slider("Stick deadzone", &s.deadzone, 0.0f, 0.6f, "%.2f");
    if (changed) app::settings_changed();
    ImGui::Spacing();
    const char* rows[][2] = {
        {"Laser + trigger", "Click what you point at"},
        {"Right trigger", "Attack / break (left click)"},
        {"Left trigger", "Use / place (right click)"},
        {"Left stick", "Move, click to sprint"},
        {"Right stick", "Turn (in the world), look, or scroll in menus"},
        {"A", "Jump"},
        {"B", "Back (Esc)"},
        {"X", "Inventory"},
        {"Y", "This VR menu"},
        {"Left grip", "Crouch"},
        {"Right grip", "Next hotbar slot"},
        {"Right stick click", "Recenter"},
    };
    if (ImGui::BeginTable("controls", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("Control", ImGuiTableColumnFlags_WidthFixed, 170 * g_scale);
        ImGui::TableSetupColumn("Action");
        for (const auto& row : rows) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, kDim);
            ImGui::TextUnformatted(row[0]);
            ImGui::PopStyleColor();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(row[1]);
        }
        ImGui::EndTable();
    }
}

void tab_general(Settings& s) {
    bool changed = false;
    changed |= ImGui::Checkbox("Start in VR when Hytale opens", &s.vr_enabled);
    changed |= ImGui::Checkbox("Turn off vsync while in VR (smoother)", &s.vsync_off_in_vr);
    if (changed) app::settings_changed();

    ImGui::Spacing();
    ImGui::TextUnformatted("Menu key");
    ImGui::SameLine(150 * g_scale);
    if (sdl::rebinding()) {
        ImGui::Button("Press a key...  (Esc cancels)");
    } else if (ImGui::Button(sdl::scancode_name(s.menu_key).c_str())) {
        sdl::begin_rebind();
    }
    const int rebound = sdl::take_rebind_result();
    if (rebound > 0) {
        s.menu_key = rebound;
        sdl::set_menu_key(rebound);
        app::settings_changed();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextUnformatted("Renderer log");
    hint("Real 3D from Hytale's own renderer is built from this log. Capture one while you're in a world.");
    if (census::capture_pending()) {
        ImGui::BeginDisabled();
        ImGui::Button("Capturing...");
        ImGui::EndDisabled();
        g_capture_waiting = true;
    } else if (ImGui::Button("Capture renderer log")) {
        census::request_capture();
        g_capture_waiting = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Open logs folder")) ShellExecuteW(nullptr, L"open", log_dir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    const std::wstring last = census::last_capture_path();
    if (g_capture_waiting && !census::capture_pending() && !last.empty()) {
        const auto slash = last.find_last_of(L'\\');
        hint(("Saved " + narrow(last.substr(slash + 1))).c_str());
    }
    ImGui::Spacing();
    hint(("Hytale VR " HVR_VERSION ", " + std::to_string(census::programs_linked()) + " shaders seen, " +
          std::to_string(census::draws_last_frame()) + " draws last frame").c_str());
}

void draw_window() {
    Settings& s = app::settings();
    const ImGuiIO& io = ImGui::GetIO();
    const float width = 600 * g_scale;
    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f}, ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({width, 0});
    ImGui::Begin("Hytale VR", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);

    const session::State state = session::state();
    const char* pill = "VR off";
    ImVec4 dot{0.45f, 0.48f, 0.53f, 1};
    if (state == session::State::Starting) {
        pill = "Starting";
        dot = kAccent;
    } else if (state == session::State::Running) {
        pill = "In VR";
        dot = {0.35f, 0.85f, 0.55f, 1};
    } else if (state == session::State::Failed) {
        pill = "Problem";
        dot = {0.95f, 0.38f, 0.35f, 1};
    }
    status_pill(pill, dot);
    ImGui::PushFont(g_title);
    ImGui::TextUnformatted("Hytale VR");
    ImGui::PopFont();
    hint(session::status_text().c_str());
    ImGui::Spacing();

    const ImVec2 big{-1, 48 * g_scale};
    if (state == session::State::Running || state == session::State::Starting) {
        if (ImGui::Button(state == session::State::Starting ? "Cancel" : "Exit VR", big)) {
            session::request_stop();
            s.vr_enabled = false;
            app::settings_changed();
        }
    } else if (accent_button(state == session::State::Failed ? "Try again" : "Enter VR", big)) {
        session::request_start();
        s.vr_enabled = true;
        app::settings_changed();
    }
    ImGui::Spacing();

    if (ImGui::BeginTabBar("tabs")) {
        if (ImGui::BeginTabItem("World")) {
            tab_world(s);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Screen")) {
            tab_screen(s);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Controls")) {
            tab_controls(s);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("General")) {
            tab_general(s);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::Spacing();
    ImGui::Separator();
    const std::string footer = sdl::scancode_name(s.menu_key) + " or Y on the left controller closes this menu";
    const float text_width = ImGui::CalcTextSize(footer.c_str()).x;
    ImGui::SetCursorPosX((ImGui::GetWindowSize().x - text_width) * 0.5f);
    ImGui::PushStyleColor(ImGuiCol_Text, kDim);
    ImGui::TextUnformatted(footer.c_str());
    ImGui::PopStyleColor();
    ImGui::End();
}

}  // namespace

void render(int pixels_w, int pixels_h, float points_to_pixels, unsigned target_fbo, float framebuffer_scale,
            unsigned extra_fbo) {
    if (pixels_w <= 0 || pixels_h <= 0) return;
    if (!g_ready) init(pixels_h);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = {static_cast<float>(pixels_w), static_cast<float>(pixels_h)};
    io.DisplayFramebufferScale = {framebuffer_scale, framebuffer_scale};
    LARGE_INTEGER now, frequency;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    float dt = static_cast<float>(now.QuadPart - g_last.QuadPart) / static_cast<float>(frequency.QuadPart);
    g_last = now;
    io.DeltaTime = dt > 0 && dt < 0.25f ? dt : 1.0f / 60.0f;
    feed_input(points_to_pixels);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
    draw_window();
    ImGui::Render();
    // The backend sets the viewport from DisplaySize * DisplayFramebufferScale.
    glr.glBindFramebuffer(gl::DRAW_FRAMEBUFFER, target_fbo);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    if (extra_fbo) {
        glr.glBindFramebuffer(gl::DRAW_FRAMEBUFFER, extra_fbo);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }
}

}  // namespace hvr::menu
