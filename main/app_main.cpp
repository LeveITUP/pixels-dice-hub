#include "board.h"
#ifdef PIXELS_DEV_TOOLS
#include "dev_tools.h"
#endif
#include "dice_model.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "pixels_ble.h"
#include "power.h"
#include "preferences.h"
#include "status_led.h"
#include "theme.h"
#include "ui.h"
#include "web_server.h"

namespace {

constexpr char kTag[] = "pixels_display";

} // namespace

extern "C" void app_main() {
  esp_err_t result = nvs_flash_init();
  if (result == ESP_ERR_NVS_NO_FREE_PAGES ||
      result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    result = nvs_flash_init();
  }
  ESP_ERROR_CHECK(result);

  static board::Hardware hardware;
  static app::DiceModel model;
  app::RestorePreferences(&model);

  ESP_ERROR_CHECK(board::Init(&hardware));
  app::InitPower(&hardware, &model);
  app::InitTheme();
  ESP_ERROR_CHECK(app::StartStatusLed(&hardware, &model));
  ESP_ERROR_CHECK(app::StartUi(&hardware, &model));
#ifdef PIXELS_DEV_TOOLS
  // Dev builds are driven over the network, so never boot with Wi-Fi off.
  app::SetPixelsScannerWifiActive(true);
  ESP_ERROR_CHECK(app::SetWebNetworkEnabled(&model, true));
#else
  const bool wifi_enabled = app::RestoreWebNetworkEnabled(false);
  app::SetPixelsScannerWifiActive(wifi_enabled);
  if (wifi_enabled) {
    ESP_ERROR_CHECK(app::StartWebServer(&model));
  } else {
    ESP_LOGI(kTag, "Wi-Fi disabled; enable it from the touchscreen");
  }
#endif
  ESP_ERROR_CHECK(app::StartPixelsScanner(&model));
#ifdef PIXELS_DEV_TOOLS
  ESP_ERROR_CHECK(app::StartDevTools(&hardware, &model));
#endif
  ESP_LOGI(kTag, "Pixels dice display started (free internal heap %u B)",
           static_cast<unsigned>(esp_get_free_internal_heap_size()));
}
