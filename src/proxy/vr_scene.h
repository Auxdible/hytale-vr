// Draws the VR view in cinema mode: Hytale's window on a big screen in a dark
// room with a floor, plus the controllers and their laser pointers.
#pragma once

#include "../core/vrmath.h"
#include "gl_api.h"

namespace hvr {

struct HandView {
    bool valid = false;
    Mat4 pose;              // standing space
    bool laser = false;
    float laser_length = 0;
    bool hovering = false;  // laser is on the screen
};

struct SceneView {
    Screen screen;
    bool floor = true;
    Vec3 eye;
    HandView hands[2];
    bool dot = false;
    float dot_u = 0, dot_v = 0;
};

class EyeTarget {
public:
    bool resize(int width, int height);
    void destroy();
    GLuint fbo() const { return fbo_; }
    GLuint texture() const { return color_; }
    int width() const { return width_; }
    int height() const { return height_; }

private:
    GLuint fbo_ = 0, color_ = 0, depth_ = 0;
    int width_ = 0, height_ = 0;
};

class Scene {
public:
    bool init();
    void destroy();
    // Copies the game window's finished frame into the screen texture.
    void capture_window(int width, int height, GLuint source_fbo);  // source 0 = the window
    float screen_aspect() const { return height_ > 0 ? static_cast<float>(width_) / height_ : 16.0f / 9.0f; }
    void render(const EyeTarget& target, const Mat4& projection, const Mat4& view, const SceneView& scene);
    // Over an eye image already drawn by Hytale: the HUD panel (premultiplied
    // texture on scene.screen) and the controllers.
    void render_overlay(GLuint fbo, int width, int height, const Mat4& projection, const Mat4& view,
                        const SceneView& scene, GLuint hud_texture);

private:
    void draw_hands(const Mat4& vp, const SceneView& scene);
    void draw(int first, int count, const Mat4& view_projection, const Mat4& model, int mode, const float color[4]);

    GLuint program_ = 0, vao_ = 0, vbo_ = 0;
    GLint u_mvp_ = -1, u_model_ = -1, u_mode_ = -1, u_color_ = -1, u_eye_ = -1, u_tex_ = -1, u_size_ = -1;
    GLuint screen_fbo_ = 0, screen_texture_ = 0;
    int width_ = 0, height_ = 0;
    Vec3 eye_;
};

}  // namespace hvr
