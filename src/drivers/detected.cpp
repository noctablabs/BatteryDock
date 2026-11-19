// Custom build: "recognised but battery not readable" fallback, for every brand.
// Runs after all other drivers. Every USB / Bluetooth HID mouse, keyboard, gamepad or headset that no driver
// claimed is listed with its brand (from the vendor id) and product name, battery unknown. These entries are
// hidden on the taskbar by default (main.cpp) and can be shown from the menu.
#include "drivers.h"

#include <algorithm>
#include <cwctype>
#include <map>

namespace {

struct Brand { USHORT vid; const wchar_t* name; };
const Brand kBrands[] = {
    { 0x046D, L"Logitech" }, { 0x1532, L"Razer" }, { 0x1B1C, L"Corsair" }, { 0x1038, L"SteelSeries" },
    { 0x0951, L"HyperX" }, { 0x03F0, L"HP / HyperX" }, { 0x0B05, L"ASUS" }, { 0x1E7D, L"ROCCAT" },
    { 0x045E, L"Microsoft" }, { 0x05AC, L"Apple" }, { 0x17EF, L"Lenovo" }, { 0x413C, L"Dell" },
    { 0x0738, L"Mad Catz / Wraith" }, { 0x093A, L"PixArt" }, { 0x3554, L"Compx" }, { 0x258A, L"Sinowealth" },
    { 0x24AE, L"Rapoo" }, { 0x25A7, L"Areson" }, { 0x3434, L"Keychron" }, { 0x04D9, L"Holtek" },
    { 0x0C45, L"Microdia" }, { 0x1A2C, L"China Resource" }, { 0x320F, L"Evision" }, { 0x05D5, L"Super Gate" },
    { 0x1BCF, L"Sunplus" }, { 0x18F8, L"Maxxter" }, { 0x2717, L"Xiaomi" }, { 0x12D1, L"Huawei" },
    { 0x04E8, L"Samsung" }, { 0x054C, L"Sony / PlayStation" }, { 0x057E, L"Nintendo" }, { 0x2DC8, L"8BitDo" },
    { 0x3537, L"GameSir" }, { 0x0E6F, L"PDP" }, { 0x0F0D, L"HORI" }, { 0x24C6, L"PowerA" },
    { 0x1949, L"Amazon" }, { 0x0B0E, L"Jabra" }, { 0x047F, L"Poly" }, { 0x1395, L"EPOS / Sennheiser" },
    { 0x0ECB, L"JBL" }, { 0x2F96, L"Glorious" }, { 0x22D4, L"Laview" }, { 0x3367, L"Endgame Gear" },
    { 0x3710, L"Pulsar" }, { 0x373E, L"Lamzu" }, { 0x373B, L"ATK" }, { 0x36A7, L"WLmouse" }, { 0x395A, L"Scyrox" },
    { 0x1D57, L"Xenta" }, { 0x2EA8, L"Darmoshark" }, { 0x248A, L"Telink" }, { 0x2B89, L"Fantech" },
    { 0x0E8D, L"MediaTek" }, { 0x1EA7, L"SHARKOON" }, { 0x3151, L"YICHIP" }, { 0x0E0F, L"VMware" },
    { 0x1A81, L"Holtek" }, { 0x09DA, L"A4Tech / Bloody" }, { 0x062A, L"MosArt" }, { 0x0458, L"Genius" },
    { 0x1D50, L"OpenMoko" }, { 0x05E3, L"Genesys" }, { 0x0DB0, L"MSI" }, { 0x1462, L"MSI" },
    { 0x2516, L"Cooler Master" }, { 0x1E71, L"NZXT" }, { 0x31E3, L"Wooting" }, { 0x3297, L"ZSA" },
    { 0x0A5C, L"Broadcom" }, { 0x0B33, L"Contour" }, { 0x256F, L"3Dconnexion" }, { 0x056A, L"Wacom" },
    { 0x28BD, L"XP-Pen" }, { 0x256C, L"Huion" }, { 0x2833, L"Meta" }, { 0x0BDA, L"Realtek" }, { 0x0489, L"Foxconn" },
    { 0x1B80, L"Zowie" }, { 0x3938, L"MCHOSE" }, { 0x3837, L"MCHOSE" }, { 0x41E4, L"MCHOSE" }, { 0x36B0, L"Ajazz" },
    { 0x0416, L"Winbond" }, { 0x1EA8, L"VXE" }, { 0x33E4, L"G-Wolves" }, { 0x3770, L"Hitscan" }, { 0x35BB, L"HECATE" },
};

const wchar_t* BrandName(USHORT vid) {
    for (const auto& b : kBrands)
        if (b.vid == vid) return b.name;
    return nullptr;
}

std::wstring Lower(std::wstring s) {
    for (auto& c : s) c = (wchar_t)towlower(c);
    return s;
}

bool Contains(const std::wstring& lowered, const wchar_t* w) { return lowered.find(w) != std::wstring::npos; }

// Things that are never a battery device: touchpads, tablets, virtual / remote input, LED controllers, etc.
bool Ignore(USHORT vid, const std::wstring& lowerName) {
    if (vid == 0 || vid == 0x06CB /* Synaptics */ || vid == 0x04F3 /* ELAN */ || vid == 0x0E0F /* VMware */ ||
        vid == 0x80EE /* VirtualBox */ || vid == 0x056A || vid == 0x28BD || vid == 0x256C /* tablets */)
        return true;
    for (const wchar_t* w : { L"touchpad", L"touch pad", L"precision", L"tablet", L"pen ", L"digitizer", L"virtual",
                              L"remote desktop", L"aura", L"led controller", L"rgb", L"ups", L"webcam", L"camera" })
        if (Contains(lowerName, w)) return true;
    return false;
}

}  // namespace

void PollDetected(PollContext& ctx) {
    // Ids a real driver has read at least once in this run: never list them as "not readable" (the mouse may be
    // asleep and the driver silent this round; the app keeps its last value meanwhile).
    static std::set<std::pair<USHORT, USHORT>> everClaimed;
    for (const auto& p : ctx.claimed) everClaimed.insert(p);

    std::map<std::pair<USHORT, USHORT>, const HidColl*> products;
    for (const auto& c : ctx.colls) {
        if (ctx.IsClaimed(c.vid, c.pid) || everClaimed.count({ c.vid, c.pid })) continue;
        bool input = c.page() == 0x01 && (c.usage() == 0x02 || c.usage() == 0x04 || c.usage() == 0x05 || c.usage() == 0x06);
        bool audio = c.page() == 0x0B || c.page() == 0x0C;
        if (!input && !audio) continue;
        auto& slot = products[{ c.vid, c.pid }];
        if (!slot || (input && !(slot->page() == 0x01))) slot = &c;
    }
    for (const auto& [id, c] : products) {
        auto [vid, pid] = id;
        std::wstring product = HidProduct(*c);
        std::wstring low = Lower(product);
        if (Ignore(vid, low)) continue;
        DeviceKind kind = KindFromCollections(ctx.colls, vid, pid);
        bool alreadyListed = false;  // Bluetooth / XInput fallbacks report by other means: same brand and kind = same device
        for (const auto& o : ctx.out) alreadyListed |= o.vid == vid && (o.kind == kind || Lower(o.name) == low);
        if (alreadyListed) continue;
        if (kind == DeviceKind::Other) {
            // Only audio collections: count it only when it calls itself a headset / headphones.
            if (!(Contains(low, L"headset") || Contains(low, L"headphone") || Contains(low, L"earphone") ||
                  Contains(low, L"buds") || Contains(low, L"wireless")))
                continue;
            kind = DeviceKind::Headset;
        } else if (Contains(low, L"headset") || Contains(low, L"headphone")) {
            kind = DeviceKind::Headset;  // headset dongles often carry a keyboard / consumer collection too
        }
        // Wired-only gear has no battery; keep devices that look wireless, plus every gamepad and headset.
        bool wirelessHint = c->path.find(L"BTHENUM") != std::wstring::npos || c->path.find(L"{00001124") != std::wstring::npos ||
                            c->path.find(L"BTHLE") != std::wstring::npos;
        for (const wchar_t* w : { L"wireless", L"receiver", L"dongle", L"2.4g", L"2.4 g", L"nano", L"unifying", L"bolt",
                                  L"lightspeed", L"hyperspeed", L"slipstream", L"bluetooth", L"bt ", L"tri-mode", L"8k", L"4k" })
            wirelessHint |= Contains(low, w);
        // The user's own wired mouse would only add a useless entry; unknown names still count (hidden by default).
        const wchar_t* brand = BrandName(vid);
        std::wstring name;
        if (!product.empty()) {
            name = product;
            if (brand) {
                std::wstring firstBrandWord = Lower(std::wstring(brand).substr(0, std::wstring(brand).find_first_of(L" /")));
                if (!Contains(low, firstBrandWord.c_str())) name = std::wstring(brand) + L" " + product;
            }
        } else {
            name = std::wstring(brand ? brand : L"Unknown") + (kind == DeviceKind::Keyboard ? L" keyboard"
                                                              : kind == DeviceKind::Headset ? L" headset"
                                                              : kind == DeviceKind::Gamepad ? L" controller" : L" mouse");
        }
        for (const wchar_t* suffix : { L" Dongle", L" Receiver", L" Wireless Receiver" }) {
            size_t l = wcslen(suffix);
            if (name.size() > l + 2 && name.compare(name.size() - l, l, suffix) == 0) name = name.substr(0, name.size() - l);
        }
        DeviceStatus s;
        s.key = std::wstring(wirelessHint ? L"detected:" : L"detected-wired:") + Hex4(vid) + L":" + Hex4(pid);
        s.name = name;
        s.kind = kind;
        s.via = wirelessHint ? L"battery level not readable yet" : L"recognised (no battery info)";
        s.vid = vid;
        s.connected = true;
        s.percent = -1;
        ctx.Add(s, vid, pid);
    }
}
