// Wraith W1 SE on its 2.4 GHz receiver 0738:A0AF (cable 0738:A09F). Custom build addition.
// Protocol from the Wraith web driver (www.w1.software, assets/index-DQXoqcTG.js, the "w1se" module):
//   vendor collection FF00:FF00, report id 0, 32 data bytes.
//   Status query: [0A AA FF DD 00 ... ][checksum], checksum = 255 - (sum of bytes 0..30 & 0xFF).
//   Reply (input report 0): [0A AA FF DD][battery %][flags][dpi stage][polling code][dpi count] ...
//   flags bit 6 = charging. Battery 0 while not charging = mouse asleep / off.
//   dpi stage is the 0-based index of the active DPI level (the web driver stores it as dpi.active),
//   dpi count the number of levels (1..8).
// Read-only: only the status query the web driver sends every 5 s is used.
// DPI values (custom build): the web driver reads the mouse's settings back as it connects, and so does the
// DPI watcher, once per connection: session start [00 AA 55] (the mouse echoes each frame), read requests
// [80|n] for packets n = 2 (level count at [1]), 5 (levels 1..6) and 6 (levels 7..8), session end
// [00 CC 33]. A level is 5 bytes [mode][value lo][value hi][..]; value + mode map to a DPI through the web
// driver's table (Zo) below. Nothing is written: the web driver blocks the two settings-changing frames
// FF AA 55 and 40 AA FF DD, and neither is ever built here.
#include "drivers.h"
#include "wraith_w1se.h"

#include <algorithm>
#include <cstdint>
#include <mutex>

#include "dpi_source.h"

namespace {

const USHORT kVid = 0x0738;
const USHORT kReceiver = 0xA0AF;
const USHORT kCable = 0xA09F;

bool IsStatus(const std::vector<BYTE>& r, size_t o) {
    return r.size() >= o + 7 && r[o] == 0x0A && r[o + 1] == 0xAA && r[o + 2] == 0xFF && r[o + 3] == 0xDD;
}

}  // namespace

namespace wraith {

bool IsStatusCollection(const HidColl& c) {
    return c.vid == kVid && (c.pid == kReceiver || c.pid == kCable) && c.page() == 0xFF00 && c.outLen() >= 33 && c.inLen() >= 33;
}

bool Parse(const std::vector<BYTE>& r, Status& out) {
    size_t o = IsStatus(r, 1) ? 1 : IsStatus(r, 0) ? 0 : SIZE_MAX;  // with or without the report id byte
    if (o == SIZE_MAX) return false;
    out.charging = (r[o + 5] & 0x40) != 0;
    out.percent = std::min<int>(100, r[o + 4]);
    out.asleep = out.percent == 0 && !out.charging;
    out.stage = -1;
    out.count = 0;
    if (r.size() >= o + 9) {
        int stage = r[o + 6], count = r[o + 8];
        if (count >= 1 && count <= 8 && stage < count) { out.stage = stage; out.count = count; }
    }
    return true;
}

// The battery poll (worker thread) and the DPI watcher talk to the same mouse; one exchange at a time.
std::mutex g_io;

bool Query(HidDev& d, const HidColl& c, Status& out, int timeoutMs, int attempts) {
    std::lock_guard<std::mutex> lock(g_io);
    std::vector<BYTE> req(c.outLen(), 0);  // [report id 0][32 data bytes]
    req[1] = 0x0A; req[2] = 0xAA; req[3] = 0xFF; req[4] = 0xDD;
    unsigned sum = 0;
    for (int i = 1; i <= 31; i++) sum += req[i];
    req[32] = (BYTE)(255 - (sum & 0xFF));
    for (int attempt = 0; attempt < attempts; attempt++) {
        d.Flush();
        if (!d.Write(req)) return false;
        ULONGLONG end = GetTickCount64() + (ULONGLONG)timeoutMs;
        std::vector<BYTE> r;
        for (;;) {
            int left = (int)((LONGLONG)end - (LONGLONG)GetTickCount64());
            if (left <= 0 || d.Read(r, left) < 0) break;
            if (Parse(r, out)) return true;
        }
    }
    return false;
}

namespace {

struct DpiCode { int dpi, raw, mode; };
const DpiCode kDpiTable[] = {
    {100,1,0}, {150,3,0}, {200,4,0}, {250,5,0}, {300,7,0}, {350,8,0}, {400,9,0}, {450,10,0},
    {500,11,0}, {550,13,0}, {600,14,0}, {650,15,0}, {700,16,0}, {750,17,0}, {800,19,0}, {850,20,0},
    {900,21,0}, {950,22,0}, {1000,23,0}, {1050,25,0}, {1100,26,0}, {1150,27,0}, {1200,28,0}, {1250,29,0},
    {1300,30,0}, {1350,32,0}, {1400,33,0}, {1450,34,0}, {1500,35,0}, {1550,36,0}, {1600,38,0}, {1650,39,0},
    {1700,40,0}, {1750,41,0}, {1800,42,0}, {1850,44,0}, {1900,45,0}, {1950,46,0}, {2000,47,0}, {2050,48,0},
    {2100,50,0}, {2150,51,0}, {2200,52,0}, {2250,53,0}, {2300,54,0}, {2350,55,0}, {2400,57,0}, {2450,58,0},
    {2500,59,0}, {2550,60,0}, {2600,61,0}, {2650,63,0}, {2700,64,0}, {2750,65,0}, {2800,66,0}, {2850,67,0},
    {2900,69,0}, {2950,70,0}, {3000,71,0}, {3050,72,0}, {3100,73,0}, {3150,75,0}, {3200,76,0}, {3250,77,0},
    {3300,78,0}, {3350,79,0}, {3400,80,0}, {3450,82,0}, {3500,83,0}, {3550,84,0}, {3600,85,0}, {3650,86,0},
    {3700,88,0}, {3750,89,0}, {3800,90,0}, {3850,91,0}, {3900,92,0}, {3950,94,0}, {4000,95,0}, {4050,96,0},
    {4100,97,0}, {4150,98,0}, {4200,100,0}, {4250,101,0}, {4300,102,0}, {4350,103,0}, {4400,104,0}, {4450,105,0},
    {4500,107,0}, {4550,108,0}, {4600,109,0}, {4650,110,0}, {4700,111,0}, {4750,113,0}, {4800,114,0}, {4850,115,0},
    {4900,116,0}, {4950,117,0}, {5000,119,0}, {5050,120,0}, {5100,121,0}, {5150,122,0}, {5200,123,0}, {5250,125,0},
    {5300,126,0}, {5350,127,0}, {5400,128,0}, {5450,129,0}, {5500,131,0}, {5550,132,0}, {5600,133,0}, {5650,134,0},
    {5700,135,0}, {5750,136,0}, {5800,138,0}, {5850,139,0}, {5900,140,0}, {5950,141,0}, {6000,142,0}, {6050,144,0},
    {6100,145,0}, {6150,146,0}, {6200,147,0}, {6250,148,0}, {6300,150,0}, {6350,151,0}, {6400,152,0}, {6450,153,0},
    {6500,154,0}, {6550,156,0}, {6600,157,0}, {6650,158,0}, {6700,159,0}, {6750,160,0}, {6800,161,0}, {6850,163,0},
    {6900,164,0}, {6950,165,0}, {7000,166,0}, {7050,167,0}, {7100,169,0}, {7150,170,0}, {7200,171,0}, {7250,172,0},
    {7300,173,0}, {7350,175,0}, {7400,176,0}, {7450,177,0}, {7500,178,0}, {7550,179,0}, {7600,181,0}, {7650,182,0},
    {7700,183,0}, {7750,184,0}, {7800,185,0}, {7850,186,0}, {7900,188,0}, {7950,189,0}, {8000,190,0}, {8050,191,0},
    {8100,192,0}, {8150,194,0}, {8200,195,0}, {8250,196,0}, {8300,197,0}, {8350,198,0}, {8400,200,0}, {8450,201,0},
    {8500,202,0}, {8550,203,0}, {8600,204,0}, {8650,206,0}, {8700,207,0}, {8750,208,0}, {8800,209,0}, {8850,210,0},
    {8900,211,0}, {8950,213,0}, {9000,214,0}, {9050,215,0}, {9100,216,0}, {9150,217,0}, {9200,219,0}, {9250,220,0},
    {9300,221,0}, {9350,222,0}, {9400,223,0}, {9450,225,0}, {9500,226,0}, {9550,227,0}, {9600,228,0}, {9650,229,0},
    {9700,231,0}, {9750,232,0}, {9800,233,0}, {9850,234,0}, {9900,235,0}, {9950,238,0}, {10000,239,0}, {10100,118,1},
    {10200,120,1}, {10300,121,1}, {10400,122,1}, {10500,123,1}, {10600,124,1}, {10700,126,1}, {10800,127,1}, {10900,128,1},
    {11000,129,1}, {11100,130,1}, {11200,131,1}, {11300,133,1}, {11400,134,1}, {11500,135,1}, {11600,136,1}, {11700,137,1},
    {11800,139,1}, {11900,140,1}, {12000,141,1}, {12100,142,1}, {12200,143,1}, {12300,144,1}, {12400,146,1}, {12500,147,1},
    {12600,148,1}, {12700,149,1}, {12800,150,1}, {12900,151,1}, {13000,153,1}, {13100,154,1}, {13200,155,1}, {13300,156,1},
    {13400,157,1}, {13500,159,1}, {13600,160,1}, {13700,161,1}, {13800,162,1}, {13900,163,1}, {14000,164,1}, {14100,166,1},
    {14200,167,1}, {14300,168,1}, {14400,169,1}, {14500,170,1}, {14600,172,1}, {14700,173,1}, {14800,174,1}, {14900,175,1},
    {15000,176,1}, {15100,177,1}, {15200,179,1}, {15300,180,1}, {15400,181,1}, {15500,182,1}, {15600,183,1}, {15700,184,1},
    {15800,186,1}, {15900,187,1}, {16000,188,1},
};

int DpiFor(int raw, int mode) {
    for (const auto& e : kDpiTable)
        if (e.raw == raw && e.mode == mode) return e.dpi;
    return 0;
}

std::vector<BYTE> Frame(std::initializer_list<BYTE> head) {
    std::vector<BYTE> f(33, 0);  // [report id 0][32 bytes]
    size_t i = 1;
    for (BYTE b : head) f[i++] = b;
    unsigned sum = 0;
    for (int k = 1; k <= 31; k++) sum += f[k];
    f[32] = (BYTE)(255 - (sum & 0xFF));
    return f;
}

// Sends a session frame and waits for the mouse to echo it back unchanged (as the web driver does).
bool SendEchoed(HidDev& d, const std::vector<BYTE>& f) {
    for (int attempt = 0; attempt < 3; attempt++) {
        d.Flush();
        if (!d.Write(f)) return false;
        ULONGLONG end = GetTickCount64() + 1000;
        std::vector<BYTE> r;
        for (;;) {
            int left = (int)((LONGLONG)end - (LONGLONG)GetTickCount64());
            if (left <= 0 || d.Read(r, left) < 0) break;
            if (r.size() >= 32 && std::equal(f.begin() + 1, f.end(), r.begin())) return true;
        }
        Sleep(200);
    }
    return false;
}

// Reads settings packet n; returns its 32 bytes or an empty vector.
std::vector<BYTE> ReadPacket(HidDev& d, BYTE n) {
    std::vector<BYTE> f = Frame({ (BYTE)(0x80 | n) });
    if (!d.Write(f)) return {};
    ULONGLONG end = GetTickCount64() + 300;
    std::vector<BYTE> r;
    for (;;) {
        int left = (int)((LONGLONG)end - (LONGLONG)GetTickCount64());
        if (left <= 0 || d.Read(r, left) < 0) return {};
        if (r.size() < 32 || IsStatus(r, 0)) continue;
        if (r[0] == (0x80 | n) || r[0] == n) return std::vector<BYTE>(r.begin(), r.begin() + 32);
    }
}

bool AllZero(const std::vector<BYTE>& p) {
    for (int i = 1; i < 31; i++) if (p[i]) return false;
    return true;
}

void AddLevels(const std::vector<BYTE>& p, int n, std::vector<int>& out) {
    for (int e = 0; e < n; e++) {
        int t = 1 + e * 5;
        int raw = p[t + 1] | (p[t + 2] << 8);
        out.push_back(raw ? DpiFor(raw, p[t]) : 0);
    }
}

}  // namespace

bool ReadDpiLevels(HidDev& d, std::vector<int>& levels) {
    std::lock_guard<std::mutex> lock(g_io);
    levels.clear();
    if (!SendEchoed(d, Frame({ 0x00, 0xAA, 0x55 }))) return false;  // session start
    std::vector<BYTE> sys2 = ReadPacket(d, 2), sys5, sys6;
    int count = 0;
    if (sys2.size() == 32 && !AllZero(sys2) && sys2[1] >= 1 && sys2[1] <= 8 && sys2[3] + 1 <= 10) count = sys2[1];
    if (count) sys5 = ReadPacket(d, 5);
    if (count > 6 && sys5.size() == 32) sys6 = ReadPacket(d, 6);
    SendEchoed(d, Frame({ 0x00, 0xCC, 0x33 }));  // session end, also after a failed read
    if (!count || sys5.size() != 32 || AllZero(sys5)) return false;
    AddLevels(sys5, 6, levels);
    if (sys6.size() == 32 && !AllZero(sys6)) AddLevels(sys6, 2, levels);
    levels.resize((size_t)count, 0);
    return true;
}

}  // namespace wraith

// ---- DPI source ------------------------------------------------------------------------------------

namespace {

class WraithDpi : public DpiSource {
public:
    explicit WraithDpi(const HidColl& c) : c_(c) {}
    bool Open() { return d_.Open(c_); }
    std::wstring Key() const override { return L"wraithw1se"; }
    USHORT Vid() const override { return kVid; }
    unsigned IntervalMs() const override { return 250; }  // the receiver answers in a few ms
    bool Read(DpiReading& out) override {
        wraith::Status st;
        if (!wraith::Query(d_, c_, st, 400, 1) || st.asleep || st.stage < 0) return false;
        // Values: read once per connection, and again if the number of levels changes; give up after
        // three failed tries (the bubble then shows the level only).
        if (st.count != levelsFor_ && tries_ < 3) {
            std::vector<int> v;
            if (wraith::ReadDpiLevels(d_, v)) { levels_ = v; levelsFor_ = st.count; }
            else tries_++;
        }
        out.stage = st.stage;
        out.count = st.count;
        out.dpi = levelsFor_ == st.count && st.stage < (int)levels_.size() ? levels_[(size_t)st.stage] : 0;
        return true;
    }
private:
    HidColl c_;
    HidDev d_;
    std::vector<int> levels_;
    int levelsFor_ = 0, tries_ = 0;
};

}  // namespace

void FindWraithDpi(const std::vector<HidColl>& colls, DpiSources& out) {
    for (const HidColl& c : colls) {
        if (!wraith::IsStatusCollection(c)) continue;
        auto s = std::make_unique<WraithDpi>(c);
        if (s->Open()) { out.push_back(std::move(s)); return; }  // one mouse: receiver or cable
    }
}

void PollWraithW1SE(PollContext& ctx) {
    for (USHORT pid : { kReceiver, kCable }) {
        if (ctx.IsClaimed(kVid, pid)) continue;
        for (const HidColl* c : ctx.Find(kVid, pid, [](const HidColl& c) { return wraith::IsStatusCollection(c); })) {
            HidDev d;
            if (!d.Open(*c)) continue;
            wraith::Status st;
            bool got = wraith::Query(d, *c, st, 800, 2);
            if (!got) continue;
            bool cable = pid == kCable;
            DeviceStatus s;
            s.key = L"wraithw1se";
            s.name = L"Wraith W1 SE";
            s.kind = DeviceKind::Mouse;
            s.via = cable ? L"USB cable" : L"2.4 GHz receiver";
            s.graceSec = 1800;
            s.charging = st.charging;
            if (st.stage >= 0) { s.dpiStage = st.stage; s.dpiCount = st.count; }
            if (st.asleep) {
                s.connected = true;
                s.asleep = true;  // keep the last value
            } else {
                s.connected = true;
                s.percent = st.percent;
            }
            ctx.Add(s, kVid, pid);
            break;
        }
    }
}
