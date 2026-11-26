cmake_minimum_required(VERSION 3.20)

project(fn_overlay LANGUAGES CXX ASM_MASM)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ImGui — vendor dans overlay/imgui/
set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/imgui")
set(IMGUI_SRC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)

add_executable(fn_overlay WIN32
    src/main.cpp
    src/overlay.cpp
    src/vmmcall.cpp
    src/vmmcall_asm.asm
    ${IMGUI_SRC}
)

target_include_directories(fn_overlay PRIVATE
    src
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

target_link_libraries(fn_overlay PRIVATE
    d3d11.lib
    dxgi.lib
    dwmapi.lib
    d3dcompiler.lib
)

target_compile_definitions(fn_overlay PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
)

# Flags CXX uniquement (pas ASM_MASM)
target_compile_options(fn_overlay PRIVATE
    $<$<AND:$<CONFIG:Release>,$<COMPILE_LANGUAGE:CXX>>:/O2 /GL>
    $<$<COMPILE_LANGUAGE:CXX>:/W4 /WX->
)

target_link_options(fn_overlay PRIVATE
    $<$<CONFIG:Release>:/LTCG>
)
