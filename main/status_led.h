#pragma once

#include <cstdint>

#include "dice_model.h"
#include "esp_err.h"

namespace board {
struct Hardware;
}

namespace app {

enum class StatusLedMode : uint8_t {
  kOff = 0,
  kSolid = 1,
  kPulse = 2,
};

enum class StatusLedState : uint8_t {
  kIdle = 0,
  kHandling = 1,
  kRolling = 2,
  kCrooked = 3,
  kComplete = 4,
};

const char *StatusLedModeName(StatusLedMode mode);
const char *StatusLedStateName(StatusLedState state);
StatusLedMode GetStatusLedMode();
StatusLedState GetStatusLedState();
bool StatusLedSupported();
bool CycleStatusLedMode();
esp_err_t StartStatusLed(board::Hardware *hardware, DiceModel *model);

} // namespace app
