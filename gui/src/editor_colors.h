#pragma once

#include "TextEditor.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace editor_colors {

struct Scheme {
    const char *id;
    const char *name;
    const TextEditor::Palette &(*palette)();
};

inline const Scheme kSchemes[] = {
    {"programmer", "Programmer", &TextEditor::GetProgrammerPalette},
    {"dark", "Dark", &TextEditor::GetDarkPalette},
    {"light", "Light", &TextEditor::GetLightPalette},
    {"retro-blue", "Retro Blue", &TextEditor::GetRetroBluePalette},
};
inline constexpr int kSchemeCount = sizeof(kSchemes) / sizeof(kSchemes[0]);

inline int Load(const std::filesystem::path &file) {
    std::ifstream input(file);
    std::string id;
    input >> id;
    for (int i = 0; i < kSchemeCount; ++i)
        if (id == kSchemes[i].id) return i;
    return 0;
}

inline void Save(const std::filesystem::path &file, int index) {
    if (index < 0 || index >= kSchemeCount) index = 0;
    std::ofstream output(file, std::ios::trunc);
    output << kSchemes[index].id << '\n';
}

} // namespace editor_colors
