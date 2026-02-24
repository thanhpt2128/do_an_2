#ifndef SENSOR_TASKS_H
#define SENSOR_TASKS_H

#include <ina219.h>
#include "battery_cc.h"

#ifdef __cplusplus
extern "C" {
#endif

// INA219 context structure
typedef struct {
    ina219_t dev;
    battery_soc_t *soc;
} ina219_context_t;

// Task functions
void ina219_soc_task(void *pvParameters);
void ds18b20_task(void *pvParameters);

#ifdef __cplusplus
}
#endif

#endif // SENSOR_TASKS_H
