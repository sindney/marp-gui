## 1. Repo scaffolding

- [x] 1.1 Create `package.json` at repo root with `@marp-team/marp-cli` (^4.5.1) devDependency; run `npm install`
- [x] 1.2 Copy `the original slide folder\programmer.css` → `themes/programmer.css` (verbatim)
- [x] 1.3 Create root `slides.md` hello-world deck (front matter: marp, theme programmer, 16:9, paginate; 3 slides: title/lead, bullets, code sample — no image references)
- [x] 1.4 Verify sample build: `npx marp slides.md --theme-set themes/programmer.css --html -o .build/slides.html`

## 2. Web live editor — REMOVED

- [x] 2.1 ~~Web tooling~~: web/ (serve.py, editor.html, start.bat) ported then **deleted** per user decision — GUI supersedes it; specs updated accordingly

## 3. GUI scaffolding (CMake + dependencies)

- [x] 3.1 Create `gui/CMakeLists.txt` (C++17) with FetchContent for SDL3 (release-3.x tag) and Dear ImGui (docking branch); vendor stb_image header
- [x] 3.2 Add ImGuiColorTextEdit via FetchContent; build it as part of the app target
- [x] 3.3 Wire `imgui_impl_sdl3` + `imgui_impl_opengl3` backends; app window running
- [x] 3.4 Root README: prerequisites, GUI usage, theme provenance (TheAncientOwl / imgui #707)

## 4. GUI editor + preview core

- [x] 4.1 Editor pane: ImGuiColorTextEdit instance, load deck file (argv[1], default `slides.md`), monospace font
- [x] 4.2 Slide map: parse `---` separators (skip front matter); cursor line → slide index; show `Slide n / total`
- [x] 4.3 Build worker: background thread running `npx marp ... --images png`, debounced ~800 ms, coalesced
- [x] 4.4 Preview pane: stb_image → GL textures; keep last-good preview on failure; status bar with marp stderr
- [x] 4.5 Save: Ctrl+S writes file + triggers rebuild; autosave toggle (default on)
- [x] 4.6 External change detection: poll deck file mtime; reload buffer + rebuild on change
- [x] 4.7 Startup dependency check: detect missing marp-cli and show clear error

## 5. GUI themes (imgui theme gallery)

- [x] 5.1 Port 13 `Setup*Style()` functions from the imgui themes gallery (ocornut/imgui#707) → `gui/src/themes.cpp/.h` (verbatim bodies in `themes_inline.cpp.inc`)
- [x] 5.2 Default to "Programmer" UI theme
- [x] 5.3 Note theme provenance in README (TheAncientOwl's collection in ocornut/imgui#707)

## 6. GUI automation tests (imgui_test_engine)

- [x] 6.1 FetchContent imgui_test_engine v1.92.1; `marp_gui_tests` target runs app + engine
- [x] 6.2 Tests: slide-map parsing, deck load, editor input, theme switching, window layout — 5/5 passing
- [x] 6.3 Runnable via `marp_gui_tests.exe` (exit 0 = all pass); ctest registered

## 7. Proper application shell

- [x] 7.1 Menu bar: File menu (Open…, Save, Save As…, Exit) replacing the bare Save button
- [x] 7.2 Settings dialog (modal): ImGui UI theme combo (moved out of toolbar), autosave toggle
- [x] 7.3 Toolbar: marp **theme** dropdown (scans `themes/*.css`; sets `--theme-set` for preview builds)
- [x] 7.4 ~~Server management~~ **dropped** (user decision: GUI preview doesn't need `marp -s`); File → Export submenu added instead (PDF / PPTX / HTML via background marp-cli, saves deck first)

## 8. Markdown syntax highlighting

- [x] 8.1 Custom `TextEditor::LanguageDefinition` for Markdown with a hand-written tokenizer (no std::regex — same pattern as the built-in fast C++ tokenizer): headers, bold/italic markers, code spans/fences, links, comments (`<!-- -->`), YAML front matter, lists
- [x] 8.2 Automation test: markdown highlighting active on the sample deck

## 9. Verification

- [x] 9.1 Fresh-clone build check: `cmake -B build && cmake --build build` succeeds (both targets)
- [x] 9.2 Automation tests pass (5/5, `marp_gui_tests.exe`)
- [ ] 9.3 Manual: File open/save round-trip; export PDF/PPTX/HTML; marp theme switch rebuilds preview; settings dialog
- [ ] 9.4 Commit (user will verify first — commit only when asked)

## 10. UX polish + diagnostics

- [x] 10.1 IME candidate window follows the editor caret (set `g.PlatformImeData` from caret line/col each frame)
- [x] 10.2 Build status dot on preview nav row (green = up to date, yellow = rebuilding/edit pending), right-aligned
- [x] 10.3 Command palette scrolls to follow Up/Down keyboard selection (`SetScrollHereY` on keyboard nav)
- [x] 10.4 Typing fixed: `SDL_StartTextInput` at startup (SDL3 requires it; editor reads `InputQueueCharacters` without raising `WantTextInput`)
- [x] 10.5 CJK/UTF-8: merged Microsoft YaHei into editor + UI fonts for Chinese glyphs; utf8_cjk test
- [x] 10.6 marp-cli missing → blocking dialog (link to install docs, Retry re-probes, Close exits); rebuilds suppressed while missing
- [x] 10.7 App icon: Marp logo (marp.app favicon) as exe .ico resource + SDL window icon
- [x] 10.8 Deck switch fixes: BuildWorker::Retarget + build-generation discard; theme resolved from exe-relative app root
- [x] 10.9 Logging system (plog-style thread-safe stream logger): thread-safe stream logger `gui/src/log.h` (LOGD/LOGI/LOGW/LOGE), console always on, Settings "Log to file" toggle writes to .gui-build/marp_gui.log
- [x] 10.10 Build-worker generation flush: every Request bumps requestGen_; a running build whose request was superseded is discarded and the latest rebuilds immediately — only the newest result is published
- [x] 10.11 IME caret anchoring: vendored TextEditor into `gui/third_party/ImGuiColorTextEdit` with `GetCaretScreenPos()` (exact cursor pixel), replacing the approximation
- [x] 10.12 Slide code syntax colors: overrode marp prettylights CSS vars in `themes/programmer.css` with the VS Code dark palette
- [x] 10.13 Removed `<`/`>` arrow buttons from preview (pages turn via wheel/↑↓/thumbnails)
