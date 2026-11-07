# Web UI

The firmware serves one responsive HTML page directly from program flash. It
does not use a filesystem, JavaScript framework, WebSocket library, or external
web assets.

## Runtime design

- The browser opens `/api/events` as a server-sent event stream and fetches a
  compact `/api/state` JSON snapshot when the model changes.
- A periodic HTTP refresh remains as a fallback when SSE is unavailable or
  disconnected.
- The existing fixed-size dice model supplies dashboard and pairing data.
- A 20-entry in-memory ring buffer records transitions into the completed-roll
  state for paired dice.
- Each paired die also has its own 20-entry in-memory history. Selecting a die
  card opens that die's newest-first roll list and briefly blinks the physical
  die. The detail view includes battery level, charging state, live roll state,
  signal strength, online status, firmware version/build, installed profile
  hash, available flash, and MCU/battery temperatures. Closing the detail view
  blinks it again.
- Blink and information requests enter one bounded BLE command queue. Commands
  connect to one die at a time, pause scanning, complete or time out, disconnect,
  and then resume scanning.
- Pair, remove, aggregate-mode, and clear-round actions use `/api/action`.
- The page follows the touchscreen layout: a home view (dice grid, aggregate,
  guided calculator, overall history) and a settings view behind the header
  cog. Settings list `Pair dice` first (its own sub-view), then Wi-Fi with the
  current address, status light (where fitted), theme, auto sleep, power off
  (battery hubs only) and factory reset. Views are addressed by URL hash
  (`#settings`, `#pair`) so the browser back button works.
- `/api/state` includes `theme` (index, name, light flag, colour palette, tile
  colours, available names), `idleSleep` (minutes, options), `powerOff`
  (supported) and `network` (status, address). The page recolours itself from
  the palette so it always matches the theme selected on the hub.
- Settings actions: `op=theme&value=N`, `op=sleep&value=MINUTES`,
  `op=wifiMode` (AP <-> saved network; the page never switches Wi-Fi off
  because that would stop the server mid-request), `op=wifiForget` (clear saved
  credentials and stay on the hotspot), `op=powerOff`, `op=factoryReset`
  (destructive ones prompt for confirmation in the page). `network` also
  reports `hasCredentials` and `canTurnOff`. Changes made on the touchscreen bump the SSE revision so open pages
  follow immediately.
- Dice cards show the current roll state with state-colored highlighting.
- The aggregate counts every completed roll until manually cleared; history is
  retained when a round is cleared.
- `/api/wifi` stores optional station credentials in NVS.
- While Wi-Fi is enabled, the `Pixels-Dice` WPA2 access point remains active
  in AP+STA mode so a failed home-network configuration cannot make the device
  unreachable.
- The touchscreen build defaults Wi-Fi off. Its settings Wi-Fi row cycles
  `OFF -> AP -> NET -> OFF` (`NET` only when a home network is saved) and the
  choice persists (`sta_on` in NVS). The header reports `OFF`, `AP`, `...`, or
  `NET`, and the settings page shows the reachable `http://` address under
  the Wi-Fi row.
- The access point uses `10.47.1.1/24` rather than the ESP-IDF default
  `192.168.4.0/24`, which commonly collides with home LANs and VLANs and causes
  clients to route hub traffic elsewhere.
- The headless build always enables Wi-Fi because the web page is its only UI.
- BLE scanning uses a 95% duty cycle while touchscreen Wi-Fi is off and 50%
  while Wi-Fi and BLE share the radio.

SSE was selected over WebSockets because dashboard updates are one-way. The
server supports one event-stream client and sends revision notifications rather
than duplicating the full state payload in the stream. A five-second heartbeat
keeps relative ages and offline state current. Additional clients continue to
work through the HTTP fallback.

## Persistence

Paired IDs, aggregate mode, touchscreen Wi-Fi enablement, and Wi-Fi credentials
survive reboot. Overall and per-die roll histories do not: persisting every
roll would create unnecessary flash wear and would require defining rollover
and retention behavior.

## Constraints

- The S3 AMOLED build must let `malloc` fall back to PSRAM
  (`CONFIG_SPIRAM_USE_MALLOC`, with Wi-Fi/LWIP and NimBLE buffers allowed in
  external RAM and LVGL using the C allocator). With the display, LVGL and BLE
  all resident, internal RAM alone is not enough for the Wi-Fi driver's RX
  buffers and `esp_wifi_start` fails with `ESP_ERR_NO_MEM`.

- Wi-Fi and BLE share the 2.4 GHz radio. Normal use is expected to work, but an
  eight-die soak test is required to quantify missed advertisements.
- The page is HTTP-only and has no user authentication. Use it on a trusted
  local network and do not expose it through router port forwarding.
- The classic ESP32 build disables Wi-Fi IRAM optimizations and uses size
  optimization to preserve instruction RAM. This trades maximum network
  throughput for additional firmware headroom, which is appropriate for the
  small dashboard responses.
