#include "status_led.h"

namespace app {

const char *StatusLedModeName(StatusLedMode) { return "OFF"; }

const char *StatusLedStateName(StatusLedState) { return "IDLE"; }

StatusLedMode GetStatusLedMode() { return StatusLedMode::kOff; }

StatusLedState GetStatusLedState() { return StatusLedState::kIdle; }

bool StatusLedSupported() { return false; }

bool CycleStatusLedMode() { return false; }

esp_err_t StartStatusLed(board::Hardware *, DiceModel *) { return ESP_OK; }

} // namespace app
