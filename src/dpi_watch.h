#pragma once
// Custom build: watches the DPI of mice that can report it (Logitech HID++, Razer, Wraith W1 SE; see
// drivers/dpi_source.h) and tells the main window when it changes, so a small bubble can show the new DPI
// over the taskbar.
#include <windows.h>
#include <string>

namespace dpiwatch {
struct Change {
    bool awake = false;  // true: not a DPI change; the mouse just started answering again (woke up)
    std::wstring key;  // device key, as in DeviceStatus::key
    USHORT vid = 0;    // to find the device when the keys differ (e.g. cable vs receiver)
    int dpi = 0;       // new DPI, 0 when only the level is known
    int stage = -1;    // 0-based active level, -1 when unknown
    int count = 0;     // number of levels, 0 when unknown
};
// Starts the watcher thread. On a change it posts `msg` to `notify` with a heap-allocated Change* in lParam
// (the receiver deletes it).
void Start(HWND notify, UINT msg);
void Stop();
// Off: the thread closes its handles and sends nothing to any mouse.
void SetEnabled(bool on);
// Looks for mice again soon (call on device arrival / removal).
void Rescan();
}
