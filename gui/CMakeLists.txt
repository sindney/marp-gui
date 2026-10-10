cmake_minimum_required(VERSION 3.24)
project(marp_gui CXX)

if(APPLE)
    enable_language(OBJC OBJCXX)
endif()
find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Keep vendored dependencies inside the single application payload.
set(BUILD_SHARED_LIBS OFF)
if(MSVC)
    # A standalone Windows executable must not require the VC runtime DLLs.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/dist" CACHE PATH "Release output directory" FORCE)
endif()

# Include binary-safe built-in resources for launches outside the checkout.
foreach(resource IN ITEMS theme sample logo)
    if(resource STREQUAL "theme")
        set(resource_path "${CMAKE_CURRENT_SOURCE_DIR}/../themes/programmer.css")
    elseif(resource STREQUAL "sample")
        set(resource_path "${CMAKE_CURRENT_SOURCE_DIR}/../slides.md")
    else()
        set(resource_path "${CMAKE_CURRENT_SOURCE_DIR}/assets/marp_logo.png")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${resource_path}")
    file(READ "${resource_path}" resource_hex HEX)
    string(REGEX REPLACE "(..)" "0x\\1," builtin_${resource} "${resource_hex}")
endforeach()
configure_file(cmake/BuiltinResources.h.in generated/builtin_resources.h @ONLY)

include(FetchContent)

# --- SDL3 ----------------------------------------------------------------------
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
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

# --- nativefiledialog-extended (cross-platform file dialogs: Win32/macOS/Linux) ---
set(NFD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(NFD_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    nfd
    GIT_REPOSITORY https://github.com/btzy/nativefiledialog-extended.git
    GIT_TAG        v1.4.1
    GIT_SHALLOW    TRUE
)

# --- ImGuiColorTextEdit ----------------------------------------------------------
# Vendored (with local patches: caret-position accessor for IME anchoring).
# Source: https://github.com/BalazsJako/ImGuiColorTextEdit
set(TEXTEDIT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ImGuiColorTextEdit)

FetchContent_MakeAvailable(SDL3 imgui nfd)

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
    src/process.cpp
    ${TEXTEDIT_DIR}/TextEditor.cpp
)
target_include_directories(marp_gui PRIVATE
    ${TEXTEDIT_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)
target_link_libraries(marp_gui PRIVATE imgui SDL3::SDL3 OpenGL::GL Threads::Threads nfd::nfd)
if(WIN32)
    target_sources(marp_gui PRIVATE resource.rc)
    target_link_libraries(marp_gui PRIVATE dbghelp imm32)
endif()
if(APPLE)
    file(GLOB MARP_THEMES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../themes/*.css")
    set_source_files_properties(${MARP_THEMES} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/themes")
    set(MARP_RESOURCES "${CMAKE_CURRENT_SOURCE_DIR}/../slides.md" assets/marp.icns)
    set_source_files_properties(${MARP_RESOURCES} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    set_source_files_properties(assets/marp_logo.png PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/gui/assets")
    target_sources(marp_gui PRIVATE ${MARP_THEMES} ${MARP_RESOURCES} assets/marp_logo.png)
    set_target_properties(marp_gui PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Marp GUI"
        MACOSX_BUNDLE_GUI_IDENTIFIER "app.marp-gui.editor"
        MACOSX_BUNDLE_ICON_FILE "marp.icns"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
    )
endif()
set_target_properties(marp_gui PROPERTIES OUTPUT_NAME marp-gui)
if (MSVC)
    target_compile_options(marp_gui PRIVATE /utf-8)
    # Emit PDBs in every config (incl. Release) so crash stack traces and the
    # minidump resolve to function/file/line. Symbols are separate .pdb files,
    # so this costs nothing at runtime.
    target_compile_options(marp_gui PRIVATE /Zi)
    # Merge the UTF-8 activeCodePage manifest with the linker's auto-generated
    # one (embedding it via resource.rc would duplicate RT_MANIFEST and fail
    # CVTRES). Fixes narrow-string std::filesystem conversions and the spawned
    # marp/node child on non-UTF-8 ANSI code pages (cp936/cp932/...).
    target_link_options(marp_gui PRIVATE /DEBUG /OPT:REF /OPT:ICF
        "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/utf8.manifest")
endif()

# Resolve the executable (including bundle/configuration paths) through CMake.
add_custom_target(run
    COMMAND $<TARGET_FILE:marp_gui>
    DEPENDS marp_gui
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.."
    USES_TERMINAL
    VERBATIM
)

if(APPLE OR WIN32)
    if(APPLE)
        set(MARP_RELEASE_PLATFORM macos)
        set(MARP_RELEASE_PAYLOAD marp-gui.app)
    else()
        set(MARP_RELEASE_PLATFORM windows)
        set(MARP_RELEASE_PAYLOAD marp-gui.exe)
    endif()
    if(CMAKE_CONFIGURATION_TYPES)
        set(MARP_RELEASE_CONFIGS "${CMAKE_CONFIGURATION_TYPES}")
    else()
        set(MARP_RELEASE_CONFIGS "${CMAKE_BUILD_TYPE}")
    endif()
    install(CODE [[
        if(NOT EXISTS "$<TARGET_FILE:marp_gui>" OR IS_DIRECTORY "$<TARGET_FILE:marp_gui>")
            message(FATAL_ERROR "Build the selected application configuration before installing: $<TARGET_FILE:marp_gui>")
        endif()
    ]] COMPONENT Runtime)
    install(TARGETS marp_gui
        BUNDLE DESTINATION "." COMPONENT Runtime
        RUNTIME DESTINATION "." COMPONENT Runtime
    )
    configure_file(cmake/ZipRelease.cmake.in ZipRelease.cmake @ONLY)
    install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/ZipRelease.cmake" COMPONENT Runtime)
endif()

# --- Automation tests (Dear ImGui Test Engine) -----------------------------------
FetchContent_Declare(
    imgui_test_engine
    GIT_REPOSITORY https://github.com/ocornut/imgui_test_engine.git
    GIT_TAG        v1.92.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui_test_engine)

# Instrument ImGui as well as the test executable (required by Test Engine).
get_target_property(IMGUI_SOURCES imgui SOURCES)
add_library(imgui_tested STATIC ${IMGUI_SOURCES})
target_include_directories(imgui_tested PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui_tested PUBLIC SDL3::SDL3)
target_compile_definitions(imgui_tested PUBLIC IMGUI_ENABLE_TEST_ENGINE)

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
target_link_libraries(imgui_te PUBLIC imgui_tested)
target_compile_definitions(imgui_te PUBLIC IMGUI_ENABLE_TEST_ENGINE IMGUI_TEST_ENGINE_ENABLE_COROUTINE_STDTHREAD_IMPL=1)

add_executable(marp_gui_tests
    src/main.cpp
    src/themes.cpp
    src/markdown_lang.cpp
    src/command_palette.cpp
    src/process.cpp
    src/tests.cpp
    ${TEXTEDIT_DIR}/TextEditor.cpp
)
target_include_directories(marp_gui_tests PRIVATE
    ${TEXTEDIT_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)
target_link_libraries(marp_gui_tests PRIVATE imgui_te SDL3::SDL3 OpenGL::GL Threads::Threads nfd::nfd)
if(WIN32)
    target_link_libraries(marp_gui_tests PRIVATE dbghelp imm32)
endif()
target_compile_definitions(marp_gui_tests PRIVATE MARP_GUI_TESTS)
if (MSVC)
    target_compile_options(marp_gui_tests PRIVATE /utf-8 /Zi)
    target_link_options(marp_gui_tests PRIVATE /DEBUG /OPT:REF /OPT:ICF
        "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/utf8.manifest")
endif()

enable_testing()
add_test(NAME gui_auto COMMAND marp_gui_tests --test-deck "${CMAKE_CURRENT_SOURCE_DIR}/../slides.md")
set_tests_properties(gui_auto PROPERTIES TIMEOUT 180 WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
if(UNIX)
    add_executable(marp_process_tests src/process.cpp src/process_tests.cpp)
    target_link_libraries(marp_process_tests PRIVATE Threads::Threads)
    add_test(NAME process_auto COMMAND marp_process_tests)
    set_tests_properties(process_auto PROPERTIES TIMEOUT 10)
endif()
