#ifndef DISPLAY_H
#define DISPLAY_H

#include "driver/i2c_master.h"
#include "u8g2.h"
#include "u8g2_esp32_hal.h"
#include "esp_log.h"

#define I2C_SDA 8  /**< I2C SDA (Serial Data) pin number */
#define I2C_SCL 9  /**< I2C SCL (Serial Clock) pin number */

/**
 * @fn u8g2_t init_oled_display(void)
 * @brief Initializes the OLED display using the u8g2 graphics library
 * 
 * This function sets up the I2C communication interface and initializes
 * the OLED display using the u8g2 library. It configures the display
 * for subsequent drawing operations.
 * 
 * @return u8g2_t structure containing the display configuration and state.
 *         This structure is used for all subsequent display operations.
 */
u8g2_t init_oled_display(void);

/**
 * @fn void show_dht_data(u8g2_t u8g2, const char *date, const char *time, const char *temp, const char *hum)
 * @brief Displays DHT sensor data on the OLED screen
 * 
 * This function renders the date, time, temperature, and humidity values
 * on the OLED display using the u8g2 graphics library. It formats and
 * positions the text appropriately on the screen.
 * 
 * @param u8g2 The u8g2 display structure initialized by init_oled_display()
 * @param date Pointer to a null-terminated string containing the date information
 * @param time Pointer to a null-terminated string containing the time information
 * @param temp Pointer to a null-terminated string containing the temperature reading
 * @param hum Pointer to a null-terminated string containing the humidity reading
 */
void show_dht_data(u8g2_t u8g2, const char *date, const char *time, const char *temp, const char *hum);

#endif