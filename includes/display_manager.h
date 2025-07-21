#ifndef DISPLAY_H
#define DISPLAY_H

#include "driver/i2c_master.h"
#include "u8g2.h"
#include "u8g2_esp32_hal.h"
#include "esp_log.h"

#define I2C_SDA 8
#define I2C_SCL 9

u8g2_t init_oled_display(void);
void show_dht_data(u8g2_t u8g2, const char *date, const char *time, const char *temp, const char *hum);

#endif