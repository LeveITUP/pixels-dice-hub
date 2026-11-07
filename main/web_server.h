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
NetworkStatus GetWebNetworkStatus();
// Formats the address the web UI is reachable at: the station IP when joined
// to a home network, otherwise the access point address.
const char *GetWebAddress(char *buffer, std::size_t size);
const char *NetworkStatusName(NetworkStatus status);

} // namespace app
