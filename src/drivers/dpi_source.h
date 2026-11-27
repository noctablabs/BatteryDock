#pragma once
// Custom build: mice that can tell their current DPI. Each brand driver that knows how to read it offers a
// DpiSource; the DPI watcher (src/dpi_watch.cpp) polls them a few times a second and shows a bubble when
// the value changes. Every query here only reads; nothing writes a mouse setting.
#include <memory>
#include <string>
#include <vector>

#include "../hid.h"

struct DpiReading {
    int dpi = 0;     // current DPI, 0 when the mouse reports only a level
    int stage = -1;  // 0-based active level, -1 when unknown
    int count = 0;   // number of levels, 0 when unknown
};

class DpiSource {
public:
    virtual ~DpiSource() = default;
    // Device key as the battery driver reports it (DeviceStatus::key), so the bubble can point at it.
    virtual std::wstring Key() const = 0;
    virtual USHORT Vid() const = 0;
    // One reading; false when the mouse did not answer (asleep, out of range, or busy).
    virtual bool Read(DpiReading& out) = 0;
    // How often to read while the mouse answers.
    virtual unsigned IntervalMs() const { return 400; }
    // Waits up to `ms` between reads. Sources whose mouse announces a settings change (a pushed report)
    // return true as soon as it arrives so the next Read() happens at once; `stop` ends the wait early.
    virtual bool Wait(unsigned ms, HANDLE stop) { WaitForSingleObject(stop, ms); return false; }
};

using DpiSources = std::vector<std::unique_ptr<DpiSource>>;

// Brand finders (each opens its own handles). `colls` is a fresh EnumHid() list.
void FindWraithDpi(const std::vector<HidColl>& colls, DpiSources& out);
void FindLogitechDpi(const std::vector<HidColl>& colls, DpiSources& out);
void FindRazerDpi(const std::vector<HidColl>& colls, DpiSources& out);
void FindCompxDpi(const std::vector<HidColl>& colls, DpiSources& out);  // Kysona-protocol mice (Scyrox V6/V8, ATK, VXE, Pulsar ...)

inline DpiSources FindDpiSources(const std::vector<HidColl>& colls) {
    DpiSources out;
    FindWraithDpi(colls, out);
    FindLogitechDpi(colls, out);
    FindRazerDpi(colls, out);
    FindCompxDpi(colls, out);
    return out;
}
