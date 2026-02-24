#ifndef MODE_CONTROL_H
#define MODE_CONTROL_H

#include <stdint.h>
#include <driver/gpio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  MODE_CHARGE_ENABLE ,
  MODE_DISCHARGE_ENABLE ,
  MODE_OPEN_CIRCUIT 
} power_mode_t;

typedef enum {
  CTRL_AUTO,
  CTRL_MANUAL
} control_src_t;

typedef enum {
    CMD_SET_THRESHOLD,
    CMD_SET_MODE
} cmd_type_t;

typedef struct {
    cmd_type_t type;
    union {
        struct {
            float soc_min;
            float soc_max;
            float t_max;
        } threshold;

        power_mode_t mode;
    };
} control_cmd_t;


extern power_mode_t auto_mode;
extern power_mode_t manual_mode;
extern control_src_t control_src;
extern uint64_t manual_timeout_ms;

void uart_handle_cmd(char *cmd);
void apply_power_mode(power_mode_t mode, gpio_num_t relay_sac, gpio_num_t relay_xa);

#ifdef __cplusplus
}
#endif

#endif // MODE_CONTROL_H

