#pragma once

#include "../board.h"

namespace board::profile {

const Capabilities &GetCapabilities();
esp_err_t InitHardware(Hardware *hardware);
esp_err_t SetBrightness(Hardware *hardware, uint8_t percent);
esp_err_t SetStatusLed(Hardware *hardware, RgbColor color);
bool ReadAcceleration(Hardware *hardware, int16_t *x, int16_t *y, int16_t *z);
void SetOrientation(Hardware *hardware, Orientation orientation);

} // namespace board::profile
