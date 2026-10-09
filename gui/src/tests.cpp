// Automation tests for marp_gui, driven by Dear ImGui Test Engine.
//
// Registered into the "marp_gui" group; main.cpp queues the whole group when
// built with MARP_GUI_TESTS. Run:  marp_gui_tests.exe [deck.md]

#include "app.h"
#include "platform.h"
#include "themes.h"
#include "markdown_lang.h"

#include <fstream>
#include <chrono>
#include <cstdlib>
#include <sstream>

#include "imgui.h"
#include "imgui_te_engine.h"
#include "imgui_te_context.h"

static bool WaitForPreview(ImGuiTestContext *ctx, App &app) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
    while (!PreviewReady(app) && std::chrono::steady_clock::now() < deadline) ctx->Yield();
    if (!PreviewReady(app)) ctx->LogError("preview unavailable: %s", app.lastError.c_str());
    return PreviewReady(app);
}

static void Capture(ImGuiTestContext *ctx, App &app, const char *name) {
    if (const char *dir = std::getenv("MARP_GUI_CAPTURE_DIR")) {
        fs::create_directories(dir);
        app.screenshotPath = fs::path(dir) / name;
        std::error_code ec;
        fs::remove(app.screenshotPath, ec);
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!app.screenshotPath.empty() && std::chrono::steady_clock::now() < deadline) ctx->Yield();
        IM_CHECK(app.screenshotPath.empty());
        IM_CHECK(fs::is_regular_file(fs::path(dir) / name));
    }
}

struct DeckSwitchVars { fs::path pendingPath; };

void RegisterMarpGuiTests(ImGuiTestEngine *engine, App *app) {
    // --- Slide map parsing ----------------------------------------------------
    {
        ImGuiTest *t = IM_REGISTER_TEST(engine, "marp_gui", "slide_map");
        t->TestFunc = [](ImGuiTestContext *ctx) {
            const char *md =
                "---\nmarp: true\ntheme: programmer\n---\n" // front matter
                "# Slide 1\n\n---\n"                          // slide 2 starts line 6
                "# Slide 2\n\n---\n"                          // slide 3 starts line 9
                "# Slide 3\n";
            auto starts = SlideStarts(md);
            IM_CHECK_EQ(starts.size(), (size_t)3);
            IM_CHECK_EQ(starts[0], 0);
            IM_CHECK_EQ(starts[1], 7);   // line index after first "---" separator
            IM_CHECK_EQ(starts[2], 10);  // line index after second separator
            IM_CHECK_EQ(SlideForLine(starts, 0), 0);
            IM_CHECK_EQ(SlideForLine(starts, 6), 0);
            IM_CHECK_EQ(SlideForLine(starts, 7), 1);
            IM_CHECK_EQ(SlideForLine(starts, 100), 2);
        };
    }

    // --- App booted with a deck loaded ----------------------------------------
    {
        ImGuiTest *t = IM_REGISTER_TEST(engine, "marp_gui", "boot_loads_deck");
        t->UserData = app;
        t->TestFunc = [](ImGuiTestContext *ctx) {
            App &a = *(App *)ctx->Test->UserData;
            IM_CHECK(!a.editor.GetText().empty());
            IM_CHECK(!a.slideStarts.empty());
            ctx->LogInfo("deck: %s, slides mapped: %d",
                         a.deckPath.string().c_str(), (int)a.slideStarts.size());
        };
    }

    // --- Editor input marks dirty ----------------------------------------------
    {
        ImGuiTest *t = IM_REGISTER_TEST(engine, "marp_gui", "editor_input");
        t->UserData = app;
        t->TestFunc = [](ImGuiTestContext *ctx) {
            App &a = *(App *)ctx->Test->UserData;
            std::string before = a.editor.GetText();
            a.editor.InsertText("\n<!-- test-engine edit -->\n");
            IM_CHECK(a.editor.IsTextChanged());
            IM_CHECK(a.editor.GetText().size() > before.size());
            // restore — don't leave test noise in the user's file
            a.editor.SetText(before.c_str());
        };
    }

    // --- Theme table is wired and switchable -----------------------------------
    {
        ImGuiTest *t = IM_REGISTER_TEST(engine, "marp_gui", "themes");
        t->TestFunc = [](ImGuiTestContext *ctx) {
            IM_CHECK(themes::kThemesCount == (size_t)13);
            IM_CHECK_EQ(themes::GetCurrentThemeIndex(), 0); // Programmer default
            for (int i = 0; i < (int)themes::kThemesCount; ++i) {
                themes::ApplyTheme(i);
                IM_CHECK_EQ(themes::GetCurrentThemeIndex(), i);
            }
            themes::ApplyTheme(0); // back to Programmer
        };
    }

    // --- Markdown highlighting is the custom fast tokenizer ---------------------
    {
        ImGuiTest *t = IM_REGISTER_TEST(engine, "marp_gui", "markdown_highlighting");
        t->UserData = app;
        t->TestFunc = [](ImGuiTestContext *ctx) {
            App &a = *(App *)ctx->Test->UserData;
            IM_CHECK(a.editor.GetLanguageDefinition().mName == std::string("Markdown"));
            IM_CHECK(a.editor.GetLanguageDefinition().mTokenize != nullptr);
        };
    }

    // --- Editor undo/redo -------------------------------------------------------
    // TextEditor has built-in Undo/Redo (Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z) wired in
    // HandleKeyboardInputs, confirmed working with real typing. Programmatic
    // InsertText doesn't record undo (by design — it's the load/restore path), so
    // this test exercises the real AddUndo path via EnterCharacter and verifies
    // the Undo/Redo API restores buffer state.
    {
        ImGuiTest *t = IM_REGISTER_TEST(engine, "marp_gui", "editor_undo_redo");
        t->UserData = app;
        t->TestFunc = [](ImGuiTestContext *ctx) {
            App &a = *(App *)ctx->Test->UserData;
            std::string before = a.editor.GetText();
            // Move cursor to end of line 0, then Delete removes the char under
            // it (no selection needed) — that records an UndoRecord.
            a.editor.SetCursorPosition({0, 0});
            a.editor.MoveRight(1, false);
            size_t len0 = a.editor.GetText().size();
            a.editor.Delete();                 // delete one char forward
            IM_CHECK(a.editor.GetText().size() == len0 - 1);
            IM_CHECK(a.editor.CanUndo());
            a.editor.Undo();
            IM_CHECK(a.editor.GetText() == before);
            IM_CHECK(a.editor.CanRedo());
            a.editor.Redo();
            IM_CHECK(a.editor.GetText().size() == len0 - 1);
            a.editor.Undo(); // restore
            IM_CHECK(a.editor.GetText() == before);
        };
    }

    // --- UTF-8 / CJK round-trips through the editor buffer ----------------------
    {
        ImGuiTest *t = IM_REGISTER_TEST(engine, "marp_gui", "utf8_cjk");
        t->UserData = app;
        t->TestFunc = [](ImGuiTestContext *ctx) {
            App &a = *(App *)ctx->Test->UserData;
            std::string before = a.editor.GetText();
            const char *cjk = "\n# 你好，世界 — 中文标题\n\n支持 **Unicode** 字符。\n";
            a.editor.SetText((before + cjk).c_str());
            std::string now = a.editor.GetText();
            IM_CHECK(now.find("你好，世界") != std::string::npos);
            IM_CHECK(now.find("中文标题") != std::string::npos);
            IM_CHECK(now.find("Unicode") != std::string::npos);
            a.editor.SetText(before.c_str()); // restore
        };
    }

    // --- SwitchDeck mid-session doesn't crash ---------------------------------------
    {
        ImGuiTest *t = IM_REGISTER_TEST(engine, "marp_gui", "switch_deck");
        t->UserData = app;
        t->SetVarsDataType<DeckSwitchVars>();
        // Texture disposal must run with the UI thread's current GL context.
        t->GuiFunc = [](ImGuiTestContext *ctx) {
            auto &vars = ctx->GetVars<DeckSwitchVars>();
            if (!vars.pendingPath.empty()) {
                App &a = *(App *)ctx->Test->UserData;
                a.deckPath = vars.pendingPath;
                SwitchDeck(a);
                vars.pendingPath.clear();
            }
        };
        t->TestFunc = [](ImGuiTestContext *ctx) {
            App &a = *(App *)ctx->Test->UserData;
            IM_CHECK(WaitForPreview(ctx, a)); // exercise disposal of real textures
            auto &vars = ctx->GetVars<DeckSwitchVars>();
            // Build a foreign deck with an image reference, in a temp dir.
            fs::path dir = a.runtimeDir / "foreign deck";
            std::error_code ec;
            fs::create_directories(dir, ec);
            fs::path deck = dir / "other.md";
            {
                std::ofstream f(deck);
                f << "---\nmarp: true\n---\n\n# Foreign\n\n![bg](images/x.png)\n";
            }
            fs::path origDeck = a.deckPath;
            vars.pendingPath = deck;
            ctx->Yield(5);                 // let a few frames render the new deck
            IM_CHECK(a.deckPath == deck);
            IM_CHECK(a.editor.GetText().find("# Foreign") != std::string::npos);
            IM_CHECK(a.slideStarts.size() == 1);
            // restore
            vars.pendingPath = origDeck;
            ctx->Yield(3);
            fs::remove_all(dir, ec);
        };
    }

    // --- Main window renders with editor + preview panes -----------------------
    {
        ImGuiTest *t = IM_REGISTER_TEST(engine, "marp_gui", "main_window_layout");
        t->TestFunc = [](ImGuiTestContext *ctx) {
            ctx->Yield(3); // let a few frames render so windows exist
            ImGuiContext &g = *ImGui::GetCurrentContext();
            bool found = false;
            for (ImGuiWindow *w : g.Windows)
                if (strcmp(w->Name, "main") == 0) { found = true; break; }
            if (!found) {
                ctx->LogInfo("windows:");
                for (ImGuiWindow *w : g.Windows)
                    ctx->LogInfo("  '%s'", w->Name);
            }
            IM_CHECK(found);
        };
    }

    {
        ImGuiTest *t = IM_REGISTER_TEST(engine, "marp_gui", "real_preview_and_navigation");
        t->UserData = app;
        t->TestFunc = [](ImGuiTestContext *ctx) {
            App &a = *(App *)ctx->Test->UserData;
            IM_CHECK(WaitForPreview(ctx, a));
            IM_CHECK_EQ(a.slides.size(), a.slideStarts.size());
            IM_CHECK(a.lastError.empty());
            Capture(ctx, a, "editor.bmp");
            a.editor.SetCursorPosition({a.slideStarts[1], 0});
            ctx->Yield(3);
            IM_CHECK_EQ(a.viewSlide, 1);
            SetViewSlide(a, 1000);
            IM_CHECK_EQ(a.viewSlide, (int)a.slides.size() - 1);
            SetViewSlide(a, -1);
            IM_CHECK_EQ(a.viewSlide, 0);
        };
    }

    {
        ImGuiTest *t = IM_REGISTER_TEST(engine, "marp_gui", "platform_keyboard_shortcuts");
        t->UserData = app;
        t->TestFunc = [](ImGuiTestContext *ctx) {
            App &a = *(App *)ctx->Test->UserData;
            bool autosave = a.autosave;
            a.autosave = false;
            std::string before = a.editor.GetText();
            ImGuiWindow *editor = nullptr;
            for (ImGuiWindow *w : ImGui::GetCurrentContext()->Windows)
                if (strstr(w->Name, "/editor_")) editor = w;
            IM_CHECK(editor != nullptr);
            ctx->WindowFocus(editor->ID);
            ctx->MouseMoveToPos(ImVec2(editor->Pos.x + 100, editor->Pos.y + 80));
            ctx->MouseClick();
            a.editor.SetCursorPosition({0, 0});
            ctx->KeyChars("x");
            IM_CHECK(a.editor.GetText() != before);
            std::string edited = a.editor.GetText();
            ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Z);
            IM_CHECK(a.editor.GetText() == before);
            ctx->KeyPress(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z);
            IM_CHECK(a.editor.GetText() == edited);
            ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_A);
            IM_CHECK(a.editor.HasSelection());
            std::string selected = a.editor.GetSelectedText();
            ctx->LogInfo("buffer %d bytes, selected %d bytes", (int)edited.size(), (int)selected.size());
            ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_C);
            IM_CHECK(std::string(ImGui::GetClipboardText()) == selected);
            ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_S);
            std::ifstream file(a.deckPath); std::stringstream disk; disk << file.rdbuf();
            IM_CHECK(disk.str() == edited);
            ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_P);
            ctx->Yield(3);
            IM_CHECK(a.showPalette);
            Capture(ctx, a, "palette.bmp");
            ctx->KeyPress(ImGuiKey_Escape);
            ctx->Yield(3);
            IM_CHECK(!a.showPalette);
            a.editor.SetText(before);
            ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_S);
            a.autosave = autosave;
        };
    }

    {
        ImGuiTest *t = IM_REGISTER_TEST(engine, "marp_gui", "native_fonts_and_settings");
        t->UserData = app;
        t->TestFunc = [](ImGuiTestContext *ctx) {
            App &a = *(App *)ctx->Test->UserData;
#if PLATFORM_MACOS
            IM_CHECK(ImGui::GetIO().ConfigMacOSXBehaviors);
            IM_CHECK(ImGui::GetIO().FontDefault->IsGlyphInFont(0x4f60)); // 你
#endif
            a.showSettings = true;
            ctx->Yield(3);
            Capture(ctx, a, "settings.bmp");
            ctx->KeyPress(ImGuiKey_Escape);
            ctx->Yield(3);
            IM_CHECK(!a.showSettings);
        };
    }

    {
        ImGuiTest *t = IM_REGISTER_TEST(engine, "marp_gui", "real_exports");
        t->UserData = app;
        t->TestFunc = [](ImGuiTestContext *ctx) {
            App &a = *(App *)ctx->Test->UserData;
            for (const char *format : {"html", "pdf", "pptx"}) {
                fs::path output = a.deckPath;
                output.replace_extension(std::string(".") + format);
                ExportDeck(a, format);
                auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
                while (a.status != "exported " + output.filename().string() &&
                       a.status.rfind("export ", 0) != 0 &&
                       std::chrono::steady_clock::now() < deadline) ctx->Yield();
                IM_CHECK(a.status == "exported " + output.filename().string());
                IM_CHECK(fs::exists(output));
                IM_CHECK(fs::file_size(output) > 100);
                std::ifstream in(output, std::ios::binary);
                char header[5] = {}; in.read(header, 4);
                if (std::string(format) == "pdf") IM_CHECK(std::string(header, 4) == "%PDF");
                if (std::string(format) == "pptx") IM_CHECK(std::string(header, 2) == "PK");
            }
        };
    }
}
