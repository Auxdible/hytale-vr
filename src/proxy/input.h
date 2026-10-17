// Sends controller input to Hytale as real keyboard and mouse input, so it
// behaves exactly like the player's own keyboard and mouse.
#pragma once

namespace hvr::input {

// Only sends while the game window has focus. Call each frame.
void set_target(void* hwnd);
bool target_focused();

void key(int sdl_scancode, bool down);
void mouse_button(int button, bool down);  // 1 left, 2 middle, 3 right
void mouse_move(int dx, int dy);
void wheel(int clicks);  // positive scrolls up
void release_all();

}  // namespace hvr::input
