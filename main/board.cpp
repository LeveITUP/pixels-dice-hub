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

esp_err_t PowerOff(Hardware *hardware) {
  if (!GetCapabilities().has_power_off) {
    return ESP_ERR_NOT_SUPPORTED;
  }
  return profile::PowerOff(hardware);
}

WakeReason GetWakeReason() { return profile::GetWakeReason(); }

const char *WakeReasonName(WakeReason reason) {
  switch (reason) {
  case WakeReason::kPowerOn:
    return "power-on";
  case WakeReason::kButton:
    return "button";
  case WakeReason::kMotion:
    return "motion";
  default:
    return "other";
  }
}

bool ReadBattery(Hardware *hardware, BatteryReading *reading) {
  if (reading == nullptr || !GetCapabilities().has_battery) {
    return false;
  }
  return profile::ReadBattery(hardware, reading);
}

int LipoPercentFromMillivolts(int millivolts) {
  // Approximate single-cell LiPo open-circuit voltage curve.
  struct Point {
    int millivolts;
    int percent;
  };
  static constexpr Point kCurve[] = {
      {3300, 0},  {3450, 3},  {3550, 8},  {3650, 20}, {3720, 35},
      {3790, 50}, {3870, 65}, {3950, 78}, {4050, 90}, {4150, 98},
      {4200, 100},
  };
  if (millivolts <= kCurve[0].millivolts) {
    return 0;
  }
  for (std::size_t i = 1; i < sizeof(kCurve) / sizeof(kCurve[0]); ++i) {
    if (millivolts <= kCurve[i].millivolts) {
      const Point &low = kCurve[i - 1];
      const Point &high = kCurve[i];
      return low.percent + (millivolts - low.millivolts) *
                               (high.percent - low.percent) /
                               (high.millivolts - low.millivolts);
    }
  }
  return 100;
}

} // namespace board
