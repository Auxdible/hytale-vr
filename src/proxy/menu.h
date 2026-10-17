// The in-game VR menu, drawn over Hytale with Dear ImGui. It shows on the
// monitor and on the big screen in the headset.
#pragma once

namespace hvr::menu {

// Draws one frame of the menu into target_fbo (0 = the window), and into
// extra_fbo too when it isn't 0 (the headset's HUD panel). Both are
// framebuffer_scale times the window's pixel size, and taller times as tall
// again (Lens shape).
void render(int pixels_w, int pixels_h, float points_to_pixels, unsigned target_fbo, float framebuffer_scale,
            unsigned extra_fbo, float taller = 1.0f);

}  // namespace hvr::menu
