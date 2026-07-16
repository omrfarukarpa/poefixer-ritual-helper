#pragma once

#include "RitualScanner.h"
#include "RitualUi.h"
#include "sdk/PluginSDK.h"

#include <optional>
#include <string>
#include <vector>

namespace RitualHelper {

inline bool IsHiddenItem(const RitualItem& it) {
    return it.name == "Hidden Item";
}

inline std::vector<RitualItem> MatchDeferItems(const RitualWindow& win,
                                               const std::vector<std::string>& rules) {
    std::vector<RitualItem> out;
    if (rules.empty()) return out;
    for (const auto& it : win.items) {
        if (IsHiddenItem(it)) continue;
        for (const auto& r : rules) {
            if (r.empty()) continue;
            if (ContainsCI(it.name, r.c_str())) {
                out.push_back(it);
                break;
            }
        }
    }
    return out;
}

enum class BottomButtonMode { None, OfferTribute, DeferItem };

struct BottomButton {
    BottomButtonMode mode = BottomButtonMode::None;
    float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
};

inline BottomButton FindBottomButton(const std::vector<UiElement>& all,
                                     const RitualWindow& win) {
    BottomButton b;
    const float gridW = static_cast<float>(win.totalBoxesX) * win.cellSize;
    const float gridH = static_cast<float>(win.totalBoxesY) * win.cellSize;
    for (const auto& e : all) {
        if (e.text.empty()) continue;
        const float cx = e.x + e.w * 0.5f;
        if (cx < win.gridX || cx > win.gridX + gridW) continue;
        if (e.y < win.gridY + gridH - 20.f || e.y > win.gridY + gridH + 220.f) continue;
        if (ContainsCI(e.text, "defer item")) {
            b.mode = BottomButtonMode::DeferItem;
        } else if (ContainsCI(e.text, "offer tribute")) {
            b.mode = BottomButtonMode::OfferTribute;
        } else {
            continue;
        }
        b.x = e.x; b.y = e.y; b.w = e.w; b.h = e.h;
        return b;
    }
    return b;
}

inline std::optional<UiElement> FindDeferToggle(const std::vector<UiElement>& all,
                                                const RitualWindow& win) {
    const float gridW = static_cast<float>(win.totalBoxesX) * win.cellSize;
    const float midX = win.gridX + gridW * 0.5f;
    for (const auto& e : all) {
        if (!e.text.empty() || e.childCount != 0) continue;
        if (e.w < 80.f || e.w > 130.f) continue;
        const float dwh = e.w - e.h;
        if (dwh > 6.f || dwh < -6.f) continue;
        const float cx = e.x + e.w * 0.5f;
        const float cy = e.y + e.h * 0.5f;
        if (cx <= midX || cx > win.gridX + gridW + 60.f) continue;
        if (cy < win.gridY - 220.f || cy > win.gridY - 20.f) continue;
        return e;
    }
    return std::nullopt;
}

}
