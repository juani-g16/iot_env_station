#include "common.h"

static void time_sync_notification_cb(struct timeval *tv)
{
    const char *TAG = "SNTP Notification";
    ESP_LOGI(TAG, "Notification of a time synchronization event");
}

void setup_sntp(void)
{
    const char *TAG = "SNTP Initialization";
    ESP_LOGI(TAG, "Initializing SNTP");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_init();

    // Set timezone to Spanish Peninsula Standard Time
    setenv("TZ", "CET-1CEST,M3.5.0/2,M10.5.0/3", 1);
    tzset();

    time_t now = 0;
    struct tm timeinfo = {0};
    int retry = 0;
    const int retry_count = 10;

    while (timeinfo.tm_year < (2020 - 1900) && ++retry < retry_count)
    {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d)", retry);
        vTaskDelay(pdMS_TO_TICKS(2000));
        time(&now);
        localtime_r(&now, &timeinfo);
    }
}

void get_current_date_time(char *date_time)
{
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    strftime(date_time, ISO8601_STR_LEN, "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
}

char *create_json_payload(const dht_data_t *data)
{
    cJSON *root = cJSON_CreateObject();
    char tempStr[7], humStr[7];
    sprintf(tempStr, "%.2f", data->temperature);
    sprintf(humStr, "%.2f", data->humidity);

    cJSON_AddStringToObject(root, "temperature", tempStr);
    cJSON_AddStringToObject(root, "humidity", humStr);
    cJSON_AddStringToObject(root, "timestamp", data->timestamp);

    // Convert to string
    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    return json_str;
}