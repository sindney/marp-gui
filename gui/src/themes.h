// ImGui theme gallery for marp_gui.
//
// Themes from the Dear ImGui community themes gallery — the styles shared
// by TheAncientOwl in the "Themes" issue thread:
//     https://github.com/ocornut/imgui/issues/707
//
// Default theme is "Programmer" to match themes/programmer.css.

#pragma once

#include <cstddef>

namespace themes {

struct ThemeEntry {
    const char *display_name;
    void (*apply)();
};

extern const ThemeEntry kThemes[];
extern const std::size_t kThemesCount;

// Default: Programmer (index 0).
void ApplyTheme(int idx);
int GetCurrentThemeIndex();

} // namespace themes
