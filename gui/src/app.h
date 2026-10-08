// Shared app state for marp_gui — pulled out of main.cpp so the automation
// tests (tests.cpp, imgui_test_engine) can poke at the same structures.

#pragma once

#include "TextEditor.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// Opaque to the tests; defined in main.cpp.
class BuildWorker;
struct SlideTexture;

struct App {
    TextEditor editor;
    fs::path deckPath;
    fs::path themePath;                       // currently selected marp theme css
    fs::path repoRoot; // cwd at launch; marp runs here
    std::unique_ptr<BuildWorker> worker;

    std::vector<int> slideStarts{0};
    int cursorSlide = 0;                      // slide under the editor cursor
    int viewSlide = 0;                        // slide shown in the preview (free browsing)
    int lastCursorLine = -1;                  // for real cursor-move detection
    std::vector<std::unique_ptr<SlideTexture>> slides;

    std::chrono::steady_clock::time_point lastEdit{};
    bool dirtySinceLastSave = false;
    bool autosave = true;
    bool buildScheduled = false;

    std::string status = "ready";
    std::string lastError;
    fs::file_time_type lastWriteTime{};
    std::chrono::steady_clock::time_point lastMtimePoll{};

    // --- App shell additions ---------------------------------------------------
    bool showSettings = false;
    bool showAbout = false;
    bool showPalette = false;                 // Ctrl+P command palette
    bool marpCliMissing = false;              // marp-cli not resolvable → blocking dialog
    bool logToFile = false;                   // Settings: mirror log to .gui-build/marp_gui.log
    float splitFrac = 0.5f;                   // editor width fraction (draggable splitter)
    float thumbFrac = 0.22f;                  // thumbnail strip height fraction (draggable)
    int uiThemeIndex = 0;                     // Programmer
    int marpThemeIndex = 0;                   // index into marpThemes
    std::vector<std::string> marpThemes;      // stems of themes/*.css
    unsigned long long buildGeneration = 0;   // bumped on deck/theme switch; stale results dropped

    bool requestExit = false;
};

// Slide map helpers (defined in main.cpp).
std::vector<int> SlideStarts(const std::string &text);
int SlideForLine(const std::vector<int> &starts, int line);

// Deck lifecycle (defined in main.cpp).
void LoadDeck(App &app);
void SwitchDeck(App &app);
