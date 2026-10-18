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
    const auto run = [&] {
        if (stereo::skip_draw(mode)) {
        } else if (stereo::draw_begin(mode, count)) {
            draw();
            stereo::draw_second();
            draw();
            stereo::draw_finish();
        } else {
            draw();
        }
    };
    const int moved = stereo::move_first_person();
    run();
    if (moved == 2 && stereo::mirror_first_person()) run();  // the arm again, on the other hand
    if (moved) stereo::restore_first_person();
    QueryPerformanceCounter(&end);
    stereo::add_draw_time(end.QuadPart - start.QuadPart);
}

}  // namespace hvr
