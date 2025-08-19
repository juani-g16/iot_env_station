#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include "mqtt_client.h"
#include "esp_log.h"

/**
 * @fn void setup_mqtt(void)
 * @brief Initializes and configures the MQTT client for IoT communication
 * 
 * This function sets up the MQTT client with the necessary configuration
 * including broker connection details and event handlers. It establishes
 * the connection to the MQTT broker and prepares the client for publishing
 * sensor data and receiving commands. The configuration parameters are
 * typically read from the ESP-IDF configuration system (menuconfig).
 */
void setup_mqtt(void);

#endif