#include "s3_amoled_164_i2c.h"

#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define I2C_PORT I2C_NUM_0
#define I2C_SDA GPIO_NUM_47
#define I2C_SCL GPIO_NUM_48
#define QMI8658_ADDRESS 0x6b
#define FT3168_ADDRESS 0x38

static esp_err_t write_register(uint8_t address, uint8_t reg, uint8_t value) {
  const uint8_t data[] = {reg, value};
  return i2c_master_write_to_device(I2C_PORT, address, data, sizeof(data),
                                    pdMS_TO_TICKS(100));
}

static esp_err_t read_registers(uint8_t address, uint8_t reg, uint8_t *data,
                                size_t length) {
  return i2c_master_write_read_device(I2C_PORT, address, &reg, 1, data, length,
                                      pdMS_TO_TICKS(20));
}

esp_err_t s3_amoled_i2c_init(void) {
  const i2c_config_t config = {
      .mode = I2C_MODE_MASTER,
      .sda_io_num = I2C_SDA,
      .scl_io_num = I2C_SCL,
      .sda_pullup_en = GPIO_PULLUP_ENABLE,
      .scl_pullup_en = GPIO_PULLUP_ENABLE,
      .master = {.clk_speed = 100000},
      .clk_flags = 0,
  };
  esp_err_t result = i2c_param_config(I2C_PORT, &config);
  if (result != ESP_OK) {
    return result;
  }
  return i2c_driver_install(I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
}

esp_err_t s3_amoled_qmi8658_init(void) {
  uint8_t id = 0;
  esp_err_t result = read_registers(QMI8658_ADDRESS, 0x00, &id, 1);
  if (result != ESP_OK) {
    return result;
  }
  if (id != 0x05) {
    return ESP_ERR_NOT_FOUND;
  }

  result = write_register(QMI8658_ADDRESS, 0x60, 0xb0);
  if (result != ESP_OK) {
    return result;
  }
  vTaskDelay(pdMS_TO_TICKS(100));
  result = write_register(QMI8658_ADDRESS, 0x02, 0x40);
  if (result != ESP_OK) {
    return result;
  }
  result = write_register(QMI8658_ADDRESS, 0x03, 0x04);
  if (result != ESP_OK) {
    return result;
  }
  result = write_register(QMI8658_ADDRESS, 0x08, 0x01);
  if (result == ESP_OK) {
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  return result;
}

bool s3_amoled_qmi8658_read(int16_t *x, int16_t *y, int16_t *z) {
  uint8_t status = 0;
  if (x == NULL || y == NULL || z == NULL ||
      read_registers(QMI8658_ADDRESS, 0x2e, &status, 1) != ESP_OK ||
      (status & 0x01U) == 0) {
    return false;
  }

  uint8_t raw[6] = {0};
  if (read_registers(QMI8658_ADDRESS, 0x35, raw, sizeof(raw)) != ESP_OK) {
    return false;
  }
  *x = (int16_t)(raw[0] | (raw[1] << 8U));
  *y = (int16_t)(raw[2] | (raw[3] << 8U));
  *z = (int16_t)(raw[4] | (raw[5] << 8U));
  return true;
}

esp_err_t s3_amoled_ft3168_init(void) {
  return write_register(FT3168_ADDRESS, 0x00, 0x00);
}

esp_err_t s3_amoled_ft3168_read(uint16_t *x, uint16_t *y, bool *pressed) {
  if (x == NULL || y == NULL || pressed == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  uint8_t points = 0;
  esp_err_t result = read_registers(FT3168_ADDRESS, 0x02, &points, 1);
  if (result != ESP_OK) {
    vTaskDelay(pdMS_TO_TICKS(1));
    result = read_registers(FT3168_ADDRESS, 0x02, &points, 1);
    if (result != ESP_OK) {
      return result;
    }
  }
  *pressed = points != 0;
  if (!*pressed) {
    return ESP_OK;
  }

  uint8_t raw[4] = {0};
  result = read_registers(FT3168_ADDRESS, 0x03, raw, sizeof(raw));
  if (result != ESP_OK) {
    vTaskDelay(pdMS_TO_TICKS(1));
    result = read_registers(FT3168_ADDRESS, 0x03, raw, sizeof(raw));
    if (result != ESP_OK) {
      return result;
    }
  }
  *x = (uint16_t)(((raw[0] & 0x0fU) << 8U) | raw[1]);
  *y = (uint16_t)(((raw[2] & 0x0fU) << 8U) | raw[3]);
  return ESP_OK;
}

esp_err_t s3_amoled_ft3168_hibernate(void) {
  // PMODE register: 0x03 = hibernate, only a hardware reset wakes it.
  return write_register(FT3168_ADDRESS, 0xa5, 0x03);
}

static esp_err_t qmi8658_ctrl9_command(uint8_t command) {
  esp_err_t result = write_register(QMI8658_ADDRESS, 0x0a, command);
  if (result != ESP_OK) {
    return result;
  }
  // Wait for CTRL9 CmdDone (STATUSINT bit7) then acknowledge.
  for (int attempt = 0; attempt < 20; ++attempt) {
    uint8_t status = 0;
    if (read_registers(QMI8658_ADDRESS, 0x2d, &status, 1) == ESP_OK &&
        (status & 0x80U) != 0) {
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(2));
  }
  result = write_register(QMI8658_ADDRESS, 0x0a, 0x00);
  vTaskDelay(pdMS_TO_TICKS(2));
  return result;
}

esp_err_t s3_amoled_qmi8658_enable_wake_on_motion(uint8_t threshold_mg) {
  // Disable sensors while reconfiguring.
  esp_err_t result = write_register(QMI8658_ADDRESS, 0x08, 0x00);
  if (result != ESP_OK) {
    return result;
  }
  // CTRL1: address auto-increment, INT1 push-pull enabled.
  result = write_register(QMI8658_ADDRESS, 0x02, 0x48);
  if (result != ESP_OK) {
    return result;
  }
  // CTRL2: accel +-2g, low-power 21 Hz ODR.
  result = write_register(QMI8658_ADDRESS, 0x03, 0x0d);
  if (result != ESP_OK) {
    return result;
  }
  // CAL1_L = threshold (1 mg/LSB); CAL1_H = INT1 select, initial level low,
  // blanking time 4 samples.
  result = write_register(QMI8658_ADDRESS, 0x0b, threshold_mg);
  if (result != ESP_OK) {
    return result;
  }
  result = write_register(QMI8658_ADDRESS, 0x0c, 0x80U | 0x04U);
  if (result != ESP_OK) {
    return result;
  }
  result = qmi8658_ctrl9_command(0x19); // CTRL_CMD_WRITE_WOM_SETTING
  if (result != ESP_OK) {
    return result;
  }
  // CTRL7: accel enabled only.
  return write_register(QMI8658_ADDRESS, 0x08, 0x01);
}
