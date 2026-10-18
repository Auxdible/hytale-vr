// The OpenGL functions HytaleVR calls itself, resolved straight from the real
// driver so they never pass through our own hooks.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#ifndef APIENTRY
#define APIENTRY __stdcall
#endif

using GLenum = unsigned int;
using GLboolean = unsigned char;
using GLbitfield = unsigned int;
using GLint = int;
using GLuint = unsigned int;
using GLsizei = int;
using GLfloat = float;
using GLchar = char;
using GLubyte = unsigned char;
using GLsizeiptr = std::ptrdiff_t;
using GLintptr = std::ptrdiff_t;
using GLuint64 = std::uint64_t;

namespace gl {
constexpr GLenum NO_ERROR_ = 0;
constexpr GLenum FALSE_ = 0, TRUE_ = 1;
constexpr GLenum POINTS = 0x0000, LINES = 0x0001, TRIANGLES = 0x0004, TRIANGLE_STRIP = 0x0005;
constexpr GLenum UNSIGNED_BYTE = 0x1401, UNSIGNED_SHORT = 0x1403, UNSIGNED_INT = 0x1405, FLOAT = 0x1406;
constexpr GLenum DEPTH_BUFFER_BIT = 0x0100, STENCIL_BUFFER_BIT = 0x0400, COLOR_BUFFER_BIT = 0x4000;
constexpr GLenum BLEND = 0x0BE2, CULL_FACE = 0x0B44, DEPTH_TEST = 0x0B71, SCISSOR_TEST = 0x0C11,
                 STENCIL_TEST = 0x0B90, FRAMEBUFFER_SRGB = 0x8DB9, MULTISAMPLE = 0x809D;
constexpr GLenum SRC_ALPHA = 0x0302, ONE_MINUS_SRC_ALPHA = 0x0303, ONE = 1;
constexpr GLenum TEXTURE_2D = 0x0DE1, TEXTURE0 = 0x84C0, RGBA = 0x1908, RGBA8 = 0x8058, SRGB8_ALPHA8 = 0x8C43;
constexpr GLenum TEXTURE_MIN_FILTER = 0x2801, TEXTURE_MAG_FILTER = 0x2800, TEXTURE_WRAP_S = 0x2802,
                 TEXTURE_WRAP_T = 0x2803, NEAREST = 0x2600, LINEAR = 0x2601, LINEAR_MIPMAP_LINEAR = 0x2703,
                 CLAMP_TO_EDGE = 0x812F, TEXTURE_MAX_ANISOTROPY = 0x84FE, TEXTURE_MAX_LEVEL = 0x813D;
constexpr GLenum TEXTURE_WIDTH = 0x1000, TEXTURE_HEIGHT = 0x1001, TEXTURE_INTERNAL_FORMAT = 0x1003,
                 TEXTURE_SAMPLES = 0x9106;
constexpr GLenum PIXEL_PACK_BUFFER = 0x88EB, PIXEL_PACK_BUFFER_BINDING = 0x88ED, STREAM_READ = 0x88E1;
constexpr GLenum FRAMEBUFFER = 0x8D40, READ_FRAMEBUFFER = 0x8CA8, DRAW_FRAMEBUFFER = 0x8CA9,
                 RENDERBUFFER = 0x8D41, FRAMEBUFFER_COMPLETE = 0x8CD5, COLOR_ATTACHMENT0 = 0x8CE0,
                 DEPTH_ATTACHMENT = 0x8D00, STENCIL_ATTACHMENT = 0x8D20, DEPTH_STENCIL_ATTACHMENT = 0x821A,
                 DEPTH24_STENCIL8 = 0x88F0, BACK = 0x0405, NONE = 0;
constexpr GLenum FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE = 0x8CD0, FRAMEBUFFER_ATTACHMENT_OBJECT_NAME = 0x8CD1,
                 FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL = 0x8CD2, FRAMEBUFFER_ATTACHMENT_TEXTURE_LAYER = 0x8CD4;
constexpr GLenum RENDERBUFFER_WIDTH = 0x8D42, RENDERBUFFER_HEIGHT = 0x8D43, RENDERBUFFER_INTERNAL_FORMAT = 0x8D44,
                 RENDERBUFFER_SAMPLES = 0x8CAB;
constexpr GLenum ARRAY_BUFFER = 0x8892, ELEMENT_ARRAY_BUFFER = 0x8893, UNIFORM_BUFFER = 0x8A11,
                 SHADER_STORAGE_BUFFER = 0x90D2, COPY_READ_BUFFER = 0x8F36, STREAM_DRAW = 0x88E0, DYNAMIC_DRAW = 0x88E8;
constexpr GLenum VERTEX_SHADER = 0x8B31, FRAGMENT_SHADER = 0x8B30, GEOMETRY_SHADER = 0x8DD9,
                 COMPUTE_SHADER = 0x91B9, COMPILE_STATUS = 0x8B81, LINK_STATUS = 0x8B82, INFO_LOG_LENGTH = 0x8B84,
                 ATTACHED_SHADERS = 0x8B85, ACTIVE_UNIFORMS = 0x8B86, ACTIVE_ATTRIBUTES = 0x8B89,
                 ACTIVE_UNIFORM_BLOCKS = 0x8A36, UNIFORM_BLOCK_BINDING = 0x8A3F, UNIFORM_BLOCK_DATA_SIZE = 0x8A40,
                 SHADER_TYPE = 0x8B4F;
constexpr GLenum FLOAT_VEC2 = 0x8B50, FLOAT_VEC3 = 0x8B51, FLOAT_VEC4 = 0x8B52, FLOAT_MAT4 = 0x8B5C,
                 FLOAT_MAT3 = 0x8B5B;
constexpr GLenum UNIFORM_BUFFER_BINDING = 0x8A28, UNIFORM_BUFFER_START = 0x8A29, UNIFORM_BUFFER_SIZE = 0x8A2A;
constexpr GLenum PROGRAM = 0x82E2, SHADER = 0x82E1, TEXTURE = 0x1702, BUFFER = 0x82E0;
constexpr GLenum VIEWPORT = 0x0BA2, SCISSOR_BOX = 0x0C10, CURRENT_PROGRAM = 0x8B8D, VERTEX_ARRAY_BINDING = 0x85B5,
                 ARRAY_BUFFER_BINDING = 0x8894, ACTIVE_TEXTURE = 0x84E0, TEXTURE_BINDING_2D = 0x8069,
                 DRAW_FRAMEBUFFER_BINDING = 0x8CA6, READ_FRAMEBUFFER_BINDING = 0x8CAA, SAMPLER_BINDING = 0x8919,
                 COLOR_WRITEMASK = 0x0C23, DEPTH_WRITEMASK = 0x0B72, BLEND_SRC_RGB = 0x80C9, BLEND_DST_RGB = 0x80C8,
                 BLEND_SRC_ALPHA = 0x80CB, BLEND_DST_ALPHA = 0x80CA, READ_BUFFER = 0x0C02, DRAW_BUFFER0 = 0x8825,
                 UNPACK_ALIGNMENT = 0x0CF5;
constexpr GLenum COLOR_CLEAR_VALUE = 0x0C22, BLEND_EQUATION_RGB = 0x8009, FUNC_ADD = 0x8006;
constexpr GLenum TEXTURE_2D_ARRAY = 0x8C1A, TEXTURE_BINDING_2D_ARRAY = 0x8C1D, TEXTURE_DEPTH = 0x8071,
                 TEXTURE_COMPARE_MODE = 0x884C, TEXTURE_COMPARE_FUNC = 0x884D, TEXTURE_BASE_LEVEL = 0x813C,
                 TEXTURE_TARGET = 0x1006, TEXTURE_IMMUTABLE_FORMAT = 0x912F;
constexpr GLenum COPY_WRITE_BUFFER = 0x8F37, STENCIL_WRITEMASK = 0x0B98, DRAW_BUFFER_ = 0x0C01, COLOR = 0x1800,
                 DEPTH = 0x1801, STENCIL = 0x1802, DEPTH_STENCIL = 0x84F9, UNSIGNED_INT_24_8 = 0x84FA,
                 DEPTH_COMPONENT = 0x1902, RED_INTEGER = 0x8D94, FLOAT_32_UNSIGNED_INT_24_8_REV = 0x8DAD,
                 FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE = 0x8CD3, MAX_COLOR_ATTACHMENTS = 0x8CDF,
                 TEXTURE_CUBE_MAP_POSITIVE_X = 0x8515, BLEND_DST = 0x0BE0;
constexpr GLenum VENDOR = 0x1F00, RENDERER = 0x1F01, VERSION = 0x1F02, SHADING_LANGUAGE_VERSION = 0x8B8C;
}  // namespace gl

// X(return type, name, parameters)
#define HVR_GL_FUNCTIONS(X)                                                                                  \
    X(GLenum, glGetError, ())                                                                                \
    X(const GLubyte*, glGetString, (GLenum name))                                                            \
    X(void, glGetIntegerv, (GLenum pname, GLint * data))                                                     \
    X(void, glGetFloatv, (GLenum pname, GLfloat * data))                                                     \
    X(void, glGetIntegeri_v, (GLenum target, GLuint index, GLint * data))                                    \
    X(GLboolean, glIsEnabled, (GLenum cap))                                                                  \
    X(void, glEnable, (GLenum cap))                                                                          \
    X(void, glDisable, (GLenum cap))                                                                         \
    X(void, glViewport, (GLint x, GLint y, GLsizei width, GLsizei height))                                   \
    X(void, glScissor, (GLint x, GLint y, GLsizei width, GLsizei height))                                    \
    X(void, glClearColor, (GLfloat r, GLfloat g, GLfloat b, GLfloat a))                                      \
    X(void, glClear, (GLbitfield mask))                                                                      \
    X(void, glColorMask, (GLboolean r, GLboolean g, GLboolean b, GLboolean a))                               \
    X(void, glDepthMask, (GLboolean flag))                                                                   \
    X(void, glBlendFunc, (GLenum sfactor, GLenum dfactor))                                                   \
    X(void, glBlendFuncSeparate, (GLenum srgb, GLenum drgb, GLenum sa, GLenum da))                           \
    X(void, glBlendEquation, (GLenum mode))                                                                  \
    X(void, glPixelStorei, (GLenum pname, GLint param))                                                      \
    X(void, glReadPixels, (GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, void* data))     \
    X(void, glGenTextures, (GLsizei n, GLuint * textures))                                                   \
    X(void, glDeleteTextures, (GLsizei n, const GLuint* textures))                                           \
    X(void, glBindTexture, (GLenum target, GLuint texture))                                                  \
    X(void, glActiveTexture, (GLenum texture))                                                               \
    X(void, glTexImage2D, (GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height,  \
                           GLint border, GLenum format, GLenum type, const void* pixels))                    \
    X(void, glTexParameteri, (GLenum target, GLenum pname, GLint param))                                     \
    X(void, glTexParameterf, (GLenum target, GLenum pname, GLfloat param))                                   \
    X(void, glGenerateMipmap, (GLenum target))                                                               \
    X(void, glBindSampler, (GLuint unit, GLuint sampler))                                                    \
    X(void, glGenFramebuffers, (GLsizei n, GLuint * ids))                                                    \
    X(void, glDeleteFramebuffers, (GLsizei n, const GLuint* ids))                                            \
    X(void, glBindFramebuffer, (GLenum target, GLuint framebuffer))                                          \
    X(void, glFramebufferTexture2D, (GLenum target, GLenum attachment, GLenum textarget, GLuint texture,     \
                                     GLint level))                                                           \
    X(void, glGenRenderbuffers, (GLsizei n, GLuint * ids))                                                   \
    X(void, glBindRenderbuffer, (GLenum target, GLuint renderbuffer))                                        \
    X(void, glDeleteRenderbuffers, (GLsizei n, const GLuint* ids))                                          \
    X(void, glRenderbufferStorage, (GLenum target, GLenum format, GLsizei width, GLsizei height))            \
    X(void, glFramebufferRenderbuffer, (GLenum target, GLenum attachment, GLenum rbtarget, GLuint rb))       \
    X(GLenum, glCheckFramebufferStatus, (GLenum target))                                                     \
    X(void, glBlitFramebuffer, (GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0, GLint dx1, \
                                GLint dy1, GLbitfield mask, GLenum filter))                                  \
    X(void, glReadBuffer, (GLenum mode))                                                                     \
    X(void, glDrawBuffer, (GLenum mode))                                                                     \
    X(void, glGetFramebufferAttachmentParameteriv, (GLenum target, GLenum attachment, GLenum pname,          \
                                                    GLint * params))                                         \
    X(GLuint, glCreateShader, (GLenum type))                                                                 \
    X(void, glDeleteShader, (GLuint shader))                                                                 \
    X(void, glShaderSource, (GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length)) \
    X(void, glCompileShader, (GLuint shader))                                                                \
    X(void, glGetShaderiv, (GLuint shader, GLenum pname, GLint * params))                                    \
    X(void, glGetShaderInfoLog, (GLuint shader, GLsizei bufSize, GLsizei * length, GLchar * log))            \
    X(GLuint, glCreateProgram, ())                                                                           \
    X(void, glAttachShader, (GLuint program, GLuint shader))                                                 \
    X(void, glLinkProgram, (GLuint program))                                                                 \
    X(void, glGetProgramiv, (GLuint program, GLenum pname, GLint * params))                                  \
    X(void, glGetProgramInfoLog, (GLuint program, GLsizei bufSize, GLsizei * length, GLchar * log))          \
    X(void, glUseProgram, (GLuint program))                                                                  \
    X(void, glGetAttachedShaders, (GLuint program, GLsizei max, GLsizei * count, GLuint * shaders))          \
    X(GLint, glGetUniformLocation, (GLuint program, const GLchar* name))                                     \
    X(void, glGetActiveUniform, (GLuint program, GLuint index, GLsizei bufSize, GLsizei * length,            \
                                 GLint * size, GLenum * type, GLchar * name))                                \
    X(void, glGetActiveAttrib, (GLuint program, GLuint index, GLsizei bufSize, GLsizei * length,             \
                                GLint * size, GLenum * type, GLchar * name))                                 \
    X(void, glGetActiveUniformBlockiv, (GLuint program, GLuint index, GLenum pname, GLint * params))         \
    X(void, glGetActiveUniformBlockName, (GLuint program, GLuint index, GLsizei bufSize, GLsizei * length,   \
                                          GLchar * name))                                                    \
    X(void, glGetUniformfv, (GLuint program, GLint location, GLfloat * params))                              \
    X(void, glUniform1i, (GLint location, GLint v0))                                                         \
    X(void, glUniform1f, (GLint location, GLfloat v0))                                                       \
    X(void, glUniform2f, (GLint location, GLfloat v0, GLfloat v1))                                           \
    X(void, glUniform3f, (GLint location, GLfloat v0, GLfloat v1, GLfloat v2))                               \
    X(void, glUniform4f, (GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3))                   \
    X(void, glUniformMatrix4fv, (GLint location, GLsizei count, GLboolean transpose, const GLfloat* value))  \
    X(void, glGenVertexArrays, (GLsizei n, GLuint * arrays))                                                 \
    X(void, glBindVertexArray, (GLuint array))                                                               \
    X(void, glGenBuffers, (GLsizei n, GLuint * buffers))                                                     \
    X(void, glBindBuffer, (GLenum target, GLuint buffer))                                                    \
    X(void, glBufferData, (GLenum target, GLsizeiptr size, const void* data, GLenum usage))                  \
    X(void, glVertexAttribPointer, (GLuint index, GLint size, GLenum type, GLboolean normalized,             \
                                    GLsizei stride, const void* pointer))                                    \
    X(void, glEnableVertexAttribArray, (GLuint index))                                                       \
    X(void, glDrawArrays, (GLenum mode, GLint first, GLsizei count))                                       \
    X(void, glDeleteBuffers, (GLsizei n, const GLuint* buffers))                                             \
    X(void, glBindBufferBase, (GLenum target, GLuint index, GLuint buffer))                                  \
    X(void, glBindBufferRange, (GLenum target, GLuint index, GLuint buffer, GLintptr offset,                 \
                                GLsizeiptr size))                                                            \
    X(void, glBufferSubData, (GLenum target, GLintptr offset, GLsizeiptr size, const void* data))            \
    X(void, glGetBufferSubData, (GLenum target, GLintptr offset, GLsizeiptr size, void* data))               \
    X(void, glCopyBufferSubData, (GLenum read, GLenum write, GLintptr read_offset, GLintptr write_offset,    \
                                  GLsizeiptr size))                                                          \
    X(void, glUniform3fv, (GLint location, GLsizei count, const GLfloat* value))                             \
    X(void, glDrawBuffers, (GLsizei n, const GLenum* bufs))                                                  \
    X(void, glGetTexLevelParameteriv, (GLenum target, GLint level, GLenum pname, GLint * params))            \
    X(void, glGetTexParameteriv, (GLenum target, GLenum pname, GLint * params))                              \
    X(void, glRenderbufferStorageMultisample, (GLenum target, GLsizei samples, GLenum format, GLsizei width, \
                                               GLsizei height))                                              \
    X(void, glClearBufferfv, (GLenum buffer, GLint drawbuffer, const GLfloat* value))                        \
    X(void, glClearBufferiv, (GLenum buffer, GLint drawbuffer, const GLint* value))                          \
    X(void, glClearBufferuiv, (GLenum buffer, GLint drawbuffer, const GLuint* value))                        \
    X(void, glClearBufferfi, (GLenum buffer, GLint drawbuffer, GLfloat depth, GLint stencil))                \
    X(void, glStencilMask, (GLuint mask))                                                                    \
    X(void, glGetBooleanv, (GLenum pname, GLboolean * data))                                                 \
    X(void, glTexImage3D, (GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height,  \
                           GLsizei depth, GLint border, GLenum format, GLenum type, const void* pixels))

// Used when the driver has them.
#define HVR_GL_OPTIONAL(X)                                                                                   \
    X(void, glGetNamedBufferSubData, (GLuint buffer, GLintptr offset, GLsizeiptr size, void* data))          \
    X(void, glGetObjectLabel, (GLenum identifier, GLuint name, GLsizei bufSize, GLsizei * length,            \
                               GLchar * label))                                                              \
    X(void, glGetNamedFramebufferAttachmentParameteriv, (GLuint fb, GLenum attachment, GLenum pname,         \
                                                         GLint * params))                                    \
    X(void, glGetTextureLevelParameteriv, (GLuint texture, GLint level, GLenum pname, GLint * params))       \
    X(void, glGetTextureParameteriv, (GLuint texture, GLenum pname, GLint * params))                         \
    X(void, glGetNamedRenderbufferParameteriv, (GLuint rb, GLenum pname, GLint * params))                    \
    X(void, glGetInteger64i_v, (GLenum target, GLuint index, long long* data))                               \
    X(void, glTexStorage2D, (GLenum target, GLsizei levels, GLenum format, GLsizei width, GLsizei height))   \
    X(void, glTexStorage3D, (GLenum target, GLsizei levels, GLenum format, GLsizei width, GLsizei height,    \
                             GLsizei depth))                                                                 \
    X(void, glBindImageTexture, (GLuint unit, GLuint texture, GLint level, GLboolean layered, GLint layer,   \
                                 GLenum access, GLenum format))

struct GlApi {
#define HVR_GL_DECLARE(ret, name, params) ret(APIENTRY* name) params = nullptr;
    HVR_GL_FUNCTIONS(HVR_GL_DECLARE)
    HVR_GL_OPTIONAL(HVR_GL_DECLARE)
#undef HVR_GL_DECLARE

    bool ready = false;
    bool has_dsa = false;
};

// Our own GL calls. Valid after gl_api_init on the render thread.
extern GlApi glr;

namespace hvr {
// Resolves every function through the real driver. Needs a current context.
bool gl_api_init();
// The real driver's wglGetProcAddress with a fallback to its exports.
void* real_gl_proc(const char* name);
}  // namespace hvr
