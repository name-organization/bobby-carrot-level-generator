#pragma once

#include <string>

namespace bc::platform {

/** @brief Saved level progress, best times and audio settings. */
struct SaveData {
    int unlockedNormal = 1;
    int unlockedEgg = 1;
    int bestSeconds[50] = {};  ///< 0 means "never finished"
    bool muted = false;

    int musicVolume = 100;
    int soundVolume = 100;
};

/** @brief Persist progress to browser localStorage or a native working-directory file. */
void saveProgress(const SaveData& data);
bool loadProgress(SaveData& out);

}  // namespace bc::platform
