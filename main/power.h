#pragma once

#include "board.h"
#include "dice_model.h"
#include "esp_err.h"

namespace app {

void InitPower(board::Hardware *hardware, DiceModel *model);
bool IsPowerOffSupported();
// Saves preferences, shuts down radios and enters deep sleep from a
// dedicated task. Safe to call from the LVGL task; returns immediately.
void RequestPowerOff();
bool IsPowerOffPending();

} // namespace app
