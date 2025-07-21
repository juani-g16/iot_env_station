#include "display_manager.h"

u8g2_t init_oled_display(void)
{
    static const char *TAG = "oled_display_module";
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
    ESP_LOGI(TAG, "Init Display");
    u8g2_InitDisplay(&u8g2); // send init sequence to the display, display is in
                             // sleep mode after this,

    ESP_LOGI(TAG, "WakeUp Display");
    u8g2_SetPowerSave(&u8g2, 0); // wake up display
    ESP_LOGI(TAG, "Clear Buffer");

    u8g2_SetFont(&u8g2, u8g2_font_unifont_t_symbols);

    u8g2_ClearBuffer(&u8g2);
    /* End of setup*/

    return u8g2;
}

void show_dht_data(u8g2_t u8g2, const char *date, const char *time, const char *temp, const char *hum)
{
    u8g2_ClearBuffer(&u8g2);
    u8g2_DrawStr(&u8g2, 0, 12, date);
    u8g2_DrawStr(&u8g2, 0, 28, time);
    u8g2_DrawStr(&u8g2, 0, 46, temp);
    u8g2_DrawGlyph(&u8g2, 110, 46, 0x2600);
    u8g2_DrawStr(&u8g2, 0, 62, hum);
    u8g2_DrawGlyph(&u8g2, 110, 62, 0x2614);
    u8g2_SendBuffer(&u8g2); // Send the buffer data to display
}
