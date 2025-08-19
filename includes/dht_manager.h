#ifndef DHT_SENSOR_H
#define DHT_SENSOR_H

#include "dht.h"

#define CONFIG_EXAMPLE_INTERNAL_PULLUP 0
#define CONFIG_EXAMPLE_TYPE_DHT11 0
#define ISO8601_STR_LEN 25 // "YYYY-MM-DDTHH:MM:SSZ" + null
#define DHT_SENSOR_TYPE (CONFIG_EXAMPLE_TYPE_AM2301 ? DHT_TYPE_AM2301 : CONFIG_EXAMPLE_TYPE_DHT11 ? DHT_TYPE_DHT11 : DHT_TYPE_SI7021)

/**
 * @brief Structure to hold DHT sensor data with timestamp
 * 
 * This structure contains temperature and humidity readings from a DHT sensor
 * along with an ISO 8601 formatted timestamp indicating when the reading was taken.
 */
typedef struct
{
    float temperature;              /**< Temperature reading in degrees Celsius */
    float humidity;                 /**< Relative humidity reading in percentage (0-100%) */
    char timestamp[ISO8601_STR_LEN]; /**< ISO 8601 format timestamp (YYYY-MM-DDTHH:MM:SSZ) */
} dht_data_t;

/**
 * @fn esp_err_t setup_dht(void)
 * @brief Initializes the DHT sensor for temperature and humidity readings
 * 
 * This function configures the DHT sensor (DHT11, DHT22, AM2301, or SI7021)
 * based on the compile-time configuration. It sets up the GPIO pin and
 * prepares the sensor for data acquisition.
 * 
 * @return ESP_OK on successful initialization, ESP_FAIL or other ESP error codes on failure
 */
esp_err_t setup_dht(void);

#endif