#include "gl_api.h"

#include <mutex>

#include "log.h"

GlApi glr;

namespace hvr {

bool gl_api_init() {
    static std::mutex mutex;
    std::lock_guard lock(mutex);
    static bool tried = false;
    if (glr.ready || tried) return glr.ready;
    tried = true;
    int missing = 0;
#define HVR_GL_RESOLVE(ret, name, params)                                          \
    glr.name = reinterpret_cast<ret(APIENTRY*) params>(real_gl_proc(#name));       \
    if (!glr.name) {                                                               \
        ++missing;                                                                 \
        logf("GL function %s is not available", #name);                           \
    }
    HVR_GL_FUNCTIONS(HVR_GL_RESOLVE)
#undef HVR_GL_RESOLVE
#define HVR_GL_RESOLVE_OPTIONAL(ret, name, params) \
    glr.name = reinterpret_cast<ret(APIENTRY*) params>(real_gl_proc(#name));
    HVR_GL_OPTIONAL(HVR_GL_RESOLVE_OPTIONAL)
#undef HVR_GL_RESOLVE_OPTIONAL
    glr.has_dsa = glr.glGetNamedBufferSubData && glr.glGetTextureLevelParameteriv &&
                  glr.glGetNamedFramebufferAttachmentParameteriv && glr.glGetNamedRenderbufferParameteriv;
    glr.ready = missing == 0;
    if (glr.ready) {
        logf("OpenGL %s on %s", reinterpret_cast<const char*>(glr.glGetString(gl::VERSION)),
             reinterpret_cast<const char*>(glr.glGetString(gl::RENDERER)));
    }
    return glr.ready;
}

}  // namespace hvr
