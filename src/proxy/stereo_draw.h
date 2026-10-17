// Runs one of Hytale's draw calls once per eye when stereo is on.
#pragma once

#include <windows.h>

#include "internal.h"
#include "stereo.h"

namespace hvr {

template <typename Draw>
inline void stereo_draw(GLenum mode, GLsizei count, Draw&& draw) {
    if (internal_call()) {
        draw();
        return;
    }
    if (!stereo::active()) {
        draw();
        return;
    }
    // Time spent here while stereo is on goes in the log's frame stats.
    LARGE_INTEGER start, end;
    QueryPerformanceCounter(&start);
    if (stereo::skip_draw(mode)) {
    } else if (stereo::draw_begin(mode, count)) {
        draw();
        stereo::draw_second();
        draw();
        stereo::draw_finish();
    } else {
        draw();
    }
    QueryPerformanceCounter(&end);
    stereo::add_draw_time(end.QuadPart - start.QuadPart);
}

}  // namespace hvr
