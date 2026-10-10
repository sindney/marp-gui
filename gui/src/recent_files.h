#pragma once

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <string>
#include <vector>

class RecentFiles {
public:
    const std::vector<std::filesystem::path> &Paths() const { return paths; }
    bool IsEnabled() const { return enabled; }

    void SetEnabled(bool value) {
        enabled = value;
        if (!enabled) paths.clear();
        Save();
    }

    void Load(const std::filesystem::path &file) {
        storage = file;
        paths.clear();
        enabled = true;
        std::ifstream input(storage, std::ios::binary);
        std::string firstLine;
        if (std::getline(input, firstLine) && firstLine == "disabled") {
            enabled = false;
            return;
        }
        input.clear();
        input.seekg(0);
        std::string value;
        while (paths.size() < 5 && input >> std::quoted(value)) {
            if (value.empty()) continue;
            auto path = Normalize(std::filesystem::u8path(value));
            if (std::none_of(paths.begin(), paths.end(), [&](const auto &p) { return SamePath(p, path); }))
                paths.push_back(path);
        }
    }

    void Remember(const std::filesystem::path &file) {
        if (!enabled) return;
        auto path = Normalize(file);
        paths.erase(std::remove_if(paths.begin(), paths.end(),
                                  [&](const auto &p) { return SamePath(p, path); }), paths.end());
        paths.insert(paths.begin(), path);
        if (paths.size() > 5) paths.resize(5);
        Save();
    }

    void Clear() {
        paths.clear();
        Save();
    }

private:
    std::filesystem::path storage;
    std::vector<std::filesystem::path> paths;
    bool enabled = true;

    static std::filesystem::path Normalize(const std::filesystem::path &path) {
        std::error_code ec;
        auto normalized = std::filesystem::weakly_canonical(path, ec);
        return ec ? path.lexically_normal() : normalized;
    }

    static bool SamePath(const std::filesystem::path &a, const std::filesystem::path &b) {
        std::error_code ec;
        return a == b || std::filesystem::equivalent(a, b, ec);
    }

    void Save() const {
        if (storage.empty()) return;
        std::ofstream output(storage, std::ios::binary | std::ios::trunc);
        if (!enabled) {
            output << "disabled\n";
            return;
        }
        for (const auto &path : paths) output << std::quoted(path.u8string()) << '\n';
    }
};
