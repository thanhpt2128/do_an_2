#include "uart_task.h"
#include "app_config.h"

#include <stdio.h>
#include <string.h>

extern "C" {
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_log.h>
#include "driver/uart.h"
#include "esp_timer.h"
#include "mode_control.h"
}

static const char *TAG = "UART";

void uart_rx_task(void *pvParameters)
{
    ESP_LOGI(TAG, "[CORE0] UART RX task started");
    uint8_t uart_data[128];
    
    printf("timestamp_ms,VBUS,VSHUNT_mV,IBUS_mA,SOC_percent,SOH_percent,capacity_gap,capacity_est,temperature\n");
    fflush(stdout);
    
    int64_t last_log_ms = esp_timer_get_time() / 1000;
    
    while (1) {
        // Đọc lệnh UART
        int len = uart_read_bytes(UART_PORT_NUM, uart_data, sizeof(uart_data) - 1, 100 / portTICK_PERIOD_MS);
        if (len > 0) {
            uart_data[len] = '\0';
            // Xóa ký tự xuống dòng
            char *newline = strchr((char*)uart_data, '\n');
            if (newline) *newline = '\0';
            newline = strchr((char*)uart_data, '\r');
            if (newline) *newline = '\0';
            
            ESP_LOGI(TAG, "UART RX: %s", uart_data);
            uart_handle_cmd((char *)uart_data);
        }
        
        // Log dữ liệu CSV mỗi 500ms
        int64_t now_ms = esp_timer_get_time() / 1000;
        if ((now_ms - last_log_ms) >= 500) {
            sensor_data_t data;
            if (xQueuePeek(g_sensor_queue, &data, 0) == pdTRUE) {
                printf("%lld,%.06f,%.06f,%.06f,%.2f,%.2f,%.6lf,%.6lf,%.2f\n",
                    data.timestamp_ms,
                    data.voltage,
                    data.shunt_voltage * 1000,
                    data.current * 1000,
                    data.soc_percent,
                    data.soh_percent,
                    data.capacity_gap,
                    data.capacity_est,
                    data.temperature);
                fflush(stdout);
            }
            last_log_ms = now_ms;
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
