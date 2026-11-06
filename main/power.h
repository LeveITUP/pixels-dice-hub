#pragma once

#include "dice_model.h"
#include "esp_err.h"

namespace board {
struct Hardware;
} // namespace board

namespace app {

struct BatteryStatus {
  bool supported;
  // False until the first successful sample.
  bool valid;
  int percent;
  int millivolts;
  bool usb_power;
  bool low;
};

void InitPower(board::Hardware *hardware, DiceModel *model);
bool IsPowerOffSupported();
bool IsBatterySupported();
BatteryStatus GetBatteryStatus();
// Saves preferences, shuts down radios and enters deep sleep from a
// dedicated task. Safe to call from the LVGL task; returns immediately.
void RequestPowerOff(const char *reason = "Powering off");
bool IsPowerOffPending();
// Short user-facing label for the in-progress power off ("Sleeping", ...).
const char *PowerOffReason();

} // namespace app
