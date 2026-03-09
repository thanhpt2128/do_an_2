#include "sensor_tasks.h"
#include "app_config.h"

#include <stdio.h>

extern "C" {
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_log.h>
#include <ina219.h>
#include "esp_timer.h"
#include "battery_cc.h"
#include "onewire_bus.h"
#include "ds18b20.h"
}

static const char *TAG = "SENSORS";

void ina219_soc_task(void *pvParameters)
{
    ina219_context_t *ctx = (ina219_context_t *)pvParameters;
    float bus_voltage, shunt_voltage, current;
    
    ESP_LOGI(TAG, "[CORE1] INA219+SOC task started");
    vTaskDelay(pdMS_TO_TICKS(5000));
    
    bsoh_load_from_nvs(ctx->soc);
    
    ESP_ERROR_CHECK(ina219_get_bus_voltage(&ctx->dev, &bus_voltage));
    ESP_LOGI(TAG, "Initial voltage: %.04f V", bus_voltage);
    
    bsoc_estimate_soc_from_ocv(ctx->soc, bus_voltage);
    ctx->soc->capacity_est = ctx->soc->soc_cc * ctx->soc->rated_capacity_Ah * ctx->soc->soh;
    
    while (1)
    {
        ESP_ERROR_CHECK(ina219_get_bus_voltage(&ctx->dev, &bus_voltage));
        ESP_ERROR_CHECK(ina219_get_shunt_voltage(&ctx->dev, &shunt_voltage));
        ESP_ERROR_CHECK(ina219_get_current(&ctx->dev, &current));
        
        // Khử nhiễu dòng điện trong khoảng -0.8mA đến 0.8mA
        if (current > -0.0008f && current < 0.0008f) {
            current = 0.0f;
        }
        
        bsoc_feed_sample(ctx->soc, bus_voltage, current);
        float soc_percent = bsoc_get_soc_percent(ctx->soc);
        float soh_percent = bsoc_get_soh_percent(ctx->soc);
        
        sensor_data_t data = {0};
        xQueuePeek(g_sensor_queue, &data, 0);  // Lấy temperature cũ (nếu có)
        
        // Cập nhật các giá trị mới
        int64_t now_ms = esp_timer_get_time() / 1000;
        data.voltage = bus_voltage;
        data.current = current;
        data.shunt_voltage = shunt_voltage;
        data.soc_percent = soc_percent;
        data.soh_percent = soh_percent;
        data.capacity_gap = ctx->soc->cap_xa;
        data.capacity_est = ctx->soc->capacity_est;
        data.timestamp_ms = now_ms;

        xQueueOverwrite(g_sensor_queue, &data);
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}


void ds18b20_task(void *pvParameters)
{
    ESP_LOGI(TAG, "[CORE1] DS18B20 task started");
    
    onewire_bus_handle_t bus_handle;
    onewire_bus_config_t bus_config = {
        .bus_gpio_num = EXAMPLE_ONEWIRE_BUS_GPIO,
        .flags = {
            .en_pull_up = true,
        }
    };
    onewire_bus_rmt_config_t rmt_config = {
        .max_rx_bytes = 10,
    };
    ESP_ERROR_CHECK(onewire_new_bus_rmt(&bus_config, &rmt_config, &bus_handle));
    
    ds18b20_device_handle_t ds18b20_handle = NULL;
    bool device_found = false;
    
    while (!device_found) {
        onewire_device_iter_handle_t iter_handle;
        onewire_device_t ds18b20_device;
        ESP_ERROR_CHECK(onewire_new_device_iter(bus_handle, &iter_handle));
        
        esp_err_t err = onewire_device_iter_get_next(iter_handle, &ds18b20_device);
        
        ESP_ERROR_CHECK(onewire_del_device_iter(iter_handle));
        
        if (err == ESP_OK) {
            ds18b20_config_t ds18b20_config = {};
            esp_err_t create_err = ds18b20_new_device_from_enumeration(&ds18b20_device, &ds18b20_config, &ds18b20_handle);
            
            if (create_err == ESP_OK) {
                ESP_LOGI(TAG, "Đã tìm thấy và khởi tạo DS18B20 thành công!");
                device_found = true;
            } else {
                ESP_LOGW(TAG, "Tìm thấy DS18B20 nhưng không khởi tạo được, thử lại sau 5s...");
                vTaskDelay(pdMS_TO_TICKS(5000));
            }
        } else {
            ESP_LOGW(TAG, "Không tìm thấy DS18B20, thử lại sau 5s...");
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }
    
    while (1) {
        float temperature;
        ESP_ERROR_CHECK(ds18b20_trigger_temperature_conversion(ds18b20_handle));
        vTaskDelay(pdMS_TO_TICKS(800)); 
        ESP_ERROR_CHECK(ds18b20_get_temperature(ds18b20_handle, &temperature));
        
        sensor_data_t data = {0};
        xQueuePeek(g_sensor_queue, &data, 0);
        data.temperature = temperature;
        xQueueOverwrite(g_sensor_queue, &data);
        
        vTaskDelay(pdMS_TO_TICKS(5000)); 
    }
}
