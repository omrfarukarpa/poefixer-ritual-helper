#pragma once

#include "../third_party/json.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

namespace RitualHelperConfig {

inline constexpr int kScanIntervalMinMs = 150;
inline constexpr int kScanIntervalMaxMs = 2000;

struct Settings {
    bool enabled = true;
    bool showOverlay = true;
    bool highlightItems = true;
    bool debugMode = false;
    int  scanIntervalMs = 400;

    std::filesystem::path SettingsPath(const std::filesystem::path& dir) const {
        return dir / "config" / "settings.json";
    }

    void Load(const std::filesystem::path& dir) {
        try {
            const auto path = SettingsPath(dir);
            if (!std::filesystem::exists(path)) return;
            std::ifstream in(path);
            if (!in.is_open()) return;
            nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
            if (j.is_discarded() || !j.is_object()) return;

            enabled = j.value("enabled", enabled);
            showOverlay = j.value("show_overlay", showOverlay);
            highlightItems = j.value("highlight_items", highlightItems);
            debugMode = j.value("debug_mode", debugMode);
            scanIntervalMs = std::clamp(j.value("scan_interval_ms", scanIntervalMs),
                                        kScanIntervalMinMs, kScanIntervalMaxMs);
        } catch (...) {}
    }

    void Save(const std::filesystem::path& dir) const {
        try {
            std::error_code ec;
            std::filesystem::create_directories(dir / "config", ec);
            nlohmann::json j;
            j["enabled"] = enabled;
            j["show_overlay"] = showOverlay;
            j["highlight_items"] = highlightItems;
            j["debug_mode"] = debugMode;
            j["scan_interval_ms"] = scanIntervalMs;
            const std::string text = j.dump(2);
            std::ofstream out(SettingsPath(dir));
            if (out.is_open()) out << text;
        } catch (...) {}
    }
};

}
