idf_component_register(SRCS "mqtt_task.cpp" "uart_task.cpp" "relay_task.cpp" "sensor_tasks.cpp" "main.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES batlibs espressif__ds18b20 espressif__onewire_bus thingsboard__thingsboard esp_wifi esp_netif nvs_flash)
