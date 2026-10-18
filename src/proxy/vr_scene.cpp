#include "vr_scene.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "avatar.h"
#include "log.h"

namespace hvr {
namespace {

const char* kVertex = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUv;
uniform mat4 uMvp;
uniform mat4 uModel;
uniform vec4 uUvRect;
out vec2 vUv;
out vec3 vWorld;
void main() {
    vUv = uUvRect.xy + aUv * uUvRect.zw;
    vWorld = (uModel * vec4(aPos, 1.0)).xyz;
    gl_Position = uMvp * vec4(aPos, 1.0);
}
)";

// Modes: 0 screen, 1 solid, 2 floor, 3 sky, 4 screen glow, 5 pointer dot, 6 HUD (premultiplied),
// 7 vignette (vWorld.xy is the offset from the view's center in clip units), 8 character skin.
const char* kFragment = R"(#version 330 core
in vec2 vUv;
in vec3 vWorld;
uniform int uMode;
uniform vec4 uColor;
uniform vec3 uEye;
uniform vec2 uSize;
uniform float uLight;
uniform sampler2D uTex;
out vec4 oColor;

float roundedBox(vec2 p, vec2 half, float r) {
    vec2 q = abs(p) - half + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

void main() {
    if (uMode == 0) {
        vec2 p = (vUv - 0.5) * uSize;
        float d = roundedBox(p, uSize * 0.5, 0.03);
        float edge = 1.0 - smoothstep(-0.004, 0.0, d);
        oColor = vec4(texture(uTex, vUv).rgb, edge);
    } else if (uMode == 1) {
        oColor = uColor;
    } else if (uMode == 2) {
        vec2 cell = vWorld.xz;
        vec2 grid = abs(fract(cell - 0.5) - 0.5) / fwidth(cell);
        float line = 1.0 - min(min(grid.x, grid.y), 1.0);
        float fade = exp(-0.09 * length(vWorld.xz - uEye.xz));
        vec3 base = vec3(0.035, 0.04, 0.05);
        vec3 color = mix(base, vec3(0.20, 0.24, 0.30), line * 0.6);
        oColor = vec4(color, fade * 0.95);
    } else if (uMode == 3) {
        vec3 dir = normalize(vWorld - uEye);
        float up = smoothstep(-0.25, 0.8, dir.y);
        vec3 horizon = vec3(0.045, 0.05, 0.065);
        vec3 zenith = vec3(0.008, 0.010, 0.016);
        oColor = vec4(mix(horizon, zenith, up), 1.0);
    } else if (uMode == 4) {
        vec2 p = (vUv - 0.5) * uSize;
        float d = roundedBox(p, uSize * 0.5 / 1.25, 0.05);
        float glow = exp(-max(d, 0.0) * 5.0) * 0.22;
        oColor = vec4(uColor.rgb, glow);
    } else if (uMode == 6) {
        oColor = texture(uTex, vUv);
    } else if (uMode == 8) {
        vec4 skin = texture(uTex, vUv);
        if (skin.a < 0.5) discard;
        vec3 n = normalize(cross(dFdx(vWorld), dFdy(vWorld)));
        float light = 0.62 + 0.38 * abs(dot(n, normalize(vec3(0.35, 1.0, 0.45))));
        oColor = vec4(skin.rgb * light * uLight, 1.0);
    } else if (uMode == 7) {
        float r = length(vWorld.xy);
        oColor = vec4(0.0, 0.0, 0.0, uColor.a * smoothstep(0.45, 1.05, r));
    } else {
        float r = length(vUv - 0.5) * 2.0;
        float ring = smoothstep(1.0, 0.8, r);
        oColor = vec4(uColor.rgb, uColor.a * ring);
    }
}
)";

struct Vertex {
    float x, y, z, u, v;
};

// Offsets into the vertex buffer.
constexpr int kQuadFirst = 0, kQuadCount = 6;
constexpr int kFloorFirst = 6, kFloorCount = 6;
constexpr int kCubeFirst = 12, kCubeCount = 36;

std::vector<Vertex> build_geometry() {
    std::vector<Vertex> v;
    // Unit quad in XY, facing +Z, uv origin bottom left.
    const Vertex quad[6] = {{-0.5f, -0.5f, 0, 0, 0}, {0.5f, -0.5f, 0, 1, 0}, {0.5f, 0.5f, 0, 1, 1},
                            {-0.5f, -0.5f, 0, 0, 0}, {0.5f, 0.5f, 0, 1, 1},  {-0.5f, 0.5f, 0, 0, 1}};
    v.insert(v.end(), quad, quad + 6);
    // Floor at y = 0, 60 m across.
    const float f = 30.0f;
    const Vertex floor[6] = {{-f, 0, -f, 0, 0}, {-f, 0, f, 0, 1}, {f, 0, f, 1, 1},
                             {-f, 0, -f, 0, 0}, {f, 0, f, 1, 1},  {f, 0, -f, 1, 0}};
    v.insert(v.end(), floor, floor + 6);
    // Unit cube.
    const float c[8][3] = {{-.5f, -.5f, -.5f}, {.5f, -.5f, -.5f}, {.5f, .5f, -.5f}, {-.5f, .5f, -.5f},
                           {-.5f, -.5f, .5f},  {.5f, -.5f, .5f},  {.5f, .5f, .5f},  {-.5f, .5f, .5f}};
    const int faces[6][4] = {{4, 5, 6, 7}, {1, 0, 3, 2}, {5, 1, 2, 6}, {0, 4, 7, 3}, {7, 6, 2, 3}, {0, 1, 5, 4}};
    for (const auto& face : faces) {
        const int order[6] = {0, 1, 2, 0, 2, 3};
        for (int i : order) v.push_back({c[face[i]][0], c[face[i]][1], c[face[i]][2], 0, 0});
    }
    return v;
}

GLuint compile(GLenum type, const char* source) {
    GLuint shader = glr.glCreateShader(type);
    glr.glShaderSource(shader, 1, &source, nullptr);
    glr.glCompileShader(shader);
    GLint ok = 0;
    glr.glGetShaderiv(shader, gl::COMPILE_STATUS, &ok);
    if (!ok) {
        char text[1024] = {};
        glr.glGetShaderInfoLog(shader, sizeof(text), nullptr, text);
        logf("VR scene shader failed: %s", text);
        glr.glDeleteShader(shader);
        return 0;
    }
    return shader;
}

// Rotation that points local -Z along a direction, plus translation.
Mat4 aim(Vec3 position, Vec3 forward) {
    const Vec3 back = normalize(forward * -1.0f);
    Vec3 up{0, 1, 0};
    if (std::fabs(dot(up, back)) > 0.98f) up = {1, 0, 0};
    const Vec3 right = normalize(cross(up, back));
    const Vec3 true_up = cross(back, right);
    Mat4 m;
    m.at(0, 0) = right.x, m.at(1, 0) = right.y, m.at(2, 0) = right.z;
    m.at(0, 1) = true_up.x, m.at(1, 1) = true_up.y, m.at(2, 1) = true_up.z;
    m.at(0, 2) = back.x, m.at(1, 2) = back.y, m.at(2, 2) = back.z;
    m.at(0, 3) = position.x, m.at(1, 3) = position.y, m.at(2, 3) = position.z;
    return m;
}

}  // namespace

bool EyeTarget::resize(int width, int height) {
    if (fbo_ && width == width_ && height == height_) return true;
    // Keep the framebuffer's name: Hytale may hold on to it (the window
    // buffer), so only its images are replaced.
    GLuint keep = fbo_;
    fbo_ = 0;
    destroy();
    fbo_ = keep;
    width_ = width;
    height_ = height;
    glr.glGenTextures(1, &color_);
    glr.glBindTexture(gl::TEXTURE_2D, color_);
    glr.glTexImage2D(gl::TEXTURE_2D, 0, static_cast<GLint>(gl::RGBA8), width, height, 0, gl::RGBA, gl::UNSIGNED_BYTE, nullptr);
    glr.glTexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, static_cast<GLint>(gl::LINEAR));
    glr.glTexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, static_cast<GLint>(gl::LINEAR));
    glr.glTexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAX_LEVEL, 0);
    glr.glGenRenderbuffers(1, &depth_);
    glr.glBindRenderbuffer(gl::RENDERBUFFER, depth_);
    glr.glRenderbufferStorage(gl::RENDERBUFFER, gl::DEPTH24_STENCIL8, width, height);
    if (!fbo_) glr.glGenFramebuffers(1, &fbo_);
    glr.glBindFramebuffer(gl::FRAMEBUFFER, fbo_);
    glr.glFramebufferTexture2D(gl::FRAMEBUFFER, gl::COLOR_ATTACHMENT0, gl::TEXTURE_2D, color_, 0);
    glr.glFramebufferRenderbuffer(gl::FRAMEBUFFER, gl::DEPTH_STENCIL_ATTACHMENT, gl::RENDERBUFFER, depth_);
    const bool complete = glr.glCheckFramebufferStatus(gl::FRAMEBUFFER) == gl::FRAMEBUFFER_COMPLETE;
    if (!complete) logf("Eye render target %dx%d is incomplete", width, height);
    return complete;
}

void EyeTarget::destroy() {
    if (fbo_) glr.glDeleteFramebuffers(1, &fbo_);
    if (color_) glr.glDeleteTextures(1, &color_);
    if (depth_) glr.glDeleteRenderbuffers(1, &depth_);
    fbo_ = color_ = depth_ = 0;
    width_ = height_ = 0;
}

bool Scene::init() {
    if (program_) return true;
    GLuint vs = compile(gl::VERTEX_SHADER, kVertex);
    GLuint fs = compile(gl::FRAGMENT_SHADER, kFragment);
    if (!vs || !fs) return false;
    program_ = glr.glCreateProgram();
    glr.glAttachShader(program_, vs);
    glr.glAttachShader(program_, fs);
    glr.glLinkProgram(program_);
    glr.glDeleteShader(vs);
    glr.glDeleteShader(fs);
    GLint ok = 0;
    glr.glGetProgramiv(program_, gl::LINK_STATUS, &ok);
    if (!ok) {
        logf("VR scene program failed to link");
        program_ = 0;
        return false;
    }
    u_mvp_ = glr.glGetUniformLocation(program_, "uMvp");
    u_model_ = glr.glGetUniformLocation(program_, "uModel");
    u_mode_ = glr.glGetUniformLocation(program_, "uMode");
    u_color_ = glr.glGetUniformLocation(program_, "uColor");
    u_eye_ = glr.glGetUniformLocation(program_, "uEye");
    u_tex_ = glr.glGetUniformLocation(program_, "uTex");
    u_size_ = glr.glGetUniformLocation(program_, "uSize");
    u_uv_ = glr.glGetUniformLocation(program_, "uUvRect");
    u_light_ = glr.glGetUniformLocation(program_, "uLight");

    const std::vector<Vertex> vertices = build_geometry();
    glr.glGenVertexArrays(1, &vao_);
    glr.glBindVertexArray(vao_);
    glr.glGenBuffers(1, &vbo_);
    glr.glBindBuffer(gl::ARRAY_BUFFER, vbo_);
    glr.glBufferData(gl::ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)), vertices.data(),
                     0x88E4 /* STATIC_DRAW */);
    glr.glEnableVertexAttribArray(0);
    glr.glVertexAttribPointer(0, 3, gl::FLOAT, gl::FALSE_, sizeof(Vertex), nullptr);
    glr.glEnableVertexAttribArray(1);
    glr.glVertexAttribPointer(1, 2, gl::FLOAT, gl::FALSE_, sizeof(Vertex), reinterpret_cast<void*>(3 * sizeof(float)));
    glr.glBindVertexArray(0);
    avatar::start_loading();
    return true;
}

// Your character's arms go to the GPU once they've loaded.
bool Scene::upload_arms() {
    const avatar::Model* model = avatar::ready();
    if (arm_vao_ && model == arm_model_) return true;
    if (!model) return false;
    if (arm_vao_) {
        // A new model (you changed your avatar): replace the old one.
        glr.glDeleteVertexArrays(1, &arm_vao_);
        glr.glDeleteBuffers(1, &arm_vbo_);
        glr.glDeleteTextures(1, &arm_texture_);
        arm_vao_ = arm_vbo_ = arm_texture_ = 0;
    }
    arm_model_ = model;
    std::vector<avatar::Vertex> vertices;
    for (int side = 0; side < 2; ++side) {
        arm_first_[side] = static_cast<int>(vertices.size());
        arm_count_[side] = static_cast<int>(model->arms[side].vertices.size());
        vertices.insert(vertices.end(), model->arms[side].vertices.begin(), model->arms[side].vertices.end());
    }
    glr.glGenTextures(1, &arm_texture_);
    glr.glBindTexture(gl::TEXTURE_2D, arm_texture_);
    glr.glTexImage2D(gl::TEXTURE_2D, 0, static_cast<GLint>(gl::RGBA8), static_cast<GLsizei>(model->width),
                     static_cast<GLsizei>(model->height), 0, gl::RGBA, gl::UNSIGNED_BYTE, model->rgba.data());
    glr.glTexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, static_cast<GLint>(gl::NEAREST));
    glr.glTexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, static_cast<GLint>(gl::NEAREST));
    glr.glTexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, static_cast<GLint>(gl::CLAMP_TO_EDGE));
    glr.glTexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, static_cast<GLint>(gl::CLAMP_TO_EDGE));
    glr.glTexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAX_LEVEL, 0);
    glr.glGenVertexArrays(1, &arm_vao_);
    glr.glBindVertexArray(arm_vao_);
    glr.glGenBuffers(1, &arm_vbo_);
    glr.glBindBuffer(gl::ARRAY_BUFFER, arm_vbo_);
    glr.glBufferData(gl::ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(avatar::Vertex)),
                     vertices.data(), 0x88E4 /* STATIC_DRAW */);
    glr.glEnableVertexAttribArray(0);
    glr.glVertexAttribPointer(0, 3, gl::FLOAT, gl::FALSE_, sizeof(avatar::Vertex), nullptr);
    glr.glEnableVertexAttribArray(1);
    glr.glVertexAttribPointer(1, 2, gl::FLOAT, gl::FALSE_, sizeof(avatar::Vertex),
                              reinterpret_cast<void*>(3 * sizeof(float)));
    glr.glBindVertexArray(vao_);
    logf("Character arms ready: %d and %d triangles", arm_count_[0] / 3, arm_count_[1] / 3);
    return true;
}

void Scene::destroy() {
    if (screen_fbo_) glr.glDeleteFramebuffers(1, &screen_fbo_);
    if (screen_texture_) glr.glDeleteTextures(1, &screen_texture_);
    screen_fbo_ = screen_texture_ = 0;
    width_ = height_ = 0;
    program_ = vao_ = vbo_ = 0;  // freed with the context
    arm_vao_ = arm_vbo_ = arm_texture_ = 0;
    arm_model_ = nullptr;
    light_fbo_ = light_texture_ = light_pbo_ = 0;
    light_pending_ = false;
}

void Scene::capture_window(int width, int height, GLuint source_fbo) {
    if (width <= 0 || height <= 0) return;
    if (!screen_fbo_ || width != width_ || height != height_) {
        if (screen_fbo_) glr.glDeleteFramebuffers(1, &screen_fbo_);
        if (screen_texture_) glr.glDeleteTextures(1, &screen_texture_);
        width_ = width;
        height_ = height;
        glr.glGenTextures(1, &screen_texture_);
        glr.glBindTexture(gl::TEXTURE_2D, screen_texture_);
        int levels = 1;
        for (int size = width > height ? width : height; size > 1; size /= 2) ++levels;
        for (int level = 0, w = width, h = height; level < levels; ++level, w = w > 1 ? w / 2 : 1, h = h > 1 ? h / 2 : 1)
            glr.glTexImage2D(gl::TEXTURE_2D, level, static_cast<GLint>(gl::RGBA8), w, h, 0, gl::RGBA, gl::UNSIGNED_BYTE, nullptr);
        glr.glTexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, static_cast<GLint>(gl::LINEAR_MIPMAP_LINEAR));
        glr.glTexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, static_cast<GLint>(gl::LINEAR));
        glr.glTexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, static_cast<GLint>(gl::CLAMP_TO_EDGE));
        glr.glTexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, static_cast<GLint>(gl::CLAMP_TO_EDGE));
        glr.glTexParameterf(gl::TEXTURE_2D, gl::TEXTURE_MAX_ANISOTROPY, 8.0f);
        while (glr.glGetError() != 0) {
        }
        glr.glGenFramebuffers(1, &screen_fbo_);
        glr.glBindFramebuffer(gl::DRAW_FRAMEBUFFER, screen_fbo_);
        glr.glFramebufferTexture2D(gl::DRAW_FRAMEBUFFER, gl::COLOR_ATTACHMENT0, gl::TEXTURE_2D, screen_texture_, 0);
        logf("Screen texture %dx%d", width, height);
    }
    glr.glBindFramebuffer(gl::READ_FRAMEBUFFER, source_fbo);
    glr.glBindFramebuffer(gl::DRAW_FRAMEBUFFER, screen_fbo_);
    glr.glDisable(gl::SCISSOR_TEST);
    glr.glDisable(gl::FRAMEBUFFER_SRGB);
    glr.glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, gl::COLOR_BUFFER_BIT, gl::NEAREST);
    glr.glBindTexture(gl::TEXTURE_2D, screen_texture_);
    glr.glGenerateMipmap(gl::TEXTURE_2D);
}

float Scene::measure_brightness(GLuint fbo, int width, int height) {
    constexpr int kSize = 32;
    if (!fbo || width <= 0 || height <= 0) return -1;
    if (!light_fbo_) {
        glr.glGenTextures(1, &light_texture_);
        glr.glBindTexture(gl::TEXTURE_2D, light_texture_);
        glr.glTexImage2D(gl::TEXTURE_2D, 0, static_cast<GLint>(gl::RGBA8), kSize, kSize, 0, gl::RGBA, gl::UNSIGNED_BYTE,
                         nullptr);
        glr.glTexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, static_cast<GLint>(gl::LINEAR));
        glr.glTexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAX_LEVEL, 0);
        glr.glGenFramebuffers(1, &light_fbo_);
        glr.glBindFramebuffer(gl::DRAW_FRAMEBUFFER, light_fbo_);
        glr.glFramebufferTexture2D(gl::DRAW_FRAMEBUFFER, gl::COLOR_ATTACHMENT0, gl::TEXTURE_2D, light_texture_, 0);
        glr.glGenBuffers(1, &light_pbo_);
        glr.glBindBuffer(gl::PIXEL_PACK_BUFFER, light_pbo_);
        glr.glBufferData(gl::PIXEL_PACK_BUFFER, kSize * kSize * 4, nullptr, gl::STREAM_READ);
        glr.glBindBuffer(gl::PIXEL_PACK_BUFFER, 0);
    }
    GLint pack = 0;
    glr.glGetIntegerv(gl::PIXEL_PACK_BUFFER_BINDING, &pack);
    glr.glBindBuffer(gl::PIXEL_PACK_BUFFER, light_pbo_);
    float brightness = -1;
    if (light_pending_) {
        static uint8_t pixels[kSize * kSize * 4];
        glr.glGetBufferSubData(gl::PIXEL_PACK_BUFFER, 0, sizeof(pixels), pixels);
        double sum = 0;
        for (int i = 0; i < kSize * kSize; ++i)
            sum += 0.2126 * pixels[i * 4] + 0.7152 * pixels[i * 4 + 1] + 0.0722 * pixels[i * 4 + 2];
        brightness = static_cast<float>(sum / (kSize * kSize * 255.0));
    }
    // The middle of the view, where you're looking, shrunk down for next frame.
    glr.glBindFramebuffer(gl::READ_FRAMEBUFFER, fbo);
    glr.glBindFramebuffer(gl::DRAW_FRAMEBUFFER, light_fbo_);
    glr.glDisable(gl::SCISSOR_TEST);
    glr.glBlitFramebuffer(width / 5, height / 5, width - width / 5, height - height / 5, 0, 0, kSize, kSize,
                          gl::COLOR_BUFFER_BIT, gl::LINEAR);
    glr.glBindFramebuffer(gl::READ_FRAMEBUFFER, light_fbo_);
    glr.glPixelStorei(0x0D05 /* PACK_ALIGNMENT */, 4);
    glr.glReadPixels(0, 0, kSize, kSize, gl::RGBA, gl::UNSIGNED_BYTE, nullptr);
    light_pending_ = true;
    glr.glBindBuffer(gl::PIXEL_PACK_BUFFER, static_cast<GLuint>(pack));
    return brightness;
}

void Scene::draw(int first, int count, const Mat4& view_projection, const Mat4& model, int mode, const float color[4]) {
    const Mat4 mvp = view_projection * model;
    glr.glUniformMatrix4fv(u_mvp_, 1, gl::FALSE_, mvp.m);
    glr.glUniformMatrix4fv(u_model_, 1, gl::FALSE_, model.m);
    glr.glUniform1i(u_mode_, mode);
    if (color) glr.glUniform4f(u_color_, color[0], color[1], color[2], color[3]);
    glr.glDrawArrays(gl::TRIANGLES, first, count);
}

void Scene::draw_hands(const Mat4& vp, const SceneView& scene) {
    const Screen& s = scene.screen;
    const Mat4 screen_frame = translation(s.center) * rotation_y(s.yaw);
    glr.glEnable(gl::DEPTH_TEST);
    glr.glDepthMask(1);
    glr.glEnable(gl::BLEND);
    glr.glBlendFuncSeparate(gl::SRC_ALPHA, gl::ONE_MINUS_SRC_ALPHA, gl::ONE, gl::ONE_MINUS_SRC_ALPHA);
    for (int side = 0; side < 2; ++side) {
        const HandView& hand = scene.hands[side];
        if (!hand.valid) continue;
        const Mat4 palm = hand.pose * translation(kPalm);
        // Lit up when it can take hold of a handle, brighter while it does.
        const float glow = 1.0f + 0.9f * hand.glow;
        glr.glUniform1f(u_light_, scene.light * glow);
        if (scene.character_arms && upload_arms() && arm_count_[side] > 0) {
            // Your character's forearm and hand, palm on the grip, arm back along the controller.
            GLint texture = 0;
            glr.glGetIntegerv(gl::TEXTURE_BINDING_2D, &texture);
            glr.glBindTexture(gl::TEXTURE_2D, arm_texture_);
            glr.glBindVertexArray(arm_vao_);
            const float k = scene.arm_scale;
            draw(arm_first_[side], arm_count_[side], vp, palm * hvr::scale({k, k, k}), 8, nullptr);
            glr.glBindVertexArray(vao_);
            glr.glBindTexture(gl::TEXTURE_2D, static_cast<GLuint>(texture));
        } else {
            // A blocky hand in Hytale's style, closed around where a held item
            // sits: fist, thumb and sleeve cuff, around the palm.
            const float l = scene.light * glow;
            const float skin[4] = {0.87f * l, 0.67f * l, 0.53f * l, 1.0f};
            const float thumb_skin[4] = {0.80f * l, 0.60f * l, 0.47f * l, 1.0f};
            const float sleeve[4] = {0.30f * l, 0.33f * l, 0.42f * l, 1.0f};
            const float mirror = side == 0 ? -1.0f : 1.0f;
            draw(kCubeFirst, kCubeCount, vp, palm * translation({0, -0.005f, 0}) * hvr::scale({0.075f, 0.085f, 0.085f}), 1,
                 skin);
            draw(kCubeFirst, kCubeCount, vp,
                 palm * translation({-0.042f * mirror, 0.02f, -0.02f}) * hvr::scale({0.022f, 0.03f, 0.045f}), 1, thumb_skin);
            draw(kCubeFirst, kCubeCount, vp, palm * translation({0, -0.005f, 0.07f}) * hvr::scale({0.07f, 0.08f, 0.06f}), 1,
                 sleeve);
        }
        if (!hand.laser) continue;
        const Vec3 origin = position_of(hand.pose);
        const Vec3 forward = normalize(transform_direction(hand.pose, {0, 0, -1}));
        const float length = hand.laser_length;
        const float beam[4] = {hand.hovering ? 1.0f : 0.75f, hand.hovering ? 0.78f : 0.82f, hand.hovering ? 0.3f : 0.95f,
                               hand.hovering ? 0.85f : 0.45f};
        draw(kCubeFirst, kCubeCount, vp,
             aim(origin + forward * (length * 0.5f), forward) * hvr::scale({0.003f, 0.003f, length}), 1, beam);
    }
    if (scene.target && scene.hands[scene.target_hand].valid) {
        // A faint line from the hand to what you'll interact with, and a mark on it.
        const Vec3 origin = position_of(scene.hands[scene.target_hand].pose * translation(kPalm + Vec3{0, 0, -0.06f}));
        const Vec3 to = scene.target_point - origin;
        const float length = hvr::length(to);
        if (length > 0.05f) {
            const Vec3 forward = to * (1.0f / length);
            const float beam[4] = {0.95f, 0.95f, 1.0f, 0.28f};
            const float mark[4] = {1.0f, 1.0f, 1.0f, 0.75f};
            glr.glDepthMask(0);
            draw(kCubeFirst, kCubeCount, vp,
                 aim(origin + forward * (length * 0.5f), forward) * hvr::scale({0.002f, 0.002f, length}), 1, beam);
            const float size = std::clamp(length * 0.012f, 0.006f, 0.04f);
            draw(kCubeFirst, kCubeCount, vp, aim(scene.target_point - forward * 0.01f, forward) * hvr::scale({size, size, size}),
                 1, mark);
            glr.glDepthMask(1);
        }
    }
    if (scene.dot) {
        const float dot_color[4] = {1.0f, 0.82f, 0.35f, 1.0f};
        const Vec3 local{(scene.dot_u - 0.5f) * s.width, (0.5f - scene.dot_v) * s.height, 0.004f};
        glr.glDisable(gl::DEPTH_TEST);
        draw(kQuadFirst, kQuadCount, vp, screen_frame * translation(local) * hvr::scale({0.03f, 0.03f, 1}), 5, dot_color);
    }
}

void Scene::render_overlay(GLuint fbo, int width, int height, const Mat4& projection, const Mat4& view,
                           const SceneView& scene, GLuint hud_texture, const HandDepth* depth) {
    if (!program_ || !fbo) return;
    glr.glBindFramebuffer(gl::DRAW_FRAMEBUFFER, fbo);
    glr.glViewport(0, 0, width, height);
    glr.glDisable(gl::SCISSOR_TEST);
    glr.glDisable(gl::STENCIL_TEST);
    glr.glDisable(gl::CULL_FACE);
    glr.glDisable(gl::FRAMEBUFFER_SRGB);
    glr.glColorMask(1, 1, 1, 1);
    glr.glDepthMask(1);
    glr.glClear(gl::DEPTH_BUFFER_BIT);
    glr.glUseProgram(program_);
    glr.glBindVertexArray(vao_);
    glr.glActiveTexture(gl::TEXTURE0);
    glr.glBindSampler(0, 0);
    glr.glUniform1i(u_tex_, 0);
    glr.glUniform4f(u_uv_, 0, 0, 1, 1);
    glr.glUniform4f(u_color_, 1, 1, 1, 1);
    glr.glUniform3f(u_eye_, scene.eye.x, scene.eye.y, scene.eye.z);
    glr.glBlendEquation(gl::FUNC_ADD);
    const Mat4 vp = projection * view;
    if (hud_texture) {
        glr.glBindTexture(gl::TEXTURE_2D, hud_texture);
        glr.glDisable(gl::DEPTH_TEST);
        glr.glDepthMask(0);
        glr.glEnable(gl::BLEND);
        glr.glBlendFuncSeparate(gl::ONE, gl::ONE_MINUS_SRC_ALPHA, gl::ONE, gl::ONE_MINUS_SRC_ALPHA);
        draw(kQuadFirst, kQuadCount, vp, scene.screen.model(), 6, nullptr);
        if (scene.hotbar) {
            const float* r = scene.hotbar_uv;
            glr.glUniform4f(u_uv_, r[0], r[1], r[2], r[3]);
            draw(kQuadFirst, kQuadCount, vp, scene.hotbar_model, 6, nullptr);
            glr.glUniform4f(u_uv_, 0, 0, 1, 1);
        }
    }
    // In Hytale's depth where it drew the held item, so the item and your
    // hand cover each other properly; else on top of everything.
    bool shared = false;
    if (depth && depth->fbo && depth_ok_) {
        for (int i = 0; i < 8 && glr.glGetError() != 0; ++i) {
        }
        glr.glBindFramebuffer(gl::READ_FRAMEBUFFER, depth->fbo);
        glr.glBlitFramebuffer(0, 0, depth->width, depth->height, 0, 0, width, height, gl::DEPTH_BUFFER_BIT, gl::NEAREST);
        const GLenum error = glr.glGetError();
        glr.glBindFramebuffer(gl::READ_FRAMEBUFFER, fbo);
        if (error) {
            depth_ok_ = false;
            logf("Hands: couldn't copy Hytale's depth (GL error 0x%x, %dx%d to %dx%d); hands stay on top", error,
                 depth->width, depth->height, width, height);
            glr.glDepthMask(1);
            glr.glClear(gl::DEPTH_BUFFER_BIT);
        } else {
            shared = true;
        }
    }
    GLint old_func = 0;
    if (shared) {
        glr.glGetIntegerv(gl::DEPTH_FUNC, &old_func);
        glr.glDepthFunc(static_cast<GLenum>(depth->depth_func));
    }
    draw_hands(shared ? depth->view_projection : vp, scene);
    if (shared) glr.glDepthFunc(static_cast<GLenum>(old_func));
    if (scene.vignette > 0.01f) {
        // Centered on straight ahead, which isn't the middle of an off-center eye image.
        const float cx = -projection.at(0, 2), cy = -projection.at(1, 2);
        glr.glDisable(gl::DEPTH_TEST);
        glr.glEnable(gl::BLEND);
        glr.glBlendFuncSeparate(gl::SRC_ALPHA, gl::ONE_MINUS_SRC_ALPHA, gl::ONE, gl::ONE_MINUS_SRC_ALPHA);
        const float color[4] = {0, 0, 0, scene.vignette};
        const Mat4 clip = hvr::scale({4, 4, 1});
        glr.glUniformMatrix4fv(u_mvp_, 1, gl::FALSE_, clip.m);
        const Mat4 offset = translation({-cx, -cy, 0}) * clip;
        glr.glUniformMatrix4fv(u_model_, 1, gl::FALSE_, offset.m);
        glr.glUniform1i(u_mode_, 7);
        glr.glUniform4f(u_color_, color[0], color[1], color[2], color[3]);
        glr.glDrawArrays(gl::TRIANGLES, kQuadFirst, kQuadCount);
    }
    glr.glBindVertexArray(0);
}

void Scene::render(const EyeTarget& target, const Mat4& projection, const Mat4& view, const SceneView& scene) {
    if (!program_) return;
    glr.glBindFramebuffer(gl::DRAW_FRAMEBUFFER, target.fbo());
    glr.glViewport(0, 0, target.width(), target.height());
    glr.glDisable(gl::SCISSOR_TEST);
    glr.glDisable(gl::STENCIL_TEST);
    glr.glDisable(gl::CULL_FACE);
    glr.glDisable(gl::FRAMEBUFFER_SRGB);
    glr.glColorMask(1, 1, 1, 1);
    glr.glDepthMask(1);
    glr.glClearColor(0.01f, 0.012f, 0.018f, 1.0f);
    glr.glClear(gl::COLOR_BUFFER_BIT | gl::DEPTH_BUFFER_BIT);
    glr.glUseProgram(program_);
    glr.glBindVertexArray(vao_);
    glr.glActiveTexture(gl::TEXTURE0);
    glr.glBindSampler(0, 0);
    glr.glBindTexture(gl::TEXTURE_2D, screen_texture_);
    glr.glUniform1i(u_tex_, 0);
    glr.glUniform4f(u_uv_, 0, 0, 1, 1);
    glr.glUniform4f(u_color_, 1, 1, 1, 1);
    glr.glBlendEquation(gl::FUNC_ADD);
    glr.glBlendFuncSeparate(gl::SRC_ALPHA, gl::ONE_MINUS_SRC_ALPHA, gl::ONE, gl::ONE_MINUS_SRC_ALPHA);
    glr.glUniform3f(u_eye_, scene.eye.x, scene.eye.y, scene.eye.z);
    const Mat4 vp = projection * view;

    // Sky
    glr.glDisable(gl::DEPTH_TEST);
    glr.glDepthMask(0);
    glr.glDisable(gl::BLEND);
    draw(kCubeFirst, kCubeCount, vp, translation(scene.eye) * hvr::scale({80, 80, 80}), 3, nullptr);

    // Floor
    glr.glEnable(gl::DEPTH_TEST);
    glr.glDepthMask(1);
    glr.glEnable(gl::BLEND);
    if (scene.floor) draw(kFloorFirst, kFloorCount, vp, Mat4{}, 2, nullptr);

    // Screen glow, then the screen itself.
    const Screen& s = scene.screen;
    const Mat4 screen_frame = translation(s.center) * rotation_y(s.yaw);
    const float glow[4] = {0.55f, 0.62f, 0.75f, 1.0f};
    glr.glDepthMask(0);
    glr.glUniform4f(u_color_, glow[0], glow[1], glow[2], glow[3]);
    {
        const float w = s.width * 1.25f, h = s.height * 1.25f;
        glr.glUniform2f(u_size_, w, h);
        draw(kQuadFirst, kQuadCount, vp, screen_frame * translation({0, 0, -0.02f}) * hvr::scale({w, h, 1}), 4, glow);
    }
    glr.glDepthMask(1);
    glr.glUniform2f(u_size_, s.width, s.height);
    draw(kQuadFirst, kQuadCount, vp, s.model(), 0, nullptr);

    draw_hands(vp, scene);
    glr.glBindVertexArray(0);
}

}  // namespace hvr
