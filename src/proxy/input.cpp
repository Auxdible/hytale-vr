#include "input.h"

#include <windows.h>

#include <cstdint>

namespace hvr::input {
namespace {

HWND g_target = nullptr;
bool g_keys[512] = {};
bool g_buttons[4] = {};

// SDL scancode to PC set-1 scan code; 0xE0xx marks extended keys.
uint16_t set1_scan(int s) {
    static const uint8_t letters[26] = {0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32,
                                        0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C};
    if (s >= 4 && s <= 29) return letters[s - 4];
    if (s >= 30 && s <= 39) return static_cast<uint16_t>(0x02 + (s - 30));  // 1..9, 0
    if (s >= 58 && s <= 67) return static_cast<uint16_t>(0x3B + (s - 58));  // F1..F10
    switch (s) {
        case 40: return 0x1C;    // Return
        case 41: return 0x01;    // Escape
        case 42: return 0x0E;    // Backspace
        case 43: return 0x0F;    // Tab
        case 44: return 0x39;    // Space
        case 45: return 0x0C;    // -
        case 46: return 0x0D;    // =
        case 47: return 0x1A;    // [
        case 48: return 0x1B;    // ]
        case 49: return 0x2B;    // backslash
        case 51: return 0x27;    // ;
        case 52: return 0x28;    // '
        case 53: return 0x29;    // `
        case 54: return 0x33;    // ,
        case 55: return 0x34;    // .
        case 56: return 0x35;    // /
        case 57: return 0x3A;    // Caps Lock
        case 68: return 0x57;    // F11
        case 69: return 0x58;    // F12
        case 73: return 0xE052;  // Insert
        case 74: return 0xE047;  // Home
        case 75: return 0xE049;  // Page Up
        case 76: return 0xE053;  // Delete
        case 77: return 0xE04F;  // End
        case 78: return 0xE051;  // Page Down
        case 79: return 0xE04D;  // Right
        case 80: return 0xE04B;  // Left
        case 81: return 0xE050;  // Down
        case 82: return 0xE048;  // Up
        case 224: return 0x1D;   // Left Ctrl
        case 225: return 0x2A;   // Left Shift
        case 226: return 0x38;   // Left Alt
        case 228: return 0xE01D; // Right Ctrl
        case 229: return 0x36;   // Right Shift
        case 230: return 0xE038; // Right Alt
        default: return 0;
    }
}

void send(INPUT& in) { SendInput(1, &in, sizeof(INPUT)); }

}  // namespace

void set_target(void* hwnd) { g_target = static_cast<HWND>(hwnd); }

bool target_focused() {
    if (!g_target) return false;
    HWND foreground = GetForegroundWindow();
    return foreground == g_target || GetAncestor(foreground, GA_ROOT) == GetAncestor(g_target, GA_ROOT);
}

void key(int sdl_scancode, bool down) {
    if (sdl_scancode <= 0 || sdl_scancode >= 512 || g_keys[sdl_scancode] == down) return;
    if (down && !target_focused()) return;
    const uint16_t scan = set1_scan(sdl_scancode);
    if (!scan) return;
    g_keys[sdl_scancode] = down;
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wScan = scan & 0xFF;
    in.ki.dwFlags = KEYEVENTF_SCANCODE | (down ? 0 : KEYEVENTF_KEYUP) | ((scan & 0xE000) ? KEYEVENTF_EXTENDEDKEY : 0);
    send(in);
}

void mouse_button(int button, bool down) {
    if (button < 1 || button > 3 || g_buttons[button] == down) return;
    if (down && !target_focused()) return;
    g_buttons[button] = down;
    INPUT in{};
    in.type = INPUT_MOUSE;
    static const DWORD downs[4] = {0, MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_MIDDLEDOWN, MOUSEEVENTF_RIGHTDOWN};
    static const DWORD ups[4] = {0, MOUSEEVENTF_LEFTUP, MOUSEEVENTF_MIDDLEUP, MOUSEEVENTF_RIGHTUP};
    in.mi.dwFlags = down ? downs[button] : ups[button];
    send(in);
}

void mouse_move(int dx, int dy) {
    if ((dx == 0 && dy == 0) || !target_focused()) return;
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dx = dx;
    in.mi.dy = dy;
    in.mi.dwFlags = MOUSEEVENTF_MOVE;
    send(in);
}

void wheel(int clicks) {
    if (clicks == 0 || !target_focused()) return;
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.mouseData = static_cast<DWORD>(clicks * WHEEL_DELTA);
    in.mi.dwFlags = MOUSEEVENTF_WHEEL;
    send(in);
}

void release_all() {
    for (int i = 0; i < 512; ++i)
        if (g_keys[i]) key(i, false);
    for (int b = 1; b <= 3; ++b)
        if (g_buttons[b]) mouse_button(b, false);
}

}  // namespace hvr::input
