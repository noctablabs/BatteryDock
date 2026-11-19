// Wraith W1 SE on its 2.4 GHz receiver 0738:A0AF (cable 0738:A09F). Custom build addition.
// Protocol from the Wraith web driver (www.w1.software, assets/index-DQXoqcTG.js, the "w1se" module):
//   vendor collection FF00:FF00, report id 0, 32 data bytes.
//   Status query: [0A AA FF DD 00 ... ][checksum], checksum = 255 - (sum of bytes 0..30 & 0xFF).
//   Reply (input report 0): [0A AA FF DD][battery %][flags][dpi stage][polling code][dpi count] ...
//   flags bit 6 = charging. Battery 0 while not charging = mouse asleep / off.
// Read-only: only the status query the web driver sends every 5 s is used.
#include "drivers.h"

#include <algorithm>
#include <cstdint>

namespace {

const USHORT kVid = 0x0738;
const USHORT kReceiver = 0xA0AF;
const USHORT kCable = 0xA09F;

bool IsStatus(const std::vector<BYTE>& r, size_t o) {
    return r.size() >= o + 7 && r[o] == 0x0A && r[o + 1] == 0xAA && r[o + 2] == 0xFF && r[o + 3] == 0xDD;
}

}  // namespace

void PollWraithW1SE(PollContext& ctx) {
    for (USHORT pid : { kReceiver, kCable }) {
        if (ctx.IsClaimed(kVid, pid)) continue;
        for (const HidColl* c : ctx.Find(kVid, pid, [](const HidColl& c) {
                 return c.page() == 0xFF00 && c.outLen() >= 33 && c.inLen() >= 33;
             })) {
            HidDev d;
            if (!d.Open(*c)) continue;
            std::vector<BYTE> req(c->outLen(), 0);  // [report id 0][32 data bytes]
            req[1] = 0x0A; req[2] = 0xAA; req[3] = 0xFF; req[4] = 0xDD;
            unsigned sum = 0;
            for (int i = 1; i <= 31; i++) sum += req[i];
            req[32] = (BYTE)(255 - (sum & 0xFF));
            bool got = false;
            int pct = -1;
            bool charging = false;
            for (int attempt = 0; attempt < 2 && !got; attempt++) {
                d.Flush();
                if (!d.Write(req)) break;
                ULONGLONG end = GetTickCount64() + 800;
                std::vector<BYTE> r;
                for (;;) {
                    int left = (int)((LONGLONG)end - (LONGLONG)GetTickCount64());
                    if (left <= 0 || d.Read(r, left) < 0) break;
                    size_t o = IsStatus(r, 1) ? 1 : IsStatus(r, 0) ? 0 : SIZE_MAX;  // with or without the report id byte
                    if (o == SIZE_MAX) continue;
                    charging = (r[o + 5] & 0x40) != 0;
                    pct = std::min<int>(100, r[o + 4]);
                    got = true;
                    break;
                }
            }
            if (!got) continue;
            bool cable = pid == kCable;
            DeviceStatus s;
            s.key = L"wraithw1se";
            s.name = L"Wraith W1 SE";
            s.kind = DeviceKind::Mouse;
            s.via = cable ? L"USB cable" : L"2.4 GHz receiver";
            s.graceSec = 1800;
            s.charging = charging;
            if (pct == 0 && !charging) {
                s.connected = true;
                s.asleep = true;  // keep the last value
            } else {
                s.connected = true;
                s.percent = pct;
            }
            ctx.Add(s, kVid, pid);
            break;
        }
    }
}
