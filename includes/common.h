#ifndef COMMON_H
#define COMMON_H

#include "dht_manager.h"
#include "esp_sntp.h"
#include "cJSON.h"
#include "esp_log.h"

#define MEASURE_INTERVAL 60 * 1000
#define WIFI_RECONNECT_INTERVAL_MS 60 * 1000
#define TIMER_ID 1
#define STACK_SIZE 4 * 1024
#define MAX_Q_SIZE 10

/**
 * @fn void setup_sntp(void)
 * @brief Initializes and configures the SNTP (Simple Network Time Protocol) client
 * 
 * This function sets up the SNTP client to synchronize the system time with
 * internet time servers. It configures the timezone and starts the SNTP service.
 */
void setup_sntp(void);

/**
 * @fn void get_current_date_time(char *date_time)
 * @brief Retrieves the current date and time in ISO 8601 format
 * 
 * This function gets the current system time and formats it as an ISO 8601
 * timestamp string (YYYY-MM-DDTHH:MM:SSZ format).
 * 
 * @param date_time Pointer to a character buffer where the formatted date/time
 *                  string will be stored. Buffer must be at least ISO8601_STR_LEN
 *                  bytes in size.
 */
void get_current_date_time(char *date_time);

/**
 * @fn char *create_json_payload(const dht_data_t *data)
 * @brief Creates a JSON payload string from DHT sensor data
 * 
 * This function takes DHT sensor data (temperature, humidity, timestamp) and
 * creates a JSON formatted string suitable for transmission via MQTT or other
 * communication protocols.
 * 
 * @param data Pointer to a dht_data_t structure containing sensor readings
 *             and timestamp information
 * @return Pointer to a dynamically allocated string containing the JSON payload.
 *         The caller is responsible for freeing this memory. Returns NULL on error.
 */
char *create_json_payload(const dht_data_t *data);

#endif