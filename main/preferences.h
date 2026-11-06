#pragma once

#include "dice_model.h"

namespace app {

void RestorePreferences(DiceModel *model);
bool SavePreferences(const DiceModel &model);

// Erases all persisted state (paired dice, settings, Wi-Fi) and reboots.
[[noreturn]] void FactoryReset();

} // namespace app
