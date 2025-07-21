#include <stdio.h>
#include <string.h>
#include <time.h>

#include "dht_manager.h"
#include "display_manager.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "mqtt_client.h"
#include "esp_sntp.h"

#include "cJSON.h"

#define MEASURE_INTERVAL 60 * 1000
#define WIFI_RECONNECT_INTERVAL_MS 60 * 1000
#define TIMER_ID 1
#define STACK_SIZE 4 * 1024
#define MAX_Q_SIZE 10

#define EXAMPLE_ESP_WIFI_SSID CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_ESP_MAXIMUM_RETRY CONFIG_ESP_MAXIMUM_RETRY

#if CONFIG_ESP_WPA3_SAE_PWE_HUNT_AND_PECK
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HUNT_AND_PECK
#define EXAMPLE_H2E_IDENTIFIER ""
#elif CONFIG_ESP_WPA3_SAE_PWE_HASH_TO_ELEMENT
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HASH_TO_ELEMENT
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#elif CONFIG_ESP_WPA3_SAE_PWE_BOTH
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#endif
#if CONFIG_ESP_WIFI_AUTH_OPEN
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
#elif CONFIG_ESP_WIFI_AUTH_WEP
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
#elif CONFIG_ESP_WIFI_AUTH_WPA_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WAPI_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
#endif

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
// #define WIFI_FAIL_BIT BIT1

TimerHandle_t timerDHT;
QueueHandle_t displayQueue;
QueueHandle_t mqttQueue;

char Current_Date_Time[100];

bool MQTT_CONNECTED = false;
esp_mqtt_client_handle_t client = NULL;

static const char *TAG = "iot_env_station";

esp_err_t set_timer(void);
esp_err_t create_tasks(void);
static void mqtt_app_start(void);

void initialize_sntp(void);
void get_current_date_time(char *date_time);
char *create_json_payload(const dht_data_t *data);

void measure_temp_hum(TimerHandle_t timer);
void task_show_data_oled(void *args);
void task_send_data_mqtt(void *args);
void task_wifi(void *args);

static void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "Notification of a time synchronization event");
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGI(TAG, "Disconnected from AP, will try to reconnect in 60 seconds");
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        initialize_sntp();
        mqtt_app_start();
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    // ESP_LOGI(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        MQTT_CONNECTED = true;

        msg_id = esp_mqtt_client_subscribe(client, "/home/office/dht", 0);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT_EVENT_DISCONNECTED");
        MQTT_CONNECTED = false;
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGW(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG,"TOPIC=%.*s", event->topic_len, event->topic);
        ESP_LOGI(TAG,"DATA=%.*s", event->data_len, event->data);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        break;
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

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
    ESP_ERROR_CHECK(dht_init());
    ESP_ERROR_CHECK(set_timer());
    ESP_ERROR_CHECK(create_tasks());
}

esp_err_t set_timer(void)
{
    timerDHT = xTimerCreate("Timer DHT",                     
                            pdMS_TO_TICKS(MEASURE_INTERVAL), 
                            pdTRUE,                          
                            (void *)TIMER_ID,                
                            measure_temp_hum                 
    );
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

    if (xTaskCreate(task_send_data_mqtt,
                    "Send data to MQTT broker",
                    STACK_SIZE,
                    &ucParameterToPass,
                    2,
                    &mqttHandle) != pdPASS)
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

    return ESP_OK;
}

static void mqtt_app_start(void)
{
    ESP_LOGI(TAG, "STARTING MQTT");
    esp_mqtt_client_config_t mqttConfig = {
        .broker.address.uri = CONFIG_BROKER_URI,
    };

    client = esp_mqtt_client_init(&mqttConfig);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, client);
    esp_mqtt_client_start(client);
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

void task_wifi(void *args)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            /* Authmode threshold resets to WPA2 as default if password matches WPA2 standards (password len => 8).
             * If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
             * to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
             * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
             */
            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
            .sae_pwe_h2e = ESP_WIFI_SAE_MODE,
            .sae_h2e_identifier = EXAMPLE_H2E_IDENTIFIER,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_task started and wifi driver started");

    while (1)
    {
        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                               WIFI_CONNECTED_BIT,
                                               pdFALSE,
                                               pdFALSE,
                                               pdMS_TO_TICKS(WIFI_RECONNECT_INTERVAL_MS));

        if ((bits & WIFI_CONNECTED_BIT) == 0)
        {
            ESP_LOGI(TAG, "WiFi not connected, trying to reconnect...");
            esp_wifi_connect();
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void initialize_sntp(void)
{
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