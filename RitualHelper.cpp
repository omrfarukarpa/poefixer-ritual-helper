#include "sdk/PluginSDK.h"

#include "config/Settings.h"
#include "game/RitualScanner.h"
#include "game/RitualUi.h"

#include <imgui.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

inline constexpr const char* kRitualHelperVersion    = "0.1.1";
inline constexpr const char* kRitualHelperMaintainer = "Omer Faruk ARPA";

using RitualHelperConfig::Settings;
using Clock = std::chrono::steady_clock;

class RitualHelperPlugin : public PluginSDK::Plugin {
public:
    const char* GetName() const override { return "Ritual Helper"; }

    bool WantsOverlay() const override {
        return m_settings.enabled && m_settings.showOverlay;
    }

    void OnEnable(bool) override {
        if (!HostCompatible()) {
            ctx()->Log.Error(
                "Ritual Helper: incompatible PoeFixer host (SDK version/size mismatch) - disabled");
            return;
        }
        if (ctx()->ImGuiContext)
            ImGui::SetCurrentContext(static_cast<ImGuiContext*>(ctx()->ImGuiContext));

        m_settings.Load(DirectoryPath());
        m_loaded = true;
        m_lastScan = Clock::now() - std::chrono::milliseconds(m_settings.scanIntervalMs);
        ctx()->Log.Info("Ritual Helper plugin enabled");
    }

    void OnDisable() override {
        m_window.reset();
        m_uiHits.clear();
        SaveSettings();
        ctx()->Log.Info("Ritual Helper plugin disabled");
    }

    void SaveSettings() override {
        if (m_loaded) m_settings.Save(DirectoryPath());
    }

    void DrawUI() override {
        if (!m_settings.enabled || !m_settings.showOverlay) return;
        if (!ctx()->Game.IsInGame()) return;
        if (ctx()->ImGuiContext)
            ImGui::SetCurrentContext(static_cast<ImGuiContext*>(ctx()->ImGuiContext));
        if (!ctx()->Game.IsForeground()) return;

        const ImVec2 disp = ImGui::GetIO().DisplaySize;
        RefreshIfNeeded(disp.x, disp.y);
        if (!m_window) return;

        ImDrawList* dl = ImGui::GetForegroundDrawList();

        if (m_settings.highlightItems) {
            for (const auto& it : m_window->items) {
                const ImVec2 a(it.rect.x + 1.f, it.rect.y + 1.f);
                const ImVec2 b(it.rect.x + it.rect.w - 1.f, it.rect.y + it.rect.h - 1.f);
                dl->AddRect(a, b, IM_COL32(80, 220, 255, 230), 0.f, 0, 2.f);
            }
        }

        char status[192];
        std::snprintf(status, sizeof(status),
                      "Ritual Helper: window='%s' items=%zu | UI hits=%zu",
                      m_window->name.c_str(), m_window->items.size(), m_uiHits.size());
        const ImVec2 pos(14.f, 58.f);
        const ImVec2 sz = ImGui::CalcTextSize(status);
        dl->AddRectFilled(ImVec2(pos.x - 4, pos.y - 2),
                          ImVec2(pos.x + sz.x + 4, pos.y + sz.y + 2),
                          IM_COL32(0, 0, 0, 200), 3.f);
        dl->AddText(pos, IM_COL32(120, 230, 255, 255), status);
    }

    void DrawSettings() override {
        if (!ctx()->ImGuiContext) return;
        ImGui::SetCurrentContext(static_cast<ImGuiContext*>(ctx()->ImGuiContext));

        ImGui::TextDisabled("Ritual Helper v%s  -  by %s",
                            kRitualHelperVersion, kRitualHelperMaintainer);
        ImGui::Checkbox("Enable Ritual Helper", &m_settings.enabled);
        ImGui::SameLine();
        ImGui::Checkbox("Show overlay", &m_settings.showOverlay);

        ImGui::TextWrapped(
            "Phase 0 (discovery): detects the Ritual 'Favours' window, outlines its "
            "items and finds the defer/reroll UI elements. Open a ritual Favours "
            "window in-game and use the dump below; defer/reroll automation comes "
            "next, built on what the dump proves.");

        ImGui::Checkbox("Outline ritual items", &m_settings.highlightItems);
        ImGui::SliderInt("Scan interval (ms)", &m_settings.scanIntervalMs,
                         RitualHelperConfig::kScanIntervalMinMs,
                         RitualHelperConfig::kScanIntervalMaxMs);

        ImGui::SeparatorText("Status");
        if (m_window) {
            ImGui::Text("Window: '%s' (id=%d, %dx%d), items=%zu",
                        m_window->name.c_str(), m_window->inventoryId,
                        m_window->totalBoxesX, m_window->totalBoxesY,
                        m_window->items.size());
        } else {
            ImGui::TextDisabled("No ritual window detected (open the Favours window).");
        }
        ImGui::Text("Ritual-keyword UI elements: %zu", m_uiHits.size());
        int shown = 0;
        for (const auto& e : m_uiHits) {
            if (shown++ >= 10) { ImGui::TextDisabled("..."); break; }
            ImGui::TextDisabled("  id='%s' text='%s'", e.stringId.c_str(), e.text.c_str());
        }

        ImGui::SeparatorText("Debug");
        ImGui::Checkbox("Debug mode", &m_settings.debugMode);
        if (m_settings.debugMode) {
            ImGui::TextWrapped(
                "With the Favours window open, click the button: it writes "
                "debug/ritual-dump.txt with every on-screen inventory (names prove "
                "how the host exposes the ritual window) and every ritual-keyword "
                "UI element (how to find the defer/reroll buttons).");
            if (ImGui::Button("Write ritual dump")) WriteDump();
            if (!m_lastDumpPath.empty())
                ImGui::TextDisabled("Last dump: %s", m_lastDumpPath.c_str());
        }
    }

private:
    Settings m_settings;
    bool m_loaded = false;

    std::optional<RitualHelper::RitualWindow> m_window;
    std::vector<RitualHelper::UiElement> m_uiHits;
    Clock::time_point m_lastScan{};
    std::string m_lastDumpPath;

    void RefreshIfNeeded(float w, float h) {
        const auto now = Clock::now();
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastScan).count();
        if (elapsed < m_settings.scanIntervalMs) return;
        m_lastScan = now;

        m_window = RitualHelper::FindRitualWindow(ctx(), w, h);
        if (m_window) {
            const auto all = RitualHelper::CollectUiTexts(ctx());
            m_uiHits = RitualHelper::FindRitualUiElements(all);
        } else {
            m_uiHits.clear();
        }
    }

    void WriteDump() {
        const ImVec2 disp = ImGui::GetIO().DisplaySize;

        std::string s = "[Ritual Helper] discovery dump\n\n";
        s += RitualHelper::BuildInventoryDump(ctx(), disp.x, disp.y);
        s += "\n";
        const auto all = RitualHelper::CollectUiTexts(ctx(), true);
        const auto win = RitualHelper::FindRitualWindow(ctx(), disp.x, disp.y);
        if (win && win->cellSize > 0.f) {
            const float headerPad = 280.f;
            s += RitualHelper::BuildRegionDump(
                all, win->gridX, win->gridY - headerPad,
                static_cast<float>(win->totalBoxesX) * win->cellSize,
                static_cast<float>(win->totalBoxesY) * win->cellSize + headerPad);
            s += "\n";
        }
        s += RitualHelper::BuildUiDump(all);

        try {
            const auto dir = DirectoryPath() / "debug";
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            const auto path = dir / "ritual-dump.txt";
            std::ofstream out(path);
            if (out.is_open()) out << s;
            m_lastDumpPath = path.string();
            ctx()->Log.Info(("[Ritual Helper] dump written to " + m_lastDumpPath).c_str());
        } catch (...) {}
    }
};

extern "C" PLUGIN_API PluginSDK::Plugin* CreatePlugin() { return new RitualHelperPlugin(); }

extern "C" PLUGIN_API void DestroyPlugin(PluginSDK::Plugin* p) { delete p; }
