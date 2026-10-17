// The few SDL3 types HytaleVR needs, matching SDL 3.2's ABI. Hytale ships
// SDL3.dll; we call it at runtime and never link against it.
#pragma once

#include <cstddef>
#include <cstdint>

namespace sdl3 {

struct Window;

constexpr uint32_t KEY_DOWN = 0x300, KEY_UP = 0x301, TEXT_EDITING = 0x302, TEXT_INPUT = 0x303;
constexpr uint32_t MOUSE_MOTION = 0x400, MOUSE_BUTTON_DOWN = 0x401, MOUSE_BUTTON_UP = 0x402, MOUSE_WHEEL = 0x403;
constexpr uint32_t WINDOW_RESIZED = 0x206, WINDOW_PIXEL_SIZE_CHANGED = 0x207;
constexpr int GETEVENT = 2;
constexpr int BUTTON_LEFT = 1, BUTTON_MIDDLE = 2, BUTTON_RIGHT = 3;
constexpr int SCANCODE_COUNT = 512;

struct KeyboardEvent {
    uint32_t type;
    uint32_t reserved;
    uint64_t timestamp;
    uint32_t windowID;
    uint32_t which;
    int32_t scancode;
    uint32_t key;
    uint16_t mod;
    uint16_t raw;
    bool down;
    bool repeat;
};

struct MouseMotionEvent {
    uint32_t type;
    uint32_t reserved;
    uint64_t timestamp;
    uint32_t windowID;
    uint32_t which;
    uint32_t state;
    float x, y, xrel, yrel;
};

struct MouseButtonEvent {
    uint32_t type;
    uint32_t reserved;
    uint64_t timestamp;
    uint32_t windowID;
    uint32_t which;
    uint8_t button;
    bool down;
    uint8_t clicks;
    uint8_t padding;
    float x, y;
};

struct MouseWheelEvent {
    uint32_t type;
    uint32_t reserved;
    uint64_t timestamp;
    uint32_t windowID;
    uint32_t which;
    float x, y;
    uint32_t direction;  // 1 = flipped
    float mouse_x, mouse_y;
    int32_t integer_x, integer_y;
};

struct WindowEvent {
    uint32_t type;
    uint32_t reserved;
    uint64_t timestamp;
    uint32_t windowID;
    int32_t data1, data2;
};

union Event {
    uint32_t type;
    struct {
        uint32_t type;
        uint32_t reserved;
        uint64_t timestamp;
    } common;
    KeyboardEvent key;
    MouseMotionEvent motion;
    MouseButtonEvent button;
    MouseWheelEvent wheel;
    WindowEvent window;
    uint8_t padding[128];
};

static_assert(sizeof(Event) == 128, "SDL_Event is 128 bytes");
static_assert(offsetof(KeyboardEvent, scancode) == 24, "SDL_KeyboardEvent layout");
static_assert(offsetof(KeyboardEvent, down) == 36, "SDL_KeyboardEvent layout");
static_assert(offsetof(MouseMotionEvent, x) == 28, "SDL_MouseMotionEvent layout");
static_assert(offsetof(MouseButtonEvent, x) == 28, "SDL_MouseButtonEvent layout");
static_assert(offsetof(WindowEvent, data1) == 20, "SDL_WindowEvent layout");
static_assert(offsetof(MouseWheelEvent, direction) == 32, "SDL_MouseWheelEvent layout");

// Marks events HytaleVR pushes itself, so its own filter lets them through.
constexpr uint32_t kOurEvent = 0x48565221;

}  // namespace sdl3
