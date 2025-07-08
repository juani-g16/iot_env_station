#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "dht.h"

#define MEASURE_INTERVAL 60 * 1000
#define TIMER_ID 1

#define CONFIG_EXAMPLE_INTERNAL_PULLUP 0
#define CONFIG_EXAMPLE_TYPE_DHT11 0

static const char *TAG = "iot_env_station";
dht_sensor_type_t sensor_type;
gpio_num_t gpio_num;
TimerHandle_t timerDHT;

esp_err_t set_timer(void);
esp_err_t dht_init(void);
void measure_temp_hum(TimerHandle_t timer);

void app_main(void)
{
    ESP_ERROR_CHECK(set_timer());
    ESP_ERROR_CHECK(dht_init());
}

esp_err_t set_timer(void)
{
    timerDHT = xTimerCreate("Timer DHT",                     // Just a text name, not used by the kernel.
                            pdMS_TO_TICKS(MEASURE_INTERVAL), // The timer period in ticks.
                            pdTRUE,                          // The timers will auto-reload themselves when they expire.
                            (void *) TIMER_ID,               // Assign each timer a unique id equal to its array index.
                            measure_temp_hum                 // Each timer calls the same callback when it expires.
    );
    if (timerDHT == NULL)
    { // The timer was not created.
        ESP_LOGE(TAG,"Timer could not be created");
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
        res=gpio_pullup_en(gpio_num);
    }
    else
    {
        res=gpio_pullup_dis(gpio_num);
    }
    return res;
}

void measure_temp_hum(TimerHandle_t timer)
{   esp_err_t res;
    float t = 0.0f, h = 0.0f;
    res = dht_read_float_data(sensor_type, gpio_num, &h, &t);
    if (res == ESP_OK)
    {
        ESP_LOGI(TAG, "Temp: %.2f C - Hum: %.2f %%",t,h);
    }else
    {
        ESP_LOGE(TAG, "Error reading data");
    }
    
    
}