#pragma once

#include <stdbool.h>

typedef enum {
    MODE_NORMAL = 0,
    MODE_HAZARD,
    MODE_ALARM
} device_mode_t;

void state_init(void);
void state_update(void);
device_mode_t state_get_mode(void);
void state_set_mode(device_mode_t mode);
void state_request_power_off(void);
bool state_is_power_off_requested(void);
bool state_is_alarm_triggered(void);
void state_arm_alarm(void);
void state_disarm_alarm(void);
