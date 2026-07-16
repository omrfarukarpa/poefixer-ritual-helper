#pragma once

#include <Windows.h>

namespace RitualHelperInput {

inline void MoveCursorScreen(int x, int y) {
    const int vsX = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int vsY = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vsW = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int vsH = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (vsW <= 0) vsW = 1;
    if (vsH <= 0) vsH = 1;

    const LONG nx = static_cast<LONG>(
        ((static_cast<long long>(x - vsX) * 65535) + (vsW - 1) / 2) / (vsW - 1 > 0 ? vsW - 1 : 1));
    const LONG ny = static_cast<LONG>(
        ((static_cast<long long>(y - vsY) * 65535) + (vsH - 1) / 2) / (vsH - 1 > 0 ? vsH - 1 : 1));

    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dx = nx;
    in.mi.dy = ny;
    in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    SendInput(1, &in, sizeof(INPUT));
}

inline bool GetCursorScreen(int& x, int& y) {
    POINT pt{};
    if (!GetCursorPos(&pt))
        return false;
    x = pt.x;
    y = pt.y;
    return true;
}

inline void LeftClickAtCursor() {
    INPUT inputs[2]{};
    inputs[0].type = INPUT_MOUSE;
    inputs[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    inputs[1].type = INPUT_MOUSE;
    inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(2, inputs, sizeof(INPUT));
}

inline bool IsRightMouseDown() {
    return (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
}

}
