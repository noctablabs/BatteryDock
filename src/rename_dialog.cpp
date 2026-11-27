#include "rename_dialog.h"
#include "device_aliases.h"
#include "i18n.h"

#include <algorithm>
#include <vector>

namespace {
constexpr wchar_t kWindowClass[] = L"BatteryDockRenameDialog";

struct EditorState {
    bool link = false;  // custom build: the same editor asks for a driver link (web address or program)
    bool dpi = false;   // custom build: ... or the DPI of each level
    int levels = 0;
    std::wstring initial;
    std::wstring result;
    HWND edit = nullptr;
    bool accepted = false;
};

// Up to 8 numbers between 50 and 50000, separated by commas (empty is fine: forget the values).
bool ValidDpiList(const std::wstring& text) {
    int count = 0;
    std::wstring cur;
    bool ok = true;
    auto flush = [&]() {
        size_t a = cur.find_first_not_of(L" \t"), b = cur.find_last_not_of(L" \t");
        std::wstring t = a == std::wstring::npos ? L"" : cur.substr(a, b - a + 1);
        cur.clear();
        if (t.empty()) return;
        if (t.size() > 5 || t.find_first_not_of(L"0123456789") != std::wstring::npos) { ok = false; return; }
        int n = std::stoi(t);
        if (n < 50 || n > 50000) ok = false;
        count++;
    };
    for (wchar_t c : text) {
        if (c == L',' || c == L';' || c == L'/') flush();
        else cur.push_back(c);
    }
    flush();
    return ok && count <= 8;
}

EditorState* State(HWND hwnd) {
    return reinterpret_cast<EditorState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

void SetControlFont(HWND control) {
    if (control) SendMessageW(control, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), TRUE);
}

LRESULT CALLBACK EditorProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)create->lpCreateParams);
        return TRUE;
    }
    EditorState* state = State(hwnd);
    if (!state) return DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg) {
        case WM_CREATE: {
            HINSTANCE inst = GetModuleHandleW(nullptr);
            const int dpi = GetDpiForWindow(hwnd);
            auto scale = [dpi](int v) { return MulDiv(v, dpi, 96); };
            std::wstring prompt = state->dpi ? std::wstring(T(L"DPI of each level, in order, separated by commas (e.g. 400, 800, 1600, 3200):"))
                                  : state->link ? T(L"Web driver address or program (empty = none):") : T(L"Device display name:");
            if (state->dpi && state->levels > 0)
                prompt += L"\n" + TF(L"This mouse has {count} levels.", { { L"count", std::to_wstring(state->levels) } });
            HWND label = CreateWindowExW(0, L"STATIC", prompt.c_str(),
                                         WS_CHILD | WS_VISIBLE, scale(16), scale(state->dpi ? 10 : 18), scale(382), scale(state->dpi ? 34 : 22),
                                         hwnd, nullptr, inst, nullptr);
            state->edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", state->initial.c_str(),
                                          WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                          scale(16), scale(45), scale(382), scale(25),
                                          hwnd, (HMENU)(INT_PTR)100, inst, nullptr);
            HWND save = CreateWindowExW(0, L"BUTTON", T(L"Save"),
                                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                                        scale(220), scale(85), scale(85), scale(29),
                                        hwnd, (HMENU)(INT_PTR)IDOK, inst, nullptr);
            HWND cancel = CreateWindowExW(0, L"BUTTON", T(L"Cancel"),
                                          WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                          scale(313), scale(85), scale(85), scale(29),
                                          hwnd, (HMENU)(INT_PTR)IDCANCEL, inst, nullptr);
            SetControlFont(label);
            SetControlFont(state->edit);
            SetControlFont(save);
            SetControlFont(cancel);
            SendMessageW(state->edit, EM_SETLIMITTEXT, state->link ? 1024 : state->dpi ? 64 : aliases::kMaxNameLength, 0);
            SetFocus(state->edit);
            SendMessageW(state->edit, EM_SETSEL, 0, -1);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK) {
                const int length = GetWindowTextLengthW(state->edit);
                std::vector<wchar_t> buffer(static_cast<size_t>(length) + 1, 0);
                GetWindowTextW(state->edit, buffer.data(), (int)buffer.size());
                std::wstring name = aliases::Trim(buffer.data());
                if (state->dpi && !ValidDpiList(name)) {
                    MessageBoxW(hwnd, T(L"Enter numbers between 50 and 50000, separated by commas."), T(L"DPI values"), MB_OK | MB_ICONINFORMATION);
                    SetFocus(state->edit);
                    return 0;
                }
                if (!state->link && !state->dpi && !aliases::ValidName(name)) {
                    MessageBoxW(hwnd, T(L"Enter a name (1-64 characters)."), T(L"Rename device"), MB_OK | MB_ICONINFORMATION);
                    SetFocus(state->edit);
                    return 0;
                }
                state->result = std::move(name);
                state->accepted = true;
                DestroyWindow(hwnd);
                return 0;
            }
            if (LOWORD(wp) == IDCANCEL) { DestroyWindow(hwnd); return 0; }
            break;
        case WM_CLOSE: DestroyWindow(hwnd); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
}  // namespace

static bool PromptImpl(HWND owner, const std::wstring& currentName, std::wstring& result, bool link, bool dpi = false, int levels = 0);

bool PromptDeviceName(HWND owner, const std::wstring& currentName, std::wstring& result) {
    return PromptImpl(owner, currentName, result, false);
}

bool PromptDeviceLink(HWND owner, const std::wstring& currentLink, std::wstring& result) {
    return PromptImpl(owner, currentLink, result, true);
}

bool PromptDpiValues(HWND owner, const std::wstring& current, int levels, std::wstring& result) {
    return PromptImpl(owner, current, result, false, true, levels);
}

static bool PromptImpl(HWND owner, const std::wstring& currentName, std::wstring& result, bool link, bool dpi, int levels) {
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.hInstance = inst;
    wc.lpfnWndProc = EditorProc;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    const int scaleDpi = owner ? GetDpiForWindow(owner) : 96;
    const int width = MulDiv(420, scaleDpi, 96), height = MulDiv(157, scaleDpi, 96);
    POINT p{};
    GetCursorPos(&p);
    HMONITOR monitor = MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{ sizeof(mi) };
    RECT area{};
    if (GetMonitorInfoW(monitor, &mi)) area = mi.rcWork;
    else SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0);
    int x = area.left + ((area.right - area.left - width) / 2);
    int y = area.top + ((area.bottom - area.top - height) / 2);
    x = std::max(x, static_cast<int>(area.left));
    y = std::max(y, static_cast<int>(area.top));

    EditorState state;
    state.initial = currentName;
    state.link = link;
    state.dpi = dpi;
    state.levels = levels;
    HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOOLWINDOW, kWindowClass,
                                   dpi ? T(L"DPI values") : link ? T(L"Driver link") : T(L"Rename device"), WS_CAPTION | WS_SYSMENU | WS_POPUP,
                                   x, y, width, height, owner, nullptr, inst, &state);
    if (!dialog) return false;

    const bool ownerEnabled = owner && IsWindowEnabled(owner);
    if (ownerEnabled) EnableWindow(owner, FALSE);
    ShowWindow(dialog, SW_SHOW);
    UpdateWindow(dialog);
    SetForegroundWindow(dialog);
    if (state.edit) {
        SetFocus(state.edit);
        SendMessageW(state.edit, EM_SETSEL, 0, -1);
    }
    MSG msg{};
    while (IsWindow(dialog)) {
        BOOL code = GetMessageW(&msg, nullptr, 0, 0);
        if (code <= 0) {
            if (code == 0) PostQuitMessage((int)msg.wParam);
            break;
        }
        if (!IsDialogMessageW(dialog, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (IsWindow(dialog)) DestroyWindow(dialog);
    if (ownerEnabled) EnableWindow(owner, TRUE);
    if (state.accepted) result = std::move(state.result);
    return state.accepted;
}
