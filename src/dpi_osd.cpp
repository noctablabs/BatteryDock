// Custom build: DPI bubble (see dpi_osd.h). A layered, click-through, never-activated popup drawn with
// per-pixel alpha: rounded body, a small tail pointing at the device on the taskbar, the DPI in large text
// and one dot per DPI level. It stays 1.5 s, then fades out; a new change while it shows restarts it.
#include "dpi_osd.h"

#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace dpiosd {
namespace {

constexpr wchar_t kClass[] = L"BatteryDockDpiOsd";
constexpr UINT_PTR TIMER_HOLD = 1, TIMER_FADE = 2;
constexpr UINT kHoldMs = 1500, kFadeStepMs = 15;
constexpr int kFadeStep = 26;

HWND g_wnd = nullptr;
int g_alpha = 255;

struct Rgb { int r, g, b; };

LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_NCHITTEST: return HTTRANSPARENT;
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_TIMER:
            if (wp == TIMER_HOLD) {
                KillTimer(h, TIMER_HOLD);
                SetTimer(h, TIMER_FADE, kFadeStepMs, nullptr);
            } else if (wp == TIMER_FADE) {
                g_alpha -= kFadeStep;
                if (g_alpha <= 0) {
                    KillTimer(h, TIMER_FADE);
                    ShowWindow(h, SW_HIDE);
                    g_alpha = 255;
                } else {
                    BLENDFUNCTION bf{ AC_SRC_OVER, 0, (BYTE)g_alpha, AC_SRC_ALPHA };
                    UpdateLayeredWindow(h, nullptr, nullptr, nullptr, nullptr, nullptr, 0, &bf, ULW_ALPHA);
                }
            }
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

bool EnsureWindow(HINSTANCE inst) {
    if (g_wnd && IsWindow(g_wnd)) return true;
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = Proc;
    wc.hInstance = inst;
    wc.lpszClassName = kClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    g_wnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
                            kClass, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, inst, nullptr);
    return g_wnd != nullptr;
}

HFONT MakeFont(int px, int weight) {
    return CreateFontW(-px, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
}

SIZE Measure(HDC dc, HFONT f, const std::wstring& s) {
    SIZE sz{ 0, 0 };
    HGDIOBJ old = SelectObject(dc, f);
    if (!s.empty()) GetTextExtentPoint32W(dc, s.c_str(), (int)s.size(), &sz);
    else {
        TEXTMETRICW tm{};
        GetTextMetricsW(dc, &tm);
        sz.cy = tm.tmHeight;
    }
    SelectObject(dc, old);
    return sz;
}

// Shape of the bubble: a rounded rectangle plus an optional triangular tail on its top or bottom edge.
struct Shape {
    double left, top, right, bottom, radius;
    int tail = 0;          // 0 none, +1 below the body (pointing down), -1 above it (pointing up)
    double tailX = 0, tailW = 0, tailH = 0;
    bool Inside(double x, double y, double inset) const {
        double l = left + inset, t = top + inset, r = right - inset, b = bottom - inset, rad = std::max(0.0, radius - inset);
        if (x >= l && x <= r && y >= t && y <= b) {
            double cx = std::clamp(x, l + rad, r - rad), cy = std::clamp(y, t + rad, b - rad);
            double dx = x - cx, dy = y - cy;
            if (dx * dx + dy * dy <= rad * rad) return true;
        }
        if (!tail) return false;
        double half = std::max(0.0, tailW / 2 - inset), h = tailH - inset * 1.4;
        if (h <= 0) return false;
        // base sits just inside the body edge so body and tail join without a seam
        double baseY = tail > 0 ? bottom - inset - 1 : top + inset + 1;
        double d = tail > 0 ? y - baseY : baseY - y;  // distance from the base towards the tip
        if (d < 0 || d > h + 1) return false;
        return std::fabs(x - tailX) <= half * (1 - d / (h + 1));
    }
};

int Coverage(const Shape& s, int x, int y, double inset) {
    int n = 0;
    for (int sy = 0; sy < 4; sy++)
        for (int sx = 0; sx < 4; sx++)
            if (s.Inside(x + (sx + 0.5) / 4, y + (sy + 0.5) / 4, inset)) n++;
    return n * 255 / 16;
}

int CircleCoverage(double cx, double cy, double r, int x, int y) {
    int n = 0;
    for (int sy = 0; sy < 4; sy++)
        for (int sx = 0; sx < 4; sx++) {
            double dx = x + (sx + 0.5) / 4 - cx, dy = y + (sy + 0.5) / 4 - cy;
            if (dx * dx + dy * dy <= r * r) n++;
        }
    return n * 255 / 16;
}

Rgb Mix(Rgb a, Rgb b, int t) {  // t: 0 -> a, 255 -> b
    return { a.r + (b.r - a.r) * t / 255, a.g + (b.g - a.g) * t / 255, a.b + (b.b - a.b) * t / 255 };
}

}  // namespace

void Show(HINSTANCE inst, const Info& info) {
    if (!EnsureWindow(inst)) return;

    HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr);
    UINT dpi = tray ? GetDpiForWindow(tray) : 0;
    if (!dpi) dpi = 96;
    auto P = [dpi](double v) { return (int)std::lround(v * dpi / 96.0); };

    APPBARDATA abd{ sizeof(abd) };
    bool haveBar = SHAppBarMessage(ABM_GETTASKBARPOS, &abd) != 0;
    UINT edge = haveBar ? abd.uEdge : ABE_BOTTOM;
    RECT bar = abd.rc;
    RECT anchor = info.anchor;
    if (!info.haveAnchor) {  // tray-icon mode or no strip: point at the notification area
        HWND notify = tray ? FindWindowExW(tray, nullptr, L"TrayNotifyWnd", nullptr) : nullptr;
        if (!(notify && GetWindowRect(notify, &anchor)) && !(tray && GetWindowRect(tray, &anchor))) {
            RECT work;
            SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
            anchor = { work.right - P(120), work.bottom, work.right - P(40), work.bottom };
            haveBar = false;
            edge = ABE_BOTTOM;
            bar = anchor;
        }
    }

    // Colours follow the taskbar theme, like Windows' own flyouts.
    Rgb bg = info.light ? Rgb{ 249, 249, 249 } : Rgb{ 44, 44, 46 };
    Rgb border = info.light ? Rgb{ 208, 208, 208 } : Rgb{ 78, 78, 84 };
    Rgb text = info.light ? Rgb{ 24, 24, 24 } : Rgb{ 255, 255, 255 };
    Rgb sub = info.light ? Rgb{ 96, 96, 96 } : Rgb{ 178, 178, 184 };
    Rgb off = info.light ? Rgb{ 200, 200, 204 } : Rgb{ 96, 96, 102 };
    Rgb accent{ GetRValue(info.accent), GetGValue(info.accent), GetBValue(info.accent) };

    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HFONT bigFont = MakeFont(P(16), FW_SEMIBOLD);
    HFONT smallFont = MakeFont(P(11), FW_NORMAL);
    SIZE bigSz = Measure(mem, bigFont, info.big);
    SIZE smallSz = Measure(mem, smallFont, info.small);

    const int pad = P(11), radius = P(8), tailW = P(14), tailH = P(7), gapAbove = P(6);
    const double dotR = 2.75 * dpi / 96.0;
    const int dotGap = P(9);
    int count = std::clamp(info.count, 0, 8);
    int dotsW = count > 0 ? (int)std::ceil((count - 1) * dotGap + 2 * dotR) : 0;
    int row2H = std::max<int>((int)std::ceil(2 * dotR), info.small.empty() ? 0 : smallSz.cy);
    int bodyW = std::max<int>(bigSz.cx, dotsW + (info.small.empty() ? 0 : P(14) + smallSz.cx)) + 2 * pad;
    bodyW = std::max(bodyW, P(104));
    int bodyH = pad - P(2) + bigSz.cy + (count > 0 || !info.small.empty() ? P(4) + row2H : 0) + pad - P(1);

    int tail = edge == ABE_TOP ? -1 : edge == ABE_BOTTOM ? 1 : 0;
    int W = bodyW, H = bodyH + (tail ? tailH : 0);
    int bodyTop = tail < 0 ? tailH : 0;

    // Place it next to the taskbar, over the device.
    HMONITOR mon = MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(mon, &mi);
    RECT area = mi.rcMonitor;
    int ax = (anchor.left + anchor.right) / 2, ay = (anchor.top + anchor.bottom) / 2;
    int x, y;
    if (tail > 0) { x = ax - W / 2; y = std::min<int>(bar.top, anchor.top) - gapAbove - H; }
    else if (tail < 0) { x = ax - W / 2; y = std::max<int>(bar.bottom, anchor.bottom) + gapAbove; }
    else if (edge == ABE_LEFT) { x = std::max<int>(bar.right, anchor.right) + P(8); y = ay - H / 2; }
    else { x = std::min<int>(bar.left, anchor.left) - P(8) - W; y = ay - H / 2; }
    x = std::clamp<int>(x, area.left + P(8), std::max<int>(area.left + P(8), area.right - P(8) - W));
    y = std::clamp<int>(y, area.top + P(4), std::max<int>(area.top + P(4), area.bottom - P(4) - H));

    Shape shape{ 0.0, (double)bodyTop, (double)W, (double)(bodyTop + bodyH), (double)radius };
    shape.tail = tail;
    shape.tailW = tailW;
    shape.tailH = tailH;
    shape.tailX = std::clamp<double>(ax - x, radius + tailW / 2.0, W - radius - tailW / 2.0);

    // Body, border and dots into a 32-bit buffer (alpha is set from the shape at the end).
    std::vector<uint32_t> px((size_t)W * H, 0);
    std::vector<uint8_t> cov((size_t)W * H, 0);
    double bw = std::max(1.0, dpi / 96.0);
    int rowY = bodyTop + pad - P(2) + bigSz.cy + P(4);
    double dotCy = rowY + row2H / 2.0;
    for (int yy = 0; yy < H; yy++)
        for (int xx = 0; xx < W; xx++) {
            int outer = Coverage(shape, xx, yy, 0);
            if (!outer) continue;
            int inner = Coverage(shape, xx, yy, bw);
            Rgb c = Mix(border, bg, inner * 255 / outer);
            for (int i = 0; i < count; i++) {
                double cx = pad + dotR + i * dotGap;
                if (std::fabs(xx + 0.5 - cx) > dotR + 1 || std::fabs(yy + 0.5 - dotCy) > dotR + 1) continue;
                int dc = CircleCoverage(cx, dotCy, dotR, xx, yy);
                if (dc) c = Mix(c, i <= info.stage ? accent : off, dc);
            }
            px[(size_t)yy * W + xx] = (uint32_t)(c.r << 16 | c.g << 8 | c.b);
            cov[(size_t)yy * W + xx] = (uint8_t)outer;
        }

    BITMAPINFO bi{};
    bi.bmiHeader = { sizeof(BITMAPINFOHEADER), W, -H, 1, 32, BI_RGB };
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp || !bits) {
        if (bmp) DeleteObject(bmp);
        DeleteObject(bigFont);
        DeleteObject(smallFont);
        DeleteDC(mem);
        ReleaseDC(nullptr, screen);
        return;
    }
    memcpy(bits, px.data(), px.size() * 4);
    HGDIOBJ oldBmp = SelectObject(mem, bmp);
    SetBkMode(mem, TRANSPARENT);
    HGDIOBJ oldFont = SelectObject(mem, bigFont);
    SetTextColor(mem, RGB(text.r, text.g, text.b));
    TextOutW(mem, pad, bodyTop + pad - P(2), info.big.c_str(), (int)info.big.size());
    if (!info.small.empty()) {
        SelectObject(mem, smallFont);
        SetTextColor(mem, RGB(sub.r, sub.g, sub.b));
        TextOutW(mem, W - pad - smallSz.cx, (int)std::lround(dotCy - smallSz.cy / 2.0), info.small.c_str(), (int)info.small.size());
    }
    SelectObject(mem, oldFont);
    GdiFlush();

    // GDI text leaves alpha at 0: take alpha from the shape and premultiply.
    auto* out = static_cast<uint32_t*>(bits);
    for (size_t i = 0; i < px.size(); i++) {
        uint32_t a = cov[i];
        uint32_t c = out[i];
        uint32_t r = ((c >> 16) & 255) * a / 255, g = ((c >> 8) & 255) * a / 255, b = (c & 255) * a / 255;
        out[i] = a << 24 | r << 16 | g << 8 | b;
    }

    KillTimer(g_wnd, TIMER_FADE);
    g_alpha = 255;
    POINT pos{ x, y }, src{ 0, 0 };
    SIZE size{ W, H };
    BLENDFUNCTION bf{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UpdateLayeredWindow(g_wnd, screen, &pos, &size, mem, &src, 0, &bf, ULW_ALPHA);
    SetWindowPos(g_wnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    SetTimer(g_wnd, TIMER_HOLD, kHoldMs, nullptr);

    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteObject(bigFont);
    DeleteObject(smallFont);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
}

void Destroy() {
    if (g_wnd) DestroyWindow(g_wnd);
    g_wnd = nullptr;
}

}  // namespace dpiosd
