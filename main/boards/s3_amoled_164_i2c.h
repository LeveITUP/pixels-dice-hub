#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t s3_amoled_i2c_init(void);
esp_err_t s3_amoled_qmi8658_init(void);
bool s3_amoled_qmi8658_read(int16_t *x, int16_t *y, int16_t *z);
esp_err_t s3_amoled_ft3168_init(void);
esp_err_t s3_amoled_ft3168_read(uint16_t *x, uint16_t *y, bool *pressed);

#ifdef __cplusplus
}
#endif
