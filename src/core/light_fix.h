// Hytale's clustered lights are sorted into a grid laid over its own camera's
// view. Drawn for an eye, a pixel would look itself up in that grid by where
// it sits in the eye's view, landing in the wrong cell, and compare itself
// with lights placed in the camera's view space: lights go missing or show up
// in the wrong places. This rewrites the two functions every lit shader uses
// so that, while hvrLightFix is on, both work in the camera's view instead.
#pragma once

#include <string>

namespace hvr {

inline bool insert_after_brace(std::string& source, const char* signature, const char* code) {
    const size_t at = source.find(signature);
    if (at == std::string::npos) return false;
    const size_t brace = source.find('{', at);
    if (brace == std::string::npos) return false;
    source.insert(brace + 1, code);
    return true;
}

// Returns true when the shader had Hytale's light cluster code and was changed.
inline bool add_light_fix(std::string& source) {
    const char* fetch = "void fetchClusterData(vec2 screenUV, float depthVS, isampler3D lightGridTexture";
    const char* compute = "vec3 computeClusteredLighting(vec3 positionVS,";
    if (source.find("hvrLightFix") != std::string::npos) return false;
    const size_t fetch_at = source.find(fetch), compute_at = source.find(compute);
    if (fetch_at == std::string::npos || compute_at == std::string::npos || compute_at < fetch_at) return false;
    std::string out = source;
    // Off by default (0), so the shader is unchanged until we turn it on per eye.
    out.insert(fetch_at,
               "uniform float hvrLightFix;\n"
               "uniform mat4 hvrEyeToCamera;\n"   // eye view space to the camera's
               "uniform mat4 hvrEyeProjection;\n"
               "uniform mat4 hvrCameraProjection;\n");
    if (!insert_after_brace(out, fetch,
                            "\n    if (hvrLightFix > 0.5) {\n"
                            "        vec2 hvrNdc = screenUV * 2.0 - 1.0;\n"
                            "        vec4 hvrP = vec4((hvrNdc.x + hvrEyeProjection[2][0]) / hvrEyeProjection[0][0] * depthVS,\n"
                            "                         (hvrNdc.y + hvrEyeProjection[2][1]) / hvrEyeProjection[1][1] * depthVS,\n"
                            "                         -depthVS, 1.0);\n"
                            "        hvrP = hvrEyeToCamera * hvrP;\n"
                            "        vec4 hvrClip = hvrCameraProjection * hvrP;\n"
                            "        screenUV = clamp(hvrClip.xy / max(hvrClip.w, 1e-4) * 0.5 + 0.5, 0.0, 1.0);\n"
                            "        depthVS = max(-hvrP.z, 1e-4);\n"
                            "    }\n"))
        return false;
    if (!insert_after_brace(out, compute,
                            "\n    if (hvrLightFix > 0.5) positionVS = (hvrEyeToCamera * vec4(positionVS, 1.0)).xyz;\n"))
        return false;
    source = out;
    return true;
}

}  // namespace hvr
