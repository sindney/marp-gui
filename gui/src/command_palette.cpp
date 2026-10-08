// gui/src/command_palette.cpp — see command_palette.h.

#include "command_palette.h"
#include "app.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "imgui.h"

namespace palette {

namespace {

struct Entry {
    const char *display;
    Command cmd;
};

char filterBuf[256] = "";
int selectedIndex = 0;
std::string lastFilter;
bool scrollToSelection = false;

bool icontains(const std::string &hay, const std::string &needle) {
    if (needle.empty()) return true;
    auto it = std::search(hay.begin(), hay.end(), needle.begin(), needle.end(),
                          [](char a, char b) {
                              return std::tolower((unsigned char)a) ==
                                     std::tolower((unsigned char)b);
                          });
    return it != hay.end();
}

} // namespace

Command Render(bool &openFlag, App &app) {
    Command result = Command::None;
    if (!openFlag) return result;

    ImGuiViewport *vp = ImGui::GetMainViewport();
    ImVec2 size(560.0f, 380.0f);
    ImVec2 pos(vp->WorkPos.x + vp->WorkSize.x * 0.5f,
               vp->WorkPos.y + vp->WorkSize.y * 0.15f);
    ImGui::SetNextWindowSize(size, ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(pos, ImGuiCond_Appearing, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowFocus();

    if (!ImGui::Begin("Command Palette", &openFlag,
                      ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return result;
    }

    if (ImGui::IsWindowAppearing()) {
        filterBuf[0] = '\0';
        lastFilter.clear();
        selectedIndex = 0;
        ImGui::SetKeyboardFocusHere();
    }
    ImGui::InputTextWithHint("##palette_filter", "Type to filter commands...",
                             filterBuf, sizeof(filterBuf));

    // Command list (enabled state baked into the label for context).
    std::vector<Entry> all = {
        {"Open...",            Command::Open},
        {"Save",               Command::Save},
        {"Save As...",         Command::SaveAs},
        {"Export to PDF",      Command::ExportPdf},
        {"Export to PPTX",     Command::ExportPptx},
        {"Export to HTML",     Command::ExportHtml},
        {app.editor.CanUndo() ? "Undo" : "Undo (nothing to undo)", Command::Undo},
        {app.editor.CanRedo() ? "Redo" : "Redo (nothing to redo)", Command::Redo},
        {"Next Slide",         Command::NextSlide},
        {"Previous Slide",     Command::PrevSlide},
        {"Settings...",        Command::Settings},
        {"About",              Command::About},
        {"Exit",               Command::Exit},
    };

    const std::string filterStr(filterBuf);
    std::vector<const Entry *> filtered;
    for (auto &e : all)
        if (icontains(e.display, filterStr)) filtered.push_back(&e);

    if (filterStr != lastFilter) { selectedIndex = 0; lastFilter = filterStr; }
    if (!filtered.empty() && selectedIndex >= (int)filtered.size())
        selectedIndex = (int)filtered.size() - 1;
    if (selectedIndex < 0) selectedIndex = 0;

    if (!filtered.empty()) {
        bool moved = false;
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false)) {
            selectedIndex = (selectedIndex + 1) % (int)filtered.size();
            moved = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false)) {
            selectedIndex = (selectedIndex - 1 + (int)filtered.size()) % (int)filtered.size();
            moved = true;
        }
        if (moved) scrollToSelection = true; // follow keyboard nav
        if (ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
            result = filtered[selectedIndex]->cmd;
            openFlag = false;
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        openFlag = false;

    ImGui::Separator();
    ImGui::BeginChild("palette_list", ImVec2(0, 0), ImGuiChildFlags_Borders);
    if (filtered.empty()) {
        ImGui::TextDisabled("No matching commands");
    } else {
        for (int i = 0; i < (int)filtered.size(); ++i) {
            bool sel = (i == selectedIndex);
            if (ImGui::Selectable(filtered[i]->display, sel)) {
                result = filtered[i]->cmd;
                openFlag = false;
            }
            if (sel) {
                ImGui::SetItemDefaultFocus();
                // follow keyboard navigation so the row scrolls into view
                if (scrollToSelection) {
                    ImGui::SetScrollHereY(0.5f);
                    scrollToSelection = false;
                }
            }
        }
    }
    ImGui::EndChild();

    ImGui::End();
    return result;
}

} // namespace palette
