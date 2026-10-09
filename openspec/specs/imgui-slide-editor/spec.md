# imgui-slide-editor Specification

## Purpose
Define the native Windows and macOS Marp editor, including live preview, editing, exports, and a shared CMake build and release workflow.

## Requirements
### Requirement: Split-view desktop editor
The GUI app SHALL present a Dear ImGui window with a markdown text editor (ImGuiColorTextEdit) on the left and a slide preview pane on the right, built on SDL3 + OpenGL3, with an adjustable split.

#### Scenario: App launches with a deck
- **WHEN** the user runs the app (optionally with a deck path as argv[1], default `slides.md`)
- **THEN** a window opens showing the deck's markdown in the editor and the rendered first slide in the preview

#### Scenario: Editing updates the buffer
- **WHEN** the user types in the editor
- **THEN** the text buffer updates immediately and a rebuild is scheduled after a debounce interval (~800 ms)

### Requirement: Live preview via marp-cli PNG rendering
The GUI SHALL invoke marp-cli with platform-appropriate process execution and literal deck/theme paths. It SHALL render previews by invoking `marp-cli` (`--images png --theme-set themes/programmer.css`) on a background thread, loading the resulting PNGs as textures, and displaying the current slide. The last good preview MUST be kept when a build fails.

#### Scenario: Successful rebuild
- **WHEN** a debounced rebuild completes successfully
- **THEN** the preview pane shows the newly rendered slide PNGs without flicker of the previous frame set

#### Scenario: Failed rebuild keeps preview
- **WHEN** marp-cli exits non-zero (e.g., markdown syntax error)
- **THEN** the previous preview remains visible and the marp-cli stderr is shown in a status bar

#### Scenario: Missing dependency reporting
- **WHEN** marp-cli or its headless browser requirement is unavailable
- **THEN** the app shows a clear error message identifying the missing dependency instead of crashing

### Requirement: Cursor-to-slide synchronization
The GUI SHALL map the editor cursor line to a slide index (splitting on `---` separator lines after YAML front matter) and display the corresponding slide in the preview, with a `n / total` slide indicator.

#### Scenario: Cursor moves to another slide
- **WHEN** the user moves the cursor into a line range belonging to a different slide
- **THEN** the preview switches to that slide and the indicator updates

#### Scenario: Slide count changes after rebuild
- **WHEN** a rebuild produces a different number of slides
- **THEN** the current slide index is clamped to the valid range and the indicator reflects the new total

### Requirement: Save and reload
The GUI SHALL save the editor buffer to the deck file on the platform save shortcut (Command+S on macOS, Ctrl+S on Windows), and MUST trigger a rebuild after saving. Autosave (debounced) SHALL be available and enabled by default.

#### Scenario: Manual save
- **WHEN** the user presses the platform save shortcut
- **THEN** the buffer is written to the deck file and a preview rebuild is triggered

#### Scenario: External file change
- **WHEN** the deck file is modified on disk by another program
- **THEN** the app detects the change (timestamp polling) and reloads the buffer and preview

### Requirement: Build system
The GUI SHALL build with CMake on Windows (MSVC) and macOS (Apple Clang), fetching Dear ImGui (docking branch), ImGuiColorTextEdit, and SDL3 via FetchContent or vendored submodules, and vendoring stb_image for PNG decoding.

#### Scenario: Clean clone build
- **WHEN** a developer runs `cmake -S gui -B gui/build -DCMAKE_BUILD_TYPE=Release` followed by `cmake --build gui/build --config Release --parallel` on a fresh clone with a C++17 toolchain
- **THEN** the app compiles and links without manually installing SDL3 or ImGui

### Requirement: Application shell with File menu and Settings dialog
The GUI SHALL provide a menu bar with a File menu (Open…, Save, Save As…, Exit) and a Settings dialog containing the ImGui UI theme picker and the autosave toggle.

#### Scenario: Open a different deck
- **WHEN** the user selects File → Open… and picks a markdown file
- **THEN** the editor loads that file and the preview rebuilds against it

#### Scenario: Settings dialog
- **WHEN** the user opens Settings from the menu bar
- **THEN** a modal dialog shows the ImGui theme combo and autosave checkbox; changes apply immediately

### Requirement: Toolbar with Marp theme selection
The GUI toolbar SHALL show a Marp theme dropdown populated from `themes/*.css`.

#### Scenario: Switch Marp theme
- **WHEN** the user picks a different Marp theme in the toolbar dropdown
- **THEN** subsequent preview rebuilds use `--theme-set themes/<name>.css`

### Requirement: Export via marp-cli
The GUI SHALL use the same portable Marp invocation for previews and exports. It SHALL provide File → Export menu items (PDF, PPTX, HTML) that invoke marp-cli in the background, writing the output next to the deck file. The deck SHALL be saved before export so output matches the editor content.

#### Scenario: Export PDF
- **WHEN** the user selects File → Export → PDF
- **THEN** the deck is saved and `marp <deck> --theme-set <theme> --pdf -o <deck>.pdf` runs in the background; success/failure surfaces in the status bar

### Requirement: Fast Markdown syntax highlighting
The editor SHALL use a custom hand-written Markdown tokenizer (no std::regex) covering headers, emphasis markers, code spans/fences, links, HTML comments, YAML front matter, and list markers.

#### Scenario: Highlighting active on sample deck
- **WHEN** the sample `slides.md` is loaded
- **THEN** the editor's language definition is the custom Markdown one and highlighting does not degrade frame rate on a multi-hundred-line deck

### Requirement: Shared setup and build workflow
The repository SHALL document one shared workflow using direct CMake commands to configure, build, and launch on Windows and macOS, and CTest to run automation. It SHALL support single- and multi-configuration generators without a build wrapper. The CMake `run` target SHALL build the app and resolve its executable through `$<TARGET_FILE:marp_gui>`, launching from the repository root. npm SHALL be used only for Marp runtime setup. Project source SHALL use centrally defined numeric `PLATFORM_WINDOWS` and `PLATFORM_MACOS` macros for operating-system branches.

#### Scenario: Same command on both platforms
- **WHEN** a developer installs Marp with the documented npm command and runs the shared CMake configure, build, and `--target run` commands
- **THEN** CMake fetches and builds dependencies with the platform's generator, and the native app launches with the sample deck

#### Scenario: Run automation through CTest
- **WHEN** a developer builds with `--config Release` and runs `ctest --test-dir gui/build -C Release --output-on-failure`
- **THEN** CTest runs automation with the selected configuration, returning nonzero if any test fails

#### Scenario: Standalone source includes platform definitions
- **WHEN** an application source includes its platform header or a header using platform branches
- **THEN** all supported platform macros have explicit values and native OS detection is confined to the central header

### Requirement: Cross-platform release ZIP
After the selected configuration is built, `cmake --install` SHALL install only the application and generate a ZIP in the install prefix, defaulting to the build directory's `dist/`. The ZIP SHALL contain one application payload: `marp-gui.app` on macOS or `marp-gui.exe` on Windows. Packaging SHALL preserve macOS bundle metadata and SHALL report archive failures without publishing a partial ZIP. Source, tests, dependency libraries, and debugging symbols SHALL NOT be packaged. Node, Marp CLI, and the browser SHALL remain external prerequisites.

#### Scenario: Install a macOS release
- **WHEN** a developer builds Release and runs `cmake --install gui/build --config Release`
- **THEN** `gui/build/dist/marp-gui-macos.zip` is created containing the app bundle and its resources

#### Scenario: Install a Windows release
- **WHEN** a developer builds Release with MSVC and runs the same install command
- **THEN** `gui/build/dist/marp-gui-windows.zip` is created containing only `marp-gui.exe`, with a static MSVC runtime and built-in theme, sample deck, and logo

#### Scenario: Launch without external application resources
- **WHEN** the executable cannot discover external themes
- **THEN** its built-in resources are written into owned per-session storage and the default deck can render with installed Marp and a browser

#### Scenario: Installation failure
- **WHEN** the selected build artifact is missing, the configuration is invalid, or ZIP creation fails
- **THEN** installation reports failure and no new final ZIP is published
