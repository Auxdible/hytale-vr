// The in-game VR menu, drawn over Hytale with Dear ImGui. It shows on the
// monitor and on the big screen in the headset.
#pragma once

namespace hvr::menu {

// Draws one frame of the menu into the window's back buffer.
void render(int pixels_w, int pixels_h, float points_to_pixels);

}  // namespace hvr::menu
