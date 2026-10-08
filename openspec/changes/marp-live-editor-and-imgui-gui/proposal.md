## Why

A working Marp live-editing setup exists at `G:\Docs\Slides\AI` (Python live server + browser editor + `programmer` theme), but it lives outside version control in a slide-content folder. We want it in this dedicated repo (`marp-gui`) as a reusable tool, and additionally want a native desktop app (Dear ImGui) that provides the same left-editor / right-live-preview workflow without needing a browser.

## What Changes

- ~~Sync the Marp web/HTML tooling~~ — **superseded**: web tooling was ported, validated, then removed. The native GUI is the single deliverable.
- Bring in the `programmer` Marp theme (`themes/programmer.css`) and a clean `slides.md` hello-world sample deck (no external assets).
- New C++ desktop app `gui/`:
  - Dear ImGui UI: left pane markdown editor (ImGuiColorTextEdit with a custom hand-written Markdown tokenizer — the stock std::regex highlighters are documented as slow), right pane live slide preview
  - SDL3 + OpenGL3 windowing/rendering backend
  - Preview rendered by shelling out to `marp-cli` (`--images png`) and displaying PNGs as textures
  - Debounced rebuild, cursor-follow slide navigation, autosave, external-change reload
  - Proper app shell: File menu (Open/Save/Save As/Exit), Settings dialog (UI theme, server port, autosave), toolbar with Marp theme dropdown + server start/stop + status
  - Built-in 13-theme ImGui gallery (TheAncientOwl's set from ocornut/imgui#707), default "Programmer"
  - Child-process management of a `marp -s` live server (start/stop, configurable port, status indicator)
  - Automation tests via Dear ImGui Test Engine (`marp_gui_tests` target)
- CMake-based build fetching all C++ deps via FetchContent.

## Capabilities

### New Capabilities

- `imgui-slide-editor`: Native desktop app with markdown text editor, live Marp slide preview via marp-cli PNG output, app shell (File menu, Settings dialog, toolbar), UI theme gallery, Marp theme selection, and managed marp server child process. SDL3 + Dear ImGui.

### Modified Capabilities

(none — fresh repo, no existing specs)

## Impact

- **New code**: `web/` (Python + HTML/JS, synced from `G:\Docs\Slides\AI`), `themes/programmer.css`, `slides.md`, `gui/` (C++17, CMake), root `package.json` for marp-cli.
- **Dependencies**: Python 3 (stdlib only), Node.js + `@marp-team/marp-cli` (already used by source setup), SDL3, Dear ImGui, ImGuiColorTextEdit, OpenGL3, a C++17 compiler.
- **Platforms**: Windows primary (start.bat, MSVC); GUI code should stay portable (SDL3/OpenGL3 are cross-platform).
- No external APIs or services affected.
