## MODIFIED Requirements

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

### Requirement: Export via marp-cli
The GUI SHALL use the same portable Marp invocation for previews and exports. It SHALL provide File → Export menu items (PDF, PPTX, HTML) that invoke marp-cli in the background, writing the output next to the deck file. The deck SHALL be saved before export so output matches the editor content.

#### Scenario: Export PDF
- **WHEN** the user selects File → Export → PDF
- **THEN** the deck is saved and `marp <deck> --theme-set <theme> --pdf -o <deck>.pdf` runs in the background; success/failure surfaces in the status bar

## ADDED Requirements

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
