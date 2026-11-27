#pragma once
// Custom build: Wraith W1 SE status query, shared by the battery driver and the DPI watcher.
#include <vector>

#include "../hid.h"

namespace wraith {
struct Status {
    int percent = -1;
    bool charging = false;
    bool asleep = false;
    int stage = -1;  // active DPI level, 0-based (-1 = not in the reply)
    int count = 0;   // number of DPI levels
};
// The vendor collection that answers the status query (receiver 0738:A0AF or cable 0738:A09F).
bool IsStatusCollection(const HidColl& c);
// Parses one input report; false when it is not a status reply.
bool Parse(const std::vector<BYTE>& report, Status& out);
// Sends the read-only status query and waits up to timeoutMs per attempt for the reply.
bool Query(HidDev& d, const HidColl& c, Status& out, int timeoutMs, int attempts);
// Reads the DPI of each level from the mouse's settings (read-only session). false when it did not answer.
bool ReadDpiLevels(HidDev& d, std::vector<int>& levels);
}
