#include "census.h"

#include <windows.h>

#include <atomic>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "internal.h"
#include "log.h"
#include "sdl_hooks.h"

namespace hvr::census {
namespace {

constexpr size_t kMaxCaptureBytes = 16u << 20;

thread_local bool t_render = false;

// Shader sources and program setup, from any thread.
std::mutex g_programs_mutex;
std::map<GLuint, std::string> g_shader_sources;
std::map<GLuint, std::string> g_labels;  // programs and framebuffers, keyed by kind << 32
std::atomic<int> g_programs_linked{0};
bool g_shader_dir_ready = false;

// Render thread state.
GLuint g_program = 0;
GLuint g_draw_fb = 0;
GLuint g_read_fb = 0;
int g_draws = 0;
std::atomic<int> g_draws_last_frame{0};
int g_viewport[4] = {};

// Capture state.
std::atomic<bool> g_capture_requested{false};
bool g_capturing = false;
std::string g_out;
std::set<GLuint> g_described_programs;
std::set<GLuint> g_described_fbs;
std::set<std::pair<GLuint, long long>> g_dumped_buffers;
std::map<GLuint, int> g_draws_per_fb;
std::map<GLuint, int> g_draws_per_program;
int g_capture_draws = 0;
std::mutex g_path_mutex;
std::wstring g_last_path;

uint64_t label_key(GLenum kind, GLuint name) { return (static_cast<uint64_t>(kind) << 32) | name; }

void out(const char* format, ...) {
    if (g_out.size() > kMaxCaptureBytes) return;
    char line[1024];
    va_list args;
    va_start(args, format);
    const int length = vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    if (length > 0) g_out.append(line, static_cast<size_t>(length < 1023 ? length : 1023));
    g_out.push_back('\n');
}

bool ensure_gl() {
    if (glr.ready) return true;
    return gl_api_init();
}

std::string label_of(GLenum kind, GLuint name) {
    if (name == 0) return {};
    if (glr.glGetObjectLabel) {
        char buffer[256] = {};
        GLsizei length = 0;
        glr.glGetObjectLabel(kind, name, sizeof(buffer), &length, buffer);
        if (length > 0) return std::string(buffer, static_cast<size_t>(length));
    }
    std::lock_guard lock(g_programs_mutex);
    const auto found = g_labels.find(label_key(kind, name));
    return found == g_labels.end() ? std::string{} : found->second;
}

const char* mode_name(GLenum mode) {
    switch (mode) {
        case 0x0000: return "points";
        case 0x0001: return "lines";
        case 0x0003: return "line_strip";
        case 0x0004: return "triangles";
        case 0x0005: return "tri_strip";
        case 0x0006: return "tri_fan";
        default: return "other";
    }
}

const char* shader_suffix(GLint type) {
    switch (type) {
        case gl::VERTEX_SHADER: return "vert";
        case gl::FRAGMENT_SHADER: return "frag";
        case gl::GEOMETRY_SHADER: return "geom";
        case gl::COMPUTE_SHADER: return "comp";
        default: return "glsl";
    }
}

void write_file(const std::wstring& path, const std::string& text) {
    FILE* file = _wfopen(path.c_str(), L"wb");
    if (!file) return;
    std::fwrite(text.data(), 1, text.size(), file);
    std::fclose(file);
}

std::wstring shader_dir() { return log_dir() + L"\\shaders"; }

// Clears last session's shader dumps once.
void prepare_shader_dir() {
    if (g_shader_dir_ready) return;
    g_shader_dir_ready = true;
    ensure_dir(log_dir());
    ensure_dir(shader_dir());
    WIN32_FIND_DATAW found;
    HANDLE search = FindFirstFileW((shader_dir() + L"\\*").c_str(), &found);
    if (search == INVALID_HANDLE_VALUE) return;
    do {
        if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) DeleteFileW((shader_dir() + L"\\" + found.cFileName).c_str());
    } while (FindNextFileW(search, &found));
    FindClose(search);
}

std::string safe_name(std::string text) {
    for (char& c : text)
        if (!(isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-')) c = '_';
    if (text.size() > 48) text.resize(48);
    return text;
}

void describe_fb(GLuint fb) {
    if (fb == 0 || !g_described_fbs.insert(fb).second) return;
    out("  framebuffer %u \"%s\"", fb, label_of(gl::FRAMEBUFFER, fb).c_str());
    if (!glr.has_dsa) return;
    GLenum attachments[10];
    const char* names[10] = {"color0", "color1", "color2", "color3", "color4", "color5", "color6", "color7", "depth", "stencil"};
    for (int i = 0; i < 8; ++i) attachments[i] = gl::COLOR_ATTACHMENT0 + i;
    attachments[8] = gl::DEPTH_ATTACHMENT;
    attachments[9] = gl::STENCIL_ATTACHMENT;
    for (int i = 0; i < 10; ++i) {
        GLint type = 0, name = 0, level = 0, layer = 0;
        glr.glGetNamedFramebufferAttachmentParameteriv(fb, attachments[i], gl::FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &type);
        if (type == 0) continue;
        glr.glGetNamedFramebufferAttachmentParameteriv(fb, attachments[i], gl::FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &name);
        GLint width = 0, height = 0, format = 0, samples = 0;
        if (type == static_cast<GLint>(gl::TEXTURE)) {
            glr.glGetNamedFramebufferAttachmentParameteriv(fb, attachments[i], gl::FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL, &level);
            glr.glGetNamedFramebufferAttachmentParameteriv(fb, attachments[i], gl::FRAMEBUFFER_ATTACHMENT_TEXTURE_LAYER, &layer);
            glr.glGetTextureLevelParameteriv(name, level, gl::TEXTURE_WIDTH, &width);
            glr.glGetTextureLevelParameteriv(name, level, gl::TEXTURE_HEIGHT, &height);
            glr.glGetTextureLevelParameteriv(name, level, gl::TEXTURE_INTERNAL_FORMAT, &format);
            glr.glGetTextureLevelParameteriv(name, level, gl::TEXTURE_SAMPLES, &samples);
            out("    %-7s texture %d level %d layer %d  %dx%d format 0x%04X samples %d \"%s\"", names[i], name, level,
                layer, width, height, format, samples, label_of(gl::TEXTURE, static_cast<GLuint>(name)).c_str());
        } else {
            glr.glGetNamedRenderbufferParameteriv(name, gl::RENDERBUFFER_WIDTH, &width);
            glr.glGetNamedRenderbufferParameteriv(name, gl::RENDERBUFFER_HEIGHT, &height);
            glr.glGetNamedRenderbufferParameteriv(name, gl::RENDERBUFFER_INTERNAL_FORMAT, &format);
            glr.glGetNamedRenderbufferParameteriv(name, gl::RENDERBUFFER_SAMPLES, &samples);
            out("    %-7s renderbuffer %d  %dx%d format 0x%04X samples %d", names[i], name, width, height, format, samples);
        }
    }
    while (glr.glGetError() != 0) {
    }
}

void dump_buffer(const char* block, GLuint buffer, long long start, long long size) {
    if (buffer == 0 || !glr.has_dsa || !g_dumped_buffers.insert({buffer, start}).second) return;
    const long long bytes = size > 0 ? (size < 4096 ? size : 4096) : 1024;
    std::vector<float> values(static_cast<size_t>(bytes / 4));
    glr.glGetNamedBufferSubData(buffer, static_cast<GLintptr>(start), static_cast<GLsizeiptr>(values.size() * 4), values.data());
    if (glr.glGetError() != 0) {
        out("      (could not read buffer %u)", buffer);
        return;
    }
    out("      %s data, buffer %u offset %lld, %zu floats:", block, buffer, start, values.size());
    for (size_t i = 0; i < values.size(); i += 4) {
        char row[160];
        int used = snprintf(row, sizeof(row), "      [%4zu]", i);
        for (size_t j = i; j < i + 4 && j < values.size(); ++j)
            used += snprintf(row + used, sizeof(row) - static_cast<size_t>(used), " %13.5f", values[j]);
        out("%s", row);
    }
}

void describe_program(GLuint program) {
    if (program == 0 || !g_described_programs.insert(program).second) return;
    out("  program %u \"%s\"", program, label_of(gl::PROGRAM, program).c_str());
    GLint blocks = 0;
    glr.glGetProgramiv(program, gl::ACTIVE_UNIFORM_BLOCKS, &blocks);
    for (GLint b = 0; b < blocks; ++b) {
        char name[128] = {};
        glr.glGetActiveUniformBlockName(program, static_cast<GLuint>(b), sizeof(name), nullptr, name);
        GLint binding = 0, data_size = 0;
        glr.glGetActiveUniformBlockiv(program, static_cast<GLuint>(b), gl::UNIFORM_BLOCK_BINDING, &binding);
        glr.glGetActiveUniformBlockiv(program, static_cast<GLuint>(b), gl::UNIFORM_BLOCK_DATA_SIZE, &data_size);
        GLint buffer = 0;
        long long start = 0, size = 0;
        glr.glGetIntegeri_v(gl::UNIFORM_BUFFER_BINDING, static_cast<GLuint>(binding), &buffer);
        if (glr.glGetInteger64i_v) {
            glr.glGetInteger64i_v(gl::UNIFORM_BUFFER_START, static_cast<GLuint>(binding), &start);
            glr.glGetInteger64i_v(gl::UNIFORM_BUFFER_SIZE, static_cast<GLuint>(binding), &size);
        }
        out("    block %-24s binding %2d size %5d  <- buffer %d offset %lld size %lld", name, binding, data_size, buffer,
            start, size);
        dump_buffer(name, static_cast<GLuint>(buffer), start, size > 0 ? size : data_size);
    }
    GLint uniforms = 0;
    glr.glGetProgramiv(program, gl::ACTIVE_UNIFORMS, &uniforms);
    for (GLint u = 0; u < uniforms && u < 96; ++u) {
        char name[128] = {};
        GLint array = 0;
        GLenum type = 0;
        glr.glGetActiveUniform(program, static_cast<GLuint>(u), sizeof(name), nullptr, &array, &type, name);
        const GLint location = glr.glGetUniformLocation(program, name);
        if (location < 0) continue;  // block member
        int floats = 0;
        switch (type) {
            case gl::FLOAT: floats = 1; break;
            case gl::FLOAT_VEC2: floats = 2; break;
            case gl::FLOAT_VEC3: floats = 3; break;
            case gl::FLOAT_VEC4: floats = 4; break;
            case gl::FLOAT_MAT3: floats = 9; break;
            case gl::FLOAT_MAT4: floats = 16; break;
            default: break;
        }
        if (floats == 0) {
            out("    uniform %-28s type 0x%04X loc %d", name, type, location);
            continue;
        }
        float values[16] = {};
        glr.glGetUniformfv(program, location, values);
        char row[512];
        int used = snprintf(row, sizeof(row), "    uniform %-28s loc %3d =", name, location);
        for (int i = 0; i < floats; ++i)
            used += snprintf(row + used, sizeof(row) - static_cast<size_t>(used), " %.5g", values[i]);
        out("%s", row);
    }
    while (glr.glGetError() != 0) {
    }
}

void begin_capture() {
    g_capturing = true;
    g_out.clear();
    g_out.reserve(4u << 20);
    g_described_programs.clear();
    g_described_fbs.clear();
    g_dumped_buffers.clear();
    g_draws_per_fb.clear();
    g_draws_per_program.clear();
    g_capture_draws = 0;
    SYSTEMTIME now;
    GetLocalTime(&now);
    out("HytaleVR renderer log %04d-%02d-%02d %02d:%02d:%02d, version %s", now.wYear, now.wMonth, now.wDay, now.wHour,
        now.wMinute, now.wSecond, HVR_VERSION);
    out("GL vendor   %s", reinterpret_cast<const char*>(glr.glGetString(gl::VENDOR)));
    out("GL renderer %s", reinterpret_cast<const char*>(glr.glGetString(gl::RENDERER)));
    out("GL version  %s", reinterpret_cast<const char*>(glr.glGetString(gl::VERSION)));
    out("Programs linked so far: %d. Shader sources are in logs\\shaders.", g_programs_linked.load());
    out("SDL input calls: %s", sdl::usage_summary().c_str());
    GLint fb = 0;
    glr.glGetIntegerv(gl::DRAW_FRAMEBUFFER_BINDING, &fb);
    GLint program = 0;
    glr.glGetIntegerv(gl::CURRENT_PROGRAM, &program);
    out("Frame starts with draw framebuffer %d, program %d", fb, program);
    out("");
}

void end_capture() {
    g_capturing = false;
    out("");
    out("Summary: %d draws", g_capture_draws);
    for (const auto& [fb, count] : g_draws_per_fb)
        out("  framebuffer %4u: %5d draws  \"%s\"", fb, count, label_of(gl::FRAMEBUFFER, fb).c_str());
    for (const auto& [program, count] : g_draws_per_program)
        out("  program     %4u: %5d draws  \"%s\"", program, count, label_of(gl::PROGRAM, program).c_str());
    ensure_dir(log_dir());
    SYSTEMTIME now;
    GetLocalTime(&now);
    wchar_t name[96];
    swprintf_s(name, L"\\renderer_%04d%02d%02d_%02d%02d%02d.txt", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
               now.wSecond);
    const std::wstring path = log_dir() + name;
    write_file(path, g_out);
    g_out.clear();
    g_out.shrink_to_fit();
    {
        std::lock_guard lock(g_path_mutex);
        g_last_path = path;
    }
    logf("Renderer log written to %ls", path.c_str());
}

bool recording() { return t_render && g_capturing; }

}  // namespace

void frame_boundary() {
    t_render = true;
    g_draws_last_frame = g_draws;
    g_draws = 0;
    if (g_capturing) end_capture();
    if (g_capture_requested.exchange(false) && ensure_gl()) begin_capture();
}

void request_capture() { g_capture_requested = true; }
bool capture_pending() { return g_capture_requested || g_capturing; }
std::wstring last_capture_path() {
    std::lock_guard lock(g_path_mutex);
    return g_last_path;
}
int draws_last_frame() { return g_draws_last_frame; }
int programs_linked() { return g_programs_linked; }

void on_shader_source(GLuint shader, GLsizei count, const GLchar* const* strings, const GLint* lengths) {
    std::string source;
    for (GLsizei i = 0; i < count; ++i) {
        if (!strings[i]) continue;
        if (lengths && lengths[i] >= 0) source.append(strings[i], static_cast<size_t>(lengths[i]));
        else source.append(strings[i]);
    }
    std::lock_guard lock(g_programs_mutex);
    g_shader_sources[shader] = std::move(source);
}

void on_attach(GLuint, GLuint) {}

void on_link(GLuint program) {
    if (!ensure_gl()) return;
    InternalScope scope;
    GLint status = 0;
    glr.glGetProgramiv(program, gl::LINK_STATUS, &status);
    GLuint shaders[8] = {};
    GLsizei count = 0;
    glr.glGetAttachedShaders(program, 8, &count, shaders);
    ++g_programs_linked;
    std::lock_guard lock(g_programs_mutex);
    prepare_shader_dir();
    const auto label = g_labels.find(label_key(gl::PROGRAM, program));
    const std::string base = "p" + std::to_string(program) + (label != g_labels.end() ? "_" + safe_name(label->second) : "");
    for (GLsizei i = 0; i < count; ++i) {
        GLint type = 0;
        glr.glGetShaderiv(shaders[i], gl::SHADER_TYPE, &type);
        const auto source = g_shader_sources.find(shaders[i]);
        if (source == g_shader_sources.end()) continue;
        write_file(shader_dir() + L"\\" + widen(base) + L"." + widen(shader_suffix(type)), source->second);
    }
    if (!status) logf("Hytale program %u failed to link", program);
}

void on_label(GLenum identifier, GLuint name, GLsizei length, const GLchar* label) {
    if (!label) return;
    std::string text = length < 0 ? std::string(label) : std::string(label, static_cast<size_t>(length));
    std::lock_guard lock(g_programs_mutex);
    g_labels[label_key(identifier, name)] = std::move(text);
}

void on_use_program(GLuint program) {
    if (!t_render) return;
    g_program = program;
    if (g_capturing) out("use program %u", program);
}

void on_bind_framebuffer(GLenum target, GLuint framebuffer) {
    if (!t_render) return;
    if (target == gl::FRAMEBUFFER || target == gl::DRAW_FRAMEBUFFER) g_draw_fb = framebuffer;
    if (target == gl::FRAMEBUFFER || target == gl::READ_FRAMEBUFFER) g_read_fb = framebuffer;
    if (g_capturing) {
        out("bind framebuffer %s %u",
            target == gl::READ_FRAMEBUFFER ? "read" : (target == gl::DRAW_FRAMEBUFFER ? "draw" : "both"), framebuffer);
    }
}

void on_bind_buffer_range(GLenum target, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size) {
    if (!recording()) return;
    const char* kind = target == gl::UNIFORM_BUFFER ? "uniform" : (target == gl::SHADER_STORAGE_BUFFER ? "storage" : "other");
    out("bind %s buffer index %u <- buffer %u offset %lld size %lld", kind, index, buffer, static_cast<long long>(offset),
        static_cast<long long>(size));
}

void on_buffer_sub_data(GLenum target, GLintptr offset, GLsizeiptr size, const void*) {
    if (!recording() || target != gl::UNIFORM_BUFFER) return;
    out("upload uniform buffer offset %lld size %lld", static_cast<long long>(offset), static_cast<long long>(size));
}

void on_blit(GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0, GLint dx1, GLint dy1, GLbitfield mask) {
    if (!recording()) return;
    out("blit fb %u (%d,%d)-(%d,%d) -> fb %u (%d,%d)-(%d,%d) mask 0x%X", g_read_fb, sx0, sy0, sx1, sy1, g_draw_fb, dx0, dy0,
        dx1, dy1, mask);
}

void on_dispatch(GLuint x, GLuint y, GLuint z) {
    if (!recording()) return;
    out("dispatch compute %u %u %u program %u", x, y, z, g_program);
}

void on_clear(GLbitfield mask) {
    if (!recording()) return;
    out("clear fb %u mask 0x%X", g_draw_fb, mask);
}

void on_clear_buffer(GLenum buffer, GLint drawbuffer) {
    if (!recording()) return;
    out("clear buffer fb %u buffer 0x%X index %d", g_draw_fb, buffer, drawbuffer);
}

void on_viewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    if (!t_render) return;
    if (g_capturing && (g_viewport[0] != x || g_viewport[1] != y || g_viewport[2] != width || g_viewport[3] != height))
        out("viewport %d %d %d %d", x, y, width, height);
    g_viewport[0] = x;
    g_viewport[1] = y;
    g_viewport[2] = width;
    g_viewport[3] = height;
}

void on_draw(const char* kind, GLenum mode, GLsizei count, GLsizei instances) {
    if (!t_render) return;
    ++g_draws;
    if (!g_capturing) return;
    InternalScope scope;
    ++g_capture_draws;
    ++g_draws_per_fb[g_draw_fb];
    ++g_draws_per_program[g_program];
    describe_fb(g_draw_fb);
    describe_program(g_program);
    out("draw %s %s count %d instances %d program %u fb %u", kind, mode_name(mode), count, instances, g_program, g_draw_fb);
}

}  // namespace hvr::census
