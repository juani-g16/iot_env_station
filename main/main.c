#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "dht.h"
#include "u8g2.h"
#include "u8g2_esp32_hal.h"

#define MEASURE_INTERVAL 60 * 1000
#define TIMER_ID 1
#define STACK_SIZE 4 * 1024

#define CONFIG_EXAMPLE_INTERNAL_PULLUP 0
#define CONFIG_EXAMPLE_TYPE_DHT11 0

#define I2C_SDA 8
#define I2C_SCL 9

static const char *TAG = "iot_env_station";
dht_sensor_type_t sensor_type;
gpio_num_t gpio_num;
TimerHandle_t timerDHT;

esp_err_t set_timer(void);
esp_err_t dht_init(void);
esp_err_t create_tasks(void);
void measure_temp_hum(TimerHandle_t timer);
void task_show_data_oled(void * args);

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

esp_err_t create_tasks(void)
{
    static uint8_t ucParameterToPass;
    TaskHandle_t handle = NULL;

    xTaskCreate(task_show_data_oled,
                "Show read data on oled display",
                STACK_SIZE,
                &ucParameterToPass,
                1,
                &handle);

    return ESP_OK;
}

void task_show_data_oled(void *args)
{   
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
        return;
    }
    
}

void measure_temp_hum(TimerHandle_t timer)
{
    esp_err_t res;
    float t = 0.0f, h = 0.0f;
    res = dht_read_float_data(sensor_type, gpio_num, &h, &t);
    if (res == ESP_OK)
    {
        ESP_LOGI(TAG, "Temp: %.2f C - Hum: %.2f %%", t, h);
    }
    else
    {
        ESP_LOGE(TAG, "Error reading data");
    }
}