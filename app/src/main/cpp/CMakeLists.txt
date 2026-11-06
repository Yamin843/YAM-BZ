cmake_minimum_required(VERSION 3.22.1)
project("yambz" CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ─── ImGui (required) ───
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/imgui)
if(NOT EXISTS ${IMGUI_DIR}/imgui.cpp)
    message(FATAL_ERROR "ImGui missing — run scripts/fetch_imgui.sh first")
endif()

add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)
target_compile_definitions(imgui PUBLIC
    IMGUI_IMPL_OPENGL_ES3
    IMGUI_DEFINE_MATH_OPERATORS
)
target_link_libraries(imgui PUBLIC GLESv3 EGL android log)

# ─── ALBATROSS_INCLUDE from external (build.sh passes it) ───
if(NOT DEFINED ALBATROSS_INCLUDE OR NOT EXISTS "${ALBATROSS_INCLUDE}/albatross.h")
    # Fallback: try relative path (for Gradle-like builds)
    set(ALBATROSS_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/../../../../albatross-libs/include)
    if(NOT EXISTS "${ALBATROSS_INCLUDE}/albatross.h")
        message(FATAL_ERROR "albatross.h not found. Pass -DALBATROSS_INCLUDE=<path>")
    endif()
endif()

# ─── YAMBZ sources ───
file(GLOB_RECURSE YAMBZ_CPP
    ${CMAKE_CURRENT_SOURCE_DIR}/yambz/*.cpp
)

add_library(yambz SHARED ${YAMBZ_CPP})

target_include_directories(yambz PRIVATE
    ${ALBATROSS_INCLUDE}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/yambz
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

target_compile_definitions(yambz PRIVATE
    YAMBZ_VERSION="1.0.0"
    YAMBZ_LOG_TAG="YAMBZ"
    IMGUI_DEFINE_MATH_OPERATORS
    YAMBZ_HAS_EMBEDDED=1
)

target_link_libraries(yambz
    imgui
    android
    log
    GLESv3
    EGL
    dl
)

# 16 KB page size (Android 15+)
target_link_options(yambz PRIVATE -Wl,-z,max-page-size=16384)
target_link_options(yambz PRIVATE -Wl,--no-undefined)
target_link_options(yambz PRIVATE -Wl,--exclude-libs,ALL)

# Size optimizations
target_compile_options(yambz PRIVATE
    $<$<CONFIG:Release>:-Os>
    -fvisibility=hidden
    -fvisibility-inlines-hidden
)
