// opengl32.dll proxy. Hytale loads this instead of the system OpenGL library
// because it sits next to HytaleClient.exe. Most exports jump straight to the
// real System32 opengl32.dll (generated/stubs.asm); the few below let us see
// each frame and the renderer's setup.
#include <windows.h>

#include "app.h"
#include "census.h"
#include "gl_api.h"
#include "gl_hooks.h"
#include "internal.h"
#include "log.h"
#include "stereo.h"
#include "stereo_draw.h"

#include "generated/export_names.inc"

extern "C" void* hvr_real_exports[kForwardedExportCount] = {};

namespace hvr {
thread_local int t_internal = 0;
}

namespace {

HMODULE g_real = nullptr;

using PFN_wglGetProcAddress = PROC(WINAPI*)(LPCSTR);
using PFN_wglSwapBuffers = BOOL(WINAPI*)(HDC);
using PFN_glClear = void(APIENTRY*)(GLbitfield);
using PFN_glViewport = void(APIENTRY*)(GLint, GLint, GLsizei, GLsizei);
using PFN_glDrawArrays = void(APIENTRY*)(GLenum, GLint, GLsizei);
using PFN_glDrawElements = void(APIENTRY*)(GLenum, GLsizei, GLenum, const void*);
using PFN_glBindTexture = void(APIENTRY*)(GLenum, GLuint);
using PFN_glColorBuffer = void(APIENTRY*)(GLenum);
using PFN_glTexImage2D = void(APIENTRY*)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
using PFN_glDeleteTextures = void(APIENTRY*)(GLsizei, const GLuint*);
using PFN_glCap = void(APIENTRY*)(GLenum);
using PFN_glBlendFunc = void(APIENTRY*)(GLenum, GLenum);

PFN_wglGetProcAddress real_wglGetProcAddress = nullptr;
PFN_wglSwapBuffers real_wglSwapBuffers = nullptr;
PFN_glClear real_glClear = nullptr;
PFN_glViewport real_glViewport = nullptr;
PFN_glDrawArrays real_glDrawArrays = nullptr;
PFN_glDrawElements real_glDrawElements = nullptr;
PFN_glBindTexture real_glBindTexture = nullptr;
PFN_glColorBuffer real_glDrawBuffer = nullptr;
PFN_glColorBuffer real_glReadBuffer = nullptr;
PFN_glTexImage2D real_glTexImage2D = nullptr;
PFN_glDeleteTextures real_glDeleteTextures = nullptr;
PFN_glCap real_glEnable = nullptr;
PFN_glCap real_glDisable = nullptr;
PFN_glBlendFunc real_glBlendFunc = nullptr;

// Stands in for an export the real library lacks, so a call returns 0
// instead of jumping to nowhere.
extern "C" INT_PTR WINAPI hvr_missing_export() { return 0; }

bool load_real_library() {
    wchar_t path[MAX_PATH] = {};
    const UINT length = GetSystemDirectoryW(path, MAX_PATH);
    if (length == 0 || length > MAX_PATH - 16) return false;
    wcscat_s(path, L"\\opengl32.dll");
    g_real = LoadLibraryW(path);
    if (!g_real) return false;
    int missing = 0;
    for (int i = 0; i < kForwardedExportCount; ++i) {
        void* proc = reinterpret_cast<void*>(GetProcAddress(g_real, kForwardedExportNames[i]));
        if (!proc) {
            proc = reinterpret_cast<void*>(&hvr_missing_export);
            ++missing;
        }
        hvr_real_exports[i] = proc;
    }
    real_wglGetProcAddress = reinterpret_cast<PFN_wglGetProcAddress>(GetProcAddress(g_real, "wglGetProcAddress"));
    real_wglSwapBuffers = reinterpret_cast<PFN_wglSwapBuffers>(GetProcAddress(g_real, "wglSwapBuffers"));
    real_glClear = reinterpret_cast<PFN_glClear>(GetProcAddress(g_real, "glClear"));
    real_glViewport = reinterpret_cast<PFN_glViewport>(GetProcAddress(g_real, "glViewport"));
    real_glDrawArrays = reinterpret_cast<PFN_glDrawArrays>(GetProcAddress(g_real, "glDrawArrays"));
    real_glDrawElements = reinterpret_cast<PFN_glDrawElements>(GetProcAddress(g_real, "glDrawElements"));
    real_glBindTexture = reinterpret_cast<PFN_glBindTexture>(GetProcAddress(g_real, "glBindTexture"));
    real_glDrawBuffer = reinterpret_cast<PFN_glColorBuffer>(GetProcAddress(g_real, "glDrawBuffer"));
    real_glReadBuffer = reinterpret_cast<PFN_glColorBuffer>(GetProcAddress(g_real, "glReadBuffer"));
    real_glTexImage2D = reinterpret_cast<PFN_glTexImage2D>(GetProcAddress(g_real, "glTexImage2D"));
    real_glDeleteTextures = reinterpret_cast<PFN_glDeleteTextures>(GetProcAddress(g_real, "glDeleteTextures"));
    real_glEnable = reinterpret_cast<PFN_glCap>(GetProcAddress(g_real, "glEnable"));
    real_glDisable = reinterpret_cast<PFN_glCap>(GetProcAddress(g_real, "glDisable"));
    real_glBlendFunc = reinterpret_cast<PFN_glBlendFunc>(GetProcAddress(g_real, "glBlendFunc"));
    hvr::logf("Loaded %ls (%d of %d forwarded exports missing)", path, missing, kForwardedExportCount);
    return real_wglGetProcAddress && real_wglSwapBuffers && real_glClear && real_glViewport && real_glDrawArrays &&
           real_glDrawElements && real_glBindTexture && real_glDrawBuffer && real_glReadBuffer && real_glTexImage2D && real_glDeleteTextures &&
           real_glEnable && real_glDisable && real_glBlendFunc;
}

}  // namespace

namespace hvr {

void* real_gl_proc(const char* name) {
    void* proc = real_wglGetProcAddress ? reinterpret_cast<void*>(real_wglGetProcAddress(name)) : nullptr;
    // Some drivers return small sentinel values instead of null for failures.
    const auto value = reinterpret_cast<intptr_t>(proc);
    if (value >= -1 && value <= 3) proc = nullptr;
    if (!proc && g_real) proc = reinterpret_cast<void*>(GetProcAddress(g_real, name));
    return proc;
}

void* real_export(const char* name) {
    return g_real ? reinterpret_cast<void*>(GetProcAddress(g_real, name)) : nullptr;
}

}  // namespace hvr

extern "C" {

PROC WINAPI hvr_wglGetProcAddress(LPCSTR name) {
    PROC real = real_wglGetProcAddress(name);
    if (!real || !name || reinterpret_cast<uintptr_t>(name) < 0x10000 || hvr::internal_call()) return real;
    return reinterpret_cast<PROC>(hvr::hook_for_proc(name, reinterpret_cast<void*>(real)));
}

BOOL WINAPI hvr_wglSwapBuffers(HDC dc) {
    if (hvr::internal_call()) return real_wglSwapBuffers(dc);
    hvr::app::before_swap(dc);
    const BOOL result = real_wglSwapBuffers(dc);
    hvr::app::after_swap();
    return result;
}

void APIENTRY hvr_glClear(GLbitfield mask) {
    if (hvr::internal_call()) return real_glClear(mask);
    hvr::census::on_clear(mask);
    real_glClear(mask);
    hvr::stereo::after_clear(mask);
}

void APIENTRY hvr_glViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    if (!hvr::internal_call()) {
        hvr::census::on_viewport(x, y, width, height);
        hvr::stereo::on_viewport(width);
    }
    real_glViewport(x, y, width, height);
}

void APIENTRY hvr_glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    if (!hvr::internal_call()) hvr::census::on_draw("DrawArrays", mode, count, 1);
    hvr::stereo_draw(mode, count, [&] { real_glDrawArrays(mode, first, count); });
}

void APIENTRY hvr_glDrawElements(GLenum mode, GLsizei count, GLenum type, const void* indices) {
    if (!hvr::internal_call()) hvr::census::on_draw("DrawElements", mode, count, 1);
    hvr::stereo_draw(mode, count, [&] { real_glDrawElements(mode, count, type, indices); });
}

void APIENTRY hvr_glBindTexture(GLenum target, GLuint texture) {
    real_glBindTexture(target, texture);
    if (!hvr::internal_call()) hvr::stereo::on_bind_texture(target, texture);
}

// Choosing BACK while "the window" is our high-resolution buffer means its
// color attachment instead.
void APIENTRY hvr_glDrawBuffer(GLenum mode) {
    real_glDrawBuffer(hvr::internal_call() ? mode : hvr::stereo::map_color_buffer(true, mode));
}

void APIENTRY hvr_glReadBuffer(GLenum mode) {
    real_glReadBuffer(hvr::internal_call() ? mode : hvr::stereo::map_color_buffer(false, mode));
}

// Texture size changes and deletions: stereo looks at its targets again.
void APIENTRY hvr_glTexImage2D(GLenum target, GLint level, GLint internal_format, GLsizei width, GLsizei height,
                               GLint border, GLenum format, GLenum type, const void* pixels) {
    real_glTexImage2D(target, level, internal_format, width, height, border, format, type, pixels);
    if (!hvr::internal_call()) hvr::stereo::on_layout_change();
}

void APIENTRY hvr_glDeleteTextures(GLsizei count, const GLuint* textures) {
    real_glDeleteTextures(count, textures);
    if (!hvr::internal_call()) hvr::stereo::on_layout_change();
}

// Tracked so stereo never has to ask the driver what's on.
void APIENTRY hvr_glEnable(GLenum cap) {
    real_glEnable(cap);
    if (!hvr::internal_call()) hvr::stereo::on_enable(cap, true);
}

void APIENTRY hvr_glDisable(GLenum cap) {
    real_glDisable(cap);
    if (!hvr::internal_call()) hvr::stereo::on_enable(cap, false);
}

void APIENTRY hvr_glBlendFunc(GLenum src, GLenum dst) {
    real_glBlendFunc(src, dst);
    if (!hvr::internal_call()) hvr::stereo::on_blend_func(src, dst, src, dst);
}

}  // extern "C"

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        hvr::paths_init(instance);
        hvr::log_init();
        hvr::logf("HytaleVR %s loaded into %ls", HVR_VERSION, hvr::client_dir().c_str());
        if (!load_real_library()) {
            hvr::logf("Could not load the system OpenGL library");
            return FALSE;
        }
    } else if (reason == DLL_PROCESS_DETACH) {
        hvr::logf("HytaleVR unloading");
    }
    return TRUE;
}
