#include "board.h"

#include <algorithm>
#include <cmath>

#include "boards/board_profile.h"

namespace board {

const Capabilities &GetCapabilities() {
  return profile::GetCapabilities();
}

bool IsLandscape(Orientation orientation) {
  return orientation == Orientation::kLandscapeLeft ||
         orientation == Orientation::kLandscapeRight;
}

int DisplayWidth(Orientation orientation) {
  const Capabilities &capabilities = GetCapabilities();
  return IsLandscape(orientation) ? capabilities.portrait_height
                                  : capabilities.portrait_width;
}

int DisplayHeight(Orientation orientation) {
  const Capabilities &capabilities = GetCapabilities();
  return IsLandscape(orientation) ? capabilities.portrait_width
                                  : capabilities.portrait_height;
}

esp_err_t Init(Hardware *hardware) {
  return profile::InitHardware(hardware);
}

esp_err_t SetBrightness(Hardware *hardware, uint8_t percent) {
  if (percent > 100) {
    return ESP_ERR_INVALID_ARG;
  }
  if (!GetCapabilities().has_brightness) {
    return ESP_ERR_NOT_SUPPORTED;
  }
  return profile::SetBrightness(hardware, percent);
}

esp_err_t SetStatusLed(Hardware *hardware, RgbColor color) {
  if (!GetCapabilities().has_status_led) {
    return ESP_ERR_NOT_SUPPORTED;
  }
  return profile::SetStatusLed(hardware, color);
}

bool ReadAcceleration(Hardware *hardware, int16_t *x, int16_t *y, int16_t *z) {
  if (!GetCapabilities().has_imu) {
    return false;
  }
  return profile::ReadAcceleration(hardware, x, y, z);
}

Orientation DetectOrientation(int16_t x, int16_t y,
                              Orientation current_orientation) {
  const Capabilities &capabilities = GetCapabilities();
  const int32_t abs_x = std::abs(static_cast<int32_t>(x));
  const int32_t abs_y = std::abs(static_cast<int32_t>(y));
  if (std::max(abs_x, abs_y) < capabilities.orientation_threshold ||
      std::abs(abs_x - abs_y) < capabilities.orientation_hysteresis) {
    return current_orientation;
  }
  if (abs_x > abs_y) {
    return x > 0 ? Orientation::kLandscapeRight : Orientation::kLandscapeLeft;
  }
  return y > 0 ? Orientation::kPortrait : Orientation::kPortraitInverted;
}

void SetOrientation(Hardware *hardware, Orientation orientation) {
  profile::SetOrientation(hardware, orientation);
}

} // namespace board
