#pragma once

#include "board.h"
#include "dice_model.h"
#include "esp_err.h"

namespace app {

esp_err_t StartUi(board::Hardware *hardware, DiceModel *model);

#ifdef PIXELS_DEV_TOOLS
// Forces a display orientation and stops following the IMU (dev builds).
void SetUiOrientation(board::Orientation orientation);
#endif

} // namespace app
