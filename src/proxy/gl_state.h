// Saves the GL state HytaleVR touches when it draws at the end of a frame,
// and puts it back so Hytale's next frame starts exactly as it left off.
#pragma once

#include "gl_api.h"

namespace hvr {

class GlStateGuard {
public:
    GlStateGuard() {
        glr.glGetIntegerv(gl::DRAW_FRAMEBUFFER_BINDING, &draw_fb_);
        glr.glGetIntegerv(gl::READ_FRAMEBUFFER_BINDING, &read_fb_);
        glr.glGetIntegerv(gl::CURRENT_PROGRAM, &program_);
        glr.glGetIntegerv(gl::VERTEX_ARRAY_BINDING, &vao_);
        glr.glGetIntegerv(gl::ARRAY_BUFFER_BINDING, &array_buffer_);
        glr.glGetIntegerv(gl::ACTIVE_TEXTURE, &active_texture_);
        glr.glActiveTexture(gl::TEXTURE0);
        glr.glGetIntegerv(gl::TEXTURE_BINDING_2D, &texture_);
        glr.glGetIntegerv(gl::SAMPLER_BINDING, &sampler_);
        glr.glGetIntegerv(gl::VIEWPORT, viewport_);
        glr.glGetIntegerv(gl::SCISSOR_BOX, scissor_);
        glr.glGetIntegerv(gl::COLOR_WRITEMASK, color_mask_);
        glr.glGetIntegerv(gl::DEPTH_WRITEMASK, &depth_mask_);
        glr.glGetIntegerv(gl::BLEND_SRC_RGB, &blend_[0]);
        glr.glGetIntegerv(gl::BLEND_DST_RGB, &blend_[1]);
        glr.glGetIntegerv(gl::BLEND_SRC_ALPHA, &blend_[2]);
        glr.glGetIntegerv(gl::BLEND_DST_ALPHA, &blend_[3]);
        glr.glGetIntegerv(gl::BLEND_EQUATION_RGB, &blend_equation_);
        glr.glGetFloatv(gl::COLOR_CLEAR_VALUE, clear_color_);
        for (int i = 0; i < kCaps; ++i) enabled_[i] = glr.glIsEnabled(kCapList[i]);
    }

    ~GlStateGuard() {
        for (int i = 0; i < kCaps; ++i) (enabled_[i] ? glr.glEnable : glr.glDisable)(kCapList[i]);
        glr.glClearColor(clear_color_[0], clear_color_[1], clear_color_[2], clear_color_[3]);
        glr.glBlendEquation(static_cast<GLenum>(blend_equation_));
        glr.glBlendFuncSeparate(blend_[0], blend_[1], blend_[2], blend_[3]);
        glr.glDepthMask(static_cast<GLboolean>(depth_mask_));
        glr.glColorMask(static_cast<GLboolean>(color_mask_[0]), static_cast<GLboolean>(color_mask_[1]),
                        static_cast<GLboolean>(color_mask_[2]), static_cast<GLboolean>(color_mask_[3]));
        glr.glScissor(scissor_[0], scissor_[1], scissor_[2], scissor_[3]);
        glr.glViewport(viewport_[0], viewport_[1], viewport_[2], viewport_[3]);
        glr.glActiveTexture(gl::TEXTURE0);
        glr.glBindSampler(0, static_cast<GLuint>(sampler_));
        glr.glBindTexture(gl::TEXTURE_2D, static_cast<GLuint>(texture_));
        glr.glActiveTexture(static_cast<GLenum>(active_texture_));
        glr.glBindVertexArray(static_cast<GLuint>(vao_));
        glr.glBindBuffer(gl::ARRAY_BUFFER, static_cast<GLuint>(array_buffer_));
        glr.glUseProgram(static_cast<GLuint>(program_));
        glr.glBindFramebuffer(gl::READ_FRAMEBUFFER, static_cast<GLuint>(read_fb_));
        glr.glBindFramebuffer(gl::DRAW_FRAMEBUFFER, static_cast<GLuint>(draw_fb_));
    }

    GlStateGuard(const GlStateGuard&) = delete;
    GlStateGuard& operator=(const GlStateGuard&) = delete;

private:
    static constexpr int kCaps = 6;
    static constexpr GLenum kCapList[kCaps] = {gl::BLEND, gl::CULL_FACE, gl::DEPTH_TEST,
                                               gl::SCISSOR_TEST, gl::STENCIL_TEST, gl::FRAMEBUFFER_SRGB};
    GLint draw_fb_ = 0, read_fb_ = 0, program_ = 0, vao_ = 0, array_buffer_ = 0, active_texture_ = 0, texture_ = 0,
          sampler_ = 0, depth_mask_ = 1, blend_equation_ = 0;
    GLint viewport_[4] = {}, scissor_[4] = {}, color_mask_[4] = {1, 1, 1, 1}, blend_[4] = {};
    GLfloat clear_color_[4] = {};
    GLboolean enabled_[kCaps] = {};
};

}  // namespace hvr
