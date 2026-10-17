#include "stereo.h"

#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "../core/stereo_math.h"
#include "log.h"
#include "vr_scene.h"

namespace hvr::stereo {
namespace {

// Hytale's SceneData block (uboSceneDataBlock), in floats.
namespace sd {
constexpr int kFirstPersonProjection = 16, kView = 32, kProjection = 48, kViewProjection = 64, kInvView = 80,
              kInvViewProjection = 96, kReprojection = 112, kProjInfos = 128, kCameraPosition = 132,
              kCameraDirection = 136, kSunDir = 168, kSunDirView = 172;
constexpr int kFloatsRead = 176;  // everything we read or rewrite sits below this
constexpr GLsizeiptr kBlockSize = 1312;
}  // namespace sd

constexpr int kUnits = 96;
constexpr int kImageUnits = 16;
constexpr int kUboIndices = 32;
constexpr GLenum kBufferTargets[] = {0x8892, 0x8893, 0x88EB, 0x88EC, 0x8A11, 0x8C2A, 0x8C8E,
                                     0x8F36, 0x8F37, 0x8F3F, 0x90D2, 0x90EE, 0x9192, 0x92C0};
constexpr int kBufferTargetCount = sizeof(kBufferTargets) / sizeof(kBufferTargets[0]);

struct UboBinding {
    GLuint buffer = 0;
    GLintptr offset = 0;
    GLsizeiptr size = -1;  // -1: bound whole (BindBufferBase)
};

struct ImageBinding {
    GLuint texture = 0;
    GLint level = 0;
    GLboolean layered = 0;
    GLint layer = 0;
    GLenum access = 0, format = 0;
};

// Hytale's GL state as this thread's context sees it.
struct Track {
    GLuint program = 0;
    GLuint draw_fb = 0, read_fb = 0;
    int active_unit = 0;
    GLuint tex2d[kUnits] = {};
    GLuint tex2d_array[kUnits] = {};
    ImageBinding images[kImageUnits];
    UboBinding ubo[kUboIndices];
    GLuint buffers[kBufferTargetCount] = {};
};
thread_local Track t;

int buffer_slot(GLenum target) {
    for (int i = 0; i < kBufferTargetCount; ++i)
        if (kBufferTargets[i] == target) return i;
    return -1;
}

// --- Textures and their twins ---

struct TextureInfo {
    GLenum target = 0;  // first target it was bound to
    GLuint twin = 0;
    int width = 0, height = 0, depth = 0;
    GLenum format = 0;
    bool twin_failed = false;
    int frame_checked = -1;
    int epoch_checked = -1;
};
std::vector<TextureInfo> g_textures;
extern int g_frame;
// Bumped whenever Hytale attaches, resizes or deletes textures and
// framebuffers. Asking the driver about them makes it wait for the GPU, so
// they're only looked at again after a change (or once in a while).
int g_layout_epoch = 0;
constexpr int kRecheckFrames = 600;
bool layout_fresh(int frame_checked, int epoch_checked) {
    return frame_checked >= 0 && epoch_checked == g_layout_epoch && g_frame - frame_checked < kRecheckFrames;
}

TextureInfo* texture_info(GLuint texture, bool create) {
    if (texture == 0 || texture > 4000000) return nullptr;
    if (texture >= g_textures.size()) {
        if (!create) return nullptr;
        g_textures.resize(texture + 1024);
    }
    return &g_textures[texture];
}

GLuint twin_of(GLuint texture) {
    if (texture == 0 || texture >= g_textures.size()) return 0;
    return g_textures[texture].twin;
}

// --- Framebuffers ---

struct Attachment {
    GLenum point = 0;
    GLint type = 0;  // GL_TEXTURE or GL_RENDERBUFFER
    GLuint name = 0;
    GLint level = 0;
    int width = 0, height = 0;
    GLenum format = 0;
    GLint samples = 0;
    bool operator==(const Attachment& o) const {
        return point == o.point && type == o.type && name == o.name && level == o.level && width == o.width &&
               height == o.height && format == o.format && samples == o.samples;
    }
};

struct FboInfo {
    int frame_checked = -1;
    int epoch_checked = -1;
    bool stereo = false;
    bool occluder = false;  // Hytale's small depth-only target for occlusion culling
    GLuint twin = 0;
    std::vector<Attachment> attachments;
    std::vector<GLuint> twin_renderbuffers;
};
std::unordered_map<GLuint, FboInfo> g_fbos;

// --- Programs ---

enum class PatchKind { Projection, InvProjection, View, Reproject, ViewProjection, Corners };

struct UniformPatch {
    PatchKind kind;
    GLint location = -1;
    int count = 1;
    int floats_each = 16;
    float mono[16 * 4] = {};
    bool have_mono = false;
};

struct ProgramInfo {
    bool queried = false;
    GLint scene_block = -1;
    GLint scene_binding = -1;
    GLint scene_size = 0;
    std::vector<UniformPatch> patches;
};
std::unordered_map<GLuint, ProgramInfo> g_programs;
std::mutex g_relink_mutex;
std::vector<GLuint> g_relinked;  // programs linked again since we looked at them

// --- SceneData buffers ---

struct Shadow {
    std::vector<uint8_t> data;
    uint64_t version = 1;
    bool written = false;
    uint8_t* map = nullptr;
    GLintptr map_offset = 0;
    GLsizeiptr map_length = 0;
    bool persistent = false;
};
std::mutex g_shadow_mutex;
std::unordered_map<GLuint, Shadow> g_shadows;  // only buffers seen holding SceneData
int g_verify_left = 3;                         // shadow reads checked against the GPU copy
bool g_readback_only = false;

struct Mono {
    Mat4 view, projection, fp_projection;
    Vec3 camera, sun;
    bool valid = false;
};

struct Eye {
    Mat4 projection, view, fp_projection, view_projection, reproject, mvp_fix, view_fix;
    Vec3 camera;
};

struct EyeUbo {
    GLuint buffer[2] = {};
    GLsizeiptr size = 0;
    int frame = -1;
    uint64_t version = 0;
};
std::map<std::pair<GLuint, GLintptr>, EyeUbo> g_eye_ubos;

// --- Frame ---

std::atomic<DWORD> g_render_thread{0};
bool g_wanted = false;
EyeSetup g_setup;
int g_setup_frame = -100;
int g_frame = 0;
bool g_failed_frame = false;
int g_main_w = 0, g_main_h = 0;    // Hytale's full-resolution 3D target size
int g_window_w = 0, g_window_h = 0;
bool g_composited = false;
bool g_hud_since_composite = false;
int g_stereo_draws = 0, g_composites = 0;
Mono g_mono;
Eye g_eye[2];
bool g_eye_valid = false;
int g_eye_frame = -1;
struct Previous {
    Mat4 projection, view;
    Vec3 camera;
    bool valid = false;
} g_previous[2];
EyeTarget g_eye_targets[2];
EyeTarget g_hud;
std::string g_last_problem;

// The virtual back buffer: while it's on, Hytale's window drawing (framebuffer
// 0) goes here instead, at the headset's resolution, and we scale it down to
// the real window at swap.
EyeTarget g_vfb;
bool g_vfb_on = false;

GLuint real_fb(GLuint fb) { return fb == 0 && g_vfb_on ? g_vfb.fbo() : fb; }

// The draw in flight.
enum class Kind { Mono, Target, Composite, Hud };
struct Pending {
    Kind kind = Kind::Mono;
    ProgramInfo* program = nullptr;
    GLint ubo_index = -1;
    EyeUbo* eye_ubo = nullptr;
    GLuint restore_units[kUnits];
    int restore_unit_count = 0;
    int restore_images[kImageUnits];
    int restore_image_count = 0;
    GLint blend[4] = {};
    bool blend_changed = false;
} g_pending;

struct Stats {
    int frames = 0, wanted = 0, stale_setup = 0, failed = 0, no_targets = 0, composited = 0, stereo = 0, no_draws = 0;
    long long draw_ticks = 0, draws = 0;
    float smoothed_max = 0;  // the most the steady view sat off Hytale's camera
} g_stats;

void problem(const char* text) {
    if (g_last_problem == text) return;
    g_last_problem = text;
    logf("Stereo: %s", text);
}

bool on_render_thread() { return GetCurrentThreadId() == g_render_thread.load(); }

void clear_errors() {
    for (int i = 0; i < 16 && glr.glGetError() != 0; ++i) {
    }
}

// Hytale's render resolution: its G-buffer once seen, else the window.
bool main_size_match(int w, int h) {
    const int mw = g_main_w ? g_main_w : g_window_w, mh = g_main_h ? g_main_h : g_window_h;
    if (mw <= 0 || mh <= 0) return false;
    for (int k = 0; k <= 5; ++k)
        if (w == (mw >> k) && h == (mh >> k)) return true;
    return false;
}

void restore_texture_binding(int unit) {
    glr.glActiveTexture(gl::TEXTURE0 + unit);
    glr.glBindTexture(gl::TEXTURE_2D, t.tex2d[unit]);
    glr.glBindTexture(gl::TEXTURE_2D_ARRAY, t.tex2d_array[unit]);
    glr.glActiveTexture(gl::TEXTURE0 + t.active_unit);
}

int texture_level_param(GLuint texture, GLenum target, GLint level, GLenum pname) {
    GLint value = 0;
    if (glr.glGetTextureLevelParameteriv) {
        glr.glGetTextureLevelParameteriv(texture, level, pname, &value);
    } else {
        glr.glBindTexture(target, texture);
        glr.glGetTexLevelParameteriv(target, level, pname, &value);
        restore_texture_binding(t.active_unit);
    }
    return value;
}

GLenum texture_target(GLuint texture) {
    TextureInfo* info = texture_info(texture, true);
    if (!info) return 0;
    if (!info->target && glr.glGetTextureParameteriv) {
        GLint target = 0;
        glr.glGetTextureParameteriv(texture, gl::TEXTURE_TARGET, &target);
        clear_errors();
        info->target = static_cast<GLenum>(target);
    }
    return info->target ? info->target : gl::TEXTURE_2D;
}

void pick_format(GLenum internal, GLenum& format, GLenum& type) {
    switch (internal) {
        case 0x81A5: case 0x81A6: case 0x81A7: case 0x8CAC:
            format = gl::DEPTH_COMPONENT, type = gl::FLOAT;
            return;
        case 0x88F0:
            format = gl::DEPTH_STENCIL, type = gl::UNSIGNED_INT_24_8;
            return;
        case 0x8CAD:
            format = gl::DEPTH_STENCIL, type = gl::FLOAT_32_UNSIGNED_INT_24_8_REV;
            return;
        case 0x8232: case 0x8234: case 0x8236: case 0x8238: case 0x823A: case 0x823C: case 0x8D70: case 0x8D76:
        case 0x8D7C: case 0x8D71: case 0x8D77: case 0x8D7D:
            format = gl::RED_INTEGER, type = gl::UNSIGNED_INT;
            return;
        case 0x8231: case 0x8233: case 0x8235: case 0x8237: case 0x8239: case 0x823B: case 0x8D82: case 0x8D88:
        case 0x8D8E: case 0x8D83: case 0x8D89: case 0x8D8F:
            format = gl::RED_INTEGER, type = 0x1404;  // INT
            return;
        default:
            format = gl::RGBA, type = gl::UNSIGNED_BYTE;
    }
}

// Makes (or checks) the right eye's copy of a 2D or 2D-array texture.
GLuint ensure_twin(GLuint texture) {
    TextureInfo* info = texture_info(texture, true);
    if (!info) return 0;
    if (info->twin && (info->frame_checked == g_frame || layout_fresh(info->frame_checked, info->epoch_checked)))
        return info->twin;
    if (!info->twin && info->twin_failed) return 0;
    info->frame_checked = g_frame;
    info->epoch_checked = g_layout_epoch;
    const GLenum target = texture_target(texture);
    if (target != gl::TEXTURE_2D && target != gl::TEXTURE_2D_ARRAY) return 0;
    const int width = texture_level_param(texture, target, 0, gl::TEXTURE_WIDTH);
    const int height = texture_level_param(texture, target, 0, gl::TEXTURE_HEIGHT);
    const int depth = target == gl::TEXTURE_2D_ARRAY ? texture_level_param(texture, target, 0, gl::TEXTURE_DEPTH) : 1;
    const GLenum format = static_cast<GLenum>(texture_level_param(texture, target, 0, gl::TEXTURE_INTERNAL_FORMAT));
    if (info->twin && info->width == width && info->height == height && info->depth == depth && info->format == format)
        return info->twin;
    if (info->twin) {
        glr.glDeleteTextures(1, &info->twin);
        info->twin = 0;
    }
    if (width <= 0 || height <= 0 || depth <= 0 || info->twin_failed) return 0;
    int levels = 1;
    while (levels < 16 && texture_level_param(texture, target, levels, gl::TEXTURE_WIDTH) > 0) ++levels;

    // Read the sampling parameters from the original, then build the copy.
    const GLenum params[] = {gl::TEXTURE_MIN_FILTER, gl::TEXTURE_MAG_FILTER, gl::TEXTURE_WRAP_S, gl::TEXTURE_WRAP_T,
                             gl::TEXTURE_COMPARE_MODE, gl::TEXTURE_COMPARE_FUNC, gl::TEXTURE_BASE_LEVEL,
                             gl::TEXTURE_MAX_LEVEL};
    GLint values[8] = {};
    glr.glBindTexture(target, texture);
    for (int i = 0; i < 8; ++i) glr.glGetTexParameteriv(target, params[i], &values[i]);
    GLuint twin = 0;
    glr.glGenTextures(1, &twin);
    glr.glBindTexture(target, twin);
    clear_errors();
    if (target == gl::TEXTURE_2D && glr.glTexStorage2D) {
        glr.glTexStorage2D(target, levels, format, width, height);
    } else if (target == gl::TEXTURE_2D_ARRAY && glr.glTexStorage3D) {
        glr.glTexStorage3D(target, levels, format, width, height, depth);
    } else {
        GLenum f = 0, ty = 0;
        pick_format(format, f, ty);
        for (int level = 0, w = width, h = height; level < levels; ++level, w = w > 1 ? w / 2 : 1, h = h > 1 ? h / 2 : 1) {
            if (target == gl::TEXTURE_2D)
                glr.glTexImage2D(target, level, static_cast<GLint>(format), w, h, 0, f, ty, nullptr);
            else
                glr.glTexImage3D(target, level, static_cast<GLint>(format), w, h, depth, 0, f, ty, nullptr);
        }
    }
    for (int i = 0; i < 8; ++i) glr.glTexParameteri(target, params[i], values[i]);
    const GLenum error = glr.glGetError();
    restore_texture_binding(t.active_unit);
    if (error != 0) {
        glr.glDeleteTextures(1, &twin);
        info->twin_failed = true;
        logf("Stereo: couldn't copy texture %u (%dx%d format 0x%X, error 0x%X)", texture, width, height, format, error);
        return 0;
    }
    info->twin = twin;
    info->width = width;
    info->height = height;
    info->depth = depth;
    info->format = format;
    return twin;
}

GLint attachment_param(GLenum target, GLenum point, GLenum pname) {
    GLint value = 0;
    glr.glGetFramebufferAttachmentParameteriv(target, point, pname, &value);
    return value;
}

bool read_attachments(GLenum target, std::vector<Attachment>& out) {
    out.clear();
    static GLint max_color = 0;
    if (!max_color) {
        glr.glGetIntegerv(gl::MAX_COLOR_ATTACHMENTS, &max_color);
        if (max_color <= 0 || max_color > 16) max_color = 8;
    }
    for (int i = 0; i < max_color + 2; ++i) {
        const GLenum point = i < max_color ? gl::COLOR_ATTACHMENT0 + i : (i == max_color ? gl::DEPTH_ATTACHMENT : gl::STENCIL_ATTACHMENT);
        Attachment a;
        a.point = point;
        a.type = attachment_param(target, point, gl::FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE);
        if (a.type == 0) continue;
        a.name = static_cast<GLuint>(attachment_param(target, point, gl::FRAMEBUFFER_ATTACHMENT_OBJECT_NAME));
        if (a.type == static_cast<GLint>(gl::TEXTURE)) {
            a.level = attachment_param(target, point, gl::FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL);
            if (attachment_param(target, point, gl::FRAMEBUFFER_ATTACHMENT_TEXTURE_LAYER) != 0 ||
                attachment_param(target, point, gl::FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE) != 0)
                return false;
            if (texture_target(a.name) != gl::TEXTURE_2D) return false;
            a.width = texture_level_param(a.name, gl::TEXTURE_2D, a.level, gl::TEXTURE_WIDTH);
            a.height = texture_level_param(a.name, gl::TEXTURE_2D, a.level, gl::TEXTURE_HEIGHT);
            a.format = static_cast<GLenum>(texture_level_param(a.name, gl::TEXTURE_2D, a.level, gl::TEXTURE_INTERNAL_FORMAT));
            a.samples = texture_level_param(a.name, gl::TEXTURE_2D, a.level, gl::TEXTURE_SAMPLES);
        } else if (a.type == static_cast<GLint>(gl::RENDERBUFFER)) {
            if (!glr.glGetNamedRenderbufferParameteriv) return false;
            GLint value = 0;
            glr.glGetNamedRenderbufferParameteriv(a.name, gl::RENDERBUFFER_WIDTH, &value);
            a.width = value;
            glr.glGetNamedRenderbufferParameteriv(a.name, gl::RENDERBUFFER_HEIGHT, &value);
            a.height = value;
            glr.glGetNamedRenderbufferParameteriv(a.name, gl::RENDERBUFFER_INTERNAL_FORMAT, &value);
            a.format = static_cast<GLenum>(value);
            glr.glGetNamedRenderbufferParameteriv(a.name, gl::RENDERBUFFER_SAMPLES, &value);
            a.samples = value;
        } else {
            return false;
        }
        out.push_back(a);
    }
    return !out.empty();
}

void destroy_twin_fbo(FboInfo& info) {
    if (info.twin) glr.glDeleteFramebuffers(1, &info.twin);
    if (!info.twin_renderbuffers.empty())
        glr.glDeleteRenderbuffers(static_cast<GLsizei>(info.twin_renderbuffers.size()), info.twin_renderbuffers.data());
    info.twin = 0;
    info.twin_renderbuffers.clear();
}

// Builds the right eye's framebuffer. fb must be bound to DRAW right now.
bool build_twin_fbo(GLuint fb, FboInfo& info) {
    GLenum draw_buffers[16];
    GLint max_draw = 8;
    glr.glGetIntegerv(0x8824 /* MAX_DRAW_BUFFERS */, &max_draw);
    if (max_draw <= 0 || max_draw > 16) max_draw = 8;
    for (int i = 0; i < max_draw; ++i) {
        GLint value = 0;
        glr.glGetIntegerv(gl::DRAW_BUFFER0 + i, &value);
        draw_buffers[i] = static_cast<GLenum>(value);
    }
    glr.glGenFramebuffers(1, &info.twin);
    glr.glBindFramebuffer(gl::DRAW_FRAMEBUFFER, info.twin);
    for (size_t i = 0; i < info.attachments.size(); ++i) {
        const Attachment& a = info.attachments[i];
        // A depth-stencil texture attached to both points goes on once.
        if (a.point == gl::STENCIL_ATTACHMENT) {
            bool shared = false;
            for (const Attachment& other : info.attachments)
                if (other.point == gl::DEPTH_ATTACHMENT && other.name == a.name && other.type == a.type) shared = true;
            if (shared) continue;
        }
        GLenum point = a.point;
        if (a.point == gl::DEPTH_ATTACHMENT) {
            for (const Attachment& other : info.attachments)
                if (other.point == gl::STENCIL_ATTACHMENT && other.name == a.name && other.type == a.type)
                    point = gl::DEPTH_STENCIL_ATTACHMENT;
        }
        if (a.type == static_cast<GLint>(gl::TEXTURE)) {
            const GLuint twin = ensure_twin(a.name);
            if (!twin) return false;
            glr.glFramebufferTexture2D(gl::DRAW_FRAMEBUFFER, point, gl::TEXTURE_2D, twin, a.level);
        } else {
            GLuint rb = 0;
            glr.glGenRenderbuffers(1, &rb);
            glr.glBindRenderbuffer(gl::RENDERBUFFER, rb);
            if (a.samples > 0)
                glr.glRenderbufferStorageMultisample(gl::RENDERBUFFER, a.samples, a.format, a.width, a.height);
            else
                glr.glRenderbufferStorage(gl::RENDERBUFFER, a.format, a.width, a.height);
            info.twin_renderbuffers.push_back(rb);
            glr.glFramebufferRenderbuffer(gl::DRAW_FRAMEBUFFER, point, gl::RENDERBUFFER, rb);
        }
    }
    glr.glDrawBuffers(max_draw, draw_buffers);
    const bool complete = glr.glCheckFramebufferStatus(gl::DRAW_FRAMEBUFFER) == gl::FRAMEBUFFER_COMPLETE;
    glr.glBindFramebuffer(gl::DRAW_FRAMEBUFFER, fb);
    if (!complete) logf("Stereo: the right eye's copy of framebuffer %u is incomplete", fb);
    return complete;
}

// Whether fb (bound to target right now) is one of Hytale's full-screen
// 3D targets, building its twin on first sight each frame.
FboInfo* classify(GLenum target, GLuint fb) {
    if (fb == 0) return nullptr;
    FboInfo& info = g_fbos[fb];
    if (info.frame_checked == g_frame || layout_fresh(info.frame_checked, info.epoch_checked))
        return info.stereo ? &info : nullptr;
    info.frame_checked = g_frame;
    info.epoch_checked = g_layout_epoch;
    std::vector<Attachment> now;
    bool usable = read_attachments(target, now);
    int colors = 0;
    bool depth = false;
    for (const Attachment& a : now) {
        if (a.samples > 0) usable = false;
        if (a.point == gl::DEPTH_ATTACHMENT) depth = true;
        if (a.point >= gl::COLOR_ATTACHMENT0 && a.point < gl::COLOR_ATTACHMENT0 + 16) ++colors;
    }
    // The G-buffer (several colors plus depth) fixes the render resolution.
    if (usable && colors >= 3 && depth && !now.empty() && (now[0].width != g_main_w || now[0].height != g_main_h)) {
        g_main_w = now[0].width;
        g_main_h = now[0].height;
        logf("Stereo: Hytale renders at %dx%d", g_main_w, g_main_h);
    }
    info.occluder = now.size() == 1 && now[0].point == gl::DEPTH_ATTACHMENT &&
                    now[0].type == static_cast<GLint>(gl::TEXTURE) && now[0].width <= 1024 && now[0].height <= 512;
    for (const Attachment& a : now)
        if (!main_size_match(a.width, a.height)) usable = false;
    if (!usable) {
        info.stereo = false;
        return nullptr;
    }
    if (info.twin && now == info.attachments) {
        info.stereo = true;
        return &info;
    }
    destroy_twin_fbo(info);
    info.attachments = now;
    if (target != gl::DRAW_FRAMEBUFFER) {
        // Twins are built while the framebuffer is bound for drawing.
        glr.glBindFramebuffer(gl::DRAW_FRAMEBUFFER, fb);
        info.stereo = build_twin_fbo(fb, info);
        glr.glBindFramebuffer(gl::DRAW_FRAMEBUFFER, real_fb(t.draw_fb));
    } else {
        info.stereo = build_twin_fbo(fb, info);
    }
    if (!info.stereo) destroy_twin_fbo(info);
    return info.stereo ? &info : nullptr;
}

// --- Programs ---

ProgramInfo& program_info(GLuint program) {
    {
        std::lock_guard lock(g_relink_mutex);
        for (GLuint relinked : g_relinked) g_programs.erase(relinked);
        g_relinked.clear();
    }
    ProgramInfo& info = g_programs[program];
    if (info.queried) return info;
    info.queried = true;
    GLint blocks = 0;
    glr.glGetProgramiv(program, gl::ACTIVE_UNIFORM_BLOCKS, &blocks);
    for (GLint i = 0; i < blocks; ++i) {
        char name[128] = {};
        glr.glGetActiveUniformBlockName(program, static_cast<GLuint>(i), sizeof(name), nullptr, name);
        if (std::strcmp(name, "uboSceneDataBlock") != 0) continue;
        info.scene_block = i;
        glr.glGetActiveUniformBlockiv(program, static_cast<GLuint>(i), gl::UNIFORM_BLOCK_BINDING, &info.scene_binding);
        glr.glGetActiveUniformBlockiv(program, static_cast<GLuint>(i), gl::UNIFORM_BLOCK_DATA_SIZE, &info.scene_size);
    }
    GLint uniforms = 0;
    glr.glGetProgramiv(program, gl::ACTIVE_UNIFORMS, &uniforms);
    for (GLint i = 0; i < uniforms; ++i) {
        char name[128] = {};
        GLint size = 0;
        GLenum type = 0;
        glr.glGetActiveUniform(program, static_cast<GLuint>(i), sizeof(name), nullptr, &size, &type, name);
        if (char* bracket = std::strchr(name, '[')) {
            if (std::strcmp(bracket, "[0]") != 0) continue;
            *bracket = 0;
        }
        struct Known {
            const char* name;
            PatchKind kind;
            GLenum type;
            int size;
        };
        static const Known known[] = {
            {"uProjectionMatrix", PatchKind::Projection, gl::FLOAT_MAT4, 1},
            {"uInvProjectionMatrix", PatchKind::InvProjection, gl::FLOAT_MAT4, 1},
            {"uViewMatrix", PatchKind::View, gl::FLOAT_MAT4, 1},
            {"uReprojectMatrix", PatchKind::Reproject, gl::FLOAT_MAT4, 1},
            {"uMVPMatrix", PatchKind::ViewProjection, gl::FLOAT_MAT4, 1},
            {"uViewProjectionMatrix", PatchKind::ViewProjection, gl::FLOAT_MAT4, 1},
            {"uSunMVPMatrix", PatchKind::ViewProjection, gl::FLOAT_MAT4, 1},
            {"uFarCorners", PatchKind::Corners, gl::FLOAT_VEC3, 4},
        };
        for (const Known& k : known) {
            if (std::strcmp(name, k.name) != 0 || type != k.type || size != k.size) continue;
            UniformPatch patch;
            patch.kind = k.kind;
            patch.location = glr.glGetUniformLocation(program, k.name);
            patch.count = k.size;
            patch.floats_each = k.type == gl::FLOAT_MAT4 ? 16 : 3;
            if (patch.location >= 0) info.patches.push_back(patch);
        }
    }
    return info;
}

// --- Camera ---

Mat4 mat_at(const float* f) {
    Mat4 m;
    std::memcpy(m.m, f, sizeof(m.m));
    return m;
}

bool read_mono(const float* f, Mono& mono) {
    mono.view = mat_at(f + sd::kView);
    mono.projection = mat_at(f + sd::kProjection);
    mono.fp_projection = mat_at(f + sd::kFirstPersonProjection);
    mono.camera = {f[sd::kCameraPosition], f[sd::kCameraPosition + 1], f[sd::kCameraPosition + 2]};
    mono.sun = {f[sd::kSunDir], f[sd::kSunDir + 1], f[sd::kSunDir + 2]};
    // Sanity: a perspective projection and a pure rotation, as Hytale has today.
    const Mat4& p = mono.projection;
    if (std::fabs(p.at(3, 2) + 1.0f) > 1e-3f || std::fabs(p.at(3, 3)) > 1e-3f || p.at(0, 0) < 0.05f) return false;
    const Mat4& v = mono.view;
    for (int r = 0; r < 3; ++r) {
        const Vec3 row{v.at(r, 0), v.at(r, 1), v.at(r, 2)};
        if (std::fabs(length(row) - 1.0f) > 0.02f) return false;
    }
    if (std::fabs(v.at(0, 3)) + std::fabs(v.at(1, 3)) + std::fabs(v.at(2, 3)) > 1e-2f) return false;
    mono.valid = true;
    return true;
}

// From the session's head space (meters, Hytale's axes, relative to where the
// camera sits) to Hytale's camera-relative world: scaled, then moved by the
// body offset.
Mat4 to_camera(Mat4 m, Vec3 body) {
    const float s = g_setup.world_scale;
    m.at(0, 3) = m.at(0, 3) * s + body.x;
    m.at(1, 3) = m.at(1, 3) * s + body.y;
    m.at(2, 3) = m.at(2, 3) * s + body.z;
    return m;
}

// Where the view sits instead of Hytale's camera: a filter that follows the
// camera's steady motion (level plus velocity, so walking doesn't lag) and
// smooths out what wobbles around it. Updated once a frame.
struct Steady {
    bool valid = false;
    int frame = -1;
    Vec3 level, velocity;
    LARGE_INTEGER at{};
} g_steady;

Vec3 steady_offset(const Vec3& camera) {
    if (!g_setup.steady || g_setup.world_locked) {
        g_steady.valid = false;
        return {};
    }
    LARGE_INTEGER now, frequency;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    if (g_steady.frame != g_frame) {
        const float dt =
            static_cast<float>(now.QuadPart - g_steady.at.QuadPart) / static_cast<float>(frequency.QuadPart);
        const bool stale = !g_steady.valid || g_frame - g_steady.frame > 5 || dt <= 0 || dt > 0.25f;
        if (stale) {
            g_steady.level = camera;
            g_steady.velocity = {};
        } else {
            const float a = 1.0f - std::exp(-dt / 0.07f);   // position
            const float b = 1.0f - std::exp(-dt / 0.25f);   // velocity
            const Vec3 predicted = g_steady.level + g_steady.velocity * dt;
            const Vec3 level = predicted + (camera - predicted) * a;
            g_steady.velocity = g_steady.velocity + ((level - g_steady.level) * (1.0f / dt) - g_steady.velocity) * b;
            g_steady.level = level;
        }
        g_steady.valid = true;
        g_steady.frame = g_frame;
        g_steady.at = now;
    }
    // A teleport or a sudden stop against a wall: don't drift far from the camera.
    Vec3 offset = g_steady.level - camera;
    const float away = length(offset);
    g_stats.smoothed_max = std::fmax(g_stats.smoothed_max, away);
    if (away > 0.35f) {
        offset = offset * (0.35f / away);
        g_steady.level = camera + offset;
    }
    return offset;
}

void compute_eyes(const Mono& mono) {
    const float s = g_setup.world_scale;
    const Vec3 steady = steady_offset(mono.camera);
    // Uses this frame's camera, so a world-locked view doesn't trail the character.
    const Vec3 body = g_setup.world_locked
                          ? Vec3{g_setup.anchor.x - mono.camera.x, g_setup.lift, g_setup.anchor.z - mono.camera.z}
                          : Vec3{g_setup.anchor.x + steady.x, g_setup.lift + steady.y, g_setup.anchor.z + steady.z};
    const Mat4 mono_vp_inverse = inverse(mono.projection * mono.view);
    const Mat4 mono_view_inverse = inverse_rigid(mono.view);
    for (int e = 0; e < 2; ++e) {
        Eye& eye = g_eye[e];
        eye.view = inverse_rigid(to_camera(g_setup.head * g_setup.eye_to_head[e], body));
        eye.projection = eye_projection(g_setup.headset_projection[e], mono.projection);
        const Mat4 fp_projection = eye_projection(g_setup.headset_projection[e], mono.fp_projection);
        if (g_setup.item_in_hand) {
            // The first-person item is drawn in Hytale's view space with this
            // matrix alone, so the controller's placement goes in here too.
            eye.fp_projection = fp_projection * eye.view * to_camera(g_setup.item, body);
        } else {
            Mat4 eye_from_head = inverse_rigid(g_setup.eye_to_head[e]);
            for (int r = 0; r < 3; ++r) eye_from_head.at(r, 3) *= s;
            eye.fp_projection = fp_projection * eye_from_head;
        }
        eye.view_projection = eye.projection * eye.view;
        eye.camera = mono.camera;
        const Previous& prev = g_previous[e];
        if (prev.valid)
            eye.reproject = prev.projection * prev.view * translation(mono.camera - prev.camera) * inverse_rigid(eye.view);
        else
            eye.reproject = eye.projection;
        eye.mvp_fix = eye.view_projection * mono_vp_inverse;
        eye.view_fix = eye.view * mono_view_inverse;
    }
    g_eye_valid = true;
    g_eye_frame = g_frame;
}

void write_mat(float* f, int at, const Mat4& m) { std::memcpy(f + at, m.m, sizeof(m.m)); }

// Reads the first part of a SceneData block straight from the GPU.
bool read_back(GLuint buffer, GLintptr offset, float* out) {
    const GLsizeiptr bytes = sd::kFloatsRead * sizeof(float);
    if (glr.glGetNamedBufferSubData) {
        glr.glGetNamedBufferSubData(buffer, offset, bytes, out);
    } else {
        glr.glBindBuffer(gl::COPY_READ_BUFFER, buffer);
        glr.glGetBufferSubData(gl::COPY_READ_BUFFER, offset, bytes, out);
        glr.glBindBuffer(gl::COPY_READ_BUFFER, t.buffers[buffer_slot(gl::COPY_READ_BUFFER)]);
    }
    return glr.glGetError() == 0;
}

bool read_shadow(GLuint buffer, GLintptr offset, float* out, uint64_t& version) {
    std::lock_guard lock(g_shadow_mutex);
    auto it = g_shadows.find(buffer);
    if (it == g_shadows.end()) {
        g_shadows[buffer];  // start shadowing it from now on
        return false;
    }
    Shadow& shadow = it->second;
    if (shadow.persistent && shadow.map) {
        // Persistently mapped: Hytale writes straight through the pointer.
        const size_t end = static_cast<size_t>(shadow.map_offset + shadow.map_length);
        if (shadow.data.size() < end) shadow.data.resize(end);
        uint8_t* at = shadow.data.data() + shadow.map_offset;
        const size_t length = static_cast<size_t>(shadow.map_length);
        if (!shadow.written || std::memcmp(at, shadow.map, length) != 0) {
            std::memcpy(at, shadow.map, length);
            shadow.written = true;
            ++shadow.version;
        }
    }
    const size_t need = static_cast<size_t>(offset) + sd::kFloatsRead * sizeof(float);
    if (!shadow.written || shadow.data.size() < need) return false;
    std::memcpy(out, shadow.data.data() + offset, sd::kFloatsRead * sizeof(float));
    version = shadow.version;
    return true;
}

// Brings the eye copies of a SceneData block up to date for this draw.
EyeUbo* refresh_scene(GLuint buffer, GLintptr offset, GLsizeiptr size) {
    EyeUbo& ubo = g_eye_ubos[{buffer, offset}];
    float f[sd::kFloatsRead];
    uint64_t version = 0;
    bool from_shadow = !g_readback_only && read_shadow(buffer, offset, f, version);
    if (from_shadow && ubo.frame == g_frame && ubo.version == version) return &ubo;
    if (!from_shadow && ubo.frame == g_frame) return &ubo;
    if (from_shadow && g_verify_left > 0) {
        float gpu[sd::kFloatsRead];
        --g_verify_left;
        if (read_back(buffer, offset, gpu) && std::memcmp(gpu, f, sizeof(f)) != 0) {
            logf("Stereo: the camera block we track differs from the GPU's; reading it back each frame instead");
            g_readback_only = true;
            from_shadow = false;
        }
    }
    if (!from_shadow && !read_back(buffer, offset, f)) return nullptr;
    Mono mono;
    if (!read_mono(f, mono)) {
        problem("Hytale's camera block doesn't look like we expect; showing the big screen this frame");
        g_failed_frame = true;
        return nullptr;
    }
    g_mono = mono;
    compute_eyes(mono);

    if (ubo.size != size) {
        if (ubo.buffer[0]) glr.glDeleteBuffers(2, ubo.buffer);
        glr.glGenBuffers(2, ubo.buffer);
        for (GLuint b : ubo.buffer) {
            glr.glBindBuffer(gl::COPY_WRITE_BUFFER, b);
            glr.glBufferData(gl::COPY_WRITE_BUFFER, size, nullptr, gl::DYNAMIC_DRAW);
        }
        ubo.size = size;
    }
    glr.glBindBuffer(gl::COPY_READ_BUFFER, buffer);
    for (int e = 0; e < 2; ++e) {
        const Eye& eye = g_eye[e];
        float out[sd::kFloatsRead];
        std::memcpy(out, f, sizeof(out));
        write_mat(out, sd::kFirstPersonProjection, eye.fp_projection);
        write_mat(out, sd::kView, eye.view);
        write_mat(out, sd::kProjection, eye.projection);
        write_mat(out, sd::kViewProjection, eye.view_projection);
        write_mat(out, sd::kInvView, inverse_rigid(eye.view));
        write_mat(out, sd::kInvViewProjection, inverse(eye.view_projection));
        write_mat(out, sd::kReprojection, eye.reproject);
        proj_infos(eye.projection, out + sd::kProjInfos);
        out[sd::kCameraDirection] = -eye.view.at(2, 0);
        out[sd::kCameraDirection + 1] = -eye.view.at(2, 1);
        out[sd::kCameraDirection + 2] = -eye.view.at(2, 2);
        const Vec3 sun_view = transform_direction(eye.view, mono.sun);
        out[sd::kSunDirView] = sun_view.x;
        out[sd::kSunDirView + 1] = sun_view.y;
        out[sd::kSunDirView + 2] = sun_view.z;
        // GPU copy first (keeps anything Hytale's shaders wrote into the block),
        // then our camera fields on top.
        glr.glBindBuffer(gl::COPY_WRITE_BUFFER, ubo.buffer[e]);
        glr.glCopyBufferSubData(gl::COPY_READ_BUFFER, gl::COPY_WRITE_BUFFER, offset, 0, size);
        const int from = sd::kFirstPersonProjection, to = sd::kCameraPosition;
        glr.glBufferSubData(gl::COPY_WRITE_BUFFER, from * 4, (to - from) * 4, out + from);
        glr.glBufferSubData(gl::COPY_WRITE_BUFFER, sd::kCameraDirection * 4, 16, out + sd::kCameraDirection);
        glr.glBufferSubData(gl::COPY_WRITE_BUFFER, sd::kSunDirView * 4, 16, out + sd::kSunDirView);
    }
    glr.glBindBuffer(gl::COPY_READ_BUFFER, t.buffers[buffer_slot(gl::COPY_READ_BUFFER)]);
    glr.glBindBuffer(gl::COPY_WRITE_BUFFER, t.buffers[buffer_slot(gl::COPY_WRITE_BUFFER)]);
    ubo.frame = g_frame;
    ubo.version = from_shadow ? version : 0;
    return &ubo;
}

void bind_eye_ubo(const Pending& p, int eye) {
    glr.glBindBufferRange(gl::UNIFORM_BUFFER, static_cast<GLuint>(p.ubo_index), p.eye_ubo->buffer[eye], 0,
                          p.eye_ubo->size);
}

void restore_ubo(const Pending& p) {
    const UboBinding& b = t.ubo[p.ubo_index];
    if (b.size < 0)
        glr.glBindBufferBase(gl::UNIFORM_BUFFER, static_cast<GLuint>(p.ubo_index), b.buffer);
    else
        glr.glBindBufferRange(gl::UNIFORM_BUFFER, static_cast<GLuint>(p.ubo_index), b.buffer, b.offset, b.size);
    glr.glBindBuffer(gl::UNIFORM_BUFFER, t.buffers[buffer_slot(gl::UNIFORM_BUFFER)]);
}

void fetch_mono(GLuint program, UniformPatch& patch) {
    for (int i = 0; i < patch.count; ++i) glr.glGetUniformfv(program, patch.location + i, patch.mono + i * patch.floats_each);
    patch.have_mono = true;
}

void set_patches(ProgramInfo& info, int eye) {
    const Eye& e = g_eye[eye];
    for (UniformPatch& patch : info.patches) {
        if (!patch.have_mono) fetch_mono(t.program, patch);
        if (patch.kind == PatchKind::Corners) {
            Vec3 mono[4], out[4];
            for (int i = 0; i < 4; ++i) mono[i] = {patch.mono[i * 3], patch.mono[i * 3 + 1], patch.mono[i * 3 + 2]};
            if (!eye_corners(mono, g_mono.projection, g_mono.view, e.projection, e.view, out).ok) continue;
            float values[12];
            for (int i = 0; i < 4; ++i) {
                values[i * 3] = out[i].x;
                values[i * 3 + 1] = out[i].y;
                values[i * 3 + 2] = out[i].z;
            }
            glr.glUniform3fv(patch.location, 4, values);
            continue;
        }
        const Mat4 x = mat_at(patch.mono);
        Mat4 value = x;
        switch (patch.kind) {
            case PatchKind::Projection:
                if (!is_perspective(x)) continue;
                value = eye_projection(g_setup.headset_projection[eye], x);
                break;
            case PatchKind::InvProjection:
                value = inverse(eye_projection(g_setup.headset_projection[eye], inverse(x)));
                break;
            case PatchKind::View:
                value = e.view_fix * x;
                break;
            case PatchKind::Reproject:
                value = e.reproject;
                break;
            case PatchKind::ViewProjection:
                if (!is_perspective(x)) continue;
                value = e.mvp_fix * x;
                break;
            default:
                continue;
        }
        glr.glUniformMatrix4fv(patch.location, 1, gl::FALSE_, value.m);
    }
}

void restore_patches(ProgramInfo& info) {
    for (UniformPatch& patch : info.patches) {
        if (patch.floats_each == 16)
            glr.glUniformMatrix4fv(patch.location, patch.count, gl::FALSE_, patch.mono);
        else
            glr.glUniform3fv(patch.location, patch.count, patch.mono);
    }
}

void remap_textures(Pending& p) {
    p.restore_unit_count = 0;
    for (int unit = 0; unit < kUnits; ++unit) {
        const GLuint a = twin_of(t.tex2d[unit]), b = twin_of(t.tex2d_array[unit]);
        if (!a && !b) continue;
        glr.glActiveTexture(gl::TEXTURE0 + unit);
        if (a) glr.glBindTexture(gl::TEXTURE_2D, a);
        if (b) glr.glBindTexture(gl::TEXTURE_2D_ARRAY, b);
        p.restore_units[p.restore_unit_count++] = static_cast<GLuint>(unit);
    }
    if (p.restore_unit_count) glr.glActiveTexture(gl::TEXTURE0 + t.active_unit);
    p.restore_image_count = 0;
    if (!glr.glBindImageTexture) return;
    for (int unit = 0; unit < kImageUnits; ++unit) {
        const ImageBinding& image = t.images[unit];
        if (!image.texture) continue;
        const GLuint twin = ensure_twin(image.texture);
        if (!twin) continue;
        glr.glBindImageTexture(static_cast<GLuint>(unit), twin, image.level, image.layered, image.layer, image.access,
                               image.format);
        p.restore_images[p.restore_image_count++] = unit;
    }
}

void restore_textures(const Pending& p) {
    for (int i = 0; i < p.restore_unit_count; ++i) {
        const int unit = static_cast<int>(p.restore_units[i]);
        glr.glActiveTexture(gl::TEXTURE0 + unit);
        glr.glBindTexture(gl::TEXTURE_2D, t.tex2d[unit]);
        glr.glBindTexture(gl::TEXTURE_2D_ARRAY, t.tex2d_array[unit]);
    }
    if (p.restore_unit_count) glr.glActiveTexture(gl::TEXTURE0 + t.active_unit);
    for (int i = 0; i < p.restore_image_count; ++i) {
        const int unit = p.restore_images[i];
        const ImageBinding& image = t.images[unit];
        glr.glBindImageTexture(static_cast<GLuint>(unit), image.texture, image.level, image.layered, image.layer,
                               image.access, image.format);
    }
}

bool samples_stereo_texture() {
    for (int unit = 0; unit < kUnits; ++unit)
        if (twin_of(t.tex2d[unit])) return true;
    return false;
}

// Blend, scissor and rasterizer-discard state as Hytale sets it, so draws
// don't have to ask the driver (each question makes it wait for the GPU).
// The first answers are checked against the driver; any mismatch goes back
// to asking.
struct Caps {
    bool blend = false, scissor = false, discard = false;
    GLenum blend_func[4] = {gl::ONE, 0 /* ZERO */, gl::ONE, 0};
} g_caps;
int g_cap_checks = 400;
bool g_caps_trusted = true;

bool* tracked_cap(GLenum cap) {
    switch (cap) {
        case gl::BLEND: return &g_caps.blend;
        case gl::SCISSOR_TEST: return &g_caps.scissor;
        case 0x8C89 /* RASTERIZER_DISCARD */: return &g_caps.discard;
        default: return nullptr;
    }
}

bool cap_enabled(GLenum cap) {
    bool* tracked = tracked_cap(cap);
    if (!tracked || !g_caps_trusted) return glr.glIsEnabled(cap) != 0;
    if (g_cap_checks > 0) {
        --g_cap_checks;
        const bool real = glr.glIsEnabled(cap) != 0;
        if (real != *tracked) {
            logf("Stereo: GL state 0x%X wasn't where we tracked it; asking the driver from now on", cap);
            g_caps_trusted = false;
            return real;
        }
    }
    return *tracked;
}

void blend_funcs(GLint out[4]) {
    if (!g_caps_trusted || g_cap_checks > 0) {
        GLint real[4];
        glr.glGetIntegerv(gl::BLEND_SRC_RGB, &real[0]);
        glr.glGetIntegerv(gl::BLEND_DST_RGB, &real[1]);
        glr.glGetIntegerv(gl::BLEND_SRC_ALPHA, &real[2]);
        glr.glGetIntegerv(gl::BLEND_DST_ALPHA, &real[3]);
        if (g_caps_trusted) {
            --g_cap_checks;
            for (int i = 0; i < 4; ++i)
                if (static_cast<GLenum>(real[i]) != g_caps.blend_func[i]) {
                    logf("Stereo: blend state wasn't where we tracked it; asking the driver from now on");
                    g_caps_trusted = false;
                    break;
                }
        }
        for (int i = 0; i < 4; ++i) out[i] = real[i];
        return;
    }
    for (int i = 0; i < 4; ++i) out[i] = static_cast<GLint>(g_caps.blend_func[i]);
}

// Clears one of our own targets, whatever masks and scissor Hytale left on.
void clear_ours(GLuint fbo, bool color, bool depth_stencil) {
    const bool scissor = cap_enabled(gl::SCISSOR_TEST);
    GLboolean color_mask[4];
    GLboolean depth_mask = 1;
    GLint stencil_mask = -1;
    glr.glGetBooleanv(gl::COLOR_WRITEMASK, color_mask);
    glr.glGetBooleanv(gl::DEPTH_WRITEMASK, &depth_mask);
    glr.glGetIntegerv(gl::STENCIL_WRITEMASK, &stencil_mask);
    glr.glDisable(gl::SCISSOR_TEST);
    glr.glColorMask(1, 1, 1, 1);
    glr.glDepthMask(1);
    glr.glStencilMask(0xFFFFFFFFu);
    glr.glBindFramebuffer(gl::DRAW_FRAMEBUFFER, fbo);
    const float zero[4] = {0, 0, 0, 0};
    if (color) glr.glClearBufferfv(gl::COLOR, 0, zero);
    if (depth_stencil) glr.glClearBufferfi(gl::DEPTH_STENCIL, 0, 1.0f, 0);
    glr.glBindFramebuffer(gl::DRAW_FRAMEBUFFER, real_fb(t.draw_fb));
    glr.glStencilMask(static_cast<GLuint>(stencil_mask));
    glr.glDepthMask(depth_mask);
    glr.glColorMask(color_mask[0], color_mask[1], color_mask[2], color_mask[3]);
    if (scissor) glr.glEnable(gl::SCISSOR_TEST);
}

bool targets_ready() {
    return g_eye_targets[0].fbo() && g_eye_targets[1].fbo() && g_hud.fbo() && g_eye_targets[0].width() == g_window_w &&
           g_eye_targets[0].height() == g_window_h;
}

void begin_composite() {
    if (!g_composited) {
        clear_ours(g_eye_targets[1].fbo(), false, true);
        clear_ours(g_hud.fbo(), true, true);
    }
    g_composited = true;
    g_hud_since_composite = false;
    ++g_composites;
}

// Copies the window's back buffer (the left eye, as Hytale just drew it).
void copy_left_eye() {
    const bool scissor = cap_enabled(gl::SCISSOR_TEST);
    glr.glDisable(gl::SCISSOR_TEST);
    glr.glBindFramebuffer(gl::READ_FRAMEBUFFER, real_fb(0));
    glr.glBindFramebuffer(gl::DRAW_FRAMEBUFFER, g_eye_targets[0].fbo());
    glr.glBlitFramebuffer(0, 0, g_window_w, g_window_h, 0, 0, g_window_w, g_window_h, gl::COLOR_BUFFER_BIT, gl::NEAREST);
    glr.glBindFramebuffer(gl::READ_FRAMEBUFFER, real_fb(t.read_fb));
    glr.glBindFramebuffer(gl::DRAW_FRAMEBUFFER, real_fb(t.draw_fb));
    if (scissor) glr.glEnable(gl::SCISSOR_TEST);
}

}  // namespace

// --- Session side ---

void set_eyes(const EyeSetup& setup) {
    g_render_thread = GetCurrentThreadId();
    g_setup = setup;
    g_setup_frame = g_frame;
    g_wanted = true;
    // Fresh poses for loose uniforms even before this frame's camera block shows up.
    if (g_mono.valid) compute_eyes(g_mono);
}

void disable() { g_wanted = false; }
bool wanted() { return g_wanted; }

bool active() {
    return g_wanted && !g_failed_frame && g_setup_frame >= g_frame - 1 && on_render_thread() && targets_ready();
}

GLuint hud_fbo_this_frame() { return g_composited ? g_hud.fbo() : 0; }

bool engine_camera(Mat4& view, Vec3& forward, Vec3& position) {
    if (!g_mono.valid) return false;
    position = g_mono.camera;
    view = g_mono.view;
    forward = {-view.at(2, 0), -view.at(2, 1), -view.at(2, 2)};
    return true;
}

FrameResult end_frame(int window_w, int window_h) {
    g_render_thread = GetCurrentThreadId();
    FrameResult result;
    if (g_composited && active()) {
        result.stereo = true;
        for (int e = 0; e < 2; ++e) {
            result.eye_texture[e] = g_eye_targets[e].texture();
            result.eye_fbo[e] = g_eye_targets[e].fbo();
        }
        result.hud_texture = g_hud.texture();
        result.hud_fbo = g_hud.fbo();
        result.width = g_window_w;
        result.height = g_window_h;
    }
    if (g_eye_valid && g_eye_frame == g_frame) {
        for (int e = 0; e < 2; ++e) {
            g_previous[e].projection = g_eye[e].projection;
            g_previous[e].view = g_eye[e].view;
            g_previous[e].camera = g_eye[e].camera;
            g_previous[e].valid = true;
        }
    } else {
        for (Previous& p : g_previous) p.valid = false;
    }
    static int logged = 0;
    if (result.stereo && logged < 1) {
        ++logged;
        logf("Stereo: first frame in the world (%d doubled draws, %d composites)", g_stereo_draws, g_composites);
    }

    // Counts for the periodic log line, to see why frames weren't per eye.
    ++g_stats.frames;
    if (g_wanted) ++g_stats.wanted;
    if (g_wanted && g_setup_frame < g_frame - 1) ++g_stats.stale_setup;
    if (g_failed_frame) ++g_stats.failed;
    if (g_wanted && !targets_ready()) ++g_stats.no_targets;
    if (g_composited) ++g_stats.composited;
    if (result.stereo) ++g_stats.stereo;
    if (g_stereo_draws == 0 && g_wanted) ++g_stats.no_draws;

    ++g_frame;
    g_failed_frame = false;
    g_composited = false;
    g_hud_since_composite = false;
    g_stereo_draws = g_composites = 0;
    g_window_w = window_w;
    g_window_h = window_h;
    if (g_wanted && window_w > 0 && window_h > 0) {
        bool ok = g_eye_targets[0].resize(window_w, window_h) && g_eye_targets[1].resize(window_w, window_h) &&
                  g_hud.resize(window_w, window_h);
        if (!ok) problem("couldn't make the eye textures");
    }
    return result;
}

// --- Hooks ---

void on_bind_texture(GLenum target, GLuint texture) {
    if (on_render_thread())
        if (TextureInfo* info = texture_info(texture, true))
            if (!info->target) info->target = target;
    if (t.active_unit < 0 || t.active_unit >= kUnits) return;
    if (target == gl::TEXTURE_2D) t.tex2d[t.active_unit] = texture;
    else if (target == gl::TEXTURE_2D_ARRAY) t.tex2d_array[t.active_unit] = texture;
}

void on_active_texture(GLenum unit) {
    const int index = static_cast<int>(unit) - static_cast<int>(gl::TEXTURE0);
    t.active_unit = index >= 0 && index < kUnits ? index : 0;
}

void on_bind_textures(GLuint first, GLsizei count, const GLuint* textures) {
    for (GLsizei i = 0; i < count; ++i) {
        const GLuint unit = first + static_cast<GLuint>(i);
        if (unit >= kUnits) break;
        const GLuint texture = textures ? textures[i] : 0;
        const GLenum target = texture && on_render_thread() ? texture_target(texture) : gl::TEXTURE_2D;
        if (!texture) {
            t.tex2d[unit] = t.tex2d_array[unit] = 0;
        } else if (target == gl::TEXTURE_2D) {
            t.tex2d[unit] = texture;
        } else if (target == gl::TEXTURE_2D_ARRAY) {
            t.tex2d_array[unit] = texture;
        }
    }
}

void on_bind_texture_unit(GLuint unit, GLuint texture) { on_bind_textures(unit, 1, &texture); }

void on_bind_image_texture(GLuint unit, GLuint texture, GLint level, GLboolean layered, GLint layer, GLenum access,
                           GLenum format) {
    if (unit >= kImageUnits) return;
    t.images[unit] = {texture, level, layered, layer, access, format};
}

void on_bind_framebuffer(GLenum target, GLuint framebuffer) {
    // Hytale may read back our buffer's name as the current binding and bind it again.
    // (Kept even after it's turned off, so that name never means anything else.)
    const bool ours = framebuffer != 0 && framebuffer == g_vfb.fbo();
    if (ours) framebuffer = 0;
    if (target == gl::FRAMEBUFFER || target == gl::DRAW_FRAMEBUFFER) t.draw_fb = framebuffer;
    if (target == gl::FRAMEBUFFER || target == gl::READ_FRAMEBUFFER) t.read_fb = framebuffer;
    if (framebuffer == 0 && on_render_thread() && (g_vfb_on || ours)) glr.glBindFramebuffer(target, real_fb(0));
}

GLenum map_color_buffer(bool draw, GLenum mode) {
    if (!g_vfb_on || !on_render_thread() || (draw ? t.draw_fb : t.read_fb) != 0) return mode;
    switch (mode) {
        case 0x0400: case 0x0401: case 0x0402: case 0x0403: case 0x0404: case 0x0405: case 0x0406: case 0x0407:
        case 0x0408:  // FRONT_LEFT .. FRONT_AND_BACK
            return gl::COLOR_ATTACHMENT0;
        default:
            return mode;
    }
}

bool set_virtual_backbuffer(int width, int height) {
    g_render_thread = GetCurrentThreadId();
    const bool on = width > 0 && height > 0;
    if (on && !g_vfb.resize(width, height)) {
        problem("couldn't make the high-resolution window buffer");
        return set_virtual_backbuffer(0, 0);
    }
    if (on != g_vfb_on) logf(on ? "Window buffer %dx%d for the headset" : "Window buffer back to the window%s", width, height);
    g_vfb_on = on;
    // Hytale's current binding of "the window" moves with it.
    if (t.draw_fb == 0) glr.glBindFramebuffer(gl::DRAW_FRAMEBUFFER, real_fb(0));
    if (t.read_fb == 0) glr.glBindFramebuffer(gl::READ_FRAMEBUFFER, real_fb(0));
    // Shrink rather than delete, so the framebuffer's name stays ours.
    if (!on && g_vfb.fbo()) g_vfb.resize(16, 16);
    return on;
}

void rebind_window() {
    if (!on_render_thread()) return;
    if (t.draw_fb == 0) glr.glBindFramebuffer(gl::DRAW_FRAMEBUFFER, real_fb(0));
    if (t.read_fb == 0) glr.glBindFramebuffer(gl::READ_FRAMEBUFFER, real_fb(0));
}

GLuint virtual_backbuffer() { return g_vfb_on ? g_vfb.fbo() : 0; }

void present_virtual_backbuffer(int window_w, int window_h) {
    if (!g_vfb_on || window_w <= 0 || window_h <= 0) return;
    const bool scissor = cap_enabled(gl::SCISSOR_TEST);
    glr.glDisable(gl::SCISSOR_TEST);
    glr.glBindFramebuffer(gl::READ_FRAMEBUFFER, g_vfb.fbo());
    glr.glBindFramebuffer(gl::DRAW_FRAMEBUFFER, 0);
    glr.glBlitFramebuffer(0, 0, g_vfb.width(), g_vfb.height(), 0, 0, window_w, window_h, gl::COLOR_BUFFER_BIT, gl::LINEAR);
    glr.glBindFramebuffer(gl::READ_FRAMEBUFFER, real_fb(t.read_fb));
    glr.glBindFramebuffer(gl::DRAW_FRAMEBUFFER, real_fb(t.draw_fb));
    if (scissor) glr.glEnable(gl::SCISSOR_TEST);
}

int render_width() { return g_main_w; }

namespace {
int g_window_viewport_w = 0;
}

void on_layout_change() { ++g_layout_epoch; }

void on_enable(GLenum cap, bool on) {
    if (bool* tracked = tracked_cap(cap)) *tracked = on;
}

void on_blend_func(GLenum src_rgb, GLenum dst_rgb, GLenum src_alpha, GLenum dst_alpha) {
    g_caps.blend_func[0] = src_rgb;
    g_caps.blend_func[1] = dst_rgb;
    g_caps.blend_func[2] = src_alpha;
    g_caps.blend_func[3] = dst_alpha;
}

void on_viewport(GLsizei width) {
    if (on_render_thread() && t.draw_fb == 0) g_window_viewport_w = width;
}

int window_viewport_width() { return g_window_viewport_w; }

void on_use_program(GLuint program) { t.program = program; }

void on_link(GLuint program) {
    std::lock_guard lock(g_relink_mutex);
    g_relinked.push_back(program);
}

void on_uniform_block_binding(GLuint program, GLuint index, GLuint binding) {
    if (!on_render_thread()) return;
    auto it = g_programs.find(program);
    if (it != g_programs.end() && it->second.scene_block == static_cast<GLint>(index))
        it->second.scene_binding = static_cast<GLint>(binding);
}

void on_bind_buffer(GLenum target, GLuint buffer) {
    const int slot = buffer_slot(target);
    if (slot >= 0) t.buffers[slot] = buffer;
}

void on_bind_buffer_range(GLenum target, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size) {
    on_bind_buffer(target, buffer);
    if (target == gl::UNIFORM_BUFFER && index < kUboIndices) t.ubo[index] = {buffer, offset, size};
}

GLuint current_program() { return t.program; }

GLuint buffer_bound(GLenum target) {
    const int slot = buffer_slot(target);
    return slot >= 0 ? t.buffers[slot] : 0;
}

void on_buffer_data(GLuint buffer, GLsizeiptr size, const void* data) {
    std::lock_guard lock(g_shadow_mutex);
    auto it = g_shadows.find(buffer);
    if (it == g_shadows.end() || size < 0 || size > (1 << 22)) return;
    Shadow& shadow = it->second;
    shadow.data.assign(static_cast<size_t>(size), 0);
    if (data) std::memcpy(shadow.data.data(), data, static_cast<size_t>(size));
    shadow.written = data != nullptr;
    shadow.map = nullptr;
    shadow.persistent = false;
    ++shadow.version;
}

void on_buffer_sub_data(GLuint buffer, GLintptr offset, GLsizeiptr size, const void* data) {
    if (!data || offset < 0 || size <= 0) return;
    std::lock_guard lock(g_shadow_mutex);
    auto it = g_shadows.find(buffer);
    if (it == g_shadows.end()) return;
    Shadow& shadow = it->second;
    const size_t end = static_cast<size_t>(offset + size);
    if (end > (1u << 22)) return;
    if (shadow.data.size() < end) shadow.data.resize(end);
    std::memcpy(shadow.data.data() + offset, data, static_cast<size_t>(size));
    shadow.written = true;
    ++shadow.version;
}

void on_map(GLuint buffer, GLintptr offset, GLsizeiptr length, GLbitfield access, void* pointer) {
    std::lock_guard lock(g_shadow_mutex);
    auto it = g_shadows.find(buffer);
    if (it == g_shadows.end() || !pointer) return;
    Shadow& shadow = it->second;
    if (length <= 0) length = static_cast<GLsizeiptr>(shadow.data.size());  // whole-buffer map
    if (length <= 0) return;
    shadow.map = static_cast<uint8_t*>(pointer);
    shadow.map_offset = offset;
    shadow.map_length = length;
    shadow.persistent = (access & 0x0040 /* MAP_PERSISTENT_BIT */) != 0;
}

void on_flush(GLuint buffer, GLintptr offset, GLsizeiptr length) {
    std::lock_guard lock(g_shadow_mutex);
    auto it = g_shadows.find(buffer);
    if (it == g_shadows.end()) return;
    Shadow& shadow = it->second;
    if (!shadow.map || offset < 0 || length <= 0 || offset + length > shadow.map_length) return;
    const size_t at = static_cast<size_t>(shadow.map_offset + offset);
    if (shadow.data.size() < at + static_cast<size_t>(length)) shadow.data.resize(at + static_cast<size_t>(length));
    std::memcpy(shadow.data.data() + at, shadow.map + offset, static_cast<size_t>(length));
    shadow.written = true;
    ++shadow.version;
}

void on_unmap(GLuint buffer) {
    std::lock_guard lock(g_shadow_mutex);
    auto it = g_shadows.find(buffer);
    if (it == g_shadows.end()) return;
    Shadow& shadow = it->second;
    if (shadow.map && !shadow.persistent) {
        const size_t at = static_cast<size_t>(shadow.map_offset);
        const size_t length = static_cast<size_t>(shadow.map_length);
        if (shadow.data.size() < at + length) shadow.data.resize(at + length);
        std::memcpy(shadow.data.data() + at, shadow.map, length);
        shadow.written = true;
        ++shadow.version;
    }
    shadow.map = nullptr;
    shadow.persistent = false;
}

void on_uniform(GLuint program, GLint location, GLsizei count, const GLfloat* value, int floats_each) {
    if (!value || location < 0 || !on_render_thread()) return;
    auto it = g_programs.find(program);
    if (it == g_programs.end()) return;
    for (UniformPatch& patch : it->second.patches) {
        if (patch.floats_each != floats_each || location < patch.location || location >= patch.location + patch.count)
            continue;
        const int first = location - patch.location;
        const int n = count < patch.count - first ? count : patch.count - first;
        std::memcpy(patch.mono + first * floats_each, value, static_cast<size_t>(n * floats_each) * sizeof(float));
        if (first == 0 && n == patch.count) patch.have_mono = true;
        else if (!patch.have_mono) fetch_mono(program, patch);
    }
}

void on_draw_buffers(GLsizei count, const GLenum* buffers) {
    if (!on_render_thread()) return;
    if (t.draw_fb == 0) {
        if (g_vfb_on && count > 0 && buffers) {
            GLenum mapped = map_color_buffer(true, buffers[0]);
            glr.glDrawBuffers(1, &mapped);
        }
        return;
    }
    auto it = g_fbos.find(t.draw_fb);
    if (it == g_fbos.end() || !it->second.twin) return;
    glr.glBindFramebuffer(gl::DRAW_FRAMEBUFFER, it->second.twin);
    glr.glDrawBuffers(count, buffers);
    glr.glBindFramebuffer(gl::DRAW_FRAMEBUFFER, real_fb(t.draw_fb));
}

void on_delete_framebuffers(GLsizei count, const GLuint* framebuffers) {
    if (!framebuffers || !on_render_thread()) return;
    for (GLsizei i = 0; i < count; ++i) {
        auto it = g_fbos.find(framebuffers[i]);
        if (it == g_fbos.end()) continue;
        destroy_twin_fbo(it->second);
        g_fbos.erase(it);
    }
}

// --- Draws ---

bool skip_draw(GLenum mode) {
    if ((mode != gl::TRIANGLES && mode != gl::POINTS) || !g_setup.no_occlusion || !active() || t.draw_fb == 0 ||
        !t.program)
        return false;
    FboInfo& info = g_fbos[t.draw_fb];
    classify(gl::DRAW_FRAMEBUFFER, t.draw_fb);
    if (!info.occluder) return false;
    static bool logged[2] = {};
    if (mode == gl::POINTS) {
        // Last frame's depth, reprojected into the occlusion map one point per
        // texel. That depth came from the eyes, not Hytale's camera, so it hides
        // chunks that are really in view.
        if (cap_enabled(0x8C89 /* RASTERIZER_DISCARD */)) return false;
        if (!logged[1]) {
            logged[1] = true;
            logf("Stereo: skipping Hytale's reprojected occlusion depth");
        }
        return true;
    }
    // Occluders are drawn with the camera block; the depth pyramid built from
    // them afterwards isn't, and runs on the cleared depth.
    if (program_info(t.program).scene_binding < 0) return false;
    if (!logged[0]) {
        logged[0] = true;
        logf("Stereo: skipping Hytale's occluders");
    }
    return true;
}

bool draw_begin(GLenum mode, GLsizei count) {
    g_pending = Pending{};
    if (!active()) return false;
    // Simulation passes (transform feedback with rasterizing off) must run once.
    if (mode == gl::POINTS && cap_enabled(0x8C89 /* RASTERIZER_DISCARD */)) return false;
    Pending& p = g_pending;
    if (t.draw_fb != 0) {
        if (!classify(gl::DRAW_FRAMEBUFFER, t.draw_fb)) return false;
        p.kind = Kind::Target;
    } else {
        const bool fullscreen = (mode == gl::TRIANGLES || mode == gl::TRIANGLE_STRIP) && count > 0 && count <= 6;
        if (fullscreen && (!g_composited || !g_hud_since_composite) && samples_stereo_texture()) {
            p.kind = Kind::Composite;
        } else if (g_composited) {
            p.kind = Kind::Hud;
        } else {
            return false;
        }
    }
    if (t.program) {
        ProgramInfo& info = program_info(t.program);
        p.program = &info;
        if (p.kind != Kind::Hud && info.scene_binding >= 0 && info.scene_binding < kUboIndices &&
            info.scene_size >= sd::kBlockSize && t.ubo[info.scene_binding].buffer) {
            const UboBinding& b = t.ubo[info.scene_binding];
            p.eye_ubo = refresh_scene(b.buffer, b.offset, info.scene_size);
            if (g_failed_frame) {
                g_pending = Pending{};
                return false;
            }
            if (p.eye_ubo) {
                p.ubo_index = info.scene_binding;
                bind_eye_ubo(p, 0);
            }
        }
        if (!g_eye_valid) {
            // No camera block seen yet this session: nothing to place the eyes with.
            if (p.ubo_index >= 0) restore_ubo(p);
            g_pending = Pending{};
            return false;
        }
        if (p.kind != Kind::Hud && !info.patches.empty()) set_patches(info, 0);
    }
    if (p.kind == Kind::Composite) begin_composite();
    ++g_stereo_draws;
    return true;
}

void draw_second() {
    Pending& p = g_pending;
    switch (p.kind) {
        case Kind::Target: {
            FboInfo& info = g_fbos[t.draw_fb];
            glr.glBindFramebuffer(gl::DRAW_FRAMEBUFFER, info.twin);
            remap_textures(p);
            break;
        }
        case Kind::Composite:
            glr.glBindFramebuffer(gl::DRAW_FRAMEBUFFER, g_eye_targets[1].fbo());
            remap_textures(p);
            break;
        case Kind::Hud:
            glr.glBindFramebuffer(gl::DRAW_FRAMEBUFFER, g_hud.fbo());
            if (cap_enabled(gl::BLEND)) {
                blend_funcs(p.blend);
                // Keep the panel's alpha meaningful: coverage accumulates.
                glr.glBlendFuncSeparate(p.blend[0], p.blend[1], gl::ONE, gl::ONE_MINUS_SRC_ALPHA);
                p.blend_changed = true;
            }
            break;
        default:
            break;
    }
    if (p.ubo_index >= 0) bind_eye_ubo(p, 1);
    if (p.program && p.kind != Kind::Hud && !p.program->patches.empty()) set_patches(*p.program, 1);
}

void draw_finish() {
    Pending& p = g_pending;
    glr.glBindFramebuffer(gl::DRAW_FRAMEBUFFER, real_fb(t.draw_fb));
    restore_textures(p);
    if (p.blend_changed) glr.glBlendFuncSeparate(p.blend[0], p.blend[1], p.blend[2], p.blend[3]);
    if (p.ubo_index >= 0) restore_ubo(p);
    if (p.program && p.kind != Kind::Hud && !p.program->patches.empty()) restore_patches(*p.program);
    if (p.kind == Kind::Composite) copy_left_eye();
    if (p.kind == Kind::Hud) g_hud_since_composite = true;
    g_pending = Pending{};
}

void after_clear(GLbitfield mask) {
    if (!active()) return;
    if (t.draw_fb == 0) {
        if (g_composited && (mask & gl::COLOR_BUFFER_BIT)) clear_ours(g_hud.fbo(), true, (mask & gl::DEPTH_BUFFER_BIT) != 0);
        return;
    }
    FboInfo* info = classify(gl::DRAW_FRAMEBUFFER, t.draw_fb);
    if (!info) return;
    glr.glBindFramebuffer(gl::DRAW_FRAMEBUFFER, info->twin);
    glr.glClear(mask);
    glr.glBindFramebuffer(gl::DRAW_FRAMEBUFFER, real_fb(t.draw_fb));
}

void after_clear_buffer(GLenum buffer, GLint drawbuffer, const void* value, int kind) {
    if (!active() || t.draw_fb == 0) return;
    FboInfo* info = classify(gl::DRAW_FRAMEBUFFER, t.draw_fb);
    if (!info) return;
    glr.glBindFramebuffer(gl::DRAW_FRAMEBUFFER, info->twin);
    if (kind == 0) glr.glClearBufferfv(buffer, drawbuffer, static_cast<const GLfloat*>(value));
    else if (kind == 1) glr.glClearBufferiv(buffer, drawbuffer, static_cast<const GLint*>(value));
    else glr.glClearBufferuiv(buffer, drawbuffer, static_cast<const GLuint*>(value));
    glr.glBindFramebuffer(gl::DRAW_FRAMEBUFFER, real_fb(t.draw_fb));
}

void after_clear_buffer_fi(GLenum buffer, GLint drawbuffer, GLfloat depth, GLint stencil) {
    if (!active() || t.draw_fb == 0) return;
    FboInfo* info = classify(gl::DRAW_FRAMEBUFFER, t.draw_fb);
    if (!info) return;
    glr.glBindFramebuffer(gl::DRAW_FRAMEBUFFER, info->twin);
    glr.glClearBufferfi(buffer, drawbuffer, depth, stencil);
    glr.glBindFramebuffer(gl::DRAW_FRAMEBUFFER, real_fb(t.draw_fb));
}

void after_blit(GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0, GLint dx1, GLint dy1, GLbitfield mask,
                GLenum filter) {
    if (!active()) return;
    FboInfo* read = classify(gl::READ_FRAMEBUFFER, t.read_fb);
    GLuint draw_twin = 0;
    bool composite = false;
    if (t.draw_fb != 0) {
        FboInfo* draw = classify(gl::DRAW_FRAMEBUFFER, t.draw_fb);
        if (!draw) return;
        draw_twin = draw->twin;
    } else {
        if (!read || !(mask & gl::COLOR_BUFFER_BIT)) return;
        composite = true;
        begin_composite();
        draw_twin = g_eye_targets[1].fbo();
        mask = gl::COLOR_BUFFER_BIT;
    }
    glr.glBindFramebuffer(gl::READ_FRAMEBUFFER, read ? read->twin : real_fb(t.read_fb));
    glr.glBindFramebuffer(gl::DRAW_FRAMEBUFFER, draw_twin);
    glr.glBlitFramebuffer(sx0, sy0, sx1, sy1, dx0, dy0, dx1, dy1, mask, filter);
    glr.glBindFramebuffer(gl::READ_FRAMEBUFFER, real_fb(t.read_fb));
    glr.glBindFramebuffer(gl::DRAW_FRAMEBUFFER, real_fb(t.draw_fb));
    if (composite) copy_left_eye();
}

void after_generate_mipmap(GLenum target) {
    if (!active() || target != gl::TEXTURE_2D || t.active_unit >= kUnits) return;
    const GLuint twin = twin_of(t.tex2d[t.active_unit]);
    if (!twin) return;
    glr.glBindTexture(gl::TEXTURE_2D, twin);
    glr.glGenerateMipmap(gl::TEXTURE_2D);
    glr.glBindTexture(gl::TEXTURE_2D, t.tex2d[t.active_unit]);
}

void add_draw_time(long long ticks) {
    g_stats.draw_ticks += ticks;
    ++g_stats.draws;
}

std::string take_frame_stats() {
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    const double ms = g_stats.stereo > 0 ? 1000.0 * static_cast<double>(g_stats.draw_ticks) /
                                               static_cast<double>(frequency.QuadPart) / g_stats.stereo
                                         : 0.0;
    char text[384];
    std::snprintf(text, sizeof(text),
                  "%d frames: %d wanted 3D, %d per eye, %d composited, %d no doubled draws, %d late setup, %d camera "
                  "failed, %d no targets; %.1f ms and %lld draws per 3D frame in our draw hooks; steady view up to "
                  "%.3f off the camera",
                  g_stats.frames, g_stats.wanted, g_stats.stereo, g_stats.composited, g_stats.no_draws,
                  g_stats.stale_setup, g_stats.failed, g_stats.no_targets, ms,
                  g_stats.stereo > 0 ? g_stats.draws / g_stats.stereo : 0LL, g_stats.smoothed_max);
    g_stats = Stats{};
    return text;
}

std::string summary() {
    char text[256];
    int stereo = 0;
    for (const auto& [fb, info] : g_fbos)
        if (info.stereo) ++stereo;
    std::snprintf(text, sizeof(text), "%d of %zu framebuffers per eye, render size %dx%d%s", stereo, g_fbos.size(),
                  g_main_w, g_main_h, g_readback_only ? ", camera read back" : "");
    return text;
}

}  // namespace hvr::stereo
