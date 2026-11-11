#pragma once

#include <string>

namespace bc::platform {

// How far the player has got. Tiny enough that a flat string beats any real
// serialisation format.
struct SaveData {
    int unlockedNormal = 1;
    int unlockedEgg = 1;
    int bestSeconds[50] = {};  // 0 means "never finished"
    bool muted = false;
    // Percent, 0 to 100, in steps of 10, set on the Settings page.
    int musicVolume = 100;
    int soundVolume = 100;
};

// On the web this is localStorage, which needs no async flush, unlike IDBFS.
// Natively it is a file next to the executable.
void saveProgress(const SaveData& data);
bool loadProgress(SaveData& out);

}  // namespace bc::platform
