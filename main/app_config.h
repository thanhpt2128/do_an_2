#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "mode_control.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============ Configuration ============
// WiFi Configuration
#define WIFI_SSID           "TruongThanh"
#define WIFI_PASSWORD       "truongthanh2004"

// ThingsBoard Configuration
#define THINGSBOARD_SERVER  "192.168.1.7"  
#define THINGSBOARD_TOKEN   "96odi4w45k7zglbwkyb1"
#define THINGSBOARD_PORT    1883

// Hardware pins
#define RELAY1   27  // xả
#define RELAY2   26  // sạc

#define I2C_PORT 0
#define I2C_ADDR CONFIG_EXAMPLE_I2C_ADDR

#define EXAMPLE_ONEWIRE_BUS_GPIO    18
#define EXAMPLE_ONEWIRE_MAX_DS18B20 1

#define UART_PORT_NUM      UART_NUM_0

// Thresholds
#define CHARGE_CUTOFF_CURRENT    0.05f  // 50mA
#define DISCHARGE_CUTOFF_VOLTAGE 2.6f   // 2.6V
#define MAX_TEMPERATURE          45.0f  // 45°C
#define MIN_TEMPERATURE          0.0f   // 0°C
#define MAX_CURRENT              2.0f   // 2A - Ngưỡng dòng tối đa an toàn

// ============ Data Structures ============
// Cấu trúc dữ liệu chia sẻ giữa các task
typedef struct {
    float voltage;
    float current;
    float shunt_voltage;
    float soc_percent;
    float soh_percent;
    float temperature;
    float capacity_gap;
    float capacity_est;
    int64_t timestamp_ms;
} sensor_data_t;

// ============ Global Variables ============
// Queue để gửi dữ liệu từ INA task sang các task khác
extern QueueHandle_t g_sensor_queue;

// WiFi connection status
extern bool g_wifi_connected;

#ifdef __cplusplus
}
#endif

#endif // APP_CONFIG_H
