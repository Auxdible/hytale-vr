#pragma once

// The OpenGL 3.3 entry points the viewer uses, loaded with wglGetProcAddress
// once a context is current. Windows' own gl.h only covers OpenGL 1.1.

#include <windows.h>
#include <GL/gl.h>

#include <cstddef>

namespace hytalevr::viewer::gl {

using GLchar = char;
using GLsizeiptr = std::ptrdiff_t;
using GLintptr = std::ptrdiff_t;

constexpr GLenum kArrayBuffer = 0x8892;
constexpr GLenum kStaticDraw = 0x88E4;
constexpr GLenum kStreamDraw = 0x88E0;
constexpr GLenum kFragmentShader = 0x8B30;
constexpr GLenum kVertexShader = 0x8B31;
constexpr GLenum kCompileStatus = 0x8B81;
constexpr GLenum kLinkStatus = 0x8B82;
constexpr GLenum kTexture2DArray = 0x8C1A;
constexpr GLenum kFramebuffer = 0x8D40;
constexpr GLenum kReadFramebuffer = 0x8CA8;
constexpr GLenum kDrawFramebuffer = 0x8CA9;
constexpr GLenum kRenderbuffer = 0x8D41;
constexpr GLenum kColorAttachment0 = 0x8CE0;
constexpr GLenum kDepthAttachment = 0x8D00;
constexpr GLenum kDepthComponent24 = 0x81A6;
constexpr GLenum kFramebufferComplete = 0x8CD5;
constexpr GLenum kTexture0 = 0x84C0;
constexpr GLenum kClampToEdge = 0x812F;
constexpr GLenum kTextureMaxAnisotropy = 0x84FE;
constexpr GLenum kTextureMaxLevel = 0x813D;
constexpr GLenum kBgra = 0x80E1;
constexpr GLenum kRgba8 = 0x8058;
constexpr GLenum kMultisample = 0x809D;
constexpr GLenum kMaxSamples = 0x8D57;

#define HYTALEVR_VIEWER_GL_FUNCTIONS(X) \
    X(GLuint, CreateShader, (GLenum)) \
    X(void, ShaderSource, (GLuint, GLsizei, const GLchar* const*, const GLint*)) \
    X(void, CompileShader, (GLuint)) \
    X(void, GetShaderiv, (GLuint, GLenum, GLint*)) \
    X(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*)) \
    X(GLuint, CreateProgram, ()) \
    X(void, AttachShader, (GLuint, GLuint)) \
    X(void, BindAttribLocation, (GLuint, GLuint, const GLchar*)) \
    X(void, LinkProgram, (GLuint)) \
    X(void, GetProgramiv, (GLuint, GLenum, GLint*)) \
    X(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*)) \
    X(void, DeleteShader, (GLuint)) \
    X(void, UseProgram, (GLuint)) \
    X(GLint, GetUniformLocation, (GLuint, const GLchar*)) \
    X(void, Uniform1i, (GLint, GLint)) \
    X(void, Uniform1f, (GLint, GLfloat)) \
    X(void, Uniform2f, (GLint, GLfloat, GLfloat)) \
    X(void, Uniform3f, (GLint, GLfloat, GLfloat, GLfloat)) \
    X(void, UniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat*)) \
    X(void, GenVertexArrays, (GLsizei, GLuint*)) \
    X(void, BindVertexArray, (GLuint)) \
    X(void, DeleteVertexArrays, (GLsizei, const GLuint*)) \
    X(void, GenBuffers, (GLsizei, GLuint*)) \
    X(void, BindBuffer, (GLenum, GLuint)) \
    X(void, BufferData, (GLenum, GLsizeiptr, const void*, GLenum)) \
    X(void, DeleteBuffers, (GLsizei, const GLuint*)) \
    X(void, EnableVertexAttribArray, (GLuint)) \
    X(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void*)) \
    X(void, ActiveTexture, (GLenum)) \
    X(void, TexImage3D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*)) \
    X(void, TexSubImage3D, (GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, const void*)) \
    X(void, GenerateMipmap, (GLenum)) \
    X(void, GenFramebuffers, (GLsizei, GLuint*)) \
    X(void, BindFramebuffer, (GLenum, GLuint)) \
    X(void, FramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint)) \
    X(void, FramebufferRenderbuffer, (GLenum, GLenum, GLenum, GLuint)) \
    X(GLenum, CheckFramebufferStatus, (GLenum)) \
    X(void, GenRenderbuffers, (GLsizei, GLuint*)) \
    X(void, BindRenderbuffer, (GLenum, GLuint)) \
    X(void, RenderbufferStorageMultisample, (GLenum, GLsizei, GLenum, GLsizei, GLsizei)) \
    X(void, BlitFramebuffer, (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum))

#define HYTALEVR_VIEWER_GL_DECLARE(ret, name, args) \
    using name##Fn = ret(WINAPI*) args;            \
    inline name##Fn name = nullptr;
HYTALEVR_VIEWER_GL_FUNCTIONS(HYTALEVR_VIEWER_GL_DECLARE)
#undef HYTALEVR_VIEWER_GL_DECLARE

// Returns the name of the first missing function, or nullptr when all loaded.
inline const char* load() {
#define HYTALEVR_VIEWER_GL_LOAD(ret, name, args)                                    \
    name = reinterpret_cast<name##Fn>(wglGetProcAddress("gl" #name));             \
    if (!name) return "gl" #name;
    HYTALEVR_VIEWER_GL_FUNCTIONS(HYTALEVR_VIEWER_GL_LOAD)
#undef HYTALEVR_VIEWER_GL_LOAD
    return nullptr;
}

} // namespace hytalevr::viewer::gl
