#include "mode_control.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

const static char *TAG = "mode_control";

// Định nghĩa biến toàn cục
power_mode_t auto_mode = MODE_OPEN_CIRCUIT;
power_mode_t manual_mode = MODE_OPEN_CIRCUIT;
control_src_t control_src = CTRL_AUTO;
uint64_t manual_timeout_ms = 0;


void uart_handle_cmd(char *cmd)
{
    if (strcmp(cmd, "S") == 0) {
        manual_mode = MODE_CHARGE_ENABLE;
        control_src = CTRL_MANUAL;
    }
    else if (strcmp(cmd, "X") == 0) {
        manual_mode = MODE_DISCHARGE_ENABLE;
        control_src = CTRL_MANUAL;
    }
    else if (strcmp(cmd, "H") == 0) {
        manual_mode = MODE_OPEN_CIRCUIT;
        control_src = CTRL_MANUAL;
    }
    else if (strcmp(cmd, "A") == 0) {
        control_src = CTRL_AUTO;
    }
    else {
        ESP_LOGI(TAG, "Lenh khong hop le. Dung: S,X,H,A");
    }

    manual_timeout_ms = xTaskGetTickCount() + pdMS_TO_TICKS(30000);
}


void apply_power_mode(power_mode_t mode, gpio_num_t relay_sac, gpio_num_t relay_xa)
{
    switch (mode) {
    case MODE_CHARGE_ENABLE:
        gpio_set_level(relay_xa, 1); // Hở xả
        gpio_set_level(relay_sac, 0); // Đóng sạc
        break;

    case MODE_DISCHARGE_ENABLE:
        gpio_set_level(relay_xa, 0); // Đóng xả
        gpio_set_level(relay_sac, 1); // Hở sạc
        break;

    case MODE_OPEN_CIRCUIT:
    default:
        gpio_set_level(relay_xa, 1); // Hở xả
        gpio_set_level(relay_sac, 1); // Hở sạc
        break;
    }
}

