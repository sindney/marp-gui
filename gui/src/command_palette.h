// gui/src/command_palette.h — Ctrl+P command palette (Sublime/VSCode-style).
//
// A top-center filterable
// command list that dispatches the SAME actions as the menus — no duplicated
// effect logic: the palette returns a command and main.cpp executes it with
// the same code path as the menu items.

#pragma once

struct App;

namespace palette {

enum class Command {
    None,
    Open,
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

// Render the palette when *openFlag is true. Returns the command chosen this
// frame (Command::None otherwise); flips *openFlag off on Esc/Enter/select.
Command Render(bool &openFlag, App &app);

} // namespace palette
