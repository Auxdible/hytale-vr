// Marks GL calls made by HytaleVR itself (ImGui, the VR scene) so our hooks
// pass them straight through instead of treating them as Hytale's.
#pragma once

namespace hvr {

extern thread_local int t_internal;

struct InternalScope {
    InternalScope() { ++t_internal; }
    ~InternalScope() { --t_internal; }
    InternalScope(const InternalScope&) = delete;
    InternalScope& operator=(const InternalScope&) = delete;
};

inline bool internal_call() { return t_internal != 0; }

}  // namespace hvr
