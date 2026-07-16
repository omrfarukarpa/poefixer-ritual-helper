#pragma once

#include "sdk/PluginSDK.h"

#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace RitualHelper {

struct ScreenRect {
    float x = 0.f;
    float y = 0.f;
    float w = 0.f;
    float h = 0.f;
};

inline bool IsPlayerSlotName(const char* name) {
    if (!name) return true;
    static const char* kSlotPrefixes[] = {
        "MainInventory", "BodyArmour", "Weapon", "Offhand", "Helm", "Amulet",
        "Ring", "Gloves", "Boots", "Belt", "Flask", "Cursor", "Trinket",
        "Charm"};
    for (const char* p : kSlotPrefixes) {
        size_t n = 0;
        while (p[n] != '\0') ++n;
        if (std::strncmp(name, p, n) == 0) return true;
    }
    return false;
}

inline bool GridOnScreen(const PluginSDK::Inventory& inv, float displayW, float displayH) {
    if (!inv.Grid.Valid || inv.Grid.CellSize <= 0.f) return false;
    if (displayW <= 0.f || displayH <= 0.f) return false;
    const float x = inv.Grid.GridScreenX;
    const float y = inv.Grid.GridScreenY;
    const float margin = 4.f;
    return x >= -margin && y >= -margin && x < displayW && y < displayH;
}

inline bool ItemOnScreen(const PluginSDK::InventoryItem& item, float displayW, float displayH) {
    if (!item.ScreenValid) return false;
    if (item.ScreenW <= 0.f || item.ScreenH <= 0.f) return false;
    if (displayW <= 0.f || displayH <= 0.f) return false;
    const float cx = item.ScreenX + item.ScreenW * 0.5f;
    const float cy = item.ScreenY + item.ScreenH * 0.5f;
    return cx >= 0.f && cy >= 0.f && cx < displayW && cy < displayH;
}

inline bool TabOnScreen(const PluginSDK::Inventory& inv, float displayW, float displayH) {
    if (GridOnScreen(inv, displayW, displayH)) return true;
    for (const auto& it : inv.Items)
        if (ItemOnScreen(it, displayW, displayH)) return true;
    return false;
}

inline std::optional<ScreenRect> ResolveItemRect(const PluginSDK::Inventory& inv,
                                                 const PluginSDK::InventoryItem& item,
                                                 float displayW, float displayH) {
    if (ItemOnScreen(item, displayW, displayH)) {
        return ScreenRect{item.ScreenX, item.ScreenY, item.ScreenW, item.ScreenH};
    }
    if (inv.Grid.Valid && GridOnScreen(inv, displayW, displayH) && inv.Grid.CellSize > 0.f) {
        const float cell = inv.Grid.CellSize;
        return ScreenRect{
            inv.Grid.GridScreenX + static_cast<float>(item.SlotX) * cell,
            inv.Grid.GridScreenY + static_cast<float>(item.SlotY) * cell,
            static_cast<float>(item.Width) * cell,
            static_cast<float>(item.Height) * cell};
    }
    return std::nullopt;
}

struct RitualItem {
    std::string name;
    int stack = 0;
    ScreenRect rect;
};

struct RitualWindow {
    int inventoryId = 0;
    std::string name;
    int totalBoxesX = 0;
    int totalBoxesY = 0;
    std::vector<RitualItem> items;
};

inline std::optional<RitualWindow> FindRitualWindow(const PluginSDK::Context* ctx,
                                                    float displayW, float displayH) {
    if (!ctx) return std::nullopt;
    const auto all = ctx->Inventory.GetAll();
    for (const auto& inv : all) {
        const char* name = ctx->Inventory.GetName(inv.InventoryId);
        if (!name || std::strncmp(name, "Ritual", 6) != 0) continue;
        if (!TabOnScreen(inv, displayW, displayH)) continue;

        RitualWindow w;
        w.inventoryId = inv.InventoryId;
        w.name = name;
        w.totalBoxesX = inv.TotalBoxesX;
        w.totalBoxesY = inv.TotalBoxesY;
        for (const auto& item : inv.Items) {
            auto r = ResolveItemRect(inv, item, displayW, displayH);
            if (!r) continue;
            RitualItem ri;
            ri.name = item.UniqueName.empty() ? item.BaseTypeName : item.UniqueName;
            ri.stack = item.StackCount;
            ri.rect = *r;
            w.items.push_back(std::move(ri));
        }
        return w;
    }
    return std::nullopt;
}

inline std::string BuildInventoryDump(const PluginSDK::Context* ctx,
                                      float displayW, float displayH) {
    std::string s = "=== on-screen inventories (non-player) ===\n";
    if (!ctx) return s;
    const auto all = ctx->Inventory.GetAll();
    char line[256];
    int shown = 0;
    for (const auto& inv : all) {
        const char* name = ctx->Inventory.GetName(inv.InventoryId);
        const bool player = IsPlayerSlotName(name);
        if (!TabOnScreen(inv, displayW, displayH)) continue;
        std::snprintf(line, sizeof(line),
                      "id=%d name='%s' boxes=%dx%d items=%zu gridValid=%d cell=%.1f%s\n",
                      inv.InventoryId, name ? name : "(null)",
                      inv.TotalBoxesX, inv.TotalBoxesY, inv.Items.size(),
                      inv.Grid.Valid ? 1 : 0, inv.Grid.CellSize,
                      player ? " [player-slot]" : "");
        s += line;
        if (player) continue;
        int n = 0;
        for (const auto& item : inv.Items) {
            if (n++ >= 24) { s += "  ...\n"; break; }
            const auto r = ResolveItemRect(inv, item, displayW, displayH);
            std::snprintf(line, sizeof(line),
                          "  slot=%d,%d stack=%d base='%s' unique='%s' rect=%s\n",
                          item.SlotX, item.SlotY, item.StackCount,
                          item.BaseTypeName.c_str(), item.UniqueName.c_str(),
                          r ? "ok" : "none");
            s += line;
        }
        if (++shown >= 8) { s += "(more inventories truncated)\n"; break; }
    }
    if (shown == 0) s += "(none)\n";
    return s;
}

}
