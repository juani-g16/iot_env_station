#ifndef DHT_SENSOR_H
#define DHT_SENSOR_H

#include "dht.h"

#define CONFIG_EXAMPLE_INTERNAL_PULLUP 0
#define CONFIG_EXAMPLE_TYPE_DHT11 0
#define ISO8601_STR_LEN 25 // "YYYY-MM-DDTHH:MM:SSZ" + null
#define DHT_SENSOR_TYPE (CONFIG_EXAMPLE_TYPE_AM2301 ? DHT_TYPE_AM2301 : CONFIG_EXAMPLE_TYPE_DHT11 ? DHT_TYPE_DHT11 : DHT_TYPE_SI7021)

typedef struct
{
    float temperature;
    float humidity;
    char timestamp[ISO8601_STR_LEN]; // ISO 8601 format
} dht_data_t;

esp_err_t dht_init(void);

#endif