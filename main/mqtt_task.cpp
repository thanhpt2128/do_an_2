#include "mqtt_task.h"
#include "app_config.h"

#include <string.h>
#include <assert.h>

extern "C" {
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_log.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "mode_control.h"
}

// ThingsBoard
#include <Espressif_MQTT_Client.h>
#include <ThingsBoard.h>
#include <Server_Side_RPC.h>

static const char *TAG = "MQTT";

// ThingsBoard MQTT Client
constexpr uint16_t MAX_MESSAGE_SIZE = 256U;
constexpr uint8_t MAX_RPC_SUBSCRIPTIONS = 3U;
constexpr uint8_t MAX_RPC_RESPONSE = 5U;

static Espressif_MQTT_Client mqttClient;
static Server_Side_RPC<MAX_RPC_SUBSCRIPTIONS, MAX_RPC_RESPONSE> rpc;
static const std::array<IAPI_Implementation*, 1U> apis = {
    &rpc
};
static ThingsBoard tb(mqttClient, MAX_MESSAGE_SIZE, MAX_MESSAGE_SIZE, Default_Max_Stack_Size, apis);

// Telemetry keys
constexpr char VOLTAGE_KEY[] = "voltage";
constexpr char CURRENT_KEY[] = "current";
constexpr char SOC_KEY[] = "soc";
constexpr char SOH_KEY[] = "soh";
constexpr char TEMPERATURE_KEY[] = "temperature";

// RPC method names and attribute keys
constexpr char RPC_SET_MODE_METHOD[] = "setMode";
constexpr char RPC_MODE_KEY[] = "mode";
constexpr char ATTR_MODE_KEY[] = "current_mode";
constexpr char ATTR_CONTROL_SRC_KEY[] = "control_source";
constexpr char MODE_CHARGE[] = "charge";
constexpr char MODE_DISCHARGE[] = "discharge";
constexpr char MODE_OPEN[] = "open";
constexpr char CTRL_AUTO_STR[] = "auto";
constexpr char CTRL_MANUAL_STR[] = "manual";

// RPC subscription flag
static bool g_rpc_subscribed = false;

// Track last sent mode to avoid redundant updates
static power_mode_t g_last_sent_mode = MODE_OPEN_CIRCUIT;
static control_src_t g_last_sent_ctrl_src = CTRL_AUTO;

// ============ RPC Callback Functions ============

/// @brief Callback function to handle mode change RPC from ThingsBoard
void processSetMode(const JsonVariantConst &data, JsonDocument &response) {
    if (!data.containsKey(RPC_MODE_KEY)) {
        ESP_LOGW(TAG, "RPC setMode: missing 'mode' parameter");
        response["error"] = "missing mode parameter";
        return;
    }
    
    const char* mode_str = data[RPC_MODE_KEY];
    power_mode_t new_mode;
    
    if (strcmp(mode_str, MODE_CHARGE) == 0) {
        new_mode = MODE_CHARGE_ENABLE;
        ESP_LOGI(TAG, "RPC: Set mode to CHARGE");
    } else if (strcmp(mode_str, MODE_DISCHARGE) == 0) {
        new_mode = MODE_DISCHARGE_ENABLE;
        ESP_LOGI(TAG, "RPC: Set mode to DISCHARGE");
    } else if (strcmp(mode_str, MODE_OPEN) == 0) {
        new_mode = MODE_OPEN_CIRCUIT;
        ESP_LOGI(TAG, "RPC: Set mode to OPEN");
    } else {
        ESP_LOGW(TAG, "RPC: Unknown mode '%s'", mode_str);
        response["error"] = "unknown mode";
        return;
    }
    
    control_src = CTRL_MANUAL;
    manual_mode = new_mode;
    manual_timeout_ms = xTaskGetTickCount() + pdMS_TO_TICKS(60000);
    
    response["status"] = "success";
    response["mode"] = mode_str;
    ESP_LOGI(TAG, "RPC: Mode changed successfully, manual timeout in 60s");
}

/// @brief Send current mode status to ThingsBoard as client attributes
void sendModeAttributes() {
    if (!tb.connected()) {
        return;
    }
    
    // Get current effective mode
    power_mode_t current_mode = (control_src == CTRL_AUTO) ? auto_mode : manual_mode;
    
    // Only send if mode or control source changed
    if (current_mode == g_last_sent_mode && control_src == g_last_sent_ctrl_src) {
        return;
    }
    
    const char* mode_str;
    switch (current_mode) {
        case MODE_CHARGE_ENABLE:
            mode_str = MODE_CHARGE;
            break;
        case MODE_DISCHARGE_ENABLE:
            mode_str = MODE_DISCHARGE;
            break;
        case MODE_OPEN_CIRCUIT:
        default:
            mode_str = MODE_OPEN;
            break;
    }
    
    const char* ctrl_src_str = (control_src == CTRL_AUTO) ? CTRL_AUTO_STR : CTRL_MANUAL_STR;
    
    bool success = true;
    success &= tb.sendAttributeData(ATTR_MODE_KEY, mode_str);
    success &= tb.sendAttributeData(ATTR_CONTROL_SRC_KEY, ctrl_src_str);
    
    if (success) {
        g_last_sent_mode = current_mode;
        g_last_sent_ctrl_src = control_src;
        ESP_LOGI(TAG, "Sent mode attributes: mode=%s, control=%s", mode_str, ctrl_src_str);
    } else {
        ESP_LOGW(TAG, "Failed to send mode attributes");
    }
}

// ============ WiFi Event Handlers ============

static void on_got_ip(void* event_handler_arg, esp_event_base_t event_base, 
                      int32_t event_id, void* event_data) 
{
    g_wifi_connected = true;
    ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
    ESP_LOGI(TAG, "WiFi connected! Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
}

static void on_wifi_disconnect(void* event_handler_arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    g_wifi_connected = false;
    ESP_LOGW(TAG, "WiFi disconnected, reconnecting...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_wifi_connect();
}

void wifi_init_sta(void)
{
    
    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));
    
    esp_netif_config_t netif_config = ESP_NETIF_DEFAULT_WIFI_STA();
    esp_netif_t *netif = esp_netif_new(&netif_config);
    assert(netif);
    
    ESP_ERROR_CHECK(esp_netif_attach_wifi_station(netif));
    
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, 
                                               &on_got_ip, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                                               &on_wifi_disconnect, NULL));
    
    ESP_ERROR_CHECK(esp_wifi_set_default_wifi_sta_handlers());
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    
    wifi_config_t wifi_config = {};
    strncpy((char*)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strncpy((char*)wifi_config.sta.password, WIFI_PASSWORD, sizeof(wifi_config.sta.password));
    
    ESP_LOGI(TAG, "Connecting to WiFi SSID: %s", wifi_config.sta.ssid);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());
}

// ============ MQTT Task ============

void mqtt_task(void *pvParameters)
{
    ESP_LOGI(TAG, "[CORE0] MQTT task started");
    
    // Chờ WiFi kết nối
    ESP_LOGI(TAG, "Waiting for WiFi connection...");
    while (!g_wifi_connected) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    ESP_LOGI(TAG, "WiFi connected! Starting ThingsBoard client...");
    
    while (1) {
        
        if (!tb.connected()) {
            ESP_LOGI(TAG, "Connecting to ThingsBoard server: %s", THINGSBOARD_SERVER);
            if (tb.connect(THINGSBOARD_SERVER, THINGSBOARD_TOKEN, THINGSBOARD_PORT)) {
                ESP_LOGI(TAG, "Connected to ThingsBoard successfully!");
                g_rpc_subscribed = false;  // Reset RPC subscription flag
            } else {
                ESP_LOGE(TAG, "Failed to connect to ThingsBoard, retry in 5s...");
                vTaskDelay(pdMS_TO_TICKS(5000));
                continue;
            }
        }
        
        // Subscribe to RPC if not already subscribed
        if (!g_rpc_subscribed) {
            const std::array<RPC_Callback, MAX_RPC_SUBSCRIPTIONS> callbacks = {
                RPC_Callback{ RPC_SET_MODE_METHOD, processSetMode }
            };
            
            if (rpc.RPC_Subscribe(callbacks.begin(), callbacks.end())) {
                g_rpc_subscribed = true;
                ESP_LOGI(TAG, "RPC subscribed successfully!");
                // Send initial mode attributes
                g_last_sent_mode = MODE_OPEN_CIRCUIT;
                g_last_sent_ctrl_src = CTRL_AUTO;
                sendModeAttributes();
            } else {
                ESP_LOGE(TAG, "Failed to subscribe RPC");
            }
        }
        
        
        sensor_data_t data;
        if (xQueuePeek(g_sensor_queue, &data, pdMS_TO_TICKS(100)) == pdTRUE) {
            // Kiểm tra kết nối WiFi
            if (!g_wifi_connected) {
                ESP_LOGW(TAG, "WiFi disconnected, waiting for reconnection...");
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }
            
            
            bool success = true;
            success &= tb.sendTelemetryData(VOLTAGE_KEY, data.voltage);
            success &= tb.sendTelemetryData(CURRENT_KEY, data.current);
            success &= tb.sendTelemetryData(SOC_KEY, data.soc_percent);
            success &= tb.sendTelemetryData(SOH_KEY, data.soh_percent);
            success &= tb.sendTelemetryData(TEMPERATURE_KEY, data.temperature);
            
            if (success) {
                ESP_LOGI(TAG, "Telemetry sent: V=%.2fV I=%.3fA SOC=%.1f%% SOH=%.1f%% T=%.1f°C",
                         data.voltage, data.current, data.soc_percent, 
                         data.soh_percent, data.temperature);
            } else {
                ESP_LOGE(TAG, "Failed to send telemetry data");
            }
        }
        
        // Send mode attributes (will only send if changed)
        sendModeAttributes();
        
        // Process MQTT messages
        tb.loop();
        
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
