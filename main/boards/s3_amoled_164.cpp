#include "board_profile.h"

#include <algorithm>
#include <cstdint>

#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_sh8601.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "s3_amoled_164_i2c.h"

namespace board::profile {
namespace {

constexpr char kTag[] = "board_s3_amoled";
constexpr int kDisplayWidth = 280;
constexpr int kDisplayHeight = 456;
constexpr int kDrawBufferRows = 40;
constexpr int kDisplayOffsetX = 20;
constexpr int kDisplayResetPin = 21;
constexpr int kDisplayChipSelectPin = 46;
constexpr int kDisplayClockPin = 10;
constexpr int kDisplayData0Pin = 11;
constexpr int kDisplayData1Pin = 12;
constexpr int kDisplayData2Pin = 13;
constexpr int kDisplayData3Pin = 14;
uint8_t brightness_percent = 75;
bool imu_available;
bool touch_available;
uint32_t last_touch_error_ms;

const uint8_t kVendorC4[] = {0x80};
const uint8_t kEnableTearing[] = {0x00};
const uint8_t kControlDisplay[] = {0x20};
const uint8_t kVendorBrightness[] = {0xff};
const sh8601_lcd_init_cmd_t kDisplayInitCommands[] = {
    {0x11, nullptr, 0, 80},
    {0xc4, kVendorC4, sizeof(kVendorC4), 0},
    {0x35, kEnableTearing, sizeof(kEnableTearing), 0},
    {0x53, kControlDisplay, sizeof(kControlDisplay), 0},
    {0x63, kVendorBrightness, sizeof(kVendorBrightness), 0},
    {0x29, nullptr, 0, 10},
};

const Capabilities kCapabilities = {
    .name = "waveshare-s3-touch-amoled-1.64-v2",
    .portrait_width = kDisplayWidth,
    .portrait_height = kDisplayHeight,
    .draw_buffer_rows = kDrawBufferRows,
    .orientation_threshold = 1800,
    .orientation_hysteresis = 450,
    .has_display = true,
    .has_touch = true,
    .has_imu = true,
    .has_brightness = true,
    .has_status_led = false,
    .is_amoled = true,
};

esp_err_t InitDisplay(Hardware *hardware) {
  spi_bus_config_t bus_config = {};
  bus_config.sclk_io_num = kDisplayClockPin;
  bus_config.data0_io_num = kDisplayData0Pin;
  bus_config.data1_io_num = kDisplayData1Pin;
  bus_config.data2_io_num = kDisplayData2Pin;
  bus_config.data3_io_num = kDisplayData3Pin;
  bus_config.max_transfer_sz =
      kDisplayWidth * kDrawBufferRows * sizeof(uint16_t);
  ESP_RETURN_ON_ERROR(spi_bus_initialize(SPI2_HOST, &bus_config, SPI_DMA_CH_AUTO),
                      kTag, "Unable to initialize display bus");

  esp_lcd_panel_io_spi_config_t io_config = {};
  io_config.cs_gpio_num = kDisplayChipSelectPin;
  io_config.dc_gpio_num = -1;
  io_config.spi_mode = 0;
  io_config.pclk_hz = 40000000;
  io_config.trans_queue_depth = 10;
  io_config.lcd_cmd_bits = 32;
  io_config.lcd_param_bits = 8;
  io_config.flags.quad_mode = true;
  ESP_RETURN_ON_ERROR(
      esp_lcd_new_panel_io_spi(
          static_cast<esp_lcd_spi_bus_handle_t>(SPI2_HOST), &io_config,
          &hardware->panel_io),
      kTag,
      "Unable to initialize display IO");

  sh8601_vendor_config_t vendor_config = {
      .init_cmds = kDisplayInitCommands,
      .init_cmds_size = static_cast<uint16_t>(
          sizeof(kDisplayInitCommands) / sizeof(kDisplayInitCommands[0])),
      .flags = {.use_qspi_interface = true},
  };
  const esp_lcd_panel_dev_config_t panel_config = {
      .reset_gpio_num = kDisplayResetPin,
      .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
      .data_endian = LCD_RGB_DATA_ENDIAN_LITTLE,
      .bits_per_pixel = 16,
      .flags = {.reset_active_high = false},
      .vendor_config = &vendor_config,
  };
  ESP_RETURN_ON_ERROR(esp_lcd_new_panel_sh8601(
                          hardware->panel_io, &panel_config, &hardware->panel),
                      kTag, "Unable to create SH8601 panel");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(hardware->panel), kTag,
                      "Unable to reset SH8601 panel");
  ESP_RETURN_ON_ERROR(esp_lcd_panel_init(hardware->panel), kTag,
                      "Unable to initialize SH8601 panel");
  ESP_RETURN_ON_ERROR(
      esp_lcd_panel_set_gap(hardware->panel, kDisplayOffsetX, 0), kTag,
      "Unable to set SH8601 display offset");
  return esp_lcd_panel_disp_on_off(hardware->panel, true);
}

void RoundDisplayArea(lv_area_t *area) {
  area->x1 &= ~1;
  area->y1 &= ~1;
  area->x2 |= 1;
  area->y2 |= 1;
}

void ReadTouch(lv_indev_t *, lv_indev_data_t *data) {
  uint16_t x = 0;
  uint16_t y = 0;
  bool pressed = false;
  const esp_err_t result = s3_amoled_ft3168_read(&x, &y, &pressed);
  if (result != ESP_OK) {
    data->state = LV_INDEV_STATE_RELEASED;
    const uint32_t now_ms =
        static_cast<uint32_t>(esp_timer_get_time() / 1000);
    if (now_ms - last_touch_error_ms >= 5000) {
      ESP_LOGW(kTag, "FT3168 read failed: %s", esp_err_to_name(result));
      last_touch_error_ms = now_ms;
    }
    return;
  }
  data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
  if (pressed) {
    data->point.x = std::min<uint16_t>(x, kDisplayWidth - 1);
    data->point.y = std::min<uint16_t>(y, kDisplayHeight - 1);
  }
}

esp_err_t InitLvgl(Hardware *hardware) {
  lvgl_port_cfg_t lvgl_config = ESP_LVGL_PORT_INIT_CONFIG();
  lvgl_config.task_stack = 8192;
  lvgl_config.task_max_sleep_ms = 100;
  ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_config), kTag,
                      "Unable to initialize LVGL");

  lvgl_port_display_cfg_t display_config = {};
  display_config.io_handle = hardware->panel_io;
  display_config.panel_handle = hardware->panel;
  display_config.control_handle = hardware->panel;
  display_config.buffer_size = kDisplayWidth * kDrawBufferRows;
  display_config.double_buffer = true;
  display_config.hres = kDisplayWidth;
  display_config.vres = kDisplayHeight;
  display_config.rounder_cb = RoundDisplayArea;
  display_config.color_format = LV_COLOR_FORMAT_RGB565;
  display_config.flags.buff_dma = true;
  display_config.flags.sw_rotate = true;
  display_config.flags.swap_bytes = true;
  hardware->display = lvgl_port_add_disp(&display_config);
  ESP_RETURN_ON_FALSE(hardware->display != nullptr, ESP_ERR_NO_MEM, kTag,
                      "Unable to add LVGL display");

  if (touch_available) {
    lv_indev_t *input = lv_indev_create();
    ESP_RETURN_ON_FALSE(input != nullptr, ESP_ERR_NO_MEM, kTag,
                        "Unable to create LVGL touch input");
    lv_indev_set_type(input, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(input, hardware->display);
    lv_indev_set_read_cb(input, ReadTouch);
  }
  return ESP_OK;
}

esp_err_t WriteBrightness(Hardware *hardware, uint8_t percent) {
  ESP_RETURN_ON_FALSE(hardware != nullptr && hardware->panel_io != nullptr,
                      ESP_ERR_INVALID_STATE, kTag,
                      "Display IO is not initialized");
  const uint8_t value =
      static_cast<uint8_t>((static_cast<uint32_t>(percent) * 255U + 50U) /
                           100U);
  constexpr int kBrightnessCommand = (0x02 << 24) | (0x51 << 8);
  return esp_lcd_panel_io_tx_param(hardware->panel_io, kBrightnessCommand,
                                   &value, sizeof(value));
}

} // namespace

const Capabilities &GetCapabilities() { return kCapabilities; }

esp_err_t InitHardware(Hardware *hardware) {
  ESP_RETURN_ON_FALSE(hardware != nullptr, ESP_ERR_INVALID_ARG, kTag,
                      "Hardware state is required");
  ESP_RETURN_ON_ERROR(InitDisplay(hardware), kTag, "Display init failed");
  ESP_RETURN_ON_ERROR(s3_amoled_i2c_init(), kTag, "I2C init failed");
  const esp_err_t imu_result = s3_amoled_qmi8658_init();
  imu_available = imu_result == ESP_OK;
  if (imu_result != ESP_OK) {
    ESP_LOGW(kTag, "Continuing without orientation sensing: %s",
             esp_err_to_name(imu_result));
  } else {
    ESP_LOGI(kTag, "QMI8658 initialized at 0x6b");
  }
  const esp_err_t touch_result = s3_amoled_ft3168_init();
  touch_available = touch_result == ESP_OK;
  if (touch_result != ESP_OK) {
    ESP_LOGW(kTag, "Continuing without touch input: %s",
             esp_err_to_name(touch_result));
  } else {
    ESP_LOGI(kTag, "FT3168 initialized");
  }
  ESP_RETURN_ON_ERROR(InitLvgl(hardware), kTag, "LVGL init failed");
  return WriteBrightness(hardware, brightness_percent);
}

esp_err_t SetBrightness(Hardware *hardware, uint8_t percent) {
  brightness_percent = percent;
  return WriteBrightness(hardware, percent);
}

esp_err_t SetStatusLed(Hardware *, RgbColor) {
  return ESP_ERR_NOT_SUPPORTED;
}

bool ReadAcceleration(Hardware *, int16_t *x, int16_t *y, int16_t *z) {
  if (!imu_available) {
    return false;
  }
  int16_t sensor_x = 0;
  int16_t sensor_y = 0;
  int16_t sensor_z = 0;
  if (!s3_amoled_qmi8658_read(&sensor_x, &sensor_y, &sensor_z)) {
    return false;
  }
  *x = sensor_y;
  *y = sensor_x;
  *z = sensor_z;
  return true;
}

void SetOrientation(Hardware *hardware, Orientation orientation) {
  if (hardware == nullptr || hardware->display == nullptr) {
    return;
  }
  const lv_display_rotation_t rotations[] = {
      LV_DISPLAY_ROTATION_0, LV_DISPLAY_ROTATION_90, LV_DISPLAY_ROTATION_180,
      LV_DISPLAY_ROTATION_270};
  lv_display_set_rotation(hardware->display,
                          rotations[static_cast<uint8_t>(orientation)]);
}

} // namespace board::profile
