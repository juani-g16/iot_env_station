/**
 * @file main.c
 * @brief IoT Environmental Station Main Application
 * 
 * This file contains the main application logic for an IoT environmental monitoring
 * station that reads temperature and humidity data from a DHT sensor, displays the
 * information on an OLED screen, and transmits the data via MQTT over WiFi.
 * 
 * The application uses FreeRTOS tasks for concurrent operation:
 * - Timer-based sensor reading
 * - OLED display updates
 * - MQTT data transmission
 * - WiFi connection management
 */

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

extern EventGroupHandle_t s_wifi_event_group;  /**< WiFi event group handle */
extern bool MQTT_CONNECTED;                    /**< MQTT connection status flag */
extern esp_mqtt_client_handle_t client;        /**< MQTT client handle */

TimerHandle_t timerDHT;     /**< Timer handle for periodic DHT sensor readings */
QueueHandle_t displayQueue; /**< Queue for sensor data to be displayed on OLED */
QueueHandle_t mqttQueue;    /**< Queue for sensor data to be sent via MQTT */

static const char *TAG = "iot_env_station"; /**< Log tag for this module */

/* Function prototypes */
esp_err_t setup_timer(void);
esp_err_t create_tasks(void);

void measure_temp_hum(TimerHandle_t timer);
void task_show_data_oled(void *args);
void task_send_data_mqtt(void *args);
void task_wifi(void *args);

/**
 * @fn void app_main(void)
 * @brief Main application entry point
 * 
 * This function initializes the IoT environmental station by:
 * - Setting up NVS (Non-Volatile Storage) for configuration data
 * - Creating FreeRTOS queues for inter-task communication
 * - Initializing the DHT sensor
 * - Setting up the measurement timer
 * - Creating and starting all application tasks
 * 
 * The function handles NVS initialization errors by erasing and reinitializing
 * the flash if necessary.
 */
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

/**
 * @fn esp_err_t setup_timer(void)
 * @brief Creates and starts the DHT sensor measurement timer
 * 
 * This function creates a FreeRTOS software timer that triggers periodic
 * temperature and humidity measurements from the DHT sensor. The timer is
 * configured to repeat at intervals defined by MEASURE_INTERVAL and calls
 * the measure_temp_hum() callback function.
 * 
 * @return ESP_OK on successful timer creation and start, ESP_FAIL on error
 */
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

/**
 * @fn esp_err_t create_tasks(void)
 * @brief Creates and starts all FreeRTOS tasks for the application
 * 
 * This function creates three main tasks:
 * 1. Display task (priority 1): Updates OLED display with sensor data
 * 2. WiFi task (priority 3): Manages WiFi connection and reconnection
 * 3. MQTT task (priority 2): Handles MQTT communication and data transmission
 * 
 * Each task is created with a stack size defined by STACK_SIZE and assigned
 * appropriate priorities for proper system operation.
 * 
 * @return ESP_OK if all tasks are created successfully, ESP_FAIL if any task creation fails
 */
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

/**
 * @fn void task_show_data_oled(void *args)
 * @brief FreeRTOS task for displaying sensor data on OLED screen
 * 
 * This task continuously monitors the display queue for new sensor data and
 * updates the OLED display with formatted temperature, humidity, date, and time
 * information. The task:
 * - Waits for data from the display queue
 * - Parses the ISO8601 timestamp from sensor data
 * - Formats date and time strings for display
 * - Updates the OLED screen with current readings
 * 
 * @param args Pointer to task parameters (unused in this implementation)
 */
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

/**
 * @fn void task_send_data_mqtt(void *args)
 * @brief FreeRTOS task for transmitting sensor data via MQTT
 * 
 * This task handles MQTT communication by:
 * - Waiting for WiFi connection establishment
 * - Initializing MQTT client and SNTP time synchronization
 * - Continuously monitoring the MQTT queue for sensor data
 * - Converting sensor data to JSON format
 * - Publishing data to the configured MQTT topic
 * 
 * The task only attempts to send data when both the queue has data and
 * the MQTT client is connected to the broker.
 * 
 * @param args Pointer to task parameters (unused in this implementation)
 */
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

/**
 * @fn void task_wifi(void *args)
 * @brief FreeRTOS task for WiFi connection management
 * 
 * This task manages the WiFi connection by:
 * - Initializing the WiFi subsystem and connecting to the configured network
 * - Continuously monitoring the connection status using event groups
 * - Automatically attempting reconnection if the connection is lost
 * - Implementing a watchdog mechanism with configurable reconnect intervals
 * 
 * The task uses the WIFI_RECONNECT_INTERVAL_MS timeout to check connection
 * status and triggers reconnection attempts when necessary.
 * 
 * @param args Pointer to task parameters (unused in this implementation)
 */
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

/**
 * @fn void measure_temp_hum(TimerHandle_t timer)
 * @brief Timer callback function for reading DHT sensor data
 * 
 * This function is called periodically by the FreeRTOS timer to:
 * - Read temperature and humidity data from the DHT sensor
 * - Capture the current timestamp in ISO8601 format
 * - Package the data into a dht_data_t structure
 * - Send the data to both display and MQTT queues for processing
 * 
 * The function handles read errors by logging appropriate error messages
 * and only sends data to queues when the sensor reading is successful.
 * Queue send operations use a timeout to prevent blocking if queues are full.
 * 
 * @param timer Handle to the timer that triggered this callback (unused)
 */
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