#pragma once

#include <Windows.h>
#include <imgui.h>

namespace RitualHelperOverlay {

inline constexpr float kButtonW = 110.f;
inline constexpr float kButtonH = 26.f;

struct ButtonResult {
    bool clicked = false;
    bool hovered = false;
    ImVec2 btnP0{};
    ImVec2 btnP1{};
};

inline bool HitRect(const ImVec2& p, ImVec2 p0, ImVec2 p1) {
    return p.x >= p0.x && p.x < p1.x && p.y >= p0.y && p.y < p1.y;
}

struct HardwareClick {
    bool wasDown = false;
    bool pressedOnRect = false;

    bool Update(bool rectActive, ImVec2 p0, ImVec2 p1) {
        const bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

        POINT pt{};
        const bool haveCursor = GetCursorPos(&pt) != 0;
        const ImVec2 mouse(static_cast<float>(pt.x), static_cast<float>(pt.y));
        const bool overRect = rectActive && haveCursor && HitRect(mouse, p0, p1);

        if (down && !wasDown)
            pressedOnRect = overRect;
        const bool released = !down && wasDown;
        const bool clicked = released && pressedOnRect && overRect;
        if (released)
            pressedOnRect = false;
        wasDown = down;
        return clicked;
    }

    void Reset() {
        wasDown = false;
        pressedOnRect = false;
    }
};

inline ButtonResult DrawOverlayButton(const ImVec2& pos, const char* label,
                                      const char* windowId) {
    ButtonResult r;
    const ImVec2 size(kButtonW, kButtonH);
    r.btnP0 = pos;
    r.btnP1 = ImVec2(pos.x + size.x, pos.y + size.y);

    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.f);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.125f, 0.10f, 0.07f, 0.92f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.13f, 0.09f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.09f, 0.07f, 0.05f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.47f, 0.38f, 0.20f, 1.f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.80f, 0.71f, 0.43f, 1.f));

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
                                 | ImGuiWindowFlags_NoBackground
                                 | ImGuiWindowFlags_NoSavedSettings
                                 | ImGuiWindowFlags_NoFocusOnAppearing
                                 | ImGuiWindowFlags_NoBringToFrontOnFocus
                                 | ImGuiWindowFlags_NoNav;

    if (ImGui::Begin(windowId, nullptr, flags)) {
        r.clicked = ImGui::Button(label, size);
        r.hovered = ImGui::IsItemHovered();
    }
    ImGui::End();

    ImGui::PopStyleColor(7);
    ImGui::PopStyleVar(5);
    return r;
}

}
