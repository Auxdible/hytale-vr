// The renderer log. Watches how Hytale builds each frame (programs, render
// targets, uniform buffers, draw calls) and writes one frame of it to
// HytaleVR\logs on request. Real stereo is built on what this shows.
#pragma once

#include <string>

#include "gl_api.h"

namespace hvr::census {

// Called at every swap on the thread that presents.
void frame_boundary();
void request_capture();
bool capture_pending();
std::wstring last_capture_path();
int draws_last_frame();
int programs_linked();

// Hooks
void on_shader_source(GLuint shader, GLsizei count, const GLchar* const* strings, const GLint* lengths);
void on_attach(GLuint program, GLuint shader);
void on_link(GLuint program);
void on_label(GLenum identifier, GLuint name, GLsizei length, const GLchar* label);
void on_use_program(GLuint program);
void on_bind_framebuffer(GLenum target, GLuint framebuffer);
void on_bind_buffer_range(GLenum target, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size);
void on_buffer_sub_data(GLenum target, GLintptr offset, GLsizeiptr size, const void* data);
void on_blit(GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0, GLint dx1, GLint dy1, GLbitfield mask);
void on_dispatch(GLuint x, GLuint y, GLuint z);
void on_clear(GLbitfield mask);
void on_clear_buffer(GLenum buffer, GLint drawbuffer);
void on_viewport(GLint x, GLint y, GLsizei width, GLsizei height);
void on_draw(const char* kind, GLenum mode, GLsizei count, GLsizei instances);

}  // namespace hvr::census
