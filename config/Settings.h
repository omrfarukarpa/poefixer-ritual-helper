#pragma once

#include "../third_party/json.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace RitualHelperConfig {

inline constexpr int kScanIntervalMinMs = 150;
inline constexpr int kScanIntervalMaxMs = 2000;

inline constexpr int kMinValueMax = 10000000;
inline constexpr int kValueUnitExalted = 0;
inline constexpr int kValueUnitDivine = 1;
inline constexpr int kRefreshMinMinutes = 15;
inline constexpr int kRefreshMaxMinutes = 60;

struct Settings {
    bool enabled = true;
    bool showOverlay = true;
    bool debugMode = false;
    int  scanIntervalMs = 400;

    bool dryRun = false;
    std::string league;
    std::vector<std::string> selectedItems;
    int  minValue = 0;
    int  minValueUnit = kValueUnitExalted;
    int  priceRefreshMinutes = 30;

    std::filesystem::path SettingsPath(const std::filesystem::path& dir) const {
        return dir / "config" / "settings.json";
    }

    static void LoadStringVec(const nlohmann::json& j, const char* key,
                              std::vector<std::string>& out) {
        if (!j.contains(key) || !j[key].is_array()) return;
        out.clear();
        for (const auto& e : j[key]) {
            if (!e.is_string()) continue;
            std::string s = e.get<std::string>();
            if (s.empty() || s.size() > 96) continue;
            out.push_back(std::move(s));
        }
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
            debugMode = j.value("debug_mode", debugMode);
            scanIntervalMs = std::clamp(j.value("scan_interval_ms", scanIntervalMs),
                                        kScanIntervalMinMs, kScanIntervalMaxMs);
            dryRun = j.value("dry_run", dryRun);
            league = j.value("league", league);
            if (league.size() > 96) league.clear();
            minValue = std::clamp(j.value("min_value", j.value("min_value_exalted", 0)),
                                  0, kMinValueMax);
            minValueUnit = std::clamp(j.value("min_value_unit", minValueUnit),
                                      kValueUnitExalted, kValueUnitDivine);
            priceRefreshMinutes = std::clamp(j.value("price_refresh_minutes", priceRefreshMinutes),
                                             kRefreshMinMinutes, kRefreshMaxMinutes);
            LoadStringVec(j, "selected_items", selectedItems);
        } catch (...) {}
    }

    void Save(const std::filesystem::path& dir) const {
        try {
            std::error_code ec;
            std::filesystem::create_directories(dir / "config", ec);
            nlohmann::json j;
            j["enabled"] = enabled;
            j["show_overlay"] = showOverlay;
            j["debug_mode"] = debugMode;
            j["scan_interval_ms"] = scanIntervalMs;
            j["dry_run"] = dryRun;
            j["league"] = league;
            j["selected_items"] = selectedItems;
            j["min_value"] = minValue;
            j["min_value_unit"] = minValueUnit;
            j["price_refresh_minutes"] = priceRefreshMinutes;
            const std::string text = j.dump(2);
            std::ofstream out(SettingsPath(dir));
            if (out.is_open()) out << text;
        } catch (...) {}
    }
};

}
