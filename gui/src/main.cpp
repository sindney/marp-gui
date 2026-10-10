// marp_gui — native Marp slide editor with live preview.
//
// Left pane : markdown editor (ImGuiColorTextEdit)
// Right pane: slide preview rendered by marp-cli (--images png) shown as GL textures
//
// Usage: marp_gui [deck.md]   (default: slides.md in the current directory)

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_opengl3.h"
#include "TextEditor.h"
#include "themes.h"
#include "markdown_lang.h"
#include "editor_colors.h"
#include "command_palette.h"
#include "log.h"
#include "crash.h"
#include "nfd.h"
#include "app.h"
#include "platform.h"
#include "process.h"
#include "builtin_resources.h"

#if PLATFORM_WINDOWS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#ifdef MARP_GUI_TESTS
#include "imgui_te_engine.h"
#include "imgui_te_ui.h"
void RegisterMarpGuiTests(ImGuiTestEngine *engine, App *app);
#endif

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include "input_state.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// (fs alias comes from app.h)

// ---------------------------------------------------------------------------
// Slide map: which editor lines belong to which slide.
// Splits on lines that are exactly "---" (Marp separator), skipping the
// YAML front matter block at the top of the file.
// ---------------------------------------------------------------------------
std::vector<int> SlideStarts(const std::string &text) {
    std::vector<int> starts{0};
    std::istringstream in(text);
    std::string line;
    bool inFrontMatter = false, fmDone = false;
    int i = 0;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        // trim
        size_t a = line.find_first_not_of(" \t");
        size_t b = line.find_last_not_of(" \t");
        std::string t = (a == std::string::npos) ? "" : line.substr(a, b - a + 1);
        if (i == 0 && t == "---") { inFrontMatter = true; ++i; continue; }
        if (inFrontMatter && !fmDone && t == "---") { inFrontMatter = false; fmDone = true; ++i; continue; }
        if (!inFrontMatter && fmDone && t == "---") starts.push_back(i + 1);
        ++i;
    }
    return starts;
}

int SlideForLine(const std::vector<int> &starts, int line) {
    int idx = 0;
    for (size_t i = 0; i < starts.size(); ++i) {
        if (starts[i] <= line) idx = (int)i; else break;
    }
    return idx;
}

// ---------------------------------------------------------------------------
// Background marp-cli build worker.
// Runs: npx marp <deck> --theme-set <theme> --allow-local-files --images png
//          -o <buildDir>/preview.png
// which produces preview.001.png, preview.002.png, ...
// ---------------------------------------------------------------------------
struct BuildResult {
    bool ok = false;
    std::string error;
    std::vector<std::string> pngPaths; // filled on success
    unsigned long long generation = 0; // matches App::buildGeneration when produced
};

class BuildWorker {
public:
    BuildWorker(std::string deck, std::string theme, fs::path buildDir)
        : deck_(std::move(deck)), theme_(std::move(theme)), buildDir_(std::move(buildDir)) {
        thread_ = std::thread([this] { Run(); });
    }

    ~BuildWorker() {
        {
            std::lock_guard<std::mutex> lk(mu_);
            quit_ = true;
            cv_.notify_all();
        }
        // Kill any in-flight marp child so join() doesn't block on a build.
        process_.Cancel();
        if (thread_.joinable()) thread_.join();
    }

    // Request a rebuild. Bumps the request generation: if a build is running,
    // its result is discarded (flushed) and the latest request runs instead.
    // Guarantees only the newest request is ever published.
    void Request() {
        std::lock_guard<std::mutex> lk(mu_);
        ++requestGen_;
        pending_ = true;
        cv_.notify_all();
    }

    // Point the worker at a different deck/theme (on deck open or theme switch)
    // and queue a rebuild. std::mutex is non-recursive, so bump the counters
    // under one lock — do NOT call Request() (it would re-lock and throw).
    void Retarget(std::string deck, std::string theme) {
        std::lock_guard<std::mutex> lk(mu_);
        deck_ = std::move(deck);
        theme_ = std::move(theme);
        ++generation_;
        ++requestGen_;   // same as Request(), inline — we're already holding mu_
        pending_ = true;
        cv_.notify_all();
    }

    bool Building() const {
        std::lock_guard<std::mutex> lk(mu_);
        return building_;
    }

    // Take the latest finished result, if any (drains it).
    bool TakeResult(BuildResult &out) {
        std::lock_guard<std::mutex> lk(mu_);
        if (!haveResult_) return false;
        out = std::move(result_);
        haveResult_ = false;
        return true;
    }

private:
    void Run() {
        while (true) {
            unsigned long long myReq;
            {
                std::unique_lock<std::mutex> lk(mu_);
                cv_.wait(lk, [this] { return quit_ || pending_; });
                if (quit_) return;
                pending_ = false;
                building_ = true;
                myReq = requestGen_;
            }

            BuildResult r = DoBuild();

            std::lock_guard<std::mutex> lk(mu_);
            // FLUSH: if a newer request arrived while this build ran, discard
            // the stale result and immediately rebuild the latest instead of
            // publishing. Only the newest request ever reaches the app.
            if (requestGen_ != myReq) {
                building_ = false;
                pending_ = true;       // triggers an immediate rebuild
                cv_.notify_all();
                continue;
            }
            result_ = std::move(r);
            haveResult_ = true;
            building_ = false;
        }
    }

    BuildResult DoBuild() {
        std::string deck, theme;
        unsigned long long gen;
        {
            std::lock_guard<std::mutex> lk(mu_);
            deck = deck_;
            theme = theme_;
            gen = generation_;
        }

        BuildResult r;
        r.generation = gen;
        std::error_code ec;
        fs::remove_all(buildDir_, ec);
        fs::create_directories(buildDir_, ec);

        std::string outBase = (buildDir_ / "preview.png").string();
        std::string errFile = (buildDir_ / "stderr.txt").string();
        auto args = mg::MarpCommand();
        args.insert(args.end(), {deck, "--theme-set", theme, "--allow-local-files",
                                  "--images", "png", "-o", outBase});
        int code = process_.Run(args, errFile);
        if (quit_) { r.ok = false; r.error = "shutdown"; return r; }

        std::string errText;
        {
            std::ifstream ef(errFile);
            std::stringstream ss;
            ss << ef.rdbuf();
            errText = ss.str();
        }

        if (code != 0) {
            r.ok = false;
            r.error = errText.empty() ? ("marp-cli exited with code " + std::to_string(code))
                                      : errText;
            return r;
        }

        // Collect preview.NNN.png in order.
        for (auto &e : fs::directory_iterator(buildDir_, ec)) {
            auto name = e.path().filename().string();
            if (name.rfind("preview.", 0) == 0 && e.path().extension() == ".png" &&
                name.find(".png") == name.size() - 4) {
                r.pngPaths.push_back(e.path().string());
            }
        }
        std::sort(r.pngPaths.begin(), r.pngPaths.end());

        if (r.pngPaths.empty()) {
            r.ok = false;
            r.error = "marp-cli produced no PNGs. " + errText;
            return r;
        }
        r.ok = true;
        r.error = errText; // may contain warnings; keep for status bar
        return r;
    }

    std::string deck_, theme_;
    unsigned long long generation_ = 0;
    unsigned long long requestGen_ = 0;   // bumped on every Request/Retarget
    fs::path buildDir_;
    std::thread thread_;
    mutable std::mutex mu_;
    std::condition_variable cv_;
    bool pending_ = false, building_ = false, haveResult_ = false;
    std::atomic<bool> quit_{false};
    BuildResult result_;
    mg::Process process_;
};

// ---------------------------------------------------------------------------
// GL texture wrapper for a decoded PNG.
// ---------------------------------------------------------------------------
struct SlideTexture {
    GLuint id = 0;
    int w = 0, h = 0;

    void Upload(const std::string &path) {
        int w = 0, h = 0, ch = 0;
        stbi_uc *data = stbi_load(path.c_str(), &w, &h, &ch, 4);
        if (!data) return;
        Reset();
        glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
        stbi_image_free(data);
        this->w = w; this->h = h;
    }

    void Reset() {
        if (id) { glDeleteTextures(1, &id); id = 0; }
        w = h = 0;
    }

    ~SlideTexture() { Reset(); }
};

// ---------------------------------------------------------------------------
// App state — the struct itself lives in app.h (shared with tests).
// ---------------------------------------------------------------------------

static std::string ReadFile(const fs::path &p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static bool CaptureFramebuffer(const fs::path &path, int w, int h) {
    if (w <= 0 || h <= 0) return false;
    std::vector<unsigned char> pixels((size_t)w * h * 4);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    const size_t stride = (size_t)w * 4;
    for (int y = 0; y < h / 2; ++y)
        std::swap_ranges(pixels.begin() + y * stride, pixels.begin() + (y + 1) * stride,
                         pixels.begin() + (h - y - 1) * stride);
    SDL_Surface *surface = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, pixels.data(), w * 4);
    if (!surface) return false;
    bool ok = SDL_SaveBMP(surface, path.string().c_str());
    SDL_DestroySurface(surface);
    return ok;
}

bool PreviewReady(const App &app) {
    return !app.slides.empty() && std::all_of(app.slides.begin(), app.slides.end(),
        [](const auto &slide) { return slide->id != 0 && slide->w > 0 && slide->h > 0; });
}

void LoadDeck(App &app) {
    std::string text = ReadFile(app.deckPath);
    app.editor.SetText(text);
    app.slideStarts = SlideStarts(text);
    app.dirtySinceLastSave = false;
    std::error_code ec;
    app.lastWriteTime = fs::last_write_time(app.deckPath, ec);
    if (fs::is_regular_file(app.deckPath, ec)) app.recentFiles.Remember(app.deckPath);
}

// Point everything at a (possibly new) deck + theme and force a fresh build.
// Clears the old preview so a stale render never shows against the new deck.
void SwitchDeck(App &app) {
    ++app.buildGeneration;
    LoadDeck(app);
    app.editor.SetSelection({0, 0}, {0, 0});
    app.editor.SetCursorPosition({0, 0});
    app.cursorSlide = 0;
    app.viewSlide = 0;
    app.lastCursorLine = -1;
    app.slides.clear();
    LOGI << "switched deck to " << app.deckPath.string();
    if (app.worker)
        app.worker->Retarget(app.deckPath.string(), app.themePath.string());
}

bool OpenDeck(App &app, const fs::path &path) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec) || !std::ifstream(path, std::ios::binary)) {
        app.status = "open failed";
        app.lastError = "Cannot open deck: " + path.string();
        LOGE << app.lastError;
        return false;
    }
    app.deckPath = fs::absolute(path);
    SwitchDeck(app);
    return true;
}

// Unified slide navigation — see app.h. Sets viewSlide (clamped) and asks the
// thumbnail strip to center the selected slide.
void SetViewSlide(App &app, int slide) {
    int total = (int)app.slides.size();
    int clamped = total > 0 ? (std::max)(0, (std::min)(slide, total - 1)) : 0;
    if (clamped != app.viewSlide) {
        app.viewSlide = clamped;
        app.centerThumbOnSync = true;
    }
}

static bool SaveDeck(App &app) {
    std::ofstream f(app.deckPath, std::ios::binary | std::ios::trunc);
    f << app.editor.GetText();
    f.close();
    if (!f) {
        app.status = "save failed";
        app.lastError = "Cannot save deck: " + app.deckPath.string();
        LOGE << app.lastError;
        return false;
    }
    app.dirtySinceLastSave = false;
    std::error_code ec;
    app.lastWriteTime = fs::last_write_time(app.deckPath, ec);
    app.status = "saved " + app.deckPath.filename().string();
    if (!app.marpCliMissing)
        app.worker->Request();
    return true;
}

// ---------------------------------------------------------------------------
// Marp theme discovery: stems of themes/*.css under repoRoot.
// ---------------------------------------------------------------------------
static void ScanMarpThemes(App &app) {
    app.marpThemes.clear();
    fs::path dir = app.repoRoot / "themes";
    std::error_code ec;
    for (auto &e : fs::directory_iterator(dir, ec)) {
        if (e.path().extension() == ".css")
            app.marpThemes.push_back(e.path().stem().string());
    }
    if (app.marpThemes.empty()) app.marpThemes.push_back("programmer");
    // keep selection valid; prefer "programmer"
    app.marpThemeIndex = 0;
    for (int i = 0; i < (int)app.marpThemes.size(); ++i)
        if (app.marpThemes[i] == "programmer") { app.marpThemeIndex = i; break; }
}

static fs::path SelectedMarpThemePath(App &app) {
    return app.repoRoot / "themes" / (app.marpThemes[app.marpThemeIndex] + ".css");
}

// Window title follows the open deck: "Marp GUI - <name>.md"
static void UpdateWindowTitle(SDL_Window *window, const App &app) {
    std::string title = "Marp GUI - " + app.deckPath.filename().string();
    if (app.dirtySinceLastSave) title += " *";
    SDL_SetWindowTitle(window, title.c_str());
}

// Owned export work: only the main thread reads/writes App state.
struct ExportResult { int code; std::string message; };
static mg::Process exportProcess;
static std::future<ExportResult> exportFuture;

void ExportDeck(App &app, const std::string &fmt) {
    if (exportFuture.valid()) { app.status = "export already running"; return; }
    if (!SaveDeck(app)) return;
    fs::path out = app.deckPath;
    out.replace_extension("." + fmt);
    auto args = mg::MarpCommand();
    args.insert(args.end(), {app.deckPath.string(), "--theme-set", app.themePath.string(),
                             "--allow-local-files", "--" + fmt, "-o", out.string()});
    fs::path log = app.runtimeDir / ("export_" + fmt + ".txt");
    app.status = "exporting " + fmt;
    exportFuture = std::async(std::launch::async, [args, log, out, fmt] {
        int code = exportProcess.Run(args, log);
        return ExportResult{code, code == 0 ? "exported " + out.filename().string()
                                            : "export " + fmt + " failed: " + ReadFile(log)};
    });
}

// ---------------------------------------------------------------------------
// File dialogs — native via nativefiledialog-extended (Win32/macOS/Linux).
// Returns true and fills outPath on selection. Caller clears ImGui held keys
// after the modal (an NFD dialog can swallow the modifier key-up and leave
// ImGui's KeyCtrl stuck — the ctrl+wheel trap).
// ---------------------------------------------------------------------------
static bool OpenFileDialog(const char *suggestDir, std::string &outPath) {
    nfdu8char_t *out = nullptr;
    nfdu8filteritem_t filter = {"Markdown", "md,markdown"};
    nfdresult_t r = NFD_OpenDialogU8(&out, &filter, 1,
                                     (suggestDir && *suggestDir) ? suggestDir : nullptr);
    ImGui::GetIO().ClearInputKeys(); // NFD modal may swallow modifier key-up
    if (r == NFD_OKAY && out) {
        outPath = out;
        NFD_FreePathU8(out);
        return true;
    }
    if (r == NFD_ERROR)
        LOGE << "NFD_OpenDialog error: " << (NFD_GetError() ? NFD_GetError() : "(unknown)");
    return false;
}

static bool SaveFileDialog(const char *suggestName, std::string &outPath) {
    nfdu8char_t *out = nullptr;
    nfdu8filteritem_t filter = {"Markdown", "md,markdown"};
    nfdresult_t r = NFD_SaveDialogU8(&out, &filter, 1, nullptr,
                                     (suggestName && *suggestName) ? suggestName : "deck.md");
    ImGui::GetIO().ClearInputKeys(); // NFD modal may swallow modifier key-up
    if (r == NFD_OKAY && out) {
        outPath = out;
        NFD_FreePathU8(out);
        return true;
    }
    if (r == NFD_ERROR)
        LOGE << "NFD_SaveDialog error: " << (NFD_GetError() ? NFD_GetError() : "(unknown)");
    return false;
}


int main(int argc, char *argv[]) {
    mg::InstallCrashHandler(); // log + minidump on unhandled exception/abort

    // --- App state ------------------------------------------------------------
    App app;
    std::string deckArg;
    fs::path screenshot;
    mg::InputState inputState;
#ifdef MARP_GUI_TESTS
    bool testDeck = true; // never modify a caller's deck during automation
#else
    bool testDeck = false;
#endif
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--screenshot" && i + 1 < argc) screenshot = fs::absolute(argv[++i]);
        else if (arg == "--test-deck" && i + 1 < argc) { deckArg = argv[++i]; testDeck = true; }
        else if (!arg.empty() && arg[0] != '-') deckArg = arg;
        else { std::fprintf(stderr, "Usage: marp_gui [deck.md] [--screenshot output.bmp]\n"); return 1; }
    }

    // SDL returns Contents/Resources for a bundle, or the executable directory.
    const char *base = SDL_GetBasePath();
    fs::path root = fs::current_path();
    for (fs::path p = base ? fs::path(base) : root; !p.empty(); p = p.parent_path()) {
        if (fs::is_directory(p / "themes")) { root = p; break; }
        if (p == p.parent_path()) break;
    }
    char *pref = SDL_GetPrefPath("marp-gui", "Marp GUI");
    if (!pref) { LOGE << SDL_GetError(); return 1; }
    fs::path userDir = pref;
    SDL_free(pref);
    app.logPath = userDir / "marp_gui.log";
    app.runtimeDir = userDir / ("session-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(app.runtimeDir);
    if (!fs::is_directory(root / "themes")) {
        root = app.runtimeDir / "resources";
        mg::WriteBuiltinResources(root);
    }
    app.repoRoot = root;
    app.recentFiles.Load((testDeck ? app.runtimeDir : userDir) / "recent-files.txt");
    app.editorColorSchemePath = (testDeck ? app.runtimeDir : userDir) / "editor-color-scheme.txt";
    app.editorColorSchemeIndex = editor_colors::Load(app.editorColorSchemePath);
    mg::ConfigureMarpEnvironment(root);
    app.deckPath = fs::absolute(deckArg.empty() ? "slides.md" : deckArg);
    if (testDeck) {
        fs::path source = deckArg.empty() && !fs::is_regular_file(app.deckPath)
            ? root / "slides.md" : app.deckPath;
        app.deckPath = app.runtimeDir / "测试 deck $literal 'quote'.md";
        fs::copy_file(source, app.deckPath);
    } else if (deckArg.empty() && !fs::is_regular_file(app.deckPath)) {
        app.deckPath = userDir / "slides.md";
        if (!fs::exists(app.deckPath)) fs::copy_file(root / "slides.md", app.deckPath);
    }
    app.themePath = app.repoRoot / "themes" / "programmer.css";
    app.screenshotPath = screenshot;

    // --- SDL + OpenGL -----------------------------------------------------------
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    // nativefiledialog-extended backend (Win32 COM / AppKit / GTK-portal).
    if (NFD_Init() != NFD_OKAY)
        LOGW << "NFD_Init failed: " << (NFD_GetError() ? NFD_GetError() : "(unknown)");

#if PLATFORM_MACOS
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
#endif
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
#if PLATFORM_MACOS
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
    const char *glslVersion = "#version 150";
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    const char *glslVersion = "#version 330 core";
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    SDL_Window *window = SDL_CreateWindow("Marp GUI - slides.md",
        1440, 900, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GLContext glCtx = SDL_GL_CreateContext(window);
    if (!glCtx || !SDL_GL_MakeCurrent(window, glCtx)) {
        LOGE << "OpenGL context failed: " << SDL_GetError();
        SDL_DestroyWindow(window); NFD_Quit(); SDL_Quit(); return 1;
    }
    SDL_GL_SetSwapInterval(1); // vsync

    // SDL3 no longer enables text input by default. The editor (TextEditor)
    // reads io.InputQueueCharacters directly and never raises WantTextInput,
    // so without this SDL_EVENT_TEXT_INPUT never fires and typing only
    // registers delete/newline (key events). Start it once for the window.
    SDL_StartTextInput(window);

    // Window icon: Marp logo (assets/marp_logo.png, resolved from app root).
    {
        fs::path iconPath = app.repoRoot / "gui" / "assets" / "marp_logo.png";
        int iw = 0, ih = 0, ich = 0;
        stbi_uc *px = stbi_load(iconPath.string().c_str(), &iw, &ih, &ich, 4);
        if (px) {
            SDL_Surface *icon = SDL_CreateSurfaceFrom(iw, ih, SDL_PIXELFORMAT_RGBA32,
                                                      px, iw * 4);
            if (icon) {
                SDL_SetWindowIcon(window, icon);
                SDL_DestroySurface(icon);
            }
            stbi_image_free(px);
        }
    }

    // --- ImGui -------------------------------------------------------------------
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
#if PLATFORM_MACOS
    io.ConfigMacOSXBehaviors = true;
#endif
    std::string iniPath = (userDir / "imgui.ini").string();
    io.IniFilename = iniPath.c_str();

    // DPI scaling: system DPI scales both widget
    // sizes and font density so the UI isn't tiny on HiDPI displays.
    float dpiScale = 1.0f;
#if PLATFORM_WINDOWS
    dpiScale = (float)GetDpiForSystem() / 96.0f;
#endif
    if (dpiScale <= 0.0f) dpiScale = 1.0f;
    {
        ImGuiStyle &style = ImGui::GetStyle();
        style.FontScaleMain = dpiScale;   // ImGui 1.92: replaces io.FontGlobalScale
        style.ScaleAllSizes(dpiScale);
    }

    ImGui::StyleColorsDark();

    ImGui_ImplSDL3_InitForOpenGL(window, glCtx);
    ImGui_ImplOpenGL3_Init(glslVersion);

    // Default UI theme: Programmer (matches themes/programmer.css).
    themes::ApplyTheme(0);

    ScanMarpThemes(app);
    app.themePath = SelectedMarpThemePath(app);

    // Check availability before loading: ImGui asserts for nonexistent fonts.
#if PLATFORM_MACOS
    const char *monoPath = "/System/Library/Fonts/Menlo.ttc";
    const char *uiPath = "/System/Library/Fonts/Supplemental/Arial.ttf";
    const char *cjkPath = "/System/Library/Fonts/PingFang.ttc";
    if (!fs::exists(cjkPath)) cjkPath = "/System/Library/Fonts/Supplemental/Songti.ttc";
#else
    const char *monoPath = "C:/Windows/Fonts/consola.ttf";
    const char *uiPath = "C:/Windows/Fonts/segoeui.ttf";
    const char *cjkPath = "C:/Windows/Fonts/msyh.ttc";
    if (!fs::exists(cjkPath)) cjkPath = "C:/Windows/Fonts/simhei.ttf";
#endif
    auto loadFont = [&](const char *path) {
        ImFont *font = fs::exists(path) ? io.Fonts->AddFontFromFileTTF(path, 16.0f) : nullptr;
        if (!font) font = io.Fonts->AddFontDefault();
        if (fs::exists(cjkPath)) {
            ImFontConfig cfg;
            cfg.MergeMode = true;
            cfg.PixelSnapH = true;
            io.Fonts->AddFontFromFileTTF(cjkPath, 16.0f, &cfg,
                                       io.Fonts->GetGlyphRangesChineseFull());
        }
        return font;
    };
    ImFont *mono = loadFont(monoPath);
    ImFont *ui = loadFont(uiPath);
    io.FontDefault = ui;

    // --- Editor setup --------------------------------------------------------------
    app.editor.SetLanguageDefinition(markdown_lang::Markdown());
    app.editor.SetPalette(editor_colors::kSchemes[app.editorColorSchemeIndex].palette());
    LoadDeck(app);

#ifdef MARP_GUI_TESTS
    // --- Test engine ---------------------------------------------------------------
    ImGuiTestEngine *testEngine = ImGuiTestEngine_CreateContext();
    ImGuiTestEngineIO &teIo = ImGuiTestEngine_GetIO(testEngine);
    teIo.ConfigVerboseLevel = ImGuiTestVerboseLevel_Info;
    teIo.ConfigVerboseLevelOnError = ImGuiTestVerboseLevel_Debug;
    teIo.ConfigLogToTTY = true;
    teIo.ConfigRunSpeed = ImGuiTestRunSpeed_Cinematic; // visible but quick
    RegisterMarpGuiTests(testEngine, &app);
    ImGuiTestEngine_Start(testEngine, ImGui::GetCurrentContext());
    ImGuiTestEngine_QueueTests(testEngine, ImGuiTestGroup_Tests, "marp_gui",
                               ImGuiTestRunFlags_RunFromCommandLine);
#endif

    mg::Process probeProcess;
    std::future<int> probeFuture;
    auto startProbe = [&] {
        auto args = mg::MarpCommand();
        args.push_back("--version");
        fs::path log = app.runtimeDir / "probe.txt";
        probeFuture = std::async(std::launch::async, [&, args, log] {
            return probeProcess.Run(args, log);
        });
    };
    startProbe();
    if (!fs::exists(app.themePath)) {
        app.lastError = "Theme not found: " + app.themePath.string();
        LOGE << app.lastError;
    }

    LOGI << "Marp GUI starting; deck=" << app.deckPath.string()
         << " theme=" << app.themePath.string()
         << " root=" << app.repoRoot.string();

    // --- Build worker ---------------------------------------------------------------
    fs::path buildDir = app.runtimeDir / "preview";
    app.worker = std::make_unique<BuildWorker>(
        app.deckPath.string(), app.themePath.string(), buildDir);
    app.worker->Request(); // initial build (probe may flip marpCliMissing later)

    bool running = true;
    auto launchTime = std::chrono::steady_clock::now();
    int captureFrames = 0;
    bool captureFailed = false;
#if PLATFORM_WINDOWS
    bool fastImePolling = false;
#endif
    inputState.Attach(window, app.editor);
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            ImGui_ImplSDL3_ProcessEvent(&ev);
            inputState.Event(ev);
            if (ev.type == SDL_EVENT_QUIT) running = false;
            if (ev.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                ev.window.windowID == SDL_GetWindowID(window))
                running = false;
        }

#if PLATFORM_WINDOWS && !defined(MARP_GUI_TESTS)
        mg::InputState::ReleaseKeysNotHeld([](int native) {
            return (GetAsyncKeyState(native) & 0x8000) != 0;
        });
#endif

        // --- Periodic logic ---------------------------------------------------------
        auto now = std::chrono::steady_clock::now();

        if (probeFuture.valid() && probeFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            int code = probeFuture.get();
            bool wasMissing = app.marpCliMissing;
            app.marpCliMissing = code != 0;
            if (code != 0) {
                app.lastError = "marp-cli not found — install Node.js and @marp-team/marp-cli";
                LOGW << app.lastError;
            } else {
                if (wasMissing) {
                    app.lastError.clear();
                    app.worker->Request();
                }
                LOGI << "marp-cli detected";
            }
        }
        if (exportFuture.valid() && exportFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            auto result = exportFuture.get();
            app.status = result.message;
            if (result.code != 0) app.lastError = result.message;
            else app.lastError.clear();
            LOGI << result.message;
        }

        // Debounce: schedule rebuild ~800 ms after last edit.
        if (app.buildScheduled && now - app.lastEdit > std::chrono::milliseconds(800)) {
            app.buildScheduled = false;
            if (app.marpCliMissing) {
                // marp-cli not installed — don't run the builder; the modal is up.
            } else if (app.autosave && app.dirtySinceLastSave) SaveDeck(app);
            else app.worker->Request();
        }

        // External change detection: poll mtime ~1 s.
        if (now - app.lastMtimePoll > std::chrono::seconds(1)) {
            app.lastMtimePoll = now;
            std::error_code ec;
            auto wt = fs::last_write_time(app.deckPath, ec);
            if (!ec && wt != app.lastWriteTime) {
                LoadDeck(app);
                if (!app.marpCliMissing) app.worker->Request();
                app.status = "reloaded (file changed on disk)";
            }
        }

        // Collect build results.
        BuildResult br;
        if (app.worker->TakeResult(br)) {
            if (br.generation != app.buildGeneration) {
                // Stale build (deck/theme changed while it ran) — discard.
                app.status = "discarding stale build";
                LOGD << "discarding stale build (gen " << br.generation
                     << " != " << app.buildGeneration << ")";
            } else if (br.ok) {
                std::vector<std::unique_ptr<SlideTexture>> next;
                for (auto &p : br.pngPaths) {
                    auto t = std::make_unique<SlideTexture>();
                    t->Upload(p);
                    if (t->id) next.push_back(std::move(t));
                }
                if (!next.empty()) {
                    app.slides = std::move(next);
                    int total = (int)app.slides.size();
                    if (app.cursorSlide >= total) app.cursorSlide = total - 1;
                    if (app.viewSlide >= total) app.viewSlide = total - 1;
                    app.status = "built " + std::to_string(total) + " slides";
                    app.lastError.clear();
                    LOGD << "built " << total << " slides";
                } else {
                    app.lastError = "failed to decode preview PNGs";
                    LOGW << app.lastError;
                }
            } else {
                // Keep last good preview.
                app.lastError = br.error;
                app.status = "build failed — keeping previous preview";
                LOGW << "build failed: " << br.error;
            }
        }

        // --- Frame ------------------------------------------------------------------
        UpdateWindowTitle(window, app);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        ImGui::PushFont(mono);

        // --- Menu bar --------------------------------------------------------------
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Open...", (io.ConfigMacOSXBehaviors ? "Cmd+O" : "Ctrl+O"))) {
                    std::string path;
                    if (OpenFileDialog(app.deckPath.parent_path().string().c_str(), path)) {
                        OpenDeck(app, path);
                    }
                }
                if (ImGui::BeginMenu("Open Recent")) {
                    fs::path selected;
                    const auto &recent = app.recentFiles.Paths();
                    if (recent.empty()) ImGui::TextDisabled(app.recentFiles.IsEnabled()
                        ? "No recent files" : "Recent file history is disabled");
                    for (int i = 0; i < (int)recent.size(); ++i) {
                        std::string label = recent[i].filename().string() + "###recent-file-" + std::to_string(i);
                        if (ImGui::MenuItem(label.c_str())) selected = recent[i];
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", recent[i].string().c_str());
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Clear Recent", nullptr, false, !recent.empty())) app.recentFiles.Clear();
                    ImGui::EndMenu();
                    if (!selected.empty()) OpenDeck(app, selected);
                }
                if (ImGui::MenuItem("Save", (io.ConfigMacOSXBehaviors ? "Cmd+S" : "Ctrl+S"))) SaveDeck(app);
                if (ImGui::MenuItem("Save As...")) {
                    std::string path;
                    std::string cur = app.deckPath.string();
                    if (SaveFileDialog(app.deckPath.filename().string().c_str(), path)) {
                        app.deckPath = fs::absolute(path);
                        SaveDeck(app);       // write buffer to the new path
                        SwitchDeck(app);     // reload from it + rebuild cleanly
                    }
                }
                ImGui::Separator();
                if (ImGui::BeginMenu("Export")) {
                    if (ImGui::MenuItem("PDF"))  ExportDeck(app, "pdf");
                    if (ImGui::MenuItem("PPTX")) ExportDeck(app, "pptx");
                    if (ImGui::MenuItem("HTML")) ExportDeck(app, "html");
                    ImGui::EndMenu();
                }
                if (ImGui::MenuItem("Commands...", (io.ConfigMacOSXBehaviors ? "Cmd+P" : "Ctrl+P"))) app.showPalette = true;
                ImGui::Separator();
                if (ImGui::MenuItem("Settings...")) app.showSettings = true;
                ImGui::Separator();
                if (ImGui::MenuItem(io.ConfigMacOSXBehaviors ? "Quit" : "Exit",
                                    io.ConfigMacOSXBehaviors ? "Cmd+Q" : nullptr)) running = false;
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Edit")) {
                if (ImGui::MenuItem("Undo", (io.ConfigMacOSXBehaviors ? "Cmd+Z" : "Ctrl+Z"), false, app.editor.CanUndo()))
                    app.editor.Undo();
                if (ImGui::MenuItem("Redo", (io.ConfigMacOSXBehaviors ? "Cmd+Shift+Z" : "Ctrl+Y"), false, app.editor.CanRedo()))
                    app.editor.Redo();
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Help")) {
                if (ImGui::MenuItem("About")) app.showAbout = true;
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }

        // --- Command palette dispatch (same code paths as the menus) ----------------
        {
            palette::Action action = palette::Render(app.showPalette, app);
            switch (action.command) {
            case palette::Command::Open: {
                std::string path;
                if (OpenFileDialog(app.deckPath.parent_path().string().c_str(), path)) {
                    OpenDeck(app, path);
                }
                break;
            }
            case palette::Command::OpenRecent: OpenDeck(app, action.path); break;
            case palette::Command::ClearRecent: app.recentFiles.Clear(); break;
            case palette::Command::Save: SaveDeck(app); break;
            case palette::Command::SaveAs: {
                std::string path;
                std::string cur = app.deckPath.string();
                if (SaveFileDialog(app.deckPath.filename().string().c_str(), path)) {
                    app.deckPath = fs::absolute(path);
                    SaveDeck(app);
                    SwitchDeck(app);
                }
                break;
            }
            case palette::Command::ExportPdf:  ExportDeck(app, "pdf"); break;
            case palette::Command::ExportPptx: ExportDeck(app, "pptx"); break;
            case palette::Command::ExportHtml: ExportDeck(app, "html"); break;
            case palette::Command::Undo: if (app.editor.CanUndo()) app.editor.Undo(); break;
            case palette::Command::Redo: if (app.editor.CanRedo()) app.editor.Redo(); break;
            case palette::Command::NextSlide: SetViewSlide(app, app.viewSlide + 1); break;
            case palette::Command::PrevSlide: SetViewSlide(app, app.viewSlide - 1); break;
            case palette::Command::Settings: app.showSettings = true; break;
            case palette::Command::About:    app.showAbout = true; break;
            case palette::Command::Exit:     running = false; break;
            case palette::Command::None: break;
            }
        }

        float topBars = ImGui::GetFrameHeight(); // menu bar only

        // --- marp-cli missing (blocking modal: Retry / Close) -------------------------
        if (app.marpCliMissing) ImGui::OpenPopup("marp-cli not found##modal");
        if (ImGui::BeginPopupModal("marp-cli not found##modal", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("marp-cli is required to render previews and exports.");
            ImGui::Spacing();
            ImGui::TextUnformatted("Install it first — see the instructions on its GitHub page:");
            ImGui::TextLinkOpenURL("github.com/marp-team/marp-cli",
                                   "https://github.com/marp-team/marp-cli");
            ImGui::Spacing();
            ImGui::TextDisabled("Quick install:  npm i -g @marp-team/marp-cli");
            ImGui::Separator();
            bool probing = probeFuture.valid();
            if (probing) ImGui::BeginDisabled();
            if (ImGui::Button(probing ? "Checking..." : "Retry", ImVec2(140, 0))) startProbe();
            if (probing) ImGui::EndDisabled();
            if (!app.marpCliMissing) ImGui::CloseCurrentPopup();
            ImGui::SameLine();
            if (ImGui::Button("Close", ImVec2(140, 0)))
                running = false; // close the app
            ImGui::EndPopup();
        }

        // --- Settings modal (Esc closes) ------------------------------------------------
        if (app.showSettings) ImGui::OpenPopup("Settings##modal");
        if (ImGui::BeginPopupModal("Settings##modal", &app.showSettings,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            // UI theme picker
            ImGui::TextUnformatted("UI Theme");
            ImGui::SetNextItemWidth(220.0f);
            if (ImGui::BeginCombo("##uitheme", themes::kThemes[themes::GetCurrentThemeIndex()].display_name)) {
                for (int i = 0; i < (int)themes::kThemesCount; ++i) {
                    bool sel = (i == themes::GetCurrentThemeIndex());
                    if (ImGui::Selectable(themes::kThemes[i].display_name, sel))
                        themes::ApplyTheme(i);
                }
                ImGui::EndCombo();
            }

            ImGui::TextUnformatted("Editor Color Scheme");
            ImGui::SetNextItemWidth(220.0f);
            if (ImGui::BeginCombo("##editorcolors", editor_colors::kSchemes[app.editorColorSchemeIndex].name)) {
                for (int i = 0; i < editor_colors::kSchemeCount; ++i) {
                    bool selected = (i == app.editorColorSchemeIndex);
                    if (ImGui::Selectable(editor_colors::kSchemes[i].name, selected)) {
                        app.editorColorSchemeIndex = i;
                        app.editor.SetPalette(editor_colors::kSchemes[i].palette());
                        editor_colors::Save(app.editorColorSchemePath, i);
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            bool insertSpaces = app.editor.IsInsertSpaces();
            if (ImGui::Checkbox("Use spaces for tabs", &insertSpaces))
                app.editor.SetInsertSpaces(insertSpaces);
            ImGui::Checkbox("Autosave", &app.autosave);
            bool saveRecentFiles = app.recentFiles.IsEnabled();
            if (ImGui::Checkbox("Save recent file list", &saveRecentFiles))
                app.recentFiles.SetEnabled(saveRecentFiles);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Disabling this clears the saved list and stops recording recently opened files.");
            if (ImGui::Checkbox("Log to file", &app.logToFile)) {
                if (app.logToFile) {
                    mg::Log::Instance().SetFileOutput(
                        app.logPath.string(), true);
                    LOGI << "log to file enabled";
                } else {
                    LOGI << "log to file disabled";
                    mg::Log::Instance().SetFileOutput("");
                }
            }

            ImGui::Separator();
            if (ImGui::Button("Close", ImVec2(120, 0)) ||
                ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                app.showSettings = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // --- About modal (Esc closes) ----------------------------------------------------
        if (app.showAbout) ImGui::OpenPopup("About##modal");
        if (ImGui::BeginPopupModal("About##modal", &app.showAbout,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Marp GUI");
            ImGui::TextDisabled("Native Marp slide editor with live preview.");
            ImGui::Separator();
            ImGui::TextLinkOpenURL("marp-cli", "https://github.com/marp-team/marp-cli");
            ImGui::TextLinkOpenURL("Dear ImGui", "https://github.com/ocornut/imgui");
            ImGui::TextLinkOpenURL("ImGuiColorTextEdit", "https://github.com/BalazsJako/ImGuiColorTextEdit");
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                app.showAbout = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        ImGui::SetNextWindowPos(ImVec2(0, topBars));
        ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, io.DisplaySize.y - topBars));
        ImGui::Begin("main", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoCollapse);

        // Left: editor — width driven by a draggable splitter.
        float availW = ImGui::GetContentRegionAvail().x;
        float splitterW = 6.0f;
        float editorW = availW * app.splitFrac;

        ImGui::BeginChild("editor-pane", ImVec2(editorW, 0), true);
        app.editor.Render("editor");

        // Anchor the native IME UI to the focused editor caret.
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) {
            ImGuiContext &g = *ImGui::GetCurrentContext();
            g.PlatformImeData.WantVisible = true;
            g.PlatformImeData.WantTextInput = true;
            g.PlatformImeData.InputPos = app.editor.GetCaretScreenPos();
            g.PlatformImeData.InputLineHeight = app.editor.GetCaretHeight();
        }
        if (app.editor.IsTextChanged()) {
            // text changed
            app.dirtySinceLastSave = true;
            app.lastEdit = now;
            app.buildScheduled = true;
            app.slideStarts = SlideStarts(app.editor.GetText());
        }
        // Cursor -> slide sync: only when the cursor actually MOVED (click/keys).
        // This lets the preview browse freely until you click a line again.
        {
            auto pos = app.editor.GetCursorPosition();
            if (pos.mLine != app.lastCursorLine) {
                app.lastCursorLine = pos.mLine;
                int slide = SlideForLine(app.slideStarts, pos.mLine);
                if (slide < (int)app.slides.size()) {
                    app.cursorSlide = slide;
                    SetViewSlide(app, slide); // unified: sets view + centers thumb
                }
            }
        }
        ImGui::EndChild();

        ImGui::SameLine(0, 0);

        // Splitter
        ImGui::InvisibleButton("##splitter", ImVec2(splitterW, -1));
        if (ImGui::IsItemHovered())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (ImGui::IsItemActive()) {
            float delta = ImGui::GetIO().MouseDelta.x;
            app.splitFrac = (std::max)(0.15f, (std::min)(0.85f, app.splitFrac + delta / availW));
        }
        // visible handle
        {
            ImVec2 p0 = ImGui::GetItemRectMin();
            ImVec2 p1 = ImGui::GetItemRectMax();
            ImU32 col = ImGui::GetColorU32(ImGui::IsItemActive() || ImGui::IsItemHovered()
                        ? ImGuiCol_SliderGrabActive : ImGuiCol_Border);
            ImGui::GetWindowDrawList()->AddRectFilled(p0, p1, col);
        }

        ImGui::SameLine(0, 0);

        // Right: preview — free browsing (arrows / scroll / thumbnails);
        // re-snaps to the cursor's slide when the editor cursor moves.
        // No scrollbar on the pane itself: children manage their own.
        ImGui::BeginChild("preview-pane", ImVec2(0, 0), true,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        {
            int total = (int)app.slides.size();
            if (total == 0) {
                ImGui::TextDisabled("No preview yet.");
                if (!app.lastError.empty())
                    ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", app.lastError.c_str());
            } else {
                // clamp view into range
                app.viewSlide = (std::max)(0, (std::min)(app.viewSlide, total - 1));

                // --- nav row: counter only (pages turn via wheel/keys/thumbs) ---
                ImGui::Text("Slide %d / %d", app.viewSlide + 1, total);
                if (app.viewSlide != app.cursorSlide) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("(cursor on %d)", app.cursorSlide + 1);
                }
                // build status dot, right-aligned: green = built & latest, yellow = refreshing
                {
                    bool building = app.worker && app.worker->Building();
                    bool stale = app.buildScheduled || app.dirtySinceLastSave;
                    ImVec4 col = (building || stale)
                        ? ImVec4(0.95f, 0.8f, 0.2f, 1.0f)   // yellow: refreshing
                        : ImVec4(0.3f, 0.85f, 0.4f, 1.0f);   // green: up to date
                    float r = 5.0f;
                    // right-align: position cursor at the right edge
                    float dotW = r * 2 + 8.0f;
                    ImGui::SameLine(ImGui::GetContentRegionAvail().x +
                                    ImGui::GetCursorPosX() - dotW);
                    ImVec2 p = ImGui::GetCursorScreenPos();
                    p.x += 6.0f; p.y += ImGui::GetTextLineHeight() * 0.5f;
                    ImGui::GetWindowDrawList()->AddCircleFilled(p, r, ImGui::GetColorU32(col));
                    ImGui::Dummy(ImVec2(dotW, ImGui::GetTextLineHeight()));
                }

                ImGui::Separator();

                // --- main slide area vs thumbnail strip: horizontal splitter ---
                // Account for every widget between here and the strip so the
                // strip (which fills the remainder) always has >= minStrip.
                ImGuiStyle &st = ImGui::GetStyle();
                const float minStrip = 72.0f, maxStripFrac = 0.5f;
                float paneH = ImGui::GetContentRegionAvail().y;
                // overhead: splitter button height + 3 vertical spacings (after
                // slide-main, after splitter, and the separator above is counted)
                const float splitterH = 6.0f;
                float overhead = splitterH + st.ItemSpacing.y * 3.0f + 4.0f;
                float stripH = paneH * app.thumbFrac;
                stripH = (std::max)(minStrip, (std::min)(paneH * maxStripFrac, stripH));
                float mainH = paneH - stripH - overhead;
                if (mainH < 60.0f) { mainH = 60.0f; stripH = paneH - mainH - overhead; }

                ImGui::BeginChild("slide-main", ImVec2(0, mainH), false,
                                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                // wheel + up/down navigate the viewed slide (unified: centers thumb)
                if (ImGui::IsWindowHovered()) {
                    float wheel = ImGui::GetIO().MouseWheel;
                    if (wheel > 0) SetViewSlide(app, app.viewSlide - 1);
                    if (wheel < 0) SetViewSlide(app, app.viewSlide + 1);
                }
                if (ImGui::IsWindowFocused()) {
                    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))   SetViewSlide(app, app.viewSlide - 1);
                    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) SetViewSlide(app, app.viewSlide + 1);
                }
                {
                    // Scale to fit, preserving aspect ratio; grows with the pane.
                    // -2px epsilon avoids a 1px rounding overflow that would
                    // otherwise trigger a phantom vertical scrollbar.
                    auto &tex = app.slides[app.viewSlide];
                    ImVec2 region = ImGui::GetContentRegionAvail();
                    float scale = (std::min)((region.x - 2.0f) / tex->w,
                                             (region.y - 2.0f) / tex->h);
                    ImVec2 size(tex->w * scale, tex->h * scale);
                    // center horizontally and vertically
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (region.x - size.x) * 0.5f);
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (region.y - size.y) * 0.5f);
                    ImGui::Image((ImTextureID)(intptr_t)tex->id, size);
                }
                ImGui::EndChild();

                // Horizontal splitter between slide and thumbnail strip
                ImGui::InvisibleButton("##hsplitter", ImVec2(-1, splitterH));
                if (ImGui::IsItemHovered())
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
                if (ImGui::IsItemActive()) {
                    float delta = ImGui::GetIO().MouseDelta.y;
                    float totalH = mainH + stripH + overhead;
                    // dragging down grows the slide area → shrinks the strip
                    app.thumbFrac = (std::max)(minStrip / totalH,
                        (std::min)(maxStripFrac, app.thumbFrac - delta / totalH));
                }
                {
                    ImVec2 p0 = ImGui::GetItemRectMin();
                    ImVec2 p1 = ImGui::GetItemRectMax();
                    ImU32 col = ImGui::GetColorU32(ImGui::IsItemActive() || ImGui::IsItemHovered()
                                ? ImGuiCol_SliderGrabActive : ImGuiCol_Border);
                    ImGui::GetWindowDrawList()->AddRectFilled(p0, p1, col);
                }

                // --- thumbnail strip (powerpoint-style) -------------------------
                // Fills remaining height; horizontal scrollbar for many pages.
                // Mouse wheel scrolls it horizontally when hovered.
                ImGui::BeginChild("thumb-strip", ImVec2(0, 0), false,
                                  ImGuiWindowFlags_HorizontalScrollbar);
                // wheel → horizontal pan while hovering the strip
                if (ImGui::IsWindowHovered()) {
                    float wheel = ImGui::GetIO().MouseWheel;
                    if (wheel != 0.0f) {
                        float cur = ImGui::GetScrollX();
                        ImGui::SetScrollX(cur - wheel * 60.0f);
                    }
                }
                float thumbInnerH = ImGui::GetContentRegionAvail().y -
                                    ImGui::GetStyle().FramePadding.y * 2.0f - 2.0f;
                if (thumbInnerH < 24.0f) thumbInnerH = 24.0f;
                for (int i = 0; i < total; ++i) {
                    auto &t = app.slides[i];
                    float tw = thumbInnerH * ((float)t->w / (float)t->h); // keep aspect
                    if (i > 0) ImGui::SameLine();
                    ImGui::PushID(i);
                    bool selected = (i == app.viewSlide);
                    if (selected)
                        ImGui::PushStyleColor(ImGuiCol_Border, ImGui::GetStyleColorVec4(ImGuiCol_SliderGrabActive));
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, selected ? 2.0f : 0.0f);
                    ImVec2 thumbPos = ImGui::GetCursorScreenPos(); // for centering
                    if (ImGui::ImageButton("##thumb", (ImTextureID)(intptr_t)t->id,
                                           ImVec2(tw, thumbInnerH)))
                        SetViewSlide(app, i); // unified
                    ImGui::PopStyleVar();
                    if (selected) ImGui::PopStyleColor();
                    // center the selected thumb whenever any nav path asked for it
                    // (caret sync, wheel, arrow keys, palette, thumb click)
                    if (selected && app.centerThumbOnSync) {
                        float itemCenter = thumbPos.x + tw * 0.5f;
                        float stripCenter = ImGui::GetWindowPos().x +
                                            ImGui::GetWindowWidth() * 0.5f;
                        float target = ImGui::GetScrollX() + (itemCenter - stripCenter);
                        ImGui::SetScrollX((std::max)(0.0f, target));
                        app.centerThumbOnSync = false;
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Slide %d", i + 1);
                    ImGui::PopID();
                }
                ImGui::EndChild();
            }
        }
        ImGui::EndChild();

        ImGui::End(); // main

        // Error bar
        if (!app.lastError.empty()) {
            ImGui::SetNextWindowPos(ImVec2(0, io.DisplaySize.y - 28));
            ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, 28));
            ImGui::Begin("errorbar", nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar);
            ImGui::TextColored(ImVec4(1, 0.55f, 0.4f, 1), "%s", app.lastError.c_str());
            ImGui::End();
        }

        ImGui::PopFont();

        // Global hotkeys (when no modal/palette is up). Undo/redo are NOT
        // handled here — the TextEditor owns Ctrl+Z/Y when focused, and the
        // Edit menu covers the rest. Intercepting them globally broke typing.
        if (!app.showSettings && !app.showAbout && !app.showPalette) {
            // ImGui maps the physical Command key to KeyCtrl on macOS.
#if PLATFORM_MACOS
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Q, false)) running = false;
#endif
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
                SaveDeck(app);
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O, false)) {
                std::string path;
                if (OpenFileDialog(app.deckPath.parent_path().string().c_str(), path)) {
                    OpenDeck(app, path);
                }
            }
        }
        // Palette toggle works regardless.
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_P, false))
            app.showPalette = !app.showPalette;

        ImGui::Render();
        int pixelW = 0, pixelH = 0;
        SDL_GetWindowSizeInPixels(window, &pixelW, &pixelH);
        glViewport(0, 0, pixelW, pixelH);
        glClearColor(0.10f, 0.10f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        if (!app.screenshotPath.empty() && PreviewReady(app) && ++captureFrames > 30) {
            if (!CaptureFramebuffer(app.screenshotPath, pixelW, pixelH)) {
                LOGE << "Screenshot failed: " << SDL_GetError();
                captureFailed = true;
            } else LOGI << "Screenshot saved: " << app.screenshotPath.string()
                        << " (" << pixelW << "x" << pixelH << ")";
            app.screenshotPath.clear();
            captureFrames = 0;
            if (!screenshot.empty()) running = false;
        }
        if (!screenshot.empty() && now - launchTime > std::chrono::seconds(60)) {
            LOGE << "Timed out waiting for preview: " << app.lastError;
            captureFailed = true;
            running = false;
        }

#ifdef MARP_GUI_TESTS
        // Post-swap: run test engine hooks; quit shortly after the queue drains.
        ImGuiTestEngine_PostSwap(testEngine);
        {
            ImGuiTestEngineResultSummary s;
            ImGuiTestEngine_GetResultSummary(testEngine, &s);
            static auto firstEmpty = std::chrono::steady_clock::time_point{};
            if (s.CountTested > 0 && ImGuiTestEngine_IsTestQueueEmpty(testEngine)) {
                if (firstEmpty == std::chrono::steady_clock::time_point{})
                    firstEmpty = now;
                if (now - firstEmpty > std::chrono::seconds(2))
                    running = false;
            }
        }
#endif

#if PLATFORM_WINDOWS
        // Pump IME messages between display refreshes.
        bool fastPolling = inputState.WantsFastPolling();
        if (fastPolling != fastImePolling) {
            SDL_GL_SetSwapInterval(fastPolling ? 0 : 1);
            fastImePolling = fastPolling;
        }
#endif
        SDL_GL_SwapWindow(window);
#if PLATFORM_WINDOWS
        if (fastImePolling) SDL_Delay(1);
#endif
    }

    // --- Shutdown --------------------------------------------------------------
    inputState.Detach();
    auto shutdownStart = std::chrono::steady_clock::now();
    auto stepMs = [&](const char *what) {
        LOGD << "shutdown: " << what << " took "
             << std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - shutdownStart).count() << " ms total";
    };

    probeProcess.Cancel();
    exportProcess.Cancel();
    if (probeFuture.valid()) probeFuture.get();
    if (exportFuture.valid()) exportFuture.get();
    app.worker.reset(); // stop worker before GL teardown
    stepMs("worker stop");
    app.slides.clear();
    stepMs("slides clear");

#ifdef MARP_GUI_TESTS
    int passed = 0, failed = 0;
    ImGuiTestEngineResultSummary summary;
    ImGuiTestEngine_GetResultSummary(testEngine, &summary);
    passed = summary.CountSuccess;
    failed = summary.CountTested - summary.CountSuccess + summary.CountInQueue;
    ImGuiTestEngine_Stop(testEngine);
    ImGuiTestEngine_DestroyContext(testEngine);
    std::fprintf(stderr, "[tests] %d passed, %d failed\n", passed, failed);
#endif

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    stepMs("imgui shutdown");
    SDL_GL_DestroyContext(glCtx);
    stepMs("GL context destroy");
    SDL_DestroyWindow(window);
    stepMs("window destroy");
    SDL_Quit();
    stepMs("SDL_Quit");
    NFD_Quit();
    std::error_code cleanupError;
    fs::remove_all(app.runtimeDir, cleanupError);
#ifdef MARP_GUI_TESTS
    return failed == 0 ? 0 : 2;
#else
    return captureFailed ? 1 : 0;
#endif
}
