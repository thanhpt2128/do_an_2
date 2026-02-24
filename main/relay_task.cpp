#include "relay_task.h"
#include "app_config.h"

#include <math.h>

extern "C" {
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_log.h>
#include "esp_timer.h"
#include "driver/gpio.h"
#include "battery_cc.h"
#include "mode_control.h"
}

static const char *TAG = "RELAY";


void relay_control_task(void *pvParameters)
{
    battery_soc_t *soc = (battery_soc_t *)pvParameters;
    power_mode_t final_mode = MODE_OPEN_CIRCUIT;
    
    int64_t low_current_start_ms = 0;
    power_mode_t last_mode = MODE_OPEN_CIRCUIT;
    int64_t open_circuit_start_ms = esp_timer_get_time() / 1000;
    int64_t last_ocv_update_ms = 0;
    
    ESP_LOGI(TAG, "[CORE1] Relay control task started");
    
    while (1) {
        // Kiểm tra timeout manual mode
        if (control_src == CTRL_MANUAL) {
            if (xTaskGetTickCount() > manual_timeout_ms) {
                control_src = CTRL_AUTO;
                ESP_LOGI(TAG, "Manual timeout, chuyển sang AUTO mode");
            } else {
                final_mode = manual_mode;
                
                // Nếu chuyển từ hở sang sạc/xả -> reset tracking
                if (last_mode == MODE_OPEN_CIRCUIT && manual_mode != MODE_OPEN_CIRCUIT) {
                    last_ocv_update_ms = 0;
                }
                // Nếu chuyển sang hở -> bắt đầu tracking
                else if (last_mode != MODE_OPEN_CIRCUIT && manual_mode == MODE_OPEN_CIRCUIT) {
                    open_circuit_start_ms = esp_timer_get_time() / 1000;
                    last_ocv_update_ms = 0;
                }
                
                last_mode = manual_mode;
            }
        }
        
        // Auto mode logic - CHỈ GIÁM SÁT VÀ BẢO VỆ
        if (control_src == CTRL_AUTO) {
            float voltage, current, temperature;
            
            // Lấy dữ liệu hiện tại từ queue
            sensor_data_t data;
            if (xQueuePeek(g_sensor_queue, &data, pdMS_TO_TICKS(100)) == pdTRUE) {
                voltage = data.voltage;
                current = data.current;
                temperature = data.temperature;
            } else {
                vTaskDelay(pdMS_TO_TICKS(200));
                continue;
            }
            
            // Kiểm tra dòng điện vượt ngưỡng an toàn (bảo vệ ưu tiên cao nhất)
            if (fabsf(current) > MAX_CURRENT) {
                if (last_mode != MODE_OPEN_CIRCUIT) {
                    final_mode = MODE_OPEN_CIRCUIT;
                    last_mode = MODE_OPEN_CIRCUIT;
                    ESP_LOGE(TAG, "Dòng điện vượt ngưỡng (%.2fA > %.2fA), tự động HỞ bảo vệ!", current, MAX_CURRENT);
                    
                    open_circuit_start_ms = esp_timer_get_time() / 1000;
                    last_ocv_update_ms = 0;
                }
            }
            // Kiểm tra nhiệt độ quá cao hoặc quá thấp
            else if (temperature > MAX_TEMPERATURE) {
                if (last_mode != MODE_OPEN_CIRCUIT) {
                    final_mode = MODE_OPEN_CIRCUIT;
                    last_mode = MODE_OPEN_CIRCUIT;
                    ESP_LOGE(TAG, "Nhiệt độ quá cao (%.1f°C), tự động HỞ bảo vệ!", temperature);
                    
                    open_circuit_start_ms = esp_timer_get_time() / 1000;
                    last_ocv_update_ms = 0;
                }
            }
            else if (temperature < MIN_TEMPERATURE) {
                if (last_mode != MODE_OPEN_CIRCUIT) {
                    final_mode = MODE_OPEN_CIRCUIT;
                    last_mode = MODE_OPEN_CIRCUIT;
                    ESP_LOGE(TAG, "Nhiệt độ quá thấp (%.1f°C), tự động HỞ bảo vệ!", temperature);
                    
                    open_circuit_start_ms = esp_timer_get_time() / 1000;
                    last_ocv_update_ms = 0;
                }
            }
            // Kiểm tra điều kiện xả hết (điện áp thấp)
            else if (voltage <= DISCHARGE_CUTOFF_VOLTAGE && last_mode == MODE_DISCHARGE_ENABLE) {
                final_mode = MODE_OPEN_CIRCUIT;
                last_mode = MODE_OPEN_CIRCUIT;
                ESP_LOGI(TAG, "Điện áp thấp (%.2fV), tự động HỞ và cập nhật SOH", voltage);
                
                // Cập nhật SOH (hàm đã tự kiểm tra SOC)
                bsoc_update_soh_on_full_discharge(soc);
                
                // Reset SOC về 0% (xả hết)
                soc->soc_cc = 0.0f;
                soc->capacity_est = 0.0;
                ESP_LOGI(TAG, "Reset SOC về 0%% sau khi xả hết");
                
                open_circuit_start_ms = esp_timer_get_time() / 1000;
                last_ocv_update_ms = 0;
            }
            // Kiểm tra điều kiện sạc đầy (dòng thấp trong 30s)
            else if (last_mode == MODE_CHARGE_ENABLE) {
                if (current < CHARGE_CUTOFF_CURRENT) {
                    if (low_current_start_ms == 0) {
                        low_current_start_ms = esp_timer_get_time() / 1000;
                    } else {
                        int64_t duration_ms = (esp_timer_get_time() / 1000) - low_current_start_ms;
                        if (duration_ms >= 30000) { // 30 giây
                            final_mode = MODE_OPEN_CIRCUIT;
                            last_mode = MODE_OPEN_CIRCUIT;
                            low_current_start_ms = 0;
                            ESP_LOGI(TAG, "Dòng thấp trong 30s, tự động HỞ và cập nhật SOH");
                            
                            // Cập nhật SOH
                            bsoc_update_soh_on_full_charge(soc);
                            
                            // Set SOC = 100% (sạc đầy)
                            soc->soc_cc = 1.0f;
                            soc->capacity_est = soc->rated_capacity_Ah * soc->soh;
                            ESP_LOGI(TAG, "Set SOC = 100%% sau khi sạc đầy");
                            
                            open_circuit_start_ms = esp_timer_get_time() / 1000;
                            last_ocv_update_ms = 0;
                        }
                    }
                } else {
                    low_current_start_ms = 0; // Reset nếu dòng tăng lại
                }
            }
            else {
                // Giữ nguyên trạng thái hiện tại
                final_mode = last_mode;
                
                // Nếu đang hở mạch -> kiểm tra cập nhật SOC từ OCV định kỳ
                if (last_mode == MODE_OPEN_CIRCUIT) {
                    int64_t now_ms = esp_timer_get_time() / 1000;
                    int64_t open_duration_ms = now_ms - open_circuit_start_ms;
                    
                    // Cập nhật định kỳ mỗi 1 tiếng (3600000ms)
                    if (open_duration_ms >= 3600000) {
                        int64_t time_since_last_update = now_ms - last_ocv_update_ms;
                        
                        if (last_ocv_update_ms == 0 || time_since_last_update >= 3600000) {
                            // Pin đã relaxation, OCV ổn định -> ước lượng lại SOC
                            ESP_LOGI(TAG, "Hở mạch %.1fh, cập nhật SOC từ OCV", open_duration_ms / 3600000.0f);
                            bsoc_estimate_soc_from_ocv(soc, voltage);
                            last_ocv_update_ms = now_ms;
                        }
                    }
                }
            }
            
            auto_mode = final_mode;
        }
        
        apply_power_mode(final_mode, (gpio_num_t)RELAY2, (gpio_num_t)RELAY1);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
