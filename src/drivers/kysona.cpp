// Kysona, Pulsar and ATK / VXE wireless mice (VID 3554, ATK dongles 373B), G-Wolves models on this protocol
// (VID 33E4, e.g. Fenrir Asym) and the original Hitscan
// Hyperlight (VID 3770, verified on the 1K receiver 3770:0200), Pulsar's newer XS-1 generation
// (VID 3710, verified on the 8K Dongle 3710:5406) and Redragon's Compx-based mice (M916 Pro 1K,
// M913 tri-mode, G49: receiver 3554:F55D / F5D5, cable F55E; verified on an M916 Pro 1K through
// the F55D receiver). They share one protocol:
// 17-byte output/input report id 08: [08][cmd][0 x14][checksum], checksum = 0x55 - sum(bytes 0..15).
//   cmd 03 = online status -> reply [6] = 1 when the mouse is connected to the dongle
//   cmd 04 = battery       -> reply [6] = percent, [7] = charging, [8..9] = millivolts
// ATK VXE Dragonfly R1 family (receivers 3554:F58A / F58E / F505, cables F58C / F58F / F506) uses the same
// protocol on vendor collection FF02:0002; its receivers expose several other 17-byte vendor collections
// (FF03..FF06), so FF02:0002 is tried first. IDs and protocol from github.com/S1berla/vxe-r1-pro-max-battery,
// github.com/orcunturkol/BatteryTray (PR #1) and HeyOkay/HaloBattery issue #273 (verified on F505).
// Reference: Linux drivers/hid/hid-kysona.c (Lode Willems) and the hid-pulsar patch discussion;
// Redragon ids from github.com/EliottV17/redragon-battery-omarchy and github.com/pingpingjinping/g49-web-control.
// The older dual-mode M913 on the Areson 25A7:FA07 / FA08 receiver is handled by areson.cpp.
#include "drivers.h"
#include "dpi_source.h"

#include <algorithm>
#include <memory>
#include <mutex>

namespace {

// Redragon uses Compx's own ids, and its receiver only calls itself "2.4G Wireless Receiver".
bool IsRedragon(USHORT vid, USHORT pid) { return vid == 0x3554 && (pid == 0xF55D || pid == 0xF5D5 || pid == 0xF55E); }

bool IsVxeR1(USHORT vid, USHORT pid) {
    return vid == 0x3554 &&
           (pid == 0xF58A || pid == 0xF58C || pid == 0xF58E || pid == 0xF58F || pid == 0xF505 || pid == 0xF506);
}

// A VXE R1 cable reports under its receiver's id, so the mouse is one entry on either connection.
// F58A / F58C verified; the other pairs follow the order PR #69 lists them in.
USHORT KeyPid(USHORT vid, USHORT pid) {
    if (vid == 0x3554 && pid == 0xF58C) return 0xF58A;
    if (vid == 0x3554 && pid == 0xF58F) return 0xF58E;
    if (vid == 0x3554 && pid == 0xF506) return 0xF505;
    return pid;
}

// HECATE (Edifier) also sells headsets under 35BB (G2 Wireless 7.1: dongles A009 / A209) with a 64-byte
// FF02:0002 vendor collection that speaks something else; only the mouse ids from HECATE Connect's
// Config.ini (dongle D3FB, mouse over cable D3FA / D3FC / D3FD) get Kysona frames.
bool IsHecateMouse(USHORT pid) { return pid == 0xD3FA || pid == 0xD3FB || pid == 0xD3FC || pid == 0xD3FD; }

bool IsCable(USHORT vid, USHORT pid) {
    bool hecate_cable = vid == 0x35BB && (pid == 0xD3FA || pid == 0xD3FC || pid == 0xD3FD);
    return (vid == 0x33E4 && GWolvesIsCable(pid)) || hecate_cable ||
           (vid == 0x3554 && (pid == 0xF55E || pid == 0xF58C || pid == 0xF58F || pid == 0xF506));
}

BYTE Checksum(const BYTE* b) {
    unsigned sum = 0;
    for (int i = 0; i < 16; i++) sum += b[i];
    return (BYTE)(0x55 - sum);
}

// The battery poll and the DPI watcher talk to the same receiver over separate handles. The 8K receivers
// (e.g. SCYROX 8K Dongle 3554:F5F7) answer one request at a time, so overlapping requests from the two
// threads made both time out: every exchange holds this lock.
std::mutex g_io;

// Sends `cmd` and waits for its checksummed reply (up to 800 ms: Scyrox's S-Center allows 700 ms).
bool Query(HidDev& d, BYTE cmd, std::vector<BYTE>& r) {
    std::lock_guard<std::mutex> lock(g_io);
    std::vector<BYTE> req(17, 0);
    req[0] = 0x08;
    req[1] = cmd;
    req[16] = Checksum(req.data());
    d.Flush();
    if (!d.Write(req)) return false;
    ULONGLONG end = GetTickCount64() + 800;
    for (;;) {
        int left = (int)((LONGLONG)end - (LONGLONG)GetTickCount64());
        if (left <= 0 || d.Read(r, left) < 17) return false;
        if (r[0] == 0x08 && r[1] == cmd && r[16] == Checksum(r.data()) && !std::equal(req.begin(), req.end(), r.begin()))
            return true;
    }
}

std::wstring CleanName(std::wstring n, USHORT vid, USHORT pid) {
    if (vid == 0x33E4) return GWolvesModelName(pid);
    if (IsRedragon(vid, pid)) return L"Redragon wireless mouse";  // shared by several models
    if (IsVxeR1(vid, pid)) return L"VXE Dragonfly R1";  // the receivers call themselves "VXE NordicMouse 1K Dongle" etc.
    for (const wchar_t* suffix : { L" Dongle", L" Receiver", L" 2.4G", L" Wireless Receiver" }) {
        size_t l = wcslen(suffix);
        if (n.size() > l && n.compare(n.size() - l, l, suffix) == 0) n = n.substr(0, n.size() - l);
    }
    if (vid == 0x3770) return L"Hitscan Hyperlight";  // the receiver only calls itself "Hitscan USB Receiver"
    if (vid == 0x35BB) return L"HECATE mouse";
    if (n.empty()) n = vid == 0x373B ? L"ATK wireless mouse" : L"Wireless mouse";
    return n;
}

}  // namespace

void PollKysona(PollContext& ctx) {
    std::vector<std::wstring> seenNames;
    for (USHORT vendor : { (USHORT)0x3554, (USHORT)0x373B, (USHORT)0x3770, (USHORT)0x33E4, (USHORT)0x3710, (USHORT)0x35BB }) {
        for (auto [vid, pid] : ctx.Products(vendor)) {
            // ATK N9 Pro / N9 Ultra headsets speak a serial protocol (atk_serial.cpp) and the ATK 8K Nano
            // dongle 1178 a Holtek feature-report protocol (atk_bitmouse.cpp); never send them mouse frames,
            // even when they stay unclaimed (ATK app holding the port, mouse asleep).
            if (vid == 0x373B && (pid == 0x11CA || pid == 0x11CB || pid == 0x1203 || pid == 0x1204 || pid == 0x1178)) continue;
            // 3554:FA09 / FB40 are the "BY" keyboard receivers (AULA, MCHOSE, also used by the Redragon
            // K673) that aula.cpp handles. Their FF02:0002 collection passes the size check below, so a
            // keyboard aula.cpp could not read got two mouse frames every poll, which kept a K673 from
            // going to sleep (issue #191). Never send them mouse frames.
            if (vid == 0x3554 && (pid == 0xFA09 || pid == 0xFB40)) continue;
            // Same for HECATE headsets (G2 Wireless 7.1, issue #1358): mouse frames only go to HECATE mouse ids.
            if (vid == 0x35BB && !IsHecateMouse(pid)) continue;
            // Kysona exposes a dedicated 17-byte collection; ATK / VXE receivers put report 08 in a
            // vendor collection that also carries larger reports. The checksummed reply filters out
            // anything that does not speak this protocol.
            auto colls = ctx.Find(vid, pid, [](const HidColl& c) {
                return (c.outLen() == 17 && c.inLen() == 17) || (c.page() >= 0xFF00 && c.outLen() >= 17 && c.inLen() >= 17);
            });
            // The battery channel is FF02:0002 where it exists; try it before the other vendor collections.
            std::stable_sort(colls.begin(), colls.end(), [](const HidColl* a, const HidColl* b) {
                return (a->page() == 0xFF02 && a->usage() == 0x0002) > (b->page() == 0xFF02 && b->usage() == 0x0002);
            });
            for (const HidColl* c : colls) {
                HidDev d;
                if (!d.Open(*c)) continue;
                std::vector<BYTE> r;
                DeviceStatus s;
                bool answered = false;
                if (Query(d, 0x03, r)) {
                    answered = true;
                    s.connected = r[6] != 0;
                }
                if ((!answered || s.connected) && (Query(d, 0x04, r) || (answered && Query(d, 0x04, r)))) {
                    answered = true;
                    s.connected = true;
                    if (r[6] <= 100) s.percent = r[6];
                    s.charging = r[7] != 0;
                    s.millivolts = (r[8] << 8) | r[9];
                } else if (answered && s.connected) {
                    s.asleep = true;  // the receiver says online but the mouse did not answer: keep the last value
                }
                if (!answered) continue;
                if (!s.connected) {
                    ctx.Claim(vid, pid);
                    break;
                }
                s.key = L"kysona:" + Hex4(vid) + L":" + Hex4(KeyPid(vid, pid));
                s.name = CleanName(HidProduct(*c), vid, pid);
                s.kind = KindFromCollections(ctx.colls, vid, pid);
                if (s.kind == DeviceKind::Other) s.kind = DeviceKind::Mouse;
                s.via = IsCable(vid, pid) ? L"USB cable" : L"2.4 GHz receiver";
                s.graceSec = 1800;
                // A mouse plugged in for charging can show up twice (cable + dongle); keep one.
                bool dup = false;
                for (auto& n : seenNames) dup |= n == s.name;
                if (dup) {
                    ctx.Claim(vid, pid);
                    for (auto& o : ctx.out)
                        if (o.name == s.name && s.charging) o.charging = true;
                } else {
                    seenNames.push_back(s.name);
                    ctx.Add(s, vid, pid);
                }
                break;
            }
        }
    }
}

// ---- DPI source (custom build) ---------------------------------------------------------------------
// The same 17-byte protocol reads the mouse's settings memory (command 08 "ReadFlashData"), as the
// Compx-based web drivers do (Scyrox S-Center "Control HUB WEB", www.scyrox.net/assets/index-*.js;
// MouseDock's DpiProtocol.cs, MIT):
//   request [08][08][00][addr hi][addr lo][len][00 ...][checksum], reply the same header + data from [6].
//   address 2 = number of DPI levels, 4 = active level (0-based); each byte is followed by 55 - byte.
// The level values are coded per sensor, so they are decoded only for Scyrox mice (S-Center's cfg.json
// lists PAW3950 and PAW3955 models): PAW3955 levels at 6912 + 6 * i, [x lo][x hi][y lo][y hi][flags][chk],
// value = (x + ((flags >> 2 & 3) << 16) + 1); PAW3950 levels at 12 + 4 * i, [x][y][flags][chk],
// value = (x + ((flags >> 2 & 3) << 8) + 1) * 50; flags bit 0 and bit 1 each double the value; every
// block's bytes add up to 55 (hex). Other brands on this protocol code their values differently: they
// get the level only (and "DPI values..." to type them in). Read-only: command 08 only reads.

namespace {

bool ReadFlash(HidDev& d, int addr, int len, std::vector<BYTE>& data) {
    std::lock_guard<std::mutex> lock(g_io);
    std::vector<BYTE> req(17, 0);
    req[0] = 0x08;
    req[1] = 0x08;
    req[3] = (BYTE)(addr >> 8);
    req[4] = (BYTE)addr;
    req[5] = (BYTE)len;
    req[16] = Checksum(req.data());
    d.Flush();
    if (!d.Write(req)) return false;
    ULONGLONG end = GetTickCount64() + 700;
    std::vector<BYTE> r;
    for (;;) {
        int left = (int)((LONGLONG)end - (LONGLONG)GetTickCount64());
        if (left <= 0 || d.Read(r, left) < 17) return false;
        if (r[0] != 0x08 || r[1] != 0x08 || r[16] != Checksum(r.data())) continue;
        if (std::equal(req.begin(), req.end(), r.begin())) continue;  // an echo of the request
        if (r[3] != req[3] || r[4] != req[4] || r[5] != len) continue;
        if (r[2] != 0) return false;  // error status: mouse not answering
        data.assign(r.begin() + 6, r.begin() + 6 + len);
        return true;
    }
}

bool Sum55(const std::vector<BYTE>& b) {
    unsigned s = 0;
    for (BYTE v : b) s += v;
    return (s & 0xFF) == 0x55;
}

bool ReadPair(HidDev& d, int addr, int& value) {
    std::vector<BYTE> b;
    if (!ReadFlash(d, addr, 2, b) || !Sum55(b)) return false;
    value = b[0];
    return true;
}

bool IsScyrox(USHORT vid, USHORT pid) {
    return vid == 0x3554 && (pid == 0xF5F4 || pid == 0xF5F6 || pid == 0xF5F7 || pid == 0xF515 || pid == 0xF517);
}

int Doubled(int v, int flags) { return v * ((flags & 1) ? 2 : 1) * ((flags & 2) ? 2 : 1); }

// Scyrox level values; empty when they do not read back as valid blocks.
std::vector<int> ReadScyroxLevels(HidDev& d, int count) {
    std::vector<int> v;
    std::vector<BYTE> b;
    for (int i = 0; i < count; i++) {  // PAW3955
        if (!ReadFlash(d, 6912 + 6 * i, 6, b) || !Sum55(b)) { v.clear(); break; }
        int value = Doubled(b[0] + (b[1] << 8) + (((b[4] >> 2) & 3) << 16) + 1, b[4] & 3);
        if (value < 50 || value > 84000) { v.clear(); break; }
        v.push_back(value);
    }
    if (!v.empty()) return v;
    for (int i = 0; i < count; i++) {  // PAW3950
        if (!ReadFlash(d, 12 + 4 * i, 4, b) || !Sum55(b)) return {};
        int value = Doubled((b[0] + (((b[2] >> 2) & 3) << 8) + 1) * 50, b[2] & 3);
        if (value < 50 || value > 60000) return {};
        v.push_back(value);
    }
    return v;
}

class CompxDpi : public DpiSource {
public:
    CompxDpi(const HidColl& c) : c_(c) {}
    bool Open() { return d_.Open(c_); }
    bool Probe() { std::vector<BYTE> r; return Query(d_, 0x03, r); }  // the receiver speaks this protocol (mouse may sleep)
    std::wstring Key() const override { return L"kysona:" + Hex4(c_.vid) + L":" + Hex4(KeyPid(c_.vid, c_.pid)); }
    USHORT Vid() const override { return c_.vid; }
    unsigned IntervalMs() const override { return 350; }
    // The mouse pushes [08][0A] "status changed" when a setting changes (S-Center reads the active DPI
    // level when bit 0 of [6] is set): read at once instead of waiting for the next turn.
    bool Wait(unsigned ms, HANDLE stop) override {
        ULONGLONG end = GetTickCount64() + ms;
        std::vector<BYTE> r;
        for (;;) {
            if (WaitForSingleObject(stop, 0) == WAIT_OBJECT_0) return false;
            int left = (int)((LONGLONG)end - (LONGLONG)GetTickCount64());
            if (left <= 0) return false;
            if (d_.Read(r, std::min(left, 50)) >= 17 && r[0] == 0x08 && r[1] == 0x0A && r[16] == Checksum(r.data()) &&
                (r[6] & 1))
                return true;
        }
    }
    bool Read(DpiReading& out) override {
        int active = 0;
        if (!ReadPair(d_, 4, active)) return false;
        if (count_ <= 0 || active >= count_) {
            int c = 0;
            if (!ReadPair(d_, 2, c) || c < 1 || c > 8) return false;
            if (c != count_) { count_ = c; levels_.clear(); tries_ = 0; }
        }
        if (active >= count_) return false;
        if (IsScyrox(c_.vid, c_.pid) && levels_.empty() && tries_ < 3) {
            levels_ = ReadScyroxLevels(d_, count_);
            if (levels_.empty()) tries_++;
        }
        out.stage = active;
        out.count = count_;
        out.dpi = active < (int)levels_.size() ? levels_[(size_t)active] : 0;
        return true;
    }
private:
    HidColl c_;
    HidDev d_;
    int count_ = 0, tries_ = 0;
    std::vector<int> levels_;
};

}  // namespace

void FindCompxDpi(const std::vector<HidColl>& colls, DpiSources& out) {
    std::vector<std::pair<USHORT, USHORT>> products;
    for (const auto& c : colls) {
        bool vendor = c.vid == 0x3554 || c.vid == 0x373B || c.vid == 0x3770 || c.vid == 0x33E4 || c.vid == 0x3710 || c.vid == 0x35BB;
        if (vendor && std::find(products.begin(), products.end(), std::make_pair(c.vid, c.pid)) == products.end())
            products.push_back({ c.vid, c.pid });
    }
    for (auto [vid, pid] : products) {
        // Same exclusions as the battery poll: devices that speak other protocols never get these frames.
        if (vid == 0x373B && (pid == 0x11CA || pid == 0x11CB || pid == 0x1203 || pid == 0x1204 || pid == 0x1178)) continue;
        if (vid == 0x3554 && (pid == 0xFA09 || pid == 0xFB40)) continue;
        if (vid == 0x35BB && !IsHecateMouse(pid)) continue;
        std::vector<const HidColl*> cs;
        for (const auto& c : colls)
            if (c.vid == vid && c.pid == pid &&
                ((c.outLen() == 17 && c.inLen() == 17) || (c.page() >= 0xFF00 && c.outLen() >= 17 && c.inLen() >= 17)))
                cs.push_back(&c);
        std::stable_sort(cs.begin(), cs.end(), [](const HidColl* a, const HidColl* b) {
            return (a->page() == 0xFF02 && a->usage() == 0x0002) > (b->page() == 0xFF02 && b->usage() == 0x0002);
        });
        for (const HidColl* c : cs) {
            auto s = std::make_unique<CompxDpi>(*c);
            if (s->Open() && s->Probe()) { out.push_back(std::move(s)); break; }
        }
    }
}
