#include "power.h"

#include <atomic>
#include <mutex>

#include "board.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "pixels_ble.h"
#include "preferences.h"
#include "web_server.h"

namespace app {
namespace {

constexpr char kTag[] = "power";
constexpr uint32_t kBatteryPollMs = 2000;
constexpr int kLowBatteryPercent = 10;
// Below this the ETA6098 protection is close; shut down cleanly first.
constexpr int kCriticalBatteryMillivolts = 3350;
constexpr int kCriticalSamplesRequired = 5;
// The ETA6098 floats VBAT between its ~4.1 V recharge threshold and 4.2 V
// while external power is present; a loaded cell off-charger sags below
// that within minutes. Hysteresis avoids flapping around the threshold.
constexpr int kChargerPresentMillivolts = 4100;
constexpr int kChargerAbsentMillivolts = 4040;
constexpr int kChargerSamplesRequired = 3;
board::Hardware *hardware;
DiceModel *model;
std::atomic_bool power_off_pending{false};
const char *power_off_reason = "Powering off";
std::mutex battery_mutex;
BatteryStatus battery{};
int filtered_millivolts;
int critical_samples;
int charger_samples;
bool charger_by_voltage;
constexpr uint8_t kDefaultIdleSleepMinutes = 15;
std::atomic<uint8_t> idle_sleep_minutes{kDefaultIdleSleepMinutes};

void LoadIdleSleep() {
  nvs_handle_t handle;
  if (nvs_open("pixels", NVS_READONLY, &handle) != ESP_OK) {
    return;
  }
  uint8_t stored;
  if (nvs_get_u8(handle, "idle_sleep", &stored) == ESP_OK) {
    for (uint8_t option : kIdleSleepChoices) {
      if (option == stored) {
        idle_sleep_minutes = stored;
      }
    }
  }
  nvs_close(handle);
}

void SaveIdleSleep(uint8_t minutes) {
  nvs_handle_t handle;
  if (nvs_open("pixels", NVS_READWRITE, &handle) != ESP_OK) {
    return;
  }
  if (nvs_set_u8(handle, "idle_sleep", minutes) == ESP_OK) {
    nvs_commit(handle);
  }
  nvs_close(handle);
}

void PowerOffTask(void *) {
  // Give the UI a moment to paint the shutdown overlay.
  vTaskDelay(pdMS_TO_TICKS(600));
  if (model != nullptr) {
    SavePreferences(*model);
  }
  esp_err_t result = StopWebServer();
  if (result != ESP_OK) {
    ESP_LOGW(kTag, "Web server stop: %s", esp_err_to_name(result));
  }
  result = StopPixelsScanner();
  if (result != ESP_OK) {
    ESP_LOGW(kTag, "BLE stop: %s", esp_err_to_name(result));
  }
  result = board::PowerOff(hardware);
  ESP_LOGE(kTag, "Power off failed: %s", esp_err_to_name(result));
  power_off_pending.store(false);
  vTaskDelete(nullptr);
}

void BatteryTask(void *) {
  bool logged = false;
  while (true) {
    board::BatteryReading reading{};
    if (board::ReadBattery(hardware, &reading)) {
      if (filtered_millivolts == 0) {
        filtered_millivolts = reading.millivolts;
      } else {
        filtered_millivolts =
            (filtered_millivolts * 3 + reading.millivolts) / 4;
      }
      const int percent =
          board::LipoPercentFromMillivolts(filtered_millivolts);
      if (filtered_millivolts >= kChargerPresentMillivolts) {
        if (++charger_samples >= kChargerSamplesRequired) {
          charger_by_voltage = true;
        }
      } else {
        charger_samples = 0;
        if (filtered_millivolts < kChargerAbsentMillivolts) {
          charger_by_voltage = false;
        }
      }
      const bool external_power = reading.usb_power || charger_by_voltage;
      {
        std::lock_guard<std::mutex> lock(battery_mutex);
        battery.valid = true;
        battery.millivolts = filtered_millivolts;
        battery.percent = percent;
        battery.external_power = external_power;
        battery.low = percent <= kLowBatteryPercent && !external_power;
      }
      if (!logged) {
        ESP_LOGI(kTag, "Battery %d mV (%d%%)%s%s", filtered_millivolts,
                 percent, reading.usb_power ? ", USB host" : "",
                 charger_by_voltage ? ", charger" : "");
        logged = true;
      }
      if (!external_power &&
          filtered_millivolts < kCriticalBatteryMillivolts) {
        if (++critical_samples >= kCriticalSamplesRequired &&
            IsPowerOffSupported()) {
          ESP_LOGW(kTag, "Battery critical (%d mV); powering off",
                   filtered_millivolts);
          RequestPowerOff("Battery empty");
        }
      } else {
        critical_samples = 0;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(kBatteryPollMs));
  }
}

} // namespace

void InitPower(board::Hardware *hw, DiceModel *dice_model) {
  hardware = hw;
  model = dice_model;
  ESP_LOGI(kTag, "Wake reason: %s",
           board::WakeReasonName(board::GetWakeReason()));
  LoadIdleSleep();
  battery.supported = IsBatterySupported();
  if (battery.supported &&
      xTaskCreate(BatteryTask, "battery", 3072, nullptr, 2, nullptr) !=
          pdPASS) {
    ESP_LOGE(kTag, "Unable to start battery task");
  }
}

bool IsPowerOffSupported() {
  return board::GetCapabilities().has_power_off;
}

bool IsBatterySupported() { return board::GetCapabilities().has_battery; }

BatteryStatus GetBatteryStatus() {
  std::lock_guard<std::mutex> lock(battery_mutex);
  return battery;
}

void RequestPowerOff(const char *reason) {
  if (!IsPowerOffSupported() || power_off_pending.exchange(true)) {
    return;
  }
  power_off_reason = reason;
  ESP_LOGI(kTag, "Power off requested: %s", reason);
  if (xTaskCreate(PowerOffTask, "power_off", 4096, nullptr, 5, nullptr) !=
      pdPASS) {
    ESP_LOGE(kTag, "Unable to start power-off task");
    power_off_pending.store(false);
  }
}

bool IsPowerOffPending() { return power_off_pending.load(); }

const char *PowerOffReason() { return power_off_reason; }

uint8_t IdleSleepMinutes() { return idle_sleep_minutes.load(); }

void CycleIdleSleep() {
  const uint8_t current = idle_sleep_minutes.load();
  constexpr std::size_t count =
      sizeof(kIdleSleepChoices) / sizeof(kIdleSleepChoices[0]);
  std::size_t index = 0;
  for (std::size_t i = 0; i < count; ++i) {
    if (kIdleSleepChoices[i] == current) {
      index = i;
    }
  }
  SetIdleSleepMinutes(kIdleSleepChoices[(index + 1) % count]);
}

bool SetIdleSleepMinutes(uint8_t minutes) {
  bool supported = false;
  for (uint8_t option : kIdleSleepChoices) {
    supported = supported || option == minutes;
  }
  if (!supported) {
    return false;
  }
  idle_sleep_minutes = minutes;
  SaveIdleSleep(minutes);
  ESP_LOGI(kTag, "Auto sleep set to %s", IdleSleepName());
  return true;
}

const char *IdleSleepName() {
  switch (idle_sleep_minutes.load()) {
  case 0:
    return "OFF";
  case 5:
    return "5 MIN";
  case 15:
    return "15 MIN";
  case 30:
    return "30 MIN";
  case 60:
    return "1 HOUR";
  default:
    return "?";
  }
}

} // namespace app
