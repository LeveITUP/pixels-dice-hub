#include "dev_tools.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pixels_protocol.h"
#include "preferences.h"
#include "ui.h"
#include "web_server.h"

namespace app {
namespace {

constexpr char kTag[] = "dev_tools";

board::Hardware *hardware;
DiceModel *model;
httpd_handle_t server;

// Synthetic pointer state read by the injected LVGL input device.
struct Gesture {
  int x0, y0, x1, y1;
  int64_t start_us;
  int64_t end_us;
};
std::atomic<bool> gesture_active{false};
Gesture gesture{};

uint64_t NowMs() { return esp_timer_get_time() / 1000; }

void ReadInjected(lv_indev_t *, lv_indev_data_t *data) {
  if (!gesture_active.load()) {
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }
  const int64_t now = esp_timer_get_time();
  if (now >= gesture.end_us) {
    gesture_active.store(false);
    data->point.x = gesture.x1;
    data->point.y = gesture.y1;
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }
  const int64_t span = gesture.end_us - gesture.start_us;
  const int64_t t = span > 0 ? (now - gesture.start_us) * 1000 / span : 1000;
  data->point.x = gesture.x0 + (gesture.x1 - gesture.x0) * t / 1000;
  data->point.y = gesture.y0 + (gesture.y1 - gesture.y0) * t / 1000;
  data->state = LV_INDEV_STATE_PRESSED;
}

bool Query(httpd_req_t *req, const char *key, char *out, size_t size) {
  char query[256];
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
    return false;
  }
  return httpd_query_key_value(query, key, out, size) == ESP_OK;
}

int QueryInt(httpd_req_t *req, const char *key, int fallback) {
  char value[32];
  return Query(req, key, value, sizeof(value)) ? atoi(value) : fallback;
}

esp_err_t Ok(httpd_req_t *req, const char *text = "ok") {
  httpd_resp_set_type(req, "text/plain");
  return httpd_resp_send(req, text, HTTPD_RESP_USE_STRLEN);
}

esp_err_t Gesture_(httpd_req_t *req, int x0, int y0, int x1, int y1,
                   int ms) {
  const int64_t now = esp_timer_get_time();
  gesture = {x0, y0, x1, y1, now, now + static_cast<int64_t>(ms) * 1000};
  gesture_active.store(true);
  vTaskDelay(pdMS_TO_TICKS(ms + 150));
  return Ok(req);
}

esp_err_t TapHandler(httpd_req_t *req) {
  const int x = QueryInt(req, "x", 0);
  const int y = QueryInt(req, "y", 0);
  return Gesture_(req, x, y, x, y, QueryInt(req, "ms", 80));
}

esp_err_t DragHandler(httpd_req_t *req) {
  return Gesture_(req, QueryInt(req, "x0", 0), QueryInt(req, "y0", 0),
                  QueryInt(req, "x1", 0), QueryInt(req, "y1", 0),
                  QueryInt(req, "ms", 300));
}

pixels::DieType ParseType(const char *text) {
  struct Entry {
    const char *name;
    pixels::DieType type;
  };
  static const Entry kEntries[] = {
      {"d4", pixels::DieType::kD4},     {"d6", pixels::DieType::kD6},
      {"d8", pixels::DieType::kD8},     {"d10", pixels::DieType::kD10},
      {"d00", pixels::DieType::kD00},   {"d12", pixels::DieType::kD12},
      {"d20", pixels::DieType::kD20},   {"d6p", pixels::DieType::kD6Pipped},
      {"d6f", pixels::DieType::kD6Fudge}, {"unknown", pixels::DieType::kUnknown},
  };
  for (const auto &entry : kEntries) {
    if (strcasecmp(entry.name, text) == 0) {
      return entry.type;
    }
  }
  return pixels::DieType::kD20;
}

pixels::RollState ParseState(const char *text) {
  if (strcasecmp(text, "rolled") == 0) return pixels::RollState::kRolled;
  if (strcasecmp(text, "handling") == 0) return pixels::RollState::kHandling;
  if (strcasecmp(text, "rolling") == 0) return pixels::RollState::kRolling;
  if (strcasecmp(text, "crooked") == 0) return pixels::RollState::kCrooked;
  if (strcasecmp(text, "onface") == 0) return pixels::RollState::kOnFace;
  return pixels::RollState::kUnknown;
}

uint8_t LedCount(pixels::DieType type) {
  switch (type) {
  case pixels::DieType::kD4: return 4;
  case pixels::DieType::kD6:
  case pixels::DieType::kD6Pipped:
  case pixels::DieType::kD6Fudge: return 6;
  case pixels::DieType::kD8: return 8;
  case pixels::DieType::kD10:
  case pixels::DieType::kD00: return 10;
  case pixels::DieType::kD12: return 12;
  default: return 20;
  }
}

esp_err_t DieHandler(httpd_req_t *req) {
  char text[32];
  pixels::Advertisement ad{};
  ad.pixel_id = static_cast<uint32_t>(strtoul(
      Query(req, "id", text, sizeof(text)) ? text : "0", nullptr, 16));
  if (ad.pixel_id == 0) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "id required");
    return ESP_OK;
  }
  ad.die_type = ParseType(Query(req, "type", text, sizeof(text)) ? text : "d20");
  ad.roll_state =
      ParseState(Query(req, "state", text, sizeof(text)) ? text : "onface");
  ad.led_count = LedCount(ad.die_type);
  ad.face_index = static_cast<uint8_t>(QueryInt(req, "face", 0));
  ad.battery_percent = static_cast<uint8_t>(QueryInt(req, "bat", 80));
  ad.charging = QueryInt(req, "chg", 0) != 0;
  ad.rssi = QueryInt(req, "rssi", -60);
  ad.colorway = static_cast<uint8_t>(QueryInt(req, "color", 1));
  ad.firmware_timestamp = 1700000000;
  ad.has_identity = true;
  ad.has_status = true;
  if (Query(req, "name", text, sizeof(text))) {
    strlcpy(ad.name, text, sizeof(ad.name));
  } else {
    snprintf(ad.name, sizeof(ad.name), "Die %lx",
             static_cast<unsigned long>(ad.pixel_id & 0xfff));
  }
  model->Ingest(ad, NowMs());
  if (QueryInt(req, "pair", 0) != 0) {
    model->Pair(ad.pixel_id);
  }
  return Ok(req);
}

esp_err_t UnpairHandler(httpd_req_t *req) {
  char text[32];
  const uint32_t id = static_cast<uint32_t>(strtoul(
      Query(req, "id", text, sizeof(text)) ? text : "0", nullptr, 16));
  model->Unpair(id);
  return Ok(req);
}

esp_err_t SaveHandler(httpd_req_t *req) {
  return Ok(req, SavePreferences(*model) ? "ok" : "failed");
}

// Internal-heap snapshot; also dumps the per-task list to the serial log.
esp_err_t HeapHandler(httpd_req_t *req) {
  char text[160];
  std::snprintf(text, sizeof(text),
                "internal free=%u largest=%u min=%u | psram free=%u\n",
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  heap_caps_print_heap_info(MALLOC_CAP_INTERNAL);
  return Ok(req, text);
}

esp_err_t OrientHandler(httpd_req_t *req) {
  SetUiOrientation(
      static_cast<board::Orientation>(QueryInt(req, "o", 0) & 3));
  vTaskDelay(pdMS_TO_TICKS(300));
  return Ok(req);
}

// Streams the active screen as an uncompressed 24-bit bottom-up BMP.
esp_err_t ShotHandler(httpd_req_t *req) {
  if (!lvgl_port_lock(2000)) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "lvgl busy");
    return ESP_OK;
  }
  lv_draw_buf_t *buf =
      lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB565);
  lvgl_port_unlock();
  if (buf == nullptr) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "snapshot");
    return ESP_OK;
  }
  const uint32_t w = buf->header.w;
  const uint32_t h = buf->header.h;
  const uint32_t stride = buf->header.stride;
  const uint32_t row_bytes = (w * 3 + 3) & ~3u;
  const uint32_t data_size = row_bytes * h;

  uint8_t header[54] = {};
  header[0] = 'B';
  header[1] = 'M';
  auto put32 = [&](int offset, uint32_t value) {
    for (int i = 0; i < 4; ++i) header[offset + i] = (value >> (8 * i)) & 0xff;
  };
  put32(2, 54 + data_size);
  put32(10, 54);
  put32(14, 40);
  put32(18, w);
  put32(22, h);
  header[26] = 1;
  header[28] = 24;
  put32(34, data_size);
  put32(38, 2835);
  put32(42, 2835);

  // One large (PSRAM) buffer: row-sized chunks stall on Nagle/delayed ACK.
  uint8_t *image = static_cast<uint8_t *>(malloc(sizeof(header) + data_size));
  if (image == nullptr) {
    lv_draw_buf_destroy(buf);
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no mem");
    return ESP_OK;
  }
  memcpy(image, header, sizeof(header));
  for (uint32_t y = 0; y < h; ++y) {
    const uint16_t *src = reinterpret_cast<const uint16_t *>(
        static_cast<const uint8_t *>(buf->data) + y * stride);
    uint8_t *row = image + sizeof(header) + (h - 1 - y) * row_bytes;
    memset(row, 0, row_bytes);
    for (uint32_t x = 0; x < w; ++x) {
      const uint16_t px = src[x];
      const uint8_t r = (px >> 11) & 0x1f;
      const uint8_t g = (px >> 5) & 0x3f;
      const uint8_t b = px & 0x1f;
      row[x * 3 + 0] = (b << 3) | (b >> 2);
      row[x * 3 + 1] = (g << 2) | (g >> 4);
      row[x * 3 + 2] = (r << 3) | (r >> 2);
    }
  }
  lv_draw_buf_destroy(buf);
  httpd_resp_set_type(req, "image/bmp");
  const esp_err_t result = httpd_resp_send(
      req, reinterpret_cast<const char *>(image), sizeof(header) + data_size);
  free(image);
  return result;
}

esp_err_t StartHttp() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 8080;
  config.ctrl_port = 32769;
  config.stack_size = 8192;
  config.max_uri_handlers = 9;
  ESP_RETURN_ON_ERROR(httpd_start(&server, &config), kTag, "httpd");

  const httpd_uri_t uris[] = {
      {.uri = "/shot", .method = HTTP_GET, .handler = ShotHandler,
       .user_ctx = nullptr},
      {.uri = "/tap", .method = HTTP_GET, .handler = TapHandler,
       .user_ctx = nullptr},
      {.uri = "/drag", .method = HTTP_GET, .handler = DragHandler,
       .user_ctx = nullptr},
      {.uri = "/die", .method = HTTP_GET, .handler = DieHandler,
       .user_ctx = nullptr},
      {.uri = "/unpair", .method = HTTP_GET, .handler = UnpairHandler,
       .user_ctx = nullptr},
      {.uri = "/orient", .method = HTTP_GET, .handler = OrientHandler,
       .user_ctx = nullptr},
      {.uri = "/save", .method = HTTP_GET, .handler = SaveHandler,
       .user_ctx = nullptr},
      {.uri = "/heap", .method = HTTP_GET, .handler = HeapHandler,
       .user_ctx = nullptr},
  };
  for (const auto &uri : uris) {
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &uri), kTag,
                        "register %s", uri.uri);
  }
  ESP_LOGW(kTag, "Dev tools listening on :8080 (not for release builds)");
  return ESP_OK;
}

// Waits for the network to come up (it may be enabled from the touchscreen
// after boot) before binding the dev server.
void DevTask(void *) {
  while (true) {
    if (server == nullptr && GetWebNetworkStatus() != NetworkStatus::kOff) {
      if (StartHttp() != ESP_OK) {
        server = nullptr;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

} // namespace

esp_err_t StartDevTools(board::Hardware *board_hardware,
                        DiceModel *dice_model) {
  hardware = board_hardware;
  model = dice_model;

  if (lvgl_port_lock(1000)) {
    lv_indev_t *input = lv_indev_create();
    lv_indev_set_type(input, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(input, hardware->display);
    lv_indev_set_read_cb(input, ReadInjected);
    lvgl_port_unlock();
  }
  return xTaskCreate(DevTask, "dev_tools", 4096, nullptr, 3, nullptr) ==
                 pdPASS
             ? ESP_OK
             : ESP_ERR_NO_MEM;
}

} // namespace app
