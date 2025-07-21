idf_component_register(
    SRCS 
        "main.c" 
        "../includes/dht_manager.c" 
        "../includes/display_manager.c"
        "../includes/wifi_manager.c"
        "../includes/mqtt_manager.c"
        "../includes/common.c"
    INCLUDE_DIRS 
        "." 
        "../includes"
    REQUIRES 
        esp_driver_i2c u8g2 u8g2-hal-esp-idf esp32-dht esp_wifi nvs_flash mqtt json
        )
