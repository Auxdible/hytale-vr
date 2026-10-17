cmake_minimum_required(VERSION 3.24)

project(hytale_vr VERSION 2.2.2 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Platform-free logic, tested on any OS.
add_library(hvr_core STATIC src/core/settings.cpp)
target_include_directories(hvr_core PUBLIC src)

enable_testing()
add_executable(core_tests tests/core_tests.cpp)
target_link_libraries(core_tests PRIVATE hvr_core)
add_test(NAME core_tests COMMAND core_tests)

if(NOT WIN32)
    return()
endif()

enable_language(ASM_MASM)
# Static runtime: nothing extra for players to install.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set_property(TARGET hvr_core core_tests PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(minhook STATIC
    third_party/minhook/src/buffer.c
    third_party/minhook/src/hook.c
    third_party/minhook/src/trampoline.c
    third_party/minhook/src/hde/hde64.c
)
target_include_directories(minhook PUBLIC third_party/minhook/include)

add_library(imgui STATIC
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC third_party/imgui)
target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)

add_library(openvr_api SHARED IMPORTED)
set_target_properties(openvr_api PROPERTIES
    IMPORTED_IMPLIB "${CMAKE_CURRENT_SOURCE_DIR}/third_party/openvr/lib/win64/openvr_api.lib"
    IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/third_party/openvr/bin/win64/openvr_api.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/openvr/headers"
)

# The opengl32.dll that Hytale loads from its own folder.
add_library(hytalevr_proxy SHARED
    src/proxy/app.cpp
    src/proxy/census.cpp
    src/proxy/gl_api.cpp
    src/proxy/gl_hooks.cpp
    src/proxy/input.cpp
    src/proxy/log.cpp
    src/proxy/menu.cpp
    src/proxy/proxy.cpp
    src/proxy/sdl_hooks.cpp
    src/proxy/stereo.cpp
    src/proxy/vr_runtime.cpp
    src/proxy/vr_scene.cpp
    src/proxy/generated/stubs.asm
    src/proxy/generated/opengl32.def
)
set_target_properties(hytalevr_proxy PROPERTIES OUTPUT_NAME opengl32)
target_compile_definitions(hytalevr_proxy PRIVATE
    HVR_VERSION="${PROJECT_VERSION}"
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)
target_compile_options(hytalevr_proxy PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4 /permissive- /EHsc /utf-8>)
target_link_libraries(hytalevr_proxy PRIVATE hvr_core minhook imgui openvr_api user32 gdi32 shell32 delayimp)
# openvr_api.dll lives in the HytaleVR folder and is loaded by full path first.
target_link_options(hytalevr_proxy PRIVATE /DELAYLOAD:openvr_api.dll)

# Player package: an installer plus the files it copies.
set(HVR_PACKAGE_FILES files)
install(FILES $<TARGET_FILE:hytalevr_proxy> DESTINATION ${HVR_PACKAGE_FILES})
install(FILES
    third_party/openvr/bin/win64/openvr_api.dll
    steamvr/actions.json
    steamvr/bindings_oculus_touch.json
    steamvr/bindings_knuckles.json
    steamvr/bindings_vive_controller.json
    DESTINATION ${HVR_PACKAGE_FILES}/HytaleVR)
file(WRITE "${CMAKE_BINARY_DIR}/VERSION.txt" "${PROJECT_VERSION}\n")
install(FILES
    "${CMAKE_BINARY_DIR}/VERSION.txt"
    package/Install.bat
    package/Uninstall.bat
    package/install.ps1
    package/README.txt
    LICENSE
    NOTICE
    DESTINATION .)
