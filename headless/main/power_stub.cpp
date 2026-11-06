#include "power.h"

namespace app {

void InitPower(board::Hardware *, DiceModel *) {}

bool IsPowerOffSupported() { return false; }

bool IsBatterySupported() { return false; }

BatteryStatus GetBatteryStatus() { return BatteryStatus{}; }

void RequestPowerOff(const char *) {}

bool IsPowerOffPending() { return false; }

const char *PowerOffReason() { return ""; }

} // namespace app
