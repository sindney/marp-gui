## Why

The native editor currently assumes Windows for linking, process execution, fonts, and shortcuts, so it cannot build or render slides on macOS. Developers need a macOS app they can launch from Terminal or Finder and verify with the existing UI automation.

## What Changes

- Build a macOS application bundle with its themes, sample deck, and icon, while retaining Windows builds.
- Run Marp previews and exports with portable, safely quoted arguments and cancellable child processes.
- Resolve bundled resources independently of the working directory and keep generated files in writable user storage.
- Support Command shortcuts, native file dialogs, macOS fonts with CJK coverage, and Retina rendering.
- Make CTest run against disposable decks; extend automation to cover rendering, shortcuts, exports, and process cancellation.
- Centralize source platform detection behind `PLATFORM_WINDOWS` / `PLATFORM_MACOS`, following fury3d.
- Provide one direct CMake configure/build/run and CTest workflow for both platforms and a compact README.
- Document setup and capture actual visual verification evidence.

## Capabilities

### New Capabilities

- `macos-desktop`: Bundle launch, resources, writable runtime storage, native interaction, and verification on macOS.

### Modified Capabilities

- `imgui-slide-editor`: Extend the build system to macOS and manual save to the platform shortcut; make rendering and export invocation portable.

## Impact

Affected areas are `gui/CMakeLists.txt`, application startup, worker lifecycle, crash handling, the vendored editor shortcut handling, automation tests, and README. Existing SDL3, ImGui, nativefiledialog, and Marp dependencies remain; macOS uses Apple Clang and the system OpenGL framework. Signing, notarization, and release distribution are outside this change.
