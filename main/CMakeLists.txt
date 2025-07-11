idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_driver_i2c u8g2 u8g2-hal-esp-idf esp32-dht esp_wifi nvs_flash mqtt)