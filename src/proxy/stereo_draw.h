// Runs one of Hytale's draw calls once per eye when stereo is on.
#pragma once

#include "internal.h"
#include "stereo.h"

namespace hvr {

template <typename Draw>
inline void stereo_draw(GLenum mode, GLsizei count, Draw&& draw) {
    if (internal_call()) {
        draw();
        return;
    }
    if (stereo::skip_draw(mode)) return;
    if (stereo::draw_begin(mode, count)) {
        draw();
        stereo::draw_second();
        draw();
        stereo::draw_finish();
    } else {
        draw();
    }
}

}  // namespace hvr
