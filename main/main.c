#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "driver/gpio.h"

#define MEASURE_INTERVAL 60 * 1000
#define TIMER_ID 1

static const char *TAG = "iot_env_station";
TimerHandle_t timerDHT;

esp_err_t set_timer();
void measure_temp_hum(TimerHandle_t timer);

void app_main(void)
{
    ESP_ERROR_CHECK(set_timer());
}

esp_err_t set_timer()
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

void measure_temp_hum(TimerHandle_t timer)
{
    ESP_LOGI(TAG,"Timer working");
}

