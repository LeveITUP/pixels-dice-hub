#include "power.h"

#include <atomic>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pixels_ble.h"
#include "preferences.h"
#include "web_server.h"

namespace app {
namespace {

constexpr char kTag[] = "power";
board::Hardware *hardware;
DiceModel *model;
std::atomic_bool power_off_pending{false};

void PowerOffTask(void *) {
  // Give the UI a moment to paint the shutdown overlay.
  vTaskDelay(pdMS_TO_TICKS(400));
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

} // namespace

void InitPower(board::Hardware *hw, DiceModel *dice_model) {
  hardware = hw;
  model = dice_model;
  ESP_LOGI(kTag, "Wake reason: %s",
           board::WakeReasonName(board::GetWakeReason()));
}

bool IsPowerOffSupported() {
  return board::GetCapabilities().has_power_off;
}

void RequestPowerOff() {
  if (!IsPowerOffSupported() || power_off_pending.exchange(true)) {
    return;
  }
  ESP_LOGI(kTag, "Power off requested");
  if (xTaskCreate(PowerOffTask, "power_off", 4096, nullptr, 5, nullptr) !=
      pdPASS) {
    ESP_LOGE(kTag, "Unable to start power-off task");
    power_off_pending.store(false);
  }
}

bool IsPowerOffPending() { return power_off_pending.load(); }

} // namespace app
