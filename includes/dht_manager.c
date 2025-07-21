
#include "dht_manager.h"

esp_err_t setup_dht(void)
{
    esp_err_t res;
    // Enable internal pull-up resistor if specified in menuconfig
    if (CONFIG_EXAMPLE_INTERNAL_PULLUP)
    {
        res = gpio_pullup_en(CONFIG_ESP_TEMP_SENSOR_GPIO);
    }
    else
    {
        res = gpio_pullup_dis(CONFIG_ESP_TEMP_SENSOR_GPIO);
    }
    return res;
}
