#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "dht.h"
#include "u8g2.h"
#include "u8g2_esp32_hal.h"

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

#define MEASURE_INTERVAL 60 * 1000
#define WIFI_RECONNECT_INTERVAL_MS 60 * 1000
#define TIMER_ID 1
#define STACK_SIZE 4 * 1024
#define MAX_Q_SIZE 10

#define CONFIG_EXAMPLE_INTERNAL_PULLUP 0
#define CONFIG_EXAMPLE_TYPE_DHT11 0

#define I2C_SDA 8
#define I2C_SCL 9

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

typedef struct
{
    float temperature;
    float humidity;
} dht_data_t;

// static int s_retry_num = 0;
dht_sensor_type_t sensor_type;
gpio_num_t gpio_num;
TimerHandle_t timerDHT;
QueueHandle_t displayQueue;
QueueHandle_t mqttQueue;

bool MQTT_CONNEECTED = false;
esp_mqtt_client_handle_t client = NULL;

static const char *TAG = "iot_env_station";

esp_err_t set_timer(void);
esp_err_t dht_init(void);
esp_err_t create_tasks(void);
static void mqtt_app_start(void);

void measure_temp_hum(TimerHandle_t timer);
void task_show_data_oled(void *args);
void task_send_data_mqtt(void *args);
void task_wifi(void *args);

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
        MQTT_CONNEECTED = true;

        msg_id = esp_mqtt_client_subscribe(client, "/home/temp", 0);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        msg_id = esp_mqtt_client_subscribe(client, "/home/humidity", 1);
        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        MQTT_CONNEECTED = false;
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
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
    timerDHT = xTimerCreate("Timer DHT",                     // Just a text name, not used by the kernel.
                            pdMS_TO_TICKS(MEASURE_INTERVAL), // The timer period in ticks.
                            pdTRUE,                          // The timers will auto-reload themselves when they expire.
                            (void *)TIMER_ID,                // Assign each timer a unique id equal to its array index.
                            measure_temp_hum                 // Each timer calls the same callback when it expires.
    );
    if (timerDHT == NULL)
    { // The timer was not created.
        ESP_LOGE(TAG, "Timer could not be created");
        return ESP_FAIL;
    }
    else
    {
        // Start the timer.  No block time is specified, and even if one was
        // it would be ignored because the scheduler has not yet been
        // started.
        if (xTimerStart(timerDHT, 0) != pdPASS)
        {
            ESP_LOGE(TAG, "The timer could not be set into the Active state");
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

esp_err_t dht_init(void)
{
    esp_err_t res;
    // Select sensor type and GPIO pin from menuconfig
    sensor_type = CONFIG_EXAMPLE_TYPE_AM2301 ? DHT_TYPE_AM2301 : CONFIG_EXAMPLE_TYPE_DHT11 ? DHT_TYPE_DHT11
                                                                                           : DHT_TYPE_SI7021;
    gpio_num = CONFIG_ESP_TEMP_SENSOR_GPIO;

    // Enable internal pull-up resistor if specified in menuconfig
    if (CONFIG_EXAMPLE_INTERNAL_PULLUP)
    {
        res = gpio_pullup_en(gpio_num);
    }
    else
    {
        res = gpio_pullup_dis(gpio_num);
    }
    return res;
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
    dht_data_t recData = {0};
    /* OLED Display Setup*/
    u8g2_t u8g2;
    u8g2_esp32_hal_t u8g2_esp32_hal = U8G2_ESP32_HAL_DEFAULT;
    u8g2_esp32_hal.bus.i2c.scl = I2C_SCL;
    u8g2_esp32_hal.bus.i2c.sda = I2C_SDA;
    u8g2_esp32_hal_init(u8g2_esp32_hal);

    u8g2_Setup_ssd1306_i2c_128x64_noname_f(&u8g2, U8G2_R0,
                                           // u8x8_byte_sw_i2c,
                                           u8g2_esp32_i2c_byte_cb,
                                           u8g2_esp32_gpio_and_delay_cb);
    u8x8_SetI2CAddress(&u8g2.u8x8, 0x78);
    ESP_LOGI(TAG, "u8g2_InitDisplay");
    u8g2_InitDisplay(&u8g2); // send init sequence to the display, display is in
                             // sleep mode after this,

    ESP_LOGI(TAG, "u8g2_SetPowerSave");
    u8g2_SetPowerSave(&u8g2, 0); // wake up display
    ESP_LOGI(TAG, "u8g2_ClearBuffer");

    u8g2_SetFont(&u8g2, u8g2_font_unifont_t_symbols);

    u8g2_ClearBuffer(&u8g2);
    /* End of setup*/

    while (true)
    {
        if (xQueueReceive(displayQueue, &recData, portMAX_DELAY))
        {
            char temp[20], hum[20];
            sprintf(temp, "Temp: %.2fC", recData.temperature);
            sprintf(hum, "Hum: %.2f %%", recData.humidity);
            u8g2_ClearBuffer(&u8g2);
            u8g2_DrawStr(&u8g2, 0, 12, "Sensor Temp/Hum");
            u8g2_DrawHLine(&u8g2, 0, 14, 128);
            u8g2_DrawStr(&u8g2, 0, 36, temp);
            u8g2_DrawGlyph(&u8g2, 110, 36, 0x2600);
            u8g2_DrawStr(&u8g2, 0, 60, hum);
            u8g2_DrawGlyph(&u8g2, 110, 60, 0x2614);
            u8g2_SendBuffer(&u8g2); // Send the buffer data to display
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
    dht_data_t recData = {0};
    char temp[20], hum[20];
    while (true)
    {
        if ((xQueueReceive(mqttQueue, &recData, portMAX_DELAY)) && MQTT_CONNEECTED)
        {
            sprintf(temp, "{\"Temp\":\"%.2fC\"}", recData.temperature);
            sprintf(hum, "{\"Hum\":\"%.2f%%\"}", recData.humidity);
            esp_mqtt_client_publish(client, "/home/temp", temp, 0, 0, 0);
            esp_mqtt_client_publish(client, "/home/humidity", hum, 0, 0, 0);
            ESP_LOGI(TAG, "Data sent to MQTT");
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
    res = dht_read_float_data(sensor_type, gpio_num, &dhtData.humidity, &dhtData.temperature);
    if (res == ESP_OK)
    {
        ESP_LOGI(TAG, "Sending data: Temp: %.2fC - Humidity: %.2f%%", dhtData.temperature, dhtData.humidity);
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
