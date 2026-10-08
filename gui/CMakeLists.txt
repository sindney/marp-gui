cmake_minimum_required(VERSION 3.24)
project(marp_gui CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# --- SDL3 ----------------------------------------------------------------------
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-3.2.16
    GIT_SHALLOW    TRUE
)

# --- Dear ImGui (docking branch) ------------------------------------------------
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.92.1-docking
    GIT_SHALLOW    TRUE
)

# --- ImGuiColorTextEdit ----------------------------------------------------------
# Vendored (with local patches: caret-position accessor for IME anchoring).
# Source: https://github.com/BalazsJako/ImGuiColorTextEdit
set(TEXTEDIT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ImGuiColorTextEdit)

FetchContent_MakeAvailable(SDL3 imgui)

# ImGui has no CMake build of its own — compile it here.
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_link_libraries(imgui PUBLIC SDL3::SDL3)

add_executable(marp_gui
    src/main.cpp
    src/themes.cpp
    src/markdown_lang.cpp
    src/command_palette.cpp
    resource.rc
    ${TEXTEDIT_DIR}/TextEditor.cpp
)
target_include_directories(marp_gui PRIVATE
    ${TEXTEDIT_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(marp_gui PRIVATE imgui SDL3::SDL3 opengl32 dbghelp)
if (MSVC)
    target_compile_options(marp_gui PRIVATE /utf-8)
endif()

# --- Automation tests (Dear ImGui Test Engine) -----------------------------------
FetchContent_Declare(
    imgui_test_engine
    GIT_REPOSITORY https://github.com/ocornut/imgui_test_engine.git
    GIT_TAG        v1.92.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui_test_engine)

add_library(imgui_te STATIC
    ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_context.cpp
    ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_coroutine.cpp
    ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_engine.cpp
    ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_exporters.cpp
    ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_perftool.cpp
    ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_ui.cpp
    ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_te_utils.cpp
    ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine/imgui_capture_tool.cpp
)
target_include_directories(imgui_te PUBLIC
    ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine
)
target_link_libraries(imgui_te PUBLIC imgui)
target_compile_definitions(imgui_te PUBLIC IMGUI_ENABLE_TEST_ENGINE IMGUI_TEST_ENGINE_ENABLE_COROUTINE_STDTHREAD_IMPL=1)

add_executable(marp_gui_tests
    src/main.cpp
    src/themes.cpp
    src/markdown_lang.cpp
    src/command_palette.cpp
    src/tests.cpp
    ${TEXTEDIT_DIR}/TextEditor.cpp
)
target_include_directories(marp_gui_tests PRIVATE
    ${TEXTEDIT_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(marp_gui_tests PRIVATE imgui_te SDL3::SDL3 opengl32 dbghelp)
target_compile_definitions(marp_gui_tests PRIVATE MARP_GUI_TESTS)
if (MSVC)
    target_compile_options(marp_gui_tests PRIVATE /utf-8)
endif()

enable_testing()
add_test(NAME gui_auto COMMAND marp_gui_tests --nogui-assertions)
