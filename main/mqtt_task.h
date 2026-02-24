#ifndef MQTT_TASK_H
#define MQTT_TASK_H

#ifdef __cplusplus
extern "C" {
#endif

// Task function
void mqtt_task(void *pvParameters);

// WiFi initialization
void wifi_init_sta(void);

#ifdef __cplusplus
}
#endif

#endif // MQTT_TASK_H
