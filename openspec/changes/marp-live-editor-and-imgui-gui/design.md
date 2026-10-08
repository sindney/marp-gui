## Context

The source setup at `G:\Docs\Slides\AI` proves the workflow: `serve.py` (Python stdlib only — ThreadingHTTPServer + hand-rolled RFC 6455 WebSocket) watches `slides.md` + `programmer.css` by MD5 polling, rebuilds via `npx marp`, and pushes `reload` over WS. `editor.html` (CodeMirror via esm.sh) autosaves through `POST /__save` and polls `/__hash` to reload an iframe preview, following the cursor's slide via `location.hash`.

This change ports that setup verbatim into `web/` of this repo, and adds a native C++ app (`gui/`) replicating the same split editor + live preview without a browser.

## Goals / Non-Goals

**Goals:**
- `web/` works standalone: `python serve.py slides.md` → open `http://localhost:8080/editor`.
- A clean hello-world `slides.md` that builds with zero external assets (no images).
- `gui/` app: left = ImGuiColorTextEdit markdown editor, right = rendered slide preview, rebuild-on-edit (debounced), slide navigation synced to cursor.
- Windows-first build (MSVC + CMake), portable code.

**Non-Goals:**
- No embedded browser / WebView2 in the GUI (rejected — heavy dependency).
- No presenter mode, PDF/PPTX export UI, or multi-deck management in the GUI (marp-cli CLI remains available for that).
- No changes to the source folder `G:\Docs\Slides\AI` (one-way sync).
- No Linux/macOS packaging or CI in this change.

## Decisions

### D1: Repo layout
```
marp-gui/
├── package.json            # @marp-team/marp-cli devDependency
├── slides.md               # hello-world sample deck (theme: programmer)
├── themes/programmer.css
├── web/                    # synced browser tooling
│   ├── serve.py  editor.html  start.bat
└── gui/                    # C++ ImGui app
    ├── CMakeLists.txt
    ├── third_party/        # FetchContent: imgui, ImGuiColorTextEdit, SDL3
    └── src/main.cpp (+ helpers)
```
Rationale: marp-cli at root so both `web/` and `gui/` invoke the same `npx marp`; theme at `themes/` is the conventional marp `--theme-set` location.

### D2: Web tooling synced as-is
Copy `serve.py`, `editor.html`, `programmer.css`, `start.bat` verbatim, adjusting only paths (`THEME = "../themes/programmer.css"` or keep theme next to serve.py — decision: keep a copy at repo root `themes/` and pass `--theme-set` with relative path from repo root; serve.py runs with `cwd=ROOT` where ROOT is repo root). Minimal edits reduce drift from the proven source.

### D3: GUI preview via marp-cli PNG, not embedded browser
`marp slides.md --theme-set themes/programmer.css --images png -o .build/preview.png` produces `preview.001.png`, `preview.002.png`, … The GUI loads these as OpenGL textures. Alternative (WebView2/CEF) rejected: multi-hundred-MB dependency, COM packaging pain, and pixel parity isn't needed since HTML deck remains available via `web/`.

Build orchestration: spawn marp-cli on a worker thread with debounce (~800 ms after last keystroke); on completion, atomically swap texture set on the render thread. Failed builds keep the last good preview and surface stderr in a status bar.

### D4: Windowing: SDL3 + OpenGL3
User preference (SDL or SFML); SDL3 chosen over SFML because Dear ImGui ships an official `imgui_impl_sdl3` backend, while SFML needs the third-party imgui-sfml binding. Renderer: official `imgui_impl_opengl3`. Dependencies via CMake FetchContent pinned to tags:
- `libsdl-org/SDL` (release-3.x)
- `ocornut/imgui` (docking branch, for docked split layout)
- `BalazsJako/ImGuiColorTextEdit` (master; provides markdown-adequate C++/plain-text highlighting — use its text editor with a simple Markdown tokenizer if available, else default)
- PNG decode: stb_image (single header, vendored)

### D5: Editor/preview sync
Mirror the web editor's model: parse slide starts by scanning for `---` separator lines (skipping YAML front matter); cursor line → slide index → show that PNG; a slide counter widget shows `n / total`. Save with Ctrl+S writes the file and triggers rebuild; autosave optional toggle (default on, 1 s debounce, like the web editor).

### D6: Config
GUI takes the deck path as argv[1] (default `slides.md` in cwd) and locates the theme relative to the deck or via `--theme` arg. Keeps the tool usable on any deck folder, not just this repo.

## Risks / Trade-offs

- **marp-cli rebuild latency (~1–3 s per build)** → acceptable for preview; debounce + build-in-progress coalescing; show a "building…" indicator so lag is legible.
- **marp-cli PNG output requires Chrome/Edge** (it drives a headless browser) → detect failure at startup and show a clear error message; document requirement in README.
- **ImGuiColorTextEdit has no first-class Markdown highlighter** → ship with plain-text or a lightweight custom tokenizer; highlighting is cosmetic, not blocking.
- **One-way sync from `G:\Docs\Slides\AI` will drift** → repo is now source of truth; note in README that the G: copy is deprecated.
- **WebSocket impl in serve.py is minimal** (no fragmentation/pong) → it is proven in daily use; port verbatim rather than rewrite.

## Open Questions

- Pin exact SDL3/ImGui tags at implementation time (latest stable).
- Whether GUI should also host the WS reload server later so web preview and GUI share one build — deferred; out of scope.

## Decision D7: Full rebuild, not partial re-render (investigated)

Considered rendering only changed slides for faster preview. Rejected after research:

- **marp-cli has no page-selection flag** — `--image` renders only page 1 (OG images), `--images` renders all pages. No `--page`/`--pages`/`--range` exists or was ever requested upstream.
- **Single-slide re-render is incorrect in general.** Marpit local directives (`class`, `backgroundColor`, `header`, `footer`, `paginate`, …) without the `_` prefix inherit *forward* to all following slides, and `paginate` page numbers depend on how many increments occurred in earlier slides. Rendering slide N in isolation (via a split/temp deck) yields wrong class/background/header/footer/page-number whenever any earlier slide set an inheritable directive.
- The only correctness-safe partial strategy is **suffix re-render from the earliest edited slide** (forward-only inheritance means slides before the edit are unchanged) — but the gain is marginal for typical decks and adds real complexity (mapping edit → earliest affected slide, reassembling prefix PNGs).

**Decision:** keep the full `npx marp … --images png` rebuild per debounced edit. It is always correct. Latency is acceptable (~1–3 s) and already mitigated by debounce + build coalescing.
