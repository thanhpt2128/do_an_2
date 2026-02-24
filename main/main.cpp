#include <stdio.h>
#include <string.h>

extern "C" {
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <ina219.h>
#include "driver/gpio.h"
#include "driver/uart.h"
#include <esp_log.h>
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "mode_control.h"
#include "battery_cc.h"
}

#include "app_config.h"
#include "sensor_tasks.h"
#include "relay_task.h"
#include "uart_task.h"
#include "mqtt_task.h"

const static char *TAG = "MAIN";

// ============ Global Variables ============
// Queue để gửi dữ liệu từ INA task sang các task khác
QueueHandle_t g_sensor_queue = NULL;

// WiFi connection status
bool g_wifi_connected = false;

// ============ Initialization ============

void init(void)
{
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << RELAY1) | (1ULL << RELAY2);
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io_conf);

    uart_config_t uart_config = {};
    uart_config.baud_rate = 115200;
    uart_config.data_bits = UART_DATA_8_BITS;
    uart_config.parity = UART_PARITY_DISABLE;
    uart_config.stop_bits = UART_STOP_BITS_1;
    uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart_param_config(UART_PORT_NUM, &uart_config);
    uart_driver_install(UART_PORT_NUM, 1024 * 2, 0, 0, NULL, 0);
    gpio_set_level((gpio_num_t)RELAY1, 1); 
    gpio_set_level((gpio_num_t)RELAY2, 1);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized");
    
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    ESP_LOGI(TAG, "Initializing WiFi...");
    wifi_init_sta();
    
    g_sensor_queue = xQueueCreate(1, sizeof(sensor_data_t));
    if (g_sensor_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create queue!");
    }
    
    control_src = CTRL_AUTO;
    auto_mode = MODE_OPEN_CIRCUIT;
    manual_mode = MODE_OPEN_CIRCUIT;
    manual_timeout_ms = 0;
}

extern "C" void app_main()
{
    init();

    ESP_LOGI(TAG, "=== Battery Management System Starting ===");
    
    ESP_ERROR_CHECK(i2cdev_init());
    
    static ina219_context_t ina_ctx;
    memset(&ina_ctx, 0, sizeof(ina_ctx));
    
    ESP_ERROR_CHECK(ina219_init_desc(&ina_ctx.dev, 0x40, I2C_NUM_0, GPIO_NUM_21, GPIO_NUM_22));
    ESP_LOGI(TAG, "Initializing INA219");
    ESP_ERROR_CHECK(ina219_init(&ina_ctx.dev));
    ESP_LOGI(TAG, "Configuring INA219");
    ESP_ERROR_CHECK(ina219_configure(&ina_ctx.dev, INA219_BUS_RANGE_16V, INA219_GAIN_0_125,
                                     INA219_RES_12BIT_16S, INA219_RES_12BIT_2S, 
                                     INA219_MODE_CONT_SHUNT_BUS));
    ESP_LOGI(TAG, "Calibrating INA219");
    ESP_ERROR_CHECK(ina219_calibrate(&ina_ctx.dev, (float)100.0 / 1000.0f));
    
    static battery_soc_t battery_soc;
    ESP_ERROR_CHECK(bsoc_init(&battery_soc, 1.060));
    ina_ctx.soc = &battery_soc;
    
    ESP_LOGI(TAG, "Creating tasks...");
    
    // ========== CORE 1 TASKS ==========
    xTaskCreatePinnedToCore(ina219_soc_task, "ina219_soc", 
                           configMINIMAL_STACK_SIZE * 8, &ina_ctx, 5, NULL, 1);
    
    xTaskCreatePinnedToCore(ds18b20_task, "ds18b20", 
                           configMINIMAL_STACK_SIZE * 4, NULL, 5, NULL, 1);
    
    xTaskCreatePinnedToCore(relay_control_task, "relay_ctrl", 
                           configMINIMAL_STACK_SIZE * 3, &battery_soc, 4, NULL, 1);
    
    // ========== CORE 0 TASKS ==========
    xTaskCreatePinnedToCore(uart_rx_task, "uart_rx", 
                           configMINIMAL_STACK_SIZE * 2, NULL, 3, NULL, 0);
    
    xTaskCreatePinnedToCore(mqtt_task, "mqtt", 
                           configMINIMAL_STACK_SIZE * 8, NULL, 3, NULL, 0);
    
    ESP_LOGI(TAG, "=== System started successfully ===");
}

