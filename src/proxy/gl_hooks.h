// Hooks for GL functions Hytale fetches through wglGetProcAddress.
#pragma once

namespace hvr {

// Returns the hook to hand Hytale for this function name (remembering the
// real one), or real itself when we don't watch this function.
void* hook_for_proc(const char* name, void* real);

// A real export of the system opengl32.dll.
void* real_export(const char* name);
void* real_gl_proc(const char* name);

// Sets the swap interval Hytale asked for, or forces 0 while VR runs.
void set_vsync_override(bool force_off);

}  // namespace hvr
