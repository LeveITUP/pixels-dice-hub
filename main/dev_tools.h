#pragma once

// Development-only helpers, compiled in with `-D PIXELS_DEV_TOOLS=1`.
//
// Serves a small HTTP API on port 8080 (requires Wi-Fi to be up):
//   GET /shot                  -> 24-bit BMP of the current screen
//   GET /tap?x=&y=[&ms=]       -> synthetic touch press at display coordinates
//   GET /drag?x0=&y0=&x1=&y1=[&ms=]
//   GET /die?id=&type=&state=&face=[&bat=&name=&rssi=&pair=1]
//                              -> feed a synthetic Pixels advertisement into
//                                 the model (and optionally pair it)
//   GET /unpair?id=            -> unpair a die
//   GET /save                  -> persist the current pairings to NVS
//   GET /orient?o=0..3         -> force display orientation (ignores IMU)
//
// Used to capture documentation screenshots; never built into release
// firmware.

#include "board.h"
#include "dice_model.h"
#include "esp_err.h"

namespace app {

esp_err_t StartDevTools(board::Hardware *hardware, DiceModel *model);

} // namespace app
