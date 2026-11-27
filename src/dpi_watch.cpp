// Custom build: DPI watcher (see dpi_watch.h).
// Mice do not announce a DPI change in any common way, so each mouse that can tell its DPI (a read-only
// query per brand, drivers/dpi_source.h) gets its own thread that asks it a few times a second (250-400
// ms by brand) and compares; mice that push a "settings changed" report are read the moment it arrives.
// One slow or sleeping mouse therefore never delays another. A mouse that does not answer (asleep,
// switched off) is asked only every 3 s until it answers again. A separate scan looks for mice when
// devices come or go and every 60 s, without stopping the threads of mice it finds again.
#include "dpi_watch.h"

#include <algorithm>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>

#include "hid.h"
#include "drivers/dpi_source.h"

namespace dpiwatch {
namespace {

HANDLE g_thread = nullptr;
HANDLE g_stop = nullptr;
HANDLE g_rescan = nullptr;
HWND g_notify = nullptr;
UINT g_msg = 0;
std::atomic<bool> g_enabled{ true };

constexpr unsigned kQuietMs = 3000;       // a mouse that does not answer
constexpr ULONGLONG kRescanMs = 60000;
constexpr ULONGLONG kRescanEmptyMs = 15000;

struct Last { int dpi = 0, stage = -1, count = 0; };
std::mutex g_lastLock;
std::map<std::wstring, Last> g_last;  // survives rescans, so finding a mouse again is not a "change"

void Post(const DpiSource& s, const DpiReading& r, bool awake = false) {
    auto* c = new Change{ awake, s.Key(), s.Vid(), r.dpi, r.stage, r.count };
    if (!g_notify || !PostMessageW(g_notify, g_msg, 0, (LPARAM)c)) delete c;
}

void Compare(const DpiSource& s, const DpiReading& r) {
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(g_lastLock);
        Last& l = g_last[s.Key()];
        if (r.dpi > 0 && l.dpi > 0)
            changed = r.dpi != l.dpi || (r.stage >= 0 && l.stage >= 0 && r.stage != l.stage);
        else if (r.stage >= 0 && l.stage >= 0)
            changed = r.stage != l.stage && r.count == l.count;
        l = { r.dpi, r.stage, r.count };
    }
    if (changed) Post(s, r);
}

// One mouse: its own thread, stopped through its own event.
struct Worker {
    std::unique_ptr<DpiSource> src;
    HANDLE stop = nullptr;
    HANDLE thread = nullptr;

    static DWORD WINAPI Run(void* p) {
        auto* w = static_cast<Worker*>(p);
        bool answering = false;
        for (;;) {
            if (WaitForSingleObject(w->stop, 0) == WAIT_OBJECT_0) return 0;
            DpiReading r;
            if (!w->src->Read(r)) {
                answering = false;
                if (WaitForSingleObject(w->stop, kQuietMs) == WAIT_OBJECT_0) return 0;
                continue;
            }
            // The mouse answers again (woke up, or switched on): the main window refreshes its battery
            // reading at once instead of showing it dimmed until the next battery poll.
            if (!answering) { answering = true; Post(*w->src, r, true); }
            Compare(*w->src, r);
            w->src->Wait(w->src->IntervalMs(), w->stop);
        }
    }
    bool Start(std::unique_ptr<DpiSource> s) {
        src = std::move(s);
        stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        thread = stop ? CreateThread(nullptr, 0, Run, this, 0, nullptr) : nullptr;
        return thread != nullptr;
    }
    ~Worker() {
        if (stop) SetEvent(stop);
        if (thread) {
            if (WaitForSingleObject(thread, 3000) == WAIT_TIMEOUT) TerminateThread(thread, 0);
            CloseHandle(thread);
        }
        if (stop) CloseHandle(stop);
    }
};

DWORD WINAPI Run(void*) {
    std::map<std::wstring, std::unique_ptr<Worker>> workers;  // by device key
    ULONGLONG nextScan = 0;
    HANDLE waits[2] = { g_stop, g_rescan };
    for (;;) {
        if (!g_enabled) {
            workers.clear();
            { std::lock_guard<std::mutex> lock(g_lastLock); g_last.clear(); }
            nextScan = 0;
            if (WaitForMultipleObjects(2, waits, FALSE, 1000) == WAIT_OBJECT_0) break;
            continue;
        }
        if (GetTickCount64() >= nextScan) {
            DpiSources found = FindDpiSources(EnumHid());
            std::map<std::wstring, bool> present;
            for (auto& s : found) {
                std::wstring key = s->Key();
                present[key] = true;
                if (workers.count(key)) continue;  // already watched: keep its thread and handles
                auto w = std::make_unique<Worker>();
                if (w->Start(std::move(s))) workers[key] = std::move(w);
            }
            for (auto it = workers.begin(); it != workers.end();)  // mice that are gone
                it = present.count(it->first) ? std::next(it) : workers.erase(it);
            nextScan = GetTickCount64() + (workers.empty() ? kRescanEmptyMs : kRescanMs);
        }
        ULONGLONG t = GetTickCount64();
        DWORD wait = nextScan > t ? (DWORD)std::min<ULONGLONG>(nextScan - t, 60000) : 1;
        DWORD w = WaitForMultipleObjects(2, waits, FALSE, wait);
        if (w == WAIT_OBJECT_0) break;
        if (w == WAIT_OBJECT_0 + 1) nextScan = GetTickCount64() + 1500;  // devices changed: let them settle
    }
    workers.clear();
    return 0;
}

}  // namespace

void Start(HWND notify, UINT msg) {
    if (g_thread) return;
    g_notify = notify;
    g_msg = msg;
    g_stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_rescan = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_thread = CreateThread(nullptr, 0, Run, nullptr, 0, nullptr);
}

void Stop() {
    if (!g_thread) return;
    SetEvent(g_stop);
    if (WaitForSingleObject(g_thread, 5000) == WAIT_TIMEOUT) TerminateThread(g_thread, 0);
    CloseHandle(g_thread);
    CloseHandle(g_stop);
    CloseHandle(g_rescan);
    g_thread = g_stop = g_rescan = nullptr;
}

void SetEnabled(bool on) { g_enabled = on; }

void Rescan() { if (g_rescan) SetEvent(g_rescan); }

}  // namespace dpiwatch
