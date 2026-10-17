#include "gl_hooks.h"

#include <windows.h>

#include <atomic>
#include <cstring>

#include "census.h"
#include "gl_api.h"
#include "internal.h"
#include "log.h"
#include "stereo.h"
#include "stereo_draw.h"

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
HVR_REAL(glActiveTexture, void, (GLenum))
HVR_REAL(glBindTextures, void, (GLuint, GLsizei, const GLuint*))
HVR_REAL(glBindTextureUnit, void, (GLuint, GLuint))
HVR_REAL(glBindImageTexture, void, (GLuint, GLuint, GLint, GLboolean, GLint, GLenum, GLenum))
HVR_REAL(glUniformBlockBinding, void, (GLuint, GLuint, GLuint))
HVR_REAL(glBindBuffer, void, (GLenum, GLuint))
HVR_REAL(glBufferData, void, (GLenum, GLsizeiptr, const void*, GLenum))
HVR_REAL(glBufferStorage, void, (GLenum, GLsizeiptr, const void*, GLbitfield))
HVR_REAL(glNamedBufferData, void, (GLuint, GLsizeiptr, const void*, GLenum))
HVR_REAL(glNamedBufferStorage, void, (GLuint, GLsizeiptr, const void*, GLbitfield))
HVR_REAL(glNamedBufferSubData, void, (GLuint, GLintptr, GLsizeiptr, const void*))
HVR_REAL(glMapBuffer, void*, (GLenum, GLenum))
HVR_REAL(glMapBufferRange, void*, (GLenum, GLintptr, GLsizeiptr, GLbitfield))
HVR_REAL(glUnmapBuffer, GLboolean, (GLenum))
HVR_REAL(glFlushMappedBufferRange, void, (GLenum, GLintptr, GLsizeiptr))
HVR_REAL(glMapNamedBuffer, void*, (GLuint, GLenum))
HVR_REAL(glMapNamedBufferRange, void*, (GLuint, GLintptr, GLsizeiptr, GLbitfield))
HVR_REAL(glUnmapNamedBuffer, GLboolean, (GLuint))
HVR_REAL(glFlushMappedNamedBufferRange, void, (GLuint, GLintptr, GLsizeiptr))
HVR_REAL(glUniformMatrix4fv, void, (GLint, GLsizei, GLboolean, const GLfloat*))
HVR_REAL(glUniform3fv, void, (GLint, GLsizei, const GLfloat*))
HVR_REAL(glProgramUniformMatrix4fv, void, (GLuint, GLint, GLsizei, GLboolean, const GLfloat*))
HVR_REAL(glProgramUniform3fv, void, (GLuint, GLint, GLsizei, const GLfloat*))
HVR_REAL(glDrawBuffers, void, (GLsizei, const GLenum*))
HVR_REAL(glDeleteFramebuffers, void, (GLsizei, const GLuint*))
HVR_REAL(glGenerateMipmap, void, (GLenum))
HVR_REAL(glClearBufferiv, void, (GLenum, GLint, const GLint*))
HVR_REAL(glClearBufferuiv, void, (GLenum, GLint, const GLuint*))

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
    stereo::on_link(program);
    if (WATCHING) census::on_link(program);
}
void APIENTRY hook_glObjectLabel(GLenum identifier, GLuint name, GLsizei length, const GLchar* label) {
    if (WATCHING) census::on_label(identifier, name, length, label);
    real_glObjectLabel(identifier, name, length, label);
}
void APIENTRY hook_glUseProgram(GLuint program) {
    if (WATCHING) census::on_use_program(program);
    real_glUseProgram(program);
    if (WATCHING) stereo::on_use_program(program);
}
void APIENTRY hook_glBindFramebuffer(GLenum target, GLuint framebuffer) {
    if (WATCHING) census::on_bind_framebuffer(target, framebuffer);
    real_glBindFramebuffer(target, framebuffer);
    if (WATCHING) stereo::on_bind_framebuffer(target, framebuffer);
}
void APIENTRY hook_glBindBufferBase(GLenum target, GLuint index, GLuint buffer) {
    if (WATCHING) census::on_bind_buffer_range(target, index, buffer, 0, -1);
    real_glBindBufferBase(target, index, buffer);
    if (WATCHING) stereo::on_bind_buffer_range(target, index, buffer, 0, -1);
}
void APIENTRY hook_glBindBufferRange(GLenum target, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size) {
    if (WATCHING) census::on_bind_buffer_range(target, index, buffer, offset, size);
    real_glBindBufferRange(target, index, buffer, offset, size);
    if (WATCHING) stereo::on_bind_buffer_range(target, index, buffer, offset, size);
}
void APIENTRY hook_glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void* data) {
    if (WATCHING) census::on_buffer_sub_data(target, offset, size, data);
    real_glBufferSubData(target, offset, size, data);
    if (WATCHING) stereo::on_buffer_sub_data(stereo::buffer_bound(target), offset, size, data);
}
void APIENTRY hook_glBlitFramebuffer(GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0, GLint dx1,
                                     GLint dy1, GLbitfield mask, GLenum filter) {
    if (WATCHING) census::on_blit(sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1, mask);
    real_glBlitFramebuffer(sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1, mask, filter);
    if (WATCHING) stereo::after_blit(sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1, mask, filter);
}
void APIENTRY hook_glDispatchCompute(GLuint x, GLuint y, GLuint z) {
    if (WATCHING) census::on_dispatch(x, y, z);
    real_glDispatchCompute(x, y, z);
}
void APIENTRY hook_glClearBufferfv(GLenum buffer, GLint drawbuffer, const GLfloat* value) {
    if (WATCHING) census::on_clear_buffer(buffer, drawbuffer);
    real_glClearBufferfv(buffer, drawbuffer, value);
    if (WATCHING) stereo::after_clear_buffer(buffer, drawbuffer, value, 0);
}
void APIENTRY hook_glClearBufferfi(GLenum buffer, GLint drawbuffer, GLfloat depth, GLint stencil) {
    if (WATCHING) census::on_clear_buffer(buffer, drawbuffer);
    real_glClearBufferfi(buffer, drawbuffer, depth, stencil);
    if (WATCHING) stereo::after_clear_buffer_fi(buffer, drawbuffer, depth, stencil);
}
void APIENTRY hook_glDrawArraysInstanced(GLenum mode, GLint first, GLsizei count, GLsizei instances) {
    if (WATCHING) census::on_draw("DrawArraysInstanced", mode, count, instances);
    stereo_draw(mode, count, [&] { real_glDrawArraysInstanced(mode, first, count, instances); });
}
void APIENTRY hook_glDrawArraysInstancedBaseInstance(GLenum mode, GLint first, GLsizei count, GLsizei instances,
                                                     GLuint base) {
    if (WATCHING) census::on_draw("DrawArraysInstancedBaseInstance", mode, count, instances);
    stereo_draw(mode, count, [&] { real_glDrawArraysInstancedBaseInstance(mode, first, count, instances, base); });
}
void APIENTRY hook_glDrawElementsInstanced(GLenum mode, GLsizei count, GLenum type, const void* indices,
                                           GLsizei instances) {
    if (WATCHING) census::on_draw("DrawElementsInstanced", mode, count, instances);
    stereo_draw(mode, count, [&] { real_glDrawElementsInstanced(mode, count, type, indices, instances); });
}
void APIENTRY hook_glDrawElementsBaseVertex(GLenum mode, GLsizei count, GLenum type, const void* indices,
                                            GLint base) {
    if (WATCHING) census::on_draw("DrawElementsBaseVertex", mode, count, 1);
    stereo_draw(mode, count, [&] { real_glDrawElementsBaseVertex(mode, count, type, indices, base); });
}
void APIENTRY hook_glDrawElementsInstancedBaseVertex(GLenum mode, GLsizei count, GLenum type, const void* indices,
                                                     GLsizei instances, GLint base) {
    if (WATCHING) census::on_draw("DrawElementsInstancedBaseVertex", mode, count, instances);
    stereo_draw(mode, count, [&] { real_glDrawElementsInstancedBaseVertex(mode, count, type, indices, instances, base); });
}
void APIENTRY hook_glDrawElementsInstancedBaseVertexBaseInstance(GLenum mode, GLsizei count, GLenum type,
                                                                 const void* indices, GLsizei instances, GLint base,
                                                                 GLuint base_instance) {
    if (WATCHING) census::on_draw("DrawElementsInstancedBaseVertexBaseInstance", mode, count, instances);
    stereo_draw(mode, count, [&] { real_glDrawElementsInstancedBaseVertexBaseInstance(mode, count, type, indices, instances, base, base_instance); });
}
void APIENTRY hook_glDrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type,
                                       const void* indices) {
    if (WATCHING) census::on_draw("DrawRangeElements", mode, count, 1);
    stereo_draw(mode, count, [&] { real_glDrawRangeElements(mode, start, end, count, type, indices); });
}
void APIENTRY hook_glDrawRangeElementsBaseVertex(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type,
                                                 const void* indices, GLint base) {
    if (WATCHING) census::on_draw("DrawRangeElementsBaseVertex", mode, count, 1);
    stereo_draw(mode, count, [&] { real_glDrawRangeElementsBaseVertex(mode, start, end, count, type, indices, base); });
}
void APIENTRY hook_glMultiDrawArrays(GLenum mode, const GLint* first, const GLsizei* count, GLsizei draws) {
    if (WATCHING) census::on_draw("MultiDrawArrays", mode, draws, 1);
    stereo_draw(mode, draws, [&] { real_glMultiDrawArrays(mode, first, count, draws); });
}
void APIENTRY hook_glMultiDrawElements(GLenum mode, const GLsizei* count, GLenum type, const void* const* indices,
                                       GLsizei draws) {
    if (WATCHING) census::on_draw("MultiDrawElements", mode, draws, 1);
    stereo_draw(mode, draws, [&] { real_glMultiDrawElements(mode, count, type, indices, draws); });
}
void APIENTRY hook_glMultiDrawElementsBaseVertex(GLenum mode, const GLsizei* count, GLenum type,
                                                 const void* const* indices, GLsizei draws, const GLint* base) {
    if (WATCHING) census::on_draw("MultiDrawElementsBaseVertex", mode, draws, 1);
    stereo_draw(mode, draws, [&] { real_glMultiDrawElementsBaseVertex(mode, count, type, indices, draws, base); });
}
void APIENTRY hook_glDrawArraysIndirect(GLenum mode, const void* indirect) {
    if (WATCHING) census::on_draw("DrawArraysIndirect", mode, 0, 0);
    stereo_draw(mode, 0, [&] { real_glDrawArraysIndirect(mode, indirect); });
}
void APIENTRY hook_glDrawElementsIndirect(GLenum mode, GLenum type, const void* indirect) {
    if (WATCHING) census::on_draw("DrawElementsIndirect", mode, 0, 0);
    stereo_draw(mode, 0, [&] { real_glDrawElementsIndirect(mode, type, indirect); });
}
void APIENTRY hook_glMultiDrawArraysIndirect(GLenum mode, const void* indirect, GLsizei draws, GLsizei stride) {
    if (WATCHING) census::on_draw("MultiDrawArraysIndirect", mode, draws, 0);
    stereo_draw(mode, draws, [&] { real_glMultiDrawArraysIndirect(mode, indirect, draws, stride); });
}
void APIENTRY hook_glMultiDrawElementsIndirect(GLenum mode, GLenum type, const void* indirect, GLsizei draws,
                                               GLsizei stride) {
    if (WATCHING) census::on_draw("MultiDrawElementsIndirect", mode, draws, 0);
    stereo_draw(mode, draws, [&] { real_glMultiDrawElementsIndirect(mode, type, indirect, draws, stride); });
}

void APIENTRY hook_glActiveTexture(GLenum unit) {
    real_glActiveTexture(unit);
    if (WATCHING) stereo::on_active_texture(unit);
}
void APIENTRY hook_glBindTextures(GLuint first, GLsizei count, const GLuint* textures) {
    real_glBindTextures(first, count, textures);
    if (WATCHING) stereo::on_bind_textures(first, count, textures);
}
void APIENTRY hook_glBindTextureUnit(GLuint unit, GLuint texture) {
    real_glBindTextureUnit(unit, texture);
    if (WATCHING) stereo::on_bind_texture_unit(unit, texture);
}
void APIENTRY hook_glBindImageTexture(GLuint unit, GLuint texture, GLint level, GLboolean layered, GLint layer,
                                      GLenum access, GLenum format) {
    real_glBindImageTexture(unit, texture, level, layered, layer, access, format);
    if (WATCHING) stereo::on_bind_image_texture(unit, texture, level, layered, layer, access, format);
}
void APIENTRY hook_glUniformBlockBinding(GLuint program, GLuint index, GLuint binding) {
    real_glUniformBlockBinding(program, index, binding);
    if (WATCHING) stereo::on_uniform_block_binding(program, index, binding);
}
void APIENTRY hook_glBindBuffer(GLenum target, GLuint buffer) {
    real_glBindBuffer(target, buffer);
    if (WATCHING) stereo::on_bind_buffer(target, buffer);
}
void APIENTRY hook_glBufferData(GLenum target, GLsizeiptr size, const void* data, GLenum usage) {
    real_glBufferData(target, size, data, usage);
    if (WATCHING) stereo::on_buffer_data(stereo::buffer_bound(target), size, data);
}
void APIENTRY hook_glBufferStorage(GLenum target, GLsizeiptr size, const void* data, GLbitfield flags) {
    real_glBufferStorage(target, size, data, flags);
    if (WATCHING) stereo::on_buffer_data(stereo::buffer_bound(target), size, data);
}
void APIENTRY hook_glNamedBufferData(GLuint buffer, GLsizeiptr size, const void* data, GLenum usage) {
    real_glNamedBufferData(buffer, size, data, usage);
    if (WATCHING) stereo::on_buffer_data(buffer, size, data);
}
void APIENTRY hook_glNamedBufferStorage(GLuint buffer, GLsizeiptr size, const void* data, GLbitfield flags) {
    real_glNamedBufferStorage(buffer, size, data, flags);
    if (WATCHING) stereo::on_buffer_data(buffer, size, data);
}
void APIENTRY hook_glNamedBufferSubData(GLuint buffer, GLintptr offset, GLsizeiptr size, const void* data) {
    real_glNamedBufferSubData(buffer, offset, size, data);
    if (WATCHING) stereo::on_buffer_sub_data(buffer, offset, size, data);
}
void* APIENTRY hook_glMapBuffer(GLenum target, GLenum access) {
    void* pointer = real_glMapBuffer(target, access);
    // Whole-buffer maps: the shadow learns the length from BufferData.
    if (WATCHING) stereo::on_map(stereo::buffer_bound(target), 0, 0, 0, pointer);
    return pointer;
}
void* APIENTRY hook_glMapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access) {
    void* pointer = real_glMapBufferRange(target, offset, length, access);
    if (WATCHING) stereo::on_map(stereo::buffer_bound(target), offset, length, access, pointer);
    return pointer;
}
GLboolean APIENTRY hook_glUnmapBuffer(GLenum target) {
    if (WATCHING) stereo::on_unmap(stereo::buffer_bound(target));
    return real_glUnmapBuffer(target);
}
void APIENTRY hook_glFlushMappedBufferRange(GLenum target, GLintptr offset, GLsizeiptr length) {
    if (WATCHING) stereo::on_flush(stereo::buffer_bound(target), offset, length);
    real_glFlushMappedBufferRange(target, offset, length);
}
void* APIENTRY hook_glMapNamedBuffer(GLuint buffer, GLenum access) {
    void* pointer = real_glMapNamedBuffer(buffer, access);
    if (WATCHING) stereo::on_map(buffer, 0, 0, 0, pointer);
    return pointer;
}
void* APIENTRY hook_glMapNamedBufferRange(GLuint buffer, GLintptr offset, GLsizeiptr length, GLbitfield access) {
    void* pointer = real_glMapNamedBufferRange(buffer, offset, length, access);
    if (WATCHING) stereo::on_map(buffer, offset, length, access, pointer);
    return pointer;
}
GLboolean APIENTRY hook_glUnmapNamedBuffer(GLuint buffer) {
    if (WATCHING) stereo::on_unmap(buffer);
    return real_glUnmapNamedBuffer(buffer);
}
void APIENTRY hook_glFlushMappedNamedBufferRange(GLuint buffer, GLintptr offset, GLsizeiptr length) {
    if (WATCHING) stereo::on_flush(buffer, offset, length);
    real_glFlushMappedNamedBufferRange(buffer, offset, length);
}
void transpose_into(const GLfloat* in, GLfloat* out) {
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) out[c * 4 + r] = in[r * 4 + c];
}
void record_matrix(GLuint program, GLint location, GLsizei count, GLboolean transpose, const GLfloat* value) {
    if (!transpose) {
        stereo::on_uniform(program, location, count, value, 16);
        return;
    }
    GLfloat m[16];
    for (GLsizei i = 0; i < count && i < 4; ++i) {
        transpose_into(value + i * 16, m);
        stereo::on_uniform(program, location + i, 1, m, 16);
    }
}
void APIENTRY hook_glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value) {
    real_glUniformMatrix4fv(location, count, transpose, value);
    if (WATCHING && stereo::wanted()) record_matrix(stereo::current_program(), location, count, transpose, value);
}
void APIENTRY hook_glUniform3fv(GLint location, GLsizei count, const GLfloat* value) {
    real_glUniform3fv(location, count, value);
    if (WATCHING && stereo::wanted()) stereo::on_uniform(stereo::current_program(), location, count, value, 3);
}
void APIENTRY hook_glProgramUniformMatrix4fv(GLuint program, GLint location, GLsizei count, GLboolean transpose,
                                             const GLfloat* value) {
    real_glProgramUniformMatrix4fv(program, location, count, transpose, value);
    if (WATCHING && stereo::wanted()) record_matrix(program, location, count, transpose, value);
}
void APIENTRY hook_glProgramUniform3fv(GLuint program, GLint location, GLsizei count, const GLfloat* value) {
    real_glProgramUniform3fv(program, location, count, value);
    if (WATCHING && stereo::wanted()) stereo::on_uniform(program, location, count, value, 3);
}
void APIENTRY hook_glDrawBuffers(GLsizei count, const GLenum* buffers) {
    real_glDrawBuffers(count, buffers);
    if (WATCHING) stereo::on_draw_buffers(count, buffers);
}
void APIENTRY hook_glDeleteFramebuffers(GLsizei count, const GLuint* framebuffers) {
    if (WATCHING) stereo::on_delete_framebuffers(count, framebuffers);
    real_glDeleteFramebuffers(count, framebuffers);
}
void APIENTRY hook_glGenerateMipmap(GLenum target) {
    real_glGenerateMipmap(target);
    if (WATCHING) stereo::after_generate_mipmap(target);
}
void APIENTRY hook_glClearBufferiv(GLenum buffer, GLint drawbuffer, const GLint* value) {
    if (WATCHING) census::on_clear_buffer(buffer, drawbuffer);
    real_glClearBufferiv(buffer, drawbuffer, value);
    if (WATCHING) stereo::after_clear_buffer(buffer, drawbuffer, value, 1);
}
void APIENTRY hook_glClearBufferuiv(GLenum buffer, GLint drawbuffer, const GLuint* value) {
    if (WATCHING) census::on_clear_buffer(buffer, drawbuffer);
    real_glClearBufferuiv(buffer, drawbuffer, value);
    if (WATCHING) stereo::after_clear_buffer(buffer, drawbuffer, value, 2);
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
    HVR_HOOK(glActiveTexture),
    HVR_HOOK(glBindTextures),
    HVR_HOOK(glBindTextureUnit),
    HVR_HOOK(glBindImageTexture),
    HVR_HOOK(glUniformBlockBinding),
    HVR_HOOK(glBindBuffer),
    HVR_HOOK(glBufferData),
    HVR_HOOK(glBufferStorage),
    HVR_HOOK(glNamedBufferData),
    HVR_HOOK(glNamedBufferStorage),
    HVR_HOOK(glNamedBufferSubData),
    HVR_HOOK(glMapBuffer),
    HVR_HOOK(glMapBufferRange),
    HVR_HOOK(glUnmapBuffer),
    HVR_HOOK(glFlushMappedBufferRange),
    HVR_HOOK(glMapNamedBuffer),
    HVR_HOOK(glMapNamedBufferRange),
    HVR_HOOK(glUnmapNamedBuffer),
    HVR_HOOK(glFlushMappedNamedBufferRange),
    HVR_HOOK(glUniformMatrix4fv),
    HVR_HOOK(glUniform3fv),
    HVR_HOOK(glProgramUniformMatrix4fv),
    HVR_HOOK(glProgramUniform3fv),
    HVR_HOOK(glDrawBuffers),
    HVR_HOOK(glDeleteFramebuffers),
    HVR_HOOK(glGenerateMipmap),
    HVR_HOOK(glClearBufferiv),
    HVR_HOOK(glClearBufferuiv),
    HVR_HOOK(wglSwapIntervalEXT),
};

// GL 1.1 functions we already wrap as exports, in case Hytale also fetches
// them through wglGetProcAddress.
const char* const kExportWrapped[] = {"glClear", "glViewport", "glDrawArrays", "glDrawElements", "glBindTexture"};

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
