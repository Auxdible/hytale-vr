// Sends controller input to Hytale as real keyboard and mouse input, so it
// behaves exactly like the player's own keyboard and mouse.
#pragma once

namespace hvr::input {

// Sends through Windows while the game window has focus; otherwise keys,
// buttons and scrolling go straight into Hytale's SDL (mouse_move doesn't).
void set_target(void* hwnd);
bool target_focused();

void key(int sdl_scancode, bool down);
void mouse_button(int button, bool down);  // 1 left, 2 middle, 3 right
void mouse_move(int dx, int dy);
void wheel(int clicks);  // positive scrolls up
void release_all();

}  // namespace hvr::input
