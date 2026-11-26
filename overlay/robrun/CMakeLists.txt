cmake_minimum_required(VERSION 3.20)
project(robrun_overlay LANGUAGES CXX ASM_MASM)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── ImGui ─────────────────────────────────────────────────────────────────────
# Place imgui/ à côté de CMakeLists.txt (clone depuis github.com/ocornut/imgui)
# ou symlink vers ../fn-overlay/imgui si déjà présent.
set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/../imgui")
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
)

# ── Exécutable ────────────────────────────────────────────────────────────────
add_executable(robrun_overlay WIN32
    src/main.cpp
    src/overlay.cpp
    src/hypercall.cpp
    src/w2s.cpp
    src/esp.cpp
    src/aimbot.cpp
    src/vmmcall_asm.asm
    ${IMGUI_SOURCES}
)

target_include_directories(robrun_overlay PRIVATE
    src/
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

target_link_libraries(robrun_overlay PRIVATE
    d3d11.lib dxgi.lib d3dcompiler.lib dwmapi.lib
)

# ── Flags de compilation ──────────────────────────────────────────────────────
# /W4 /permissive- uniquement sur les fichiers CXX — ml64 ne les accepte pas
target_compile_options(robrun_overlay PRIVATE
    $<$<AND:$<CONFIG:Release>,$<COMPILE_LANGUAGE:CXX>>:/O2 /GL>
    $<$<COMPILE_LANGUAGE:CXX>:/W4 /permissive->
)

target_link_options(robrun_overlay PRIVATE
    $<$<CONFIG:Release>:/LTCG>
)

target_compile_definitions(robrun_overlay PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
)
