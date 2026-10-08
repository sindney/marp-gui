// Automation tests for marp_gui, driven by Dear ImGui Test Engine.
//
// Registered into the "marp_gui" group; main.cpp queues the whole group when
// built with MARP_GUI_TESTS. Run:  marp_gui_tests.exe [deck.md]

#include "app.h"
#include "themes.h"
#include "markdown_lang.h"

#include "imgui.h"
#include "imgui_te_engine.h"
#include "imgui_te_context.h"

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
}
