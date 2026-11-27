#pragma once
// Custom build: a small bubble over the taskbar that shows a mouse's new DPI for a moment after the DPI
// button is pressed ("1600 DPI", one dot per level, the active ones filled).
#include <windows.h>
#include <string>

namespace dpiosd {
struct Info {
    std::wstring big;     // "1600 DPI", or "DPI 2 / 4" when the level values are not known
    std::wstring small;   // "2 / 4" next to the dots; empty to leave it out
    int stage = 0;        // 0-based active level
    int count = 0;        // number of levels (dots)
    COLORREF accent = RGB(96, 205, 255);
    bool light = false;   // light taskbar theme
    bool haveAnchor = false;
    RECT anchor{};        // screen rectangle of the device on the taskbar strip; the bubble points at it
};
void Show(HINSTANCE inst, const Info& info);
void Destroy();
}
