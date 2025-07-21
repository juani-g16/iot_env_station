#include <stdio.h>
#include <string.h>
#include <time.h>

#include "dht_manager.h"
#include "display_manager.h"
#include "wifi_manager.h"
#include "mqtt_manager.h"
#include "common.h"

#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/queue.h"

#include "esp_system.h"
#include "esp_event.h"
#include "nvs_flash.h"

extern EventGroupHandle_t s_wifi_event_group;
extern bool MQTT_CONNECTED;
extern esp_mqtt_client_handle_t client;

TimerHandle_t timerDHT;
QueueHandle_t displayQueue;
QueueHandle_t mqttQueue;

static const char *TAG = "iot_env_station";

esp_err_t setup_timer(void);
esp_err_t create_tasks(void);

void measure_temp_hum(TimerHandle_t timer);
void task_show_data_oled(void *args);
void task_send_data_mqtt(void *args);
void task_wifi(void *args);

void app_main(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    displayQueue = xQueueCreate(MAX_Q_SIZE, sizeof(dht_data_t));
    mqttQueue = xQueueCreate(MAX_Q_SIZE, sizeof(dht_data_t));
    ESP_ERROR_CHECK(setup_dht());
    ESP_ERROR_CHECK(setup_timer());
    ESP_ERROR_CHECK(create_tasks());
}

esp_err_t setup_timer(void)
{
    timerDHT = xTimerCreate("Timer DHT",
                            pdMS_TO_TICKS(MEASURE_INTERVAL),
                            pdTRUE,
                            (void *)TIMER_ID,
                            measure_temp_hum);
    if (timerDHT == NULL)
    {
        ESP_LOGE(TAG, "Timer could not be created");
        return ESP_FAIL;
    }
    else
    {
        if (xTimerStart(timerDHT, 0) != pdPASS)
        {
            ESP_LOGE(TAG, "The timer could not be set into the Active state");
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

esp_err_t create_tasks(void)
{
    static uint8_t ucParameterToPass;
    TaskHandle_t displayHandle = NULL;
    TaskHandle_t mqttHandle = NULL;
    TaskHandle_t wifiHandle = NULL;

    if (xTaskCreate(task_show_data_oled,
                    "Show read data on oled display",
                    STACK_SIZE,
                    &ucParameterToPass,
                    1,
                    &displayHandle) != pdPASS)
    {
        return ESP_FAIL;
    }

    if (xTaskCreate(task_wifi,
                    "Task to connect to wifi",
                    STACK_SIZE,
                    &ucParameterToPass,
                    3,
                    &wifiHandle) != pdPASS)
    {
        return ESP_FAIL;
    }

    if (xTaskCreate(task_send_data_mqtt,
                    "Send data to MQTT broker",
                    STACK_SIZE,
                    &ucParameterToPass,
                    2,
                    &mqttHandle) != pdPASS)
    {
        return ESP_FAIL;
    }

    return ESP_OK;
}

void task_show_data_oled(void *args)
{
    dht_data_t sensorData = {0};
    u8g2_t u8g2;
    // OLED Display Setup
    u8g2 = init_oled_display();

    while (true)
    {
        if (xQueueReceive(displayQueue, &sensorData, portMAX_DELAY))
        {
            struct tm timeinfo;
            char temp_str[20], hum_str[20], date_str[17], time_str[12];
            // Parse ISO8601 string back to time
            strptime(sensorData.timestamp, "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
            // Format the date as "dd/mm/YYYY"
            strftime(date_str, sizeof(date_str), "Date: %d/%m/%Y", &timeinfo);
            // Format the time as "HH:mm"
            strftime(time_str, sizeof(time_str), "Time: %H:%M", &timeinfo);
            sprintf(temp_str, "Temp: %.2fC", sensorData.temperature);
            sprintf(hum_str, "Hum:  %.2f %%", sensorData.humidity);
            show_dht_data(u8g2, date_str, time_str, temp_str, hum_str);
        }
        else
        {
            ESP_LOGE(TAG, "Error receiving data or no data in buffer");
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void task_send_data_mqtt(void *args)
{
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT,
                                           pdFALSE,
                                           pdTRUE,
                                           pdMS_TO_TICKS(portMAX_DELAY));

    if ((bits & WIFI_CONNECTED_BIT) == 1)
    {
        ESP_LOGI(TAG, "WiFi connected, starting MQTT task and SNTP");
        setup_mqtt();
        setup_sntp();
    }

    dht_data_t sensorData = {0};
    while (true)
    {
        if ((xQueueReceive(mqttQueue, &sensorData, portMAX_DELAY)) && MQTT_CONNECTED)
        {
            // Convert to JSON string
            char *json_str = create_json_payload(&sensorData);
            esp_mqtt_client_publish(client, "/home/office/dht", json_str, 0, 0, 0);
            free(json_str);
        }
        else
        {
            ESP_LOGE(TAG, "Error receiving data or no data in buffer");
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void task_wifi(void *args)
{
    setup_wifi();

    while (true)
    {
        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                               WIFI_CONNECTED_BIT,
                                               pdFALSE,
                                               pdFALSE,
                                               pdMS_TO_TICKS(WIFI_RECONNECT_INTERVAL_MS));

        if ((bits & WIFI_CONNECTED_BIT) == 0)
        {
            esp_wifi_connect();
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void measure_temp_hum(TimerHandle_t timer)
{
    esp_err_t res;
    dht_data_t dhtData;
    res = dht_read_float_data(DHT_SENSOR_TYPE, CONFIG_ESP_TEMP_SENSOR_GPIO, &dhtData.humidity, &dhtData.temperature);
    get_current_date_time(dhtData.timestamp);
    if (res == ESP_OK)
    {
        if (xQueueSend(displayQueue, &dhtData, pdMS_TO_TICKS(100)) != pdPASS)
        {
            ESP_LOGE(TAG, "Error sending data to display queue");
        }
        if (xQueueSend(mqttQueue, &dhtData, pdMS_TO_TICKS(100)) != pdPASS)
        {
            ESP_LOGE(TAG, "Error sending data to MQTT queue");
        }
    }
    else
    {
        ESP_LOGE(TAG, "Error reading data");
    }
}