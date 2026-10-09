# macOS verification

Verified locally on 2026-10-09 with macOS 26.6.2, Apple Silicon arm64,
Apple Clang 21.0.0, CMake 4.3.4, Ninja, Marp CLI 4.5.1, and Google Chrome.
Marp was installed in ignored repository-local `node_modules`.

## Build and automation

```bash
npm install --no-save --package-lock=false @marp-team/marp-cli
cmake -S gui -B gui/build -DCMAKE_BUILD_TYPE=Release
cmake --build gui/build --config Release --parallel
ctest --test-dir gui/build -C Release --output-on-failure
openspec validate macos-desktop-readiness --strict
git diff --check
```

- Release build succeeds for the native arm64 app, GUI tests, and process tests.
- CTest: **2/2 suites pass**, with **13 GUI tests** and **7 process checks**.
  The final single-configuration run took 21.49 seconds. Log: `.build/cmake-ctest.log`;
  detailed assertions: `gui/build/Testing/Temporary/LastTest.log`.
- Real three-slide PNG preview textures, cursor navigation, Command editing,
  undo/redo, select-all/copy, save, palette, Settings, and HTML/PDF/PPTX exports
  pass against a disposable deck named with spaces, Chinese, dollar signs,
  and apostrophes. Export checks verify PDF/ZIP signatures and nonempty files.
- Deck switching waits for actual preview textures and dispatches through the
  engine's main-thread GUI callback, exercising texture disposal safely.
- Process checks cover literal arguments, exit codes, missing executables,
  cancellation, and cancellation before a subsequent spawn.
- The sample's only tracked edits are the intended platform shortcut wording;
  automation operates on its own copy.
- Bundle plist validation and strict OpenSpec validation pass; no whitespace errors.

## Launch and visual inspection

Actual framebuffer screenshots were opened and inspected at **2880 × 1800**:
the full editor, correct slide aspect ratio, selected thumbnail border,
scrollable thumbnail strip, legible text, palette, Settings, and Chinese glyphs
are visible. Screenshot artifacts are generated under ignored `.build/`.

| Check | Evidence |
| --- | --- |
| Final editor, palette, Settings | `.build/captures/editor.png`, `palette.png`, `settings.png` |
| Chinese editor text and rendered slides | `.build/cjk.png` |
| Launch from `/tmp` with `PATH=/usr/bin:/bin` | `.build/finder-launch.png`, `.build/finder-launch.log` |
| Launch through `open -n -W … --args --screenshot …` from `/tmp` | `.build/launchservices.png` |
| Bundle copied outside the checkout, with spaces in its path | `.build/relocated.png`, `.build/relocated.log` |
| Native AppKit Open and Save As dialogs | `.build/native-open.png`, `.build/native-save.png`, `.build/native.log` |

The relocated bundle resolves its packaged theme and editable user-storage
sample. Marp remains an external dependency, supplied on PATH for this test;
Node is discovered in the normal Homebrew location. Open was exercised with
physical Command+O through macOS UI automation, then a file was selected.
Save As was exercised through the palette and produced a separate markdown
file on disk. Native panel screenshots contain local filesystem names and
are kept as local review artifacts.

## Lifecycle checks

`.build/check-shutdown.py` copies the app into a temporary directory, supplies
a Marp fixture that starts a 30-second descendant process, and sends physical
Command+Q. The app exits with code 0 in under one second, and both child and
descendant are reaped. `.build/shutdown.log` records that run.

A separate crash-handler probe raises SIGABRT. It terminates in under one
second with a single stderr diagnostic, confirming the handler no longer
recurses or attempts to acquire the normal logger's locks.

## Scope of verification

Windows and Intel macOS were not executed on this arm64 host. Platform guards
retain Windows resources, MSVC options, and dbghelp, and Windows uses owned
job objects for child cleanup. Signing, notarization, and release publication
remain outside this change. All source and OpenSpec changes are left
uncommitted and the change remains active for review.

## Shared build workflow follow-up

The platform guards now use numeric macros from `gui/src/platform.h`, matching
fury3d's convention. Native OS detection occurs only in that header. Standalone
preprocessor checks confirm `PLATFORM_MACOS=1` / `PLATFORM_WINDOWS=0` on this Mac,
and the inverse for a simulated Windows define. Compiler-specific checks remain
compiler-specific.

The shared workflow uses direct CMake and CTest commands, following fury3d.
There is no Node build wrapper; npm only installs the Marp runtime dependency.

```bash
cmake -S gui -B gui/build -DCMAKE_BUILD_TYPE=Release
cmake --build gui/build --config Release --parallel
ctest --test-dir gui/build -C Release --output-on-failure
cmake --build gui/build --config Release --target run
```

- The single-configuration Ninja build passes 13 GUI tests and 7 process checks
  in 21.49 seconds.
- A separate Ninja Multi-Config build passes the same suites in 21.67 seconds,
  using the already-fetched dependency sources. The same commands were used with
  `gui/build-multi` as the build directory. This exercises configuration handling
  needed by Visual Studio generators without claiming a Windows run.
- The CMake `run` target depends on the app and resolves `$<TARGET_FILE:marp_gui>`
  for the selected configuration, including the macOS bundle executable.
- Both `run` targets were exercised and returned code 0 after physical Command+Q.
  The multi-config target was invoked from `/tmp`; CMake correctly launched the
  app from the repository root with the sample deck and bundled programmer theme.
- The editor, three rendered slides, and thumbnail strip were visually inspected.
  Single-config evidence: `.build/cmake-run.png`. Multi-config framebuffer evidence:
  `.build/cmake-multi-framebuffer.png`, captured from the same app executable.
  The multi-config window-server capture was blank, so framebuffer capture was
  used for its visual inspection.
- Logs: `.build/cmake-configure.log`, `.build/cmake-build.log`,
  `.build/cmake-ctest.log`, `.build/cmake-run.log`, and corresponding
  `.build/cmake-multi-*.log` files.
- README is 37 lines and uses one command set for Windows and macOS.

Windows runtime execution still requires a Windows host. Everything remains
uncommitted for review.
