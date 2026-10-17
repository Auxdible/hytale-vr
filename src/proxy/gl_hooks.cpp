#include "gl_hooks.h"

#include <windows.h>

#include <atomic>
#include <cstring>

#include "census.h"
#include "gl_api.h"
#include "internal.h"
#include "log.h"

namespace hvr {
namespace {

template <typename F>
F as(void* p) {
    return reinterpret_cast<F>(p);
}

#define HVR_REAL(name, ret, params) \
    using PFN_##name = ret(APIENTRY*) params; \
    PFN_##name real_##name = nullptr;

HVR_REAL(glShaderSource, void, (GLuint, GLsizei, const GLchar* const*, const GLint*))
HVR_REAL(glAttachShader, void, (GLuint, GLuint))
HVR_REAL(glLinkProgram, void, (GLuint))
HVR_REAL(glObjectLabel, void, (GLenum, GLuint, GLsizei, const GLchar*))
HVR_REAL(glUseProgram, void, (GLuint))
HVR_REAL(glBindFramebuffer, void, (GLenum, GLuint))
HVR_REAL(glBindBufferBase, void, (GLenum, GLuint, GLuint))
HVR_REAL(glBindBufferRange, void, (GLenum, GLuint, GLuint, GLintptr, GLsizeiptr))
HVR_REAL(glBufferSubData, void, (GLenum, GLintptr, GLsizeiptr, const void*))
HVR_REAL(glBlitFramebuffer, void, (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum))
HVR_REAL(glDispatchCompute, void, (GLuint, GLuint, GLuint))
HVR_REAL(glClearBufferfv, void, (GLenum, GLint, const GLfloat*))
HVR_REAL(glClearBufferfi, void, (GLenum, GLint, GLfloat, GLint))
HVR_REAL(glDrawArraysInstanced, void, (GLenum, GLint, GLsizei, GLsizei))
HVR_REAL(glDrawArraysInstancedBaseInstance, void, (GLenum, GLint, GLsizei, GLsizei, GLuint))
HVR_REAL(glDrawElementsInstanced, void, (GLenum, GLsizei, GLenum, const void*, GLsizei))
HVR_REAL(glDrawElementsBaseVertex, void, (GLenum, GLsizei, GLenum, const void*, GLint))
HVR_REAL(glDrawElementsInstancedBaseVertex, void, (GLenum, GLsizei, GLenum, const void*, GLsizei, GLint))
HVR_REAL(glDrawElementsInstancedBaseVertexBaseInstance, void,
         (GLenum, GLsizei, GLenum, const void*, GLsizei, GLint, GLuint))
HVR_REAL(glDrawRangeElements, void, (GLenum, GLuint, GLuint, GLsizei, GLenum, const void*))
HVR_REAL(glDrawRangeElementsBaseVertex, void, (GLenum, GLuint, GLuint, GLsizei, GLenum, const void*, GLint))
HVR_REAL(glMultiDrawArrays, void, (GLenum, const GLint*, const GLsizei*, GLsizei))
HVR_REAL(glMultiDrawElements, void, (GLenum, const GLsizei*, GLenum, const void* const*, GLsizei))
HVR_REAL(glMultiDrawElementsBaseVertex, void,
         (GLenum, const GLsizei*, GLenum, const void* const*, GLsizei, const GLint*))
HVR_REAL(glDrawArraysIndirect, void, (GLenum, const void*))
HVR_REAL(glDrawElementsIndirect, void, (GLenum, GLenum, const void*))
HVR_REAL(glMultiDrawArraysIndirect, void, (GLenum, const void*, GLsizei, GLsizei))
HVR_REAL(glMultiDrawElementsIndirect, void, (GLenum, GLenum, const void*, GLsizei, GLsizei))

using PFN_wglSwapIntervalEXT = BOOL(WINAPI*)(int);
PFN_wglSwapIntervalEXT real_wglSwapIntervalEXT = nullptr;
std::atomic<int> g_game_interval{1};
std::atomic<bool> g_force_vsync_off{false};

#define WATCHING (!internal_call())

void APIENTRY hook_glShaderSource(GLuint shader, GLsizei count, const GLchar* const* strings, const GLint* lengths) {
    if (WATCHING) census::on_shader_source(shader, count, strings, lengths);
    real_glShaderSource(shader, count, strings, lengths);
}
void APIENTRY hook_glAttachShader(GLuint program, GLuint shader) {
    if (WATCHING) census::on_attach(program, shader);
    real_glAttachShader(program, shader);
}
void APIENTRY hook_glLinkProgram(GLuint program) {
    real_glLinkProgram(program);
    if (WATCHING) census::on_link(program);
}
void APIENTRY hook_glObjectLabel(GLenum identifier, GLuint name, GLsizei length, const GLchar* label) {
    if (WATCHING) census::on_label(identifier, name, length, label);
    real_glObjectLabel(identifier, name, length, label);
}
void APIENTRY hook_glUseProgram(GLuint program) {
    if (WATCHING) census::on_use_program(program);
    real_glUseProgram(program);
}
void APIENTRY hook_glBindFramebuffer(GLenum target, GLuint framebuffer) {
    if (WATCHING) census::on_bind_framebuffer(target, framebuffer);
    real_glBindFramebuffer(target, framebuffer);
}
void APIENTRY hook_glBindBufferBase(GLenum target, GLuint index, GLuint buffer) {
    if (WATCHING) census::on_bind_buffer_range(target, index, buffer, 0, -1);
    real_glBindBufferBase(target, index, buffer);
}
void APIENTRY hook_glBindBufferRange(GLenum target, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size) {
    if (WATCHING) census::on_bind_buffer_range(target, index, buffer, offset, size);
    real_glBindBufferRange(target, index, buffer, offset, size);
}
void APIENTRY hook_glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void* data) {
    if (WATCHING) census::on_buffer_sub_data(target, offset, size, data);
    real_glBufferSubData(target, offset, size, data);
}
void APIENTRY hook_glBlitFramebuffer(GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0, GLint dx1,
                                     GLint dy1, GLbitfield mask, GLenum filter) {
    if (WATCHING) census::on_blit(sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1, mask);
    real_glBlitFramebuffer(sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1, mask, filter);
}
void APIENTRY hook_glDispatchCompute(GLuint x, GLuint y, GLuint z) {
    if (WATCHING) census::on_dispatch(x, y, z);
    real_glDispatchCompute(x, y, z);
}
void APIENTRY hook_glClearBufferfv(GLenum buffer, GLint drawbuffer, const GLfloat* value) {
    if (WATCHING) census::on_clear_buffer(buffer, drawbuffer);
    real_glClearBufferfv(buffer, drawbuffer, value);
}
void APIENTRY hook_glClearBufferfi(GLenum buffer, GLint drawbuffer, GLfloat depth, GLint stencil) {
    if (WATCHING) census::on_clear_buffer(buffer, drawbuffer);
    real_glClearBufferfi(buffer, drawbuffer, depth, stencil);
}
void APIENTRY hook_glDrawArraysInstanced(GLenum mode, GLint first, GLsizei count, GLsizei instances) {
    if (WATCHING) census::on_draw("DrawArraysInstanced", mode, count, instances);
    real_glDrawArraysInstanced(mode, first, count, instances);
}
void APIENTRY hook_glDrawArraysInstancedBaseInstance(GLenum mode, GLint first, GLsizei count, GLsizei instances,
                                                     GLuint base) {
    if (WATCHING) census::on_draw("DrawArraysInstancedBaseInstance", mode, count, instances);
    real_glDrawArraysInstancedBaseInstance(mode, first, count, instances, base);
}
void APIENTRY hook_glDrawElementsInstanced(GLenum mode, GLsizei count, GLenum type, const void* indices,
                                           GLsizei instances) {
    if (WATCHING) census::on_draw("DrawElementsInstanced", mode, count, instances);
    real_glDrawElementsInstanced(mode, count, type, indices, instances);
}
void APIENTRY hook_glDrawElementsBaseVertex(GLenum mode, GLsizei count, GLenum type, const void* indices,
                                            GLint base) {
    if (WATCHING) census::on_draw("DrawElementsBaseVertex", mode, count, 1);
    real_glDrawElementsBaseVertex(mode, count, type, indices, base);
}
void APIENTRY hook_glDrawElementsInstancedBaseVertex(GLenum mode, GLsizei count, GLenum type, const void* indices,
                                                     GLsizei instances, GLint base) {
    if (WATCHING) census::on_draw("DrawElementsInstancedBaseVertex", mode, count, instances);
    real_glDrawElementsInstancedBaseVertex(mode, count, type, indices, instances, base);
}
void APIENTRY hook_glDrawElementsInstancedBaseVertexBaseInstance(GLenum mode, GLsizei count, GLenum type,
                                                                 const void* indices, GLsizei instances, GLint base,
                                                                 GLuint base_instance) {
    if (WATCHING) census::on_draw("DrawElementsInstancedBaseVertexBaseInstance", mode, count, instances);
    real_glDrawElementsInstancedBaseVertexBaseInstance(mode, count, type, indices, instances, base, base_instance);
}
void APIENTRY hook_glDrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type,
                                       const void* indices) {
    if (WATCHING) census::on_draw("DrawRangeElements", mode, count, 1);
    real_glDrawRangeElements(mode, start, end, count, type, indices);
}
void APIENTRY hook_glDrawRangeElementsBaseVertex(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type,
                                                 const void* indices, GLint base) {
    if (WATCHING) census::on_draw("DrawRangeElementsBaseVertex", mode, count, 1);
    real_glDrawRangeElementsBaseVertex(mode, start, end, count, type, indices, base);
}
void APIENTRY hook_glMultiDrawArrays(GLenum mode, const GLint* first, const GLsizei* count, GLsizei draws) {
    if (WATCHING) census::on_draw("MultiDrawArrays", mode, draws, 1);
    real_glMultiDrawArrays(mode, first, count, draws);
}
void APIENTRY hook_glMultiDrawElements(GLenum mode, const GLsizei* count, GLenum type, const void* const* indices,
                                       GLsizei draws) {
    if (WATCHING) census::on_draw("MultiDrawElements", mode, draws, 1);
    real_glMultiDrawElements(mode, count, type, indices, draws);
}
void APIENTRY hook_glMultiDrawElementsBaseVertex(GLenum mode, const GLsizei* count, GLenum type,
                                                 const void* const* indices, GLsizei draws, const GLint* base) {
    if (WATCHING) census::on_draw("MultiDrawElementsBaseVertex", mode, draws, 1);
    real_glMultiDrawElementsBaseVertex(mode, count, type, indices, draws, base);
}
void APIENTRY hook_glDrawArraysIndirect(GLenum mode, const void* indirect) {
    if (WATCHING) census::on_draw("DrawArraysIndirect", mode, 0, 0);
    real_glDrawArraysIndirect(mode, indirect);
}
void APIENTRY hook_glDrawElementsIndirect(GLenum mode, GLenum type, const void* indirect) {
    if (WATCHING) census::on_draw("DrawElementsIndirect", mode, 0, 0);
    real_glDrawElementsIndirect(mode, type, indirect);
}
void APIENTRY hook_glMultiDrawArraysIndirect(GLenum mode, const void* indirect, GLsizei draws, GLsizei stride) {
    if (WATCHING) census::on_draw("MultiDrawArraysIndirect", mode, draws, 0);
    real_glMultiDrawArraysIndirect(mode, indirect, draws, stride);
}
void APIENTRY hook_glMultiDrawElementsIndirect(GLenum mode, GLenum type, const void* indirect, GLsizei draws,
                                               GLsizei stride) {
    if (WATCHING) census::on_draw("MultiDrawElementsIndirect", mode, draws, 0);
    real_glMultiDrawElementsIndirect(mode, type, indirect, draws, stride);
}

BOOL WINAPI hook_wglSwapIntervalEXT(int interval) {
    g_game_interval = interval;
    return real_wglSwapIntervalEXT(g_force_vsync_off ? 0 : interval);
}

struct Hook {
    const char* name;
    void* hook;
    void** real;
};

#define HVR_HOOK(name) {#name, reinterpret_cast<void*>(&hook_##name), reinterpret_cast<void**>(&real_##name)}

const Hook kHooks[] = {
    HVR_HOOK(glShaderSource),
    HVR_HOOK(glAttachShader),
    HVR_HOOK(glLinkProgram),
    HVR_HOOK(glObjectLabel),
    HVR_HOOK(glUseProgram),
    HVR_HOOK(glBindFramebuffer),
    HVR_HOOK(glBindBufferBase),
    HVR_HOOK(glBindBufferRange),
    HVR_HOOK(glBufferSubData),
    HVR_HOOK(glBlitFramebuffer),
    HVR_HOOK(glDispatchCompute),
    HVR_HOOK(glClearBufferfv),
    HVR_HOOK(glClearBufferfi),
    HVR_HOOK(glDrawArraysInstanced),
    HVR_HOOK(glDrawArraysInstancedBaseInstance),
    HVR_HOOK(glDrawElementsInstanced),
    HVR_HOOK(glDrawElementsBaseVertex),
    HVR_HOOK(glDrawElementsInstancedBaseVertex),
    HVR_HOOK(glDrawElementsInstancedBaseVertexBaseInstance),
    HVR_HOOK(glDrawRangeElements),
    HVR_HOOK(glDrawRangeElementsBaseVertex),
    HVR_HOOK(glMultiDrawArrays),
    HVR_HOOK(glMultiDrawElements),
    HVR_HOOK(glMultiDrawElementsBaseVertex),
    HVR_HOOK(glDrawArraysIndirect),
    HVR_HOOK(glDrawElementsIndirect),
    HVR_HOOK(glMultiDrawArraysIndirect),
    HVR_HOOK(glMultiDrawElementsIndirect),
    HVR_HOOK(wglSwapIntervalEXT),
};

// GL 1.1 functions we already wrap as exports, in case Hytale also fetches
// them through wglGetProcAddress.
const char* const kExportWrapped[] = {"glClear", "glViewport", "glDrawArrays", "glDrawElements"};

}  // namespace

void* hook_for_proc(const char* name, void* real) {
    for (const Hook& hook : kHooks) {
        if (std::strcmp(hook.name, name) != 0) continue;
        if (!*hook.real) {
            *hook.real = real;
        } else if (*hook.real != real) {
            logf("wglGetProcAddress(%s) changed from %p to %p; keeping the first", name, *hook.real, real);
        }
        return hook.hook;
    }
    for (const char* wrapped : kExportWrapped) {
        if (std::strcmp(wrapped, name) == 0) {
            static HMODULE self = [] {
                HMODULE module = nullptr;
                GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   reinterpret_cast<LPCWSTR>(&hook_for_proc), &module);
                return module;
            }();
            if (void* ours = reinterpret_cast<void*>(GetProcAddress(self, name))) return ours;
        }
    }
    return real;
}

void set_vsync_override(bool force_off) {
    if (g_force_vsync_off.exchange(force_off) == force_off) return;
    if (!real_wglSwapIntervalEXT) real_wglSwapIntervalEXT = as<PFN_wglSwapIntervalEXT>(real_gl_proc("wglSwapIntervalEXT"));
    if (!real_wglSwapIntervalEXT) return;
    InternalScope scope;
    real_wglSwapIntervalEXT(force_off ? 0 : g_game_interval.load());
    logf("Vsync %s", force_off ? "turned off while VR runs" : "restored to Hytale's setting");
}

}  // namespace hvr
