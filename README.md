# marp-gui

A native [Marp](https://marp.app) slide editor with live preview — Dear ImGui
desktop app (SDL3 + OpenGL3): markdown editor on the left, rendered slides on
the right.

![](gui/screenshot.png)

## Prerequisites

- **[marp-cli](https://github.com/marp-team/marp-cli)** — does the actual rendering. Install it first:
  ```bash
  npm i -g @marp-team/marp-cli
  ```
- **Chrome or Edge** — marp-cli drives a headless browser for PNG/PDF/PPTX output
- **MSVC + CMake ≥ 3.24** — to build the app

## Build

```bash
cmake -S gui -B gui/build -G "Visual Studio 17 2022" -A x64
cmake --build gui/build --config Release

.\gui\build\Release\marp_gui.exe                  # edits slides.md in cwd
.\gui\build\Release\marp_gui.exe path\to\deck.md  # any deck
```

## Automation

Built with [Dear ImGui Test Engine](https://github.com/ocornut/imgui_test_engine):

```bash
cmake --build gui/build --config Release --target marp_gui_tests
.\gui\build\Release\marp_gui_tests.exe slides.md   # exit code 0 = all pass
```

Covers: slide-map parsing, deck load, editor input, UI theme switching,
markdown highlighting, undo/redo, window layout (7 tests).

## Special thanks

- https://github.com/ocornut/imgui
- https://github.com/ocornut/imgui_test_engine
- https://github.com/marp-team/marp-cli
- ImGui themes shared by **TheAncientOwl** in
[ocornut/imgui issue #707](https://github.com/ocornut/imgui/issues/707)
- Claude Code + Kimi K3