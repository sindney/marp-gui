// gui/src/command_palette.h — Ctrl+P command palette (Sublime/VSCode-style).
//
// A top-center filterable
// command list that dispatches the SAME actions as the menus — no duplicated
// effect logic: the palette returns a command and main.cpp executes it with
// the same code path as the menu items.

#pragma once

#include <filesystem>

struct App;

namespace palette {

enum class Command {
    None,
    Open,
    OpenRecent,
    ClearRecent,
    Save,
    SaveAs,
    ExportPdf,
    ExportPptx,
    ExportHtml,
    Undo,
    Redo,
    Settings,
    About,
    Exit,
    NextSlide,
    PrevSlide,
};

struct Action {
    Command command = Command::None;
    std::filesystem::path path;
};

// Render the palette and return the selected action.
Action Render(bool &openFlag, App &app);

} // namespace palette
