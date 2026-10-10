// Automation tests for marp_gui, driven by Dear ImGui Test Engine.
//
// Registered into the "marp_gui" group; main.cpp queues the whole group when
// built with MARP_GUI_TESTS. Run:  marp_gui_tests.exe [deck.md]

#include "app.h"
#include "platform.h"
#include "themes.h"
#include "markdown_lang.h"
#include "input_state.h"

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
    {
        ImGuiTest *t = IM_REGISTER_TEST(engine, "marp_gui", "recent_files_persistence");
        t->UserData = app;
        t->TestFunc = [](ImGuiTestContext *ctx) {
            App &a = *(App *)ctx->Test->UserData;
            fs::path dir = a.runtimeDir / "recent files";
            fs::create_directories(dir);
            RecentFiles recent;
            recent.Load(dir / "history.txt");
            for (int i = 0; i < 7; ++i) {
                fs::path path = dir / ("测试 deck " + std::to_string(i) + ".md");
                std::ofstream(path) << "# Recent\n";
                recent.Remember(path);
            }
            IM_CHECK_EQ(recent.Paths().size(), (size_t)5);
            IM_CHECK(recent.Paths().front().filename() == fs::path("测试 deck 6.md"));
            fs::path reopen = recent.Paths()[3];
            recent.Remember(reopen.parent_path() / "." / reopen.filename());
            IM_CHECK_EQ(recent.Paths().size(), (size_t)5);
            IM_CHECK(recent.Paths().front() == reopen);
            RecentFiles restored;
            restored.Load(dir / "history.txt");
            IM_CHECK(restored.Paths() == recent.Paths());
            IM_CHECK(restored.IsEnabled());
            restored.SetEnabled(false);
            IM_CHECK(restored.Paths().empty());
            restored.Remember(reopen);
            IM_CHECK(restored.Paths().empty());
            std::ifstream disabledFile(dir / "history.txt", std::ios::binary);
            std::stringstream disabledData;
            disabledData << disabledFile.rdbuf();
            disabledFile.close();
            IM_CHECK(disabledData.str() == "disabled\n");
            recent.Load(dir / "history.txt");
            IM_CHECK(!recent.IsEnabled());
            recent.Remember(reopen);
            recent.Clear();
            restored.Load(dir / "history.txt");
            IM_CHECK(!restored.IsEnabled());
            IM_CHECK(restored.Paths().empty());
            restored.SetEnabled(true);
            restored.Remember(reopen);
            recent.Load(dir / "history.txt");
            IM_CHECK(recent.IsEnabled());
            IM_CHECK_EQ(recent.Paths().size(), (size_t)1);
            IM_CHECK(recent.Paths().front() == reopen);
            restored.Clear();
            recent.Load(dir / "history.txt");
            IM_CHECK(recent.Paths().empty());
        };
    }
    {
        ImGuiTest *t = IM_REGISTER_TEST(engine, "marp_gui", "markdown_emoji_tokens");
        t->TestFunc = [](ImGuiTestContext *ctx) {
            auto tokenize = markdown_lang::Markdown().mTokenize;
            const char *begin = nullptr, *end = nullptr;
            auto color = TextEditor::PaletteIndex::Default;
            for (const char *text : {":smile:", ":+1:", ":woman-technologist:"}) {
                IM_CHECK(tokenize(text, text + strlen(text), begin, end, color));
                IM_CHECK(begin == text && end == text + strlen(text));
                IM_CHECK(color == TextEditor::PaletteIndex::Default);
            }
            for (const char *text : {"marp: true", "theme:\tprogrammer", "key:"}) {
                IM_CHECK(tokenize(text, text + strlen(text), begin, end, color));
                IM_CHECK(color == TextEditor::PaletteIndex::KnownIdentifier);
            }
            const char *text = "https://example.com";
            IM_CHECK(tokenize(text, text + strlen(text), begin, end, color));
            IM_CHECK(color == TextEditor::PaletteIndex::Default);
        };
    }
    {
        ImGuiTest *t = IM_REGISTER_TEST(engine, "marp_gui", "editor_mouse_selection");
        t->UserData = app;
        t->TestFunc = [](ImGuiTestContext *ctx) {
            App &a = *(App *)ctx->Test->UserData;
            std::string before = a.editor.GetText();
            bool autosave = a.autosave;
            a.autosave = false;
            ImGuiWindow *window = nullptr;
            for (ImGuiWindow *w : ImGui::GetCurrentContext()->Windows)
                if (strstr(w->Name, "/editor_")) window = w;
            IM_CHECK(window != nullptr);
            ctx->WindowFocus(window->ID);
            a.editor.SetText("selection visible\nsecond line");
            a.editor.SetCursorPosition({0, 0});
            a.editor.SetSelection({0, 0}, {0, 0});
            ctx->Yield(3);
            ImVec2 start = a.editor.GetCaretScreenPos();
            float width = ImGui::GetIO().Fonts->Fonts[0]->CalcTextSizeA(
                a.editor.GetCaretHeight(), FLT_MAX, -1.0f, "selection").x;
            ctx->MouseMoveToPos(ImVec2(start.x + 1.0f, start.y + 3.0f));
            ctx->MouseDown();
            ctx->MouseMoveToPos(ImVec2(start.x + width, start.y + 3.0f));
            ctx->MouseUp();
            ctx->Yield(2);
            IM_CHECK(a.editor.HasSelection());
            std::string selected = a.editor.GetSelectedText();
            IM_CHECK(selected == "selection");
            ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_C);
            IM_CHECK(std::string(ImGui::GetClipboardText()) == selected);
            ImU32 color = TextEditor::GetProgrammerPalette()[(int)TextEditor::PaletteIndex::Selection];
            int vertices = 0;
            ImVec2 boundsMin(FLT_MAX, FLT_MAX), boundsMax(-FLT_MAX, -FLT_MAX);
            for (const auto& vertex : window->DrawList->VtxBuffer) {
                if (vertex.col != color) continue;
                ++vertices;
                boundsMin = ImMin(boundsMin, vertex.pos);
                boundsMax = ImMax(boundsMax, vertex.pos);
            }
            IM_CHECK(vertices >= 4);
            IM_CHECK(boundsMax.x - boundsMin.x > 20.0f);
            IM_CHECK(boundsMax.y - boundsMin.y > 5.0f);
            IM_CHECK(window->InnerClipRect.Overlaps(ImRect(boundsMin, boundsMax)));
            Capture(ctx, a, "selection.bmp");
            a.editor.SetText(before);
            a.editor.SetSelection({0, 0}, {0, 0});
            a.editor.SetCursorPosition({0, 0});
            a.autosave = autosave;
        };
    }
    {
        ImGuiTest *t = IM_REGISTER_TEST(engine, "marp_gui", "editor_scrolled_selection");
        t->UserData = app;
        t->TestFunc = [](ImGuiTestContext *ctx) {
            App &a = *(App *)ctx->Test->UserData;
            std::string before = a.editor.GetText();
            bool autosave = a.autosave;
            a.autosave = false;
            ImGuiWindow *window = nullptr;
            for (ImGuiWindow *w : ImGui::GetCurrentContext()->Windows)
                if (strstr(w->Name, "/editor_")) window = w;
            IM_CHECK(window != nullptr);
            ctx->WindowFocus(window->ID);
            a.editor.SetText(R"deck(---
marp: true
theme: programmer
---

<!-- _class: lead -->
<!-- _paginate: skip -->

# AI 时代 - 手游性能优化
**个人提效**案例与思考

> by xinhou

---

<!-- _class: lead -->
<!-- _paginate: skip -->

# 数据

辅助筛选，自动分析

# 工具

开发，使用工具

# 经验

沉淀为知识库、文本复用

---

<!-- _header: 日常工作 -->

:x: **TAPD、企微**收到性能问题，拉群，抓数据
从企微文档查找并**手动下载**对应性能数据，解压到本地
通过**工具、脚本**分析数据，根据**个人经验**（脑袋里、文档中）得出结论

:white_check_mark: TAPD、企微收到性能问题，拉群，抓数据
将企微文档**转发**至 iMate、Knot 机器人，**自动**寻找下载对应的性能数据
通过 **Skill、MCP、CLI** 分析数据，根据**知识库中的经验**得出结论

> 现实：企微文档机器人权限**限时**。下载企微云盘文件大小有**限制**。机器人文件传输大小有**限制**。
直接拒绝了大文件分析，如 UTrace、GPU Capture 的自动化分析

---

<!-- _header: 数据 -->

**特点**：巨量、分散

- 平台提供 CLI、API 让 Agent、Claw 找到并下载

**目的**：让 AI 看懂数据

- 提供 CLI、API 让 AI Query
- 转换为结构化数据：如 SQLite 数据库

---

<!-- _header: 工具 -->

**分类**：GUI 工具、CLI 工具

让 AI **理解数据**是工具的**基础**

Vibe Coding 人手一套定制化工具 :ok:

自己用的好是从**个人**提效转换为**团队**提效的**前提**
)deck");
            a.editor.SetCursorPosition({0, 0});
            a.editor.SetSelection({0, 0}, {0, 0});
            ctx->Yield(3);
            auto lines = a.editor.GetTextLines();
            for (const char *prefix : {":x:", "- 平台提供 CLI", "自己用的好"}) {
                int line = 0;
                while (line < (int)lines.size() && lines[line].rfind(prefix, 0) != 0) ++line;
                IM_CHECK(line < (int)lines.size());
                int columns = 0;
                for (const unsigned char *p = (const unsigned char *)prefix; *p; ++p)
                    if ((*p & 0xc0) != 0x80) ++columns;
                a.editor.SetCursorPosition({line, columns});
                ctx->Yield(3);
                ctx->ScrollToY(window->ID, (line - 8) * a.editor.GetCaretHeight());
                ctx->Yield(3);
                float scrollY = window->Scroll.y;
                IM_CHECK(scrollY > 0.0f);
                ImVec2 end = a.editor.GetCaretScreenPos();
                a.editor.SetCursorPosition({line, 0});
                ctx->Yield(3);
                ImVec2 start = a.editor.GetCaretScreenPos();
                IM_CHECK(window->InnerClipRect.Contains(start));
                IM_CHECK(window->InnerClipRect.Contains(end));
                ctx->MouseMoveToPos(ImVec2(start.x + 1.0f, start.y + 3.0f));
                ctx->MouseDown();
                ctx->MouseMoveToPos(ImVec2(end.x, end.y + 3.0f));
                ctx->MouseUp();
                ctx->Yield(2);
                IM_CHECK(a.editor.HasSelection());
                IM_CHECK(a.editor.GetSelectedText() == prefix);
                ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_C);
                IM_CHECK(std::string(ImGui::GetClipboardText()) == prefix);
                ImU32 color = TextEditor::GetProgrammerPalette()[(int)TextEditor::PaletteIndex::Selection];
                int vertices = 0;
                for (const auto &vertex : window->DrawList->VtxBuffer)
                    if (vertex.col == color && vertex.pos.y >= start.y &&
                        vertex.pos.y <= start.y + a.editor.GetCaretHeight()) ++vertices;
                IM_CHECK(vertices >= 4);
                IM_CHECK_EQ(window->Scroll.y, scrollY);
                Capture(ctx, a, "scrolled-selection.bmp");
                a.editor.SetSelection({line, 0}, {line, 0});
            }
            a.editor.SetText(before);
            a.editor.SetSelection({0, 0}, {0, 0});
            a.editor.SetCursorPosition({0, 0});
            a.autosave = autosave;
        };
    }
    {
        ImGuiTest *t = IM_REGISTER_TEST(engine, "marp_gui", "editor_navigation_and_tabs");
        t->UserData = app;
        t->TestFunc = [](ImGuiTestContext *ctx) {
            App &a = *(App *)ctx->Test->UserData;
            std::string before = a.editor.GetText();
            bool autosave = a.autosave;
            a.autosave = false;
            ImGuiWindow *window = nullptr;
            for (ImGuiWindow *w : ImGui::GetCurrentContext()->Windows)
                if (strstr(w->Name, "/editor_")) window = w;
            IM_CHECK(window != nullptr);
            ctx->WindowFocus(window->ID);
            auto reset = [&](const std::string& text, TextEditor::Coordinates pos) {
                a.editor.SetText(text);
                a.editor.SetCursorPosition(pos);
                a.editor.SetSelection(pos, pos);
                ctx->Yield(2);
            };
            reset("::", {0, 2});
            ctx->KeyPress(ImGuiKey_LeftArrow);
            IM_CHECK_EQ(a.editor.GetCursorPosition().mColumn, 1);
            IM_CHECK(!ImGui::GetIO().NavActive);
            ctx->KeyPress(ImGuiKey_RightArrow);
            IM_CHECK_EQ(a.editor.GetCursorPosition().mColumn, 2);
            std::string longText;
            for (int i = 0; i < 200; ++i) longText += "line\n";
            reset(longText, {0, 2});
            ctx->Yield(3);
            IM_CHECK(window->ScrollbarY);
            ctx->MouseMoveToPos(ImVec2(window->Pos.x + window->Size.x - window->ScrollbarSizes.x * 0.5f,
                                      window->InnerRect.Max.y - 20.0f));
            ctx->MouseClick();
            IM_CHECK_EQ(a.editor.GetCursorPosition().mLine, 0);
            IM_CHECK_EQ(a.editor.GetCursorPosition().mColumn, 2);
            IM_CHECK(window->Scroll.y > 0.0f);
            IM_CHECK(a.editor.IsInsertSpaces());
            reset("x", {0, 1});
            ctx->KeyPress(ImGuiKey_Tab);
            IM_CHECK(a.editor.GetText() == "x   \n");
            IM_CHECK_EQ(a.editor.GetCursorPosition().mColumn, 4);
            a.editor.Undo();
            IM_CHECK(a.editor.GetText() == "x\n");
            a.editor.Redo();
            IM_CHECK(a.editor.GetText() == "x   \n");
            a.editor.SetInsertSpaces(false);
            reset("x", {0, 1});
            ctx->KeyPress(ImGuiKey_Tab);
            IM_CHECK(a.editor.GetText() == "x\t\n");
            a.editor.SetInsertSpaces(true);
            reset("    x", {0, 5});
            ctx->KeyPress(ImGuiMod_Shift | ImGuiKey_Tab);
            IM_CHECK(a.editor.GetText() == "x\n");
            IM_CHECK_EQ(a.editor.GetCursorPosition().mColumn, 1);
            a.editor.Undo();
            IM_CHECK(a.editor.GetText() == "    x\n");
            reset("a\nb", {1, 1});
            a.editor.SetSelection({0, 0}, {1, 1});
            ctx->KeyPress(ImGuiKey_Tab);
            IM_CHECK(a.editor.GetText() == "    a\n    b\n");
            a.editor.Undo();
            IM_CHECK(a.editor.GetText() == "a\nb\n");
            a.editor.Redo();
            IM_CHECK(a.editor.GetText() == "    a\n    b\n");
            a.editor.SetText(before);
            a.editor.SetSelection({0, 0}, {0, 0});
            a.editor.SetCursorPosition({0, 0});
            a.autosave = autosave;
        };
    }
    {
        ImGuiTest *t = IM_REGISTER_TEST(engine, "marp_gui", "editor_word_delete_and_ime");
        t->UserData = app;
        t->TestFunc = [](ImGuiTestContext *ctx) {
            App &a = *(App *)ctx->Test->UserData;
            std::string before = a.editor.GetText();
            bool autosave = a.autosave;
            a.autosave = false;
            ImGuiWindow *window = nullptr;
            for (ImGuiWindow *w : ImGui::GetCurrentContext()->Windows)
                if (strstr(w->Name, "/editor_")) window = w;
            IM_CHECK(window != nullptr);
            ctx->WindowFocus(window->ID);
            a.editor.SetText("one two");
            a.editor.SetCursorPosition({0, 7});
            a.editor.SetSelection({0, 7}, {0, 7});
            ctx->Yield(2);
#if PLATFORM_WINDOWS
            HWND hwnd = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw);
            HIMC context = ImmGetContext(hwnd);
            IM_CHECK(context != nullptr);
            COMPOSITIONFORM form{};
            CANDIDATEFORM candidate{};
            bool point = ImmGetCompositionWindow(context, &form) && form.dwStyle == CFS_POINT;
            bool candidates = ImmGetCandidateWindow(context, 0, &candidate) && candidate.dwStyle == CFS_EXCLUDE;
            ImmReleaseContext(hwnd, context);
            IM_CHECK(point);
            IM_CHECK(candidates);
#endif
            ctx->KeyPress(ImGuiMod_Shift | ImGuiKey_Backspace);
            IM_CHECK(a.editor.GetText() == "one \n");
            a.editor.Undo();
            IM_CHECK(a.editor.GetText() == "one two\n");
            ctx->KeyPress(ImGuiMod_Ctrl | ImGuiKey_Backspace);
            IM_CHECK(a.editor.GetText() == "one \n");
            a.editor.Undo();
            a.editor.SetImeComposing(true);
            ctx->KeyPress(ImGuiKey_LeftArrow);
            ctx->KeyPress(ImGuiKey_Backspace);
            ctx->KeyPress(ImGuiKey_Tab);
            IM_CHECK(a.editor.GetText() == "one two\n");
            IM_CHECK_EQ(a.editor.GetCursorPosition().mColumn, 7);
            ctx->KeyChars("\xe4\xb8\xad");
            IM_CHECK(a.editor.GetText() == "one two\xe4\xb8\xad\n");
            a.editor.SetImeComposing(false);
            a.editor.SetText("one \xe4\xb8\xad\xe6\x96\x87");
            a.editor.SetCursorPosition({0, 6});
            a.editor.SetSelection({0, 6}, {0, 6});
            ctx->KeyPress(ImGuiMod_Shift | ImGuiKey_Backspace);
            IM_CHECK(a.editor.GetText() == "one \n");
            a.editor.Undo();
            IM_CHECK(a.editor.GetText() == "one \xe4\xb8\xad\xe6\x96\x87\n");
            a.editor.SetText(before);
            a.editor.SetSelection({0, 0}, {0, 0});
            a.editor.SetCursorPosition({0, 0});
            a.autosave = autosave;
        };
    }
    {
        ImGuiTest *t = IM_REGISTER_TEST(engine, "marp_gui", "recover_missing_key_release");
        t->TestFunc = [](ImGuiTestContext *ctx) {
            ImGuiContext *original = ImGui::GetCurrentContext();
            ImGuiContext *isolated = ImGui::CreateContext();
            auto *right = ImGui::GetKeyData(ImGuiKey_RightArrow);
            auto *del = ImGui::GetKeyData(ImGuiKey_Delete);
            right->Down = del->Down = true;
            right->DownDuration = del->DownDuration = 1.0f;
            ImGui::GetKeyData((ImGuiKey)ImGuiMod_Ctrl)->Down = true;
            mg::InputState::ReleaseKeysNotHeld([](int native) { return native == 0x27; });
            ImGui::UpdateInputEvents(false);
            bool heldPreserved = right->Down;
            bool deleteReleased = !del->Down;
            mg::InputState::ReleaseKeysNotHeld([](int) { return false; });
            ImGui::UpdateInputEvents(false);
            bool rightReleased = !right->Down && !ImGui::IsKeyPressed(ImGuiKey_RightArrow);
            bool modifierReleased = !ImGui::GetKeyData((ImGuiKey)ImGuiMod_Ctrl)->Down;
            ImGui::GetIO().AddKeyEvent(ImGuiKey_RightArrow, true);
            ImGui::GetIO().AddKeyEvent(ImGuiKey_RightArrow, false);
            mg::InputState::ReleaseKeysNotHeld([](int) { return false; });
            ImGui::UpdateInputEvents(true);
            bool tapPressed = right->Down;
            ImGui::UpdateInputEvents(true);
            bool tapReleased = !right->Down && isolated->InputEventsQueue.empty();
            ImGui::DestroyContext(isolated);
            ImGui::SetCurrentContext(original);
            IM_CHECK(heldPreserved);
            IM_CHECK(deleteReleased);
            IM_CHECK(rightReleased);
            IM_CHECK(modifierReleased);
            IM_CHECK(tapPressed && tapReleased);
        };
    }
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
    {
        ImGuiTest *t = IM_REGISTER_TEST(engine, "marp_gui", "recent_files_menu_and_palette");
        t->UserData = app;
        t->SetVarsDataType<DeckSwitchVars>();
        t->GuiFunc = [](ImGuiTestContext *ctx) {
            auto &vars = ctx->GetVars<DeckSwitchVars>();
            if (!vars.pendingPath.empty()) {
                OpenDeck(*(App *)ctx->Test->UserData, vars.pendingPath);
                vars.pendingPath.clear();
            }
        };
        t->TestFunc = [](ImGuiTestContext *ctx) {
            App &a = *(App *)ctx->Test->UserData;
            auto &vars = ctx->GetVars<DeckSwitchVars>();
            fs::path original = a.deckPath;
            RecentFiles originalRecent = a.recentFiles;
            bool autosave = a.autosave;
            a.autosave = false;
            a.recentFiles.Clear();
            for (int i = 0; i < 6; ++i) {
                fs::path path = a.runtimeDir / ("recent-" + std::to_string(i) + ".md");
                std::ofstream(path) << "---\nmarp: true\n---\n# Recent " << i << '\n';
                a.recentFiles.Remember(path);
            }
            IM_CHECK_EQ(a.recentFiles.Paths().size(), (size_t)5);
            ctx->MenuClick("//##MainMenuBar/File/Open Recent/recent-2.md###recent-file-3");
            ctx->Yield(3);
            IM_CHECK(a.deckPath.filename() == fs::path("recent-2.md"));
            IM_CHECK(a.editor.GetText().find("# Recent 2") != std::string::npos);
            IM_CHECK(!a.editor.HasSelection());
            IM_CHECK(a.editor.GetCursorPosition() == TextEditor::Coordinates(0, 0));
            IM_CHECK(a.recentFiles.Paths().front() == a.deckPath);
            ctx->MenuClick("//##MainMenuBar/File/Open Recent/Clear Recent");
            IM_CHECK(a.recentFiles.Paths().empty());
            for (int i = 0; i < 5; ++i)
                a.recentFiles.Remember(a.runtimeDir / ("recent-" + std::to_string(i) + ".md"));

            ImGuiIO &io = ImGui::GetIO();
            float delay = io.KeyRepeatDelay, rate = io.KeyRepeatRate;
            io.KeyRepeatDelay = io.KeyRepeatRate = 0.15f;
            for (ImGuiKey key : {ImGuiKey_DownArrow, ImGuiKey_UpArrow}) {
                auto paths = a.recentFiles.Paths();
                a.showPalette = true;
                ctx->Yield(3);
                ctx->SetRef("Command Palette");
                ctx->ItemClick("##palette_filter");
                ctx->KeyChars("Open Recent:");
                ctx->KeyDown(key);
                ctx->SleepNoSkip(0.28f, 0.01f);
                ctx->KeyUp(key);
                ctx->KeyPress(ImGuiKey_Enter);
                ctx->Yield(3);
                IM_CHECK(!a.showPalette);
                IM_CHECK(a.deckPath != paths[key == ImGuiKey_DownArrow ? 1 : 4]);
                IM_CHECK(a.recentFiles.Paths().front() == a.deckPath);
                IM_CHECK(a.editor.GetText().find("# Recent ") != std::string::npos);
                IM_CHECK(!a.editor.HasSelection());
                IM_CHECK(a.editor.GetCursorPosition() == TextEditor::Coordinates(0, 0));
            }
            io.KeyRepeatDelay = delay;
            io.KeyRepeatRate = rate;
            a.showPalette = true;
            ctx->Yield(3);
            ctx->ItemClick("##palette_filter");
            ctx->KeyChars("Clear Recent");
            ctx->KeyPress(ImGuiKey_Enter);
            IM_CHECK(a.recentFiles.Paths().empty());
            fs::path activePath = a.deckPath;
            std::string activeText = a.editor.GetText();
            IM_CHECK(!OpenDeck(a, a.runtimeDir / "missing.md"));
            IM_CHECK(a.deckPath == activePath);
            IM_CHECK(a.editor.GetText() == activeText);
            IM_CHECK(a.recentFiles.Paths().empty());
            a.recentFiles.Remember(a.deckPath);
            a.showSettings = true;
            ctx->Yield(3);
            ctx->SetRef("Settings##modal");
            ctx->ItemUncheck("Save recent file list");
            IM_CHECK(!a.recentFiles.IsEnabled());
            IM_CHECK(a.recentFiles.Paths().empty());
            ctx->ItemClick("Close");
            vars.pendingPath = a.runtimeDir / "recent-0.md";
            ctx->Yield(3);
            IM_CHECK(a.deckPath == a.runtimeDir / "recent-0.md");
            IM_CHECK(a.recentFiles.Paths().empty());
            RecentFiles disabled;
            disabled.Load(a.runtimeDir / "recent-files.txt");
            IM_CHECK(!disabled.IsEnabled());
            IM_CHECK(disabled.Paths().empty());
            a.showSettings = true;
            ctx->Yield(3);
            ctx->ItemCheck("Save recent file list");
            IM_CHECK(a.recentFiles.IsEnabled());
            ctx->ItemClick("Close");
            vars.pendingPath = original;
            ctx->Yield(3);
            IM_CHECK(a.recentFiles.Paths().front() == original);
            a.recentFiles = originalRecent;
            a.recentFiles.Remember(original);
            a.autosave = autosave;
        };
    }
}
