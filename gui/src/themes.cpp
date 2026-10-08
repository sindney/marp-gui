// ImGui theme gallery for marp_gui.
//
// Themes from the Dear ImGui community themes gallery — the styles shared
// by TheAncientOwl in the "Themes" issue thread:
//     https://github.com/ocornut/imgui/issues/707

#include "themes.h"

#include "imgui.h"

namespace themes {

namespace {
// The 13 theme bodies are kept verbatim in themes_inline.cpp.inc.
#include "themes_inline.cpp.inc"
} // namespace

const ThemeEntry kThemes[] = {
    {"Programmer",       &SetupImGuiProgrammerStyle},
    {"Forest Green",     &SetupForestGreenStyle},
    {"Amethyst",         &SetupImGuiAmethystStyle},
    {"Sapphire",         &SetupImGuiSapphireStyle},
    {"Amber Yellow",     &SetupImGuiAmberYellowStyle},
    {"Dracula",          &SetupImGuiDraculaStyle},
    {"Catppuccin Mocha", &SetupImGuiCatppuccinMochaStyle},
    {"Gruvbox Hard",     &SetupImGuiGruvboxHardStyle},
    {"Crimson Vesuvius", &SetupImGuiCrimsonVesuviusStyle},
    {"Rose Quartz",      &SetupImGuiRoseQuartzStyle},
    {"Cyberpunk",        &SetupImGuiCyberpunkStyle},
    {"Paper And Ink",    &SetupImGuiPaperAndInkStyle},
    {"Dark",             &SetupImGuiDarkStyle},
};

const std::size_t kThemesCount = sizeof(kThemes) / sizeof(kThemes[0]);

static int s_CurrentThemeIndex = 0; // Programmer

void ApplyTheme(int idx) {
    if (idx < 0 || idx >= (int)kThemesCount) idx = 0;
    s_CurrentThemeIndex = idx;
    kThemes[idx].apply();
}

int GetCurrentThemeIndex() { return s_CurrentThemeIndex; }

} // namespace themes
