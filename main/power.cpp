#include "power.h"

#include <atomic>
#include <mutex>

#include "board.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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
board::Hardware *hardware;
DiceModel *model;
std::atomic_bool power_off_pending{false};
const char *power_off_reason = "Powering off";
std::mutex battery_mutex;
BatteryStatus battery{};
int filtered_millivolts;
int critical_samples;

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
      {
        std::lock_guard<std::mutex> lock(battery_mutex);
        battery.valid = true;
        battery.millivolts = filtered_millivolts;
        battery.percent = percent;
        battery.usb_power = reading.usb_power;
        battery.low = percent <= kLowBatteryPercent && !reading.usb_power;
      }
      if (!logged) {
        ESP_LOGI(kTag, "Battery %d mV (%d%%)%s", filtered_millivolts, percent,
                 reading.usb_power ? ", USB connected" : "");
        logged = true;
      }
      if (!reading.usb_power &&
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

} // namespace app
