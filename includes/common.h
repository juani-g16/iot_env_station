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

void setup_sntp(void);
void get_current_date_time(char *date_time);
char *create_json_payload(const dht_data_t *data);

#endif