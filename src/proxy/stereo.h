// Real stereo: renders Hytale's own 3D frame once per eye.
//
// Each draw Hytale makes into one of its full-screen render targets runs
// twice: first as usual with the left eye's camera, then into a twin target
// with the right eye's camera. The camera comes from Hytale's SceneData
// uniform block, which we copy per eye with the headset's matrices, plus a
// few loose uniforms (projection, view, frustum corners) set per eye. The
// final composite into the window also goes to the right eye's texture, and
// everything Hytale draws after it (the HUD and menus) is copied onto a
// transparent panel for the headset.
#pragma once

#include <string>

#include "../core/vrmath.h"
#include "gl_api.h"

namespace hvr::stereo {

// --- From the VR session (render thread) ---

struct EyeSetup {
    Mat4 headset_projection[2];  // GetProjectionMatrix, any near/far
    Mat4 eye_to_head[2];         // GetEyeToHeadTransform
    Mat4 head;                   // headset pose in Hytale's axes, relative to where the camera sits
    float world_scale = 1;       // world units per meter
    bool no_occlusion = true;    // skip Hytale's occluder pass so nothing near the edges of the eyes gets culled
    // Where the head's neutral spot sits, horizontally, in world units. When
    // world_locked it's a fixed world position (x, z), so the view stays put
    // while the character catches up; otherwise it's an offset from the camera.
    Vec3 anchor;
    bool world_locked = false;
    // Hytale's first-person arms and held item, stuck to the head; hidden when false.
    bool show_arms = false;
    // Hytale's hand and held item carried by a controller instead: its grip
    // relative to the headset (meters), and how big they're drawn.
    bool hytale_hand = false;
    bool grip_valid[2] = {};
    Mat4 grip[2];  // [0] left hand
    int item_hand = 1;
    enum class Held { Other, Bow, Food, Block } held = Held::Other;  // what the plugin says is held
    float item_size = 1;
    // Your character's hands: each palm from the head (meters), how many
    // meters a model unit is, and where the hand and its item point sit in the
    // palm's frame (model units). Held items go where Hytale's hand holds them.
    Mat4 palm[2];
    float arm_unit = 1.0f / 32.0f;
    bool hand_valid = false, attachment_valid = false;
    Mat4 hand, attachment;  // the item hand's
    // Smooths small, quick wobbles out of Hytale's camera position (bobbing,
    // sway), while still following walking, jumping and falling.
    bool steady = true;
    // Raises the view this many world units (undoing Hytale's own crouch drop
    // while you crouch for real).
    float lift = 0;
};

// Turns stereo on for the coming frames, with this frame's poses.
void set_eyes(const EyeSetup& setup);
void disable();
bool wanted();

// At swap: whether the frame just drawn was rendered per eye, and its
// textures (window-sized RGBA8). hud is premultiplied alpha.
struct FrameResult {
    bool stereo = false;
    GLuint eye_texture[2] = {};
    GLuint eye_fbo[2] = {};
    GLuint hud_texture = 0;
    GLuint hud_fbo = 0;
    int width = 0, height = 0;
};
FrameResult end_frame(int window_w, int window_h);
// The HUD target for the menu to draw into this frame (0 when not stereo).
GLuint hud_fbo_this_frame();

// Hytale's own camera this frame, read from SceneData: rotation-only view
// (camera-relative), forward direction. Valid once a stereo frame ran.
bool engine_camera(Mat4& view, Vec3& forward, Vec3& position);
// Where the view's neutral spot sat from Hytale's camera last frame (world
// units, Hytale's axes): the steady view's offset, plus lift.
Vec3 view_offset();

// --- Hooks ---

bool active();  // stereo drawing is on this frame

void on_bind_texture(GLenum target, GLuint texture);
void on_active_texture(GLenum unit);
void on_bind_textures(GLuint first, GLsizei count, const GLuint* textures);
void on_bind_texture_unit(GLuint unit, GLuint texture);
void on_bind_image_texture(GLuint unit, GLuint texture, GLint level, GLboolean layered, GLint layer, GLenum access,
                           GLenum format);
void on_bind_framebuffer(GLenum target, GLuint framebuffer);
void on_use_program(GLuint program);
void on_link(GLuint program);
void on_shader_source(GLuint shader, const std::string& source);
void on_model_matrix(GLuint program, GLint location, const GLfloat* value);  // untransposed 4x4
void on_uniform_block_binding(GLuint program, GLuint index, GLuint binding);
void on_bind_buffer(GLenum target, GLuint buffer);
void on_bind_buffer_range(GLenum target, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size);
void on_buffer_data(GLuint buffer, GLsizeiptr size, const void* data);
void on_buffer_sub_data(GLuint buffer, GLintptr offset, GLsizeiptr size, const void* data);
void on_map(GLuint buffer, GLintptr offset, GLsizeiptr length, GLbitfield access, void* pointer);
void on_flush(GLuint buffer, GLintptr offset, GLsizeiptr length);  // relative to the mapped range
void on_unmap(GLuint buffer);
GLuint buffer_bound(GLenum target);
GLuint current_program();
void on_uniform(GLuint program, GLint location, GLsizei count, const GLfloat* value, int floats_each);
void on_draw_buffers(GLsizei count, const GLenum* buffers);
void on_delete_framebuffers(GLsizei count, const GLuint* framebuffers);
// Hytale attached, resized or deleted a texture or framebuffer.
void on_layout_change();
// Blend, scissor and rasterizer-discard changes, tracked to avoid asking the driver.
void on_enable(GLenum cap, bool on);
void on_blend_func(GLenum src_rgb, GLenum dst_rgb, GLenum src_alpha, GLenum dst_alpha);

// Draw doubling. Returns true when the draw must run a second time: call
// draw, second(), draw again, then finish(). When it returns false nothing
// was changed.
bool draw_begin(GLenum mode, GLsizei count);
// Around each draw: moves a piece of Hytale's first person into the hand (or
// hides it), and puts Hytale's own matrix back after.
bool move_first_person();
void restore_first_person();
// True for draws that should not run at all in VR (Hytale's occluders).
bool skip_draw(GLenum mode);
void draw_second();
void draw_finish();

// Clears and blits on stereo targets are repeated on their twins.
void after_clear(GLbitfield mask);
void after_clear_buffer(GLenum buffer, GLint drawbuffer, const void* value, int kind);  // kind 0 fv, 1 iv, 2 uiv
void after_clear_buffer_fi(GLenum buffer, GLint drawbuffer, GLfloat depth, GLint stencil);
void after_blit(GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0, GLint dx1, GLint dy1, GLbitfield mask,
                GLenum filter);
void after_generate_mipmap(GLenum target);

std::string summary();
// Why recent frames were or weren't drawn per eye; resets the counts.
std::string take_frame_stats();
void add_draw_time(long long ticks);  // QueryPerformanceCounter ticks spent in a wrapped draw

// Virtual back buffer: Hytale's window drawing goes to an offscreen buffer of
// this size (0 turns it off). Returns whether it's on.
bool set_virtual_backbuffer(int width, int height);
GLuint virtual_backbuffer();  // 0 when off
// Binds "the window" again for whatever Hytale has bound as it, after a change.
void rebind_window();
// Scales it into the real window. Call at swap, after everything is drawn.
void present_virtual_backbuffer(int window_w, int window_h);
// glDrawBuffer/glReadBuffer while "the window" is bound: BACK becomes the
// buffer's color attachment.
GLenum map_color_buffer(bool draw, GLenum mode);
int render_width();  // Hytale's 3D resolution as last seen, 0 before
// Hytale's viewport width when it last drew to "the window", 0 before.
void on_viewport(GLsizei width);
int window_viewport_width();

}  // namespace hvr::stereo
