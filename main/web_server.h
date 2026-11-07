#pragma once

#include "dice_model.h"
#include "esp_err.h"

#include <cstddef>

namespace app {

enum class NetworkStatus {
  kOff,
  kAccessPoint,
  kConnecting,
  kConnected,
};

esp_err_t StartWebServer(DiceModel *model);
esp_err_t StopWebServer();
esp_err_t SetWebNetworkEnabled(DiceModel *model, bool enabled);
bool RestoreWebNetworkEnabled(bool default_value);
bool IsWebNetworkEnabled();
// Saved home-network credentials exist in NVS.
bool HasWebStationCredentials();
// Whether the hub should join the saved home network (persisted). When false
// the hub stays in access-point-only mode even if credentials are saved.
bool IsWebStationEnabled();
esp_err_t SetWebStationEnabled(bool enabled);
// Forget the saved home network and fall back to the access point.
esp_err_t ClearWebStationCredentials();
// Steps OFF -> AP -> NET (when credentials are saved) -> OFF. With
// allow_off=false the cycle wraps from NET back to AP instead.
esp_err_t CycleWebNetworkMode(DiceModel *model, bool allow_off);
NetworkStatus GetWebNetworkStatus();
// Formats the address the web UI is reachable at: the station IP when joined
// to a home network, otherwise the access point address.
const char *GetWebAddress(char *buffer, std::size_t size);
const char *NetworkStatusName(NetworkStatus status);

} // namespace app
