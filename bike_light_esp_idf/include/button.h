#pragma once

#include <stdbool.h>
#include "esp_err.h"

typedef enum {
    BUTTON_ACTION_NONE = 0,
    BUTTON_ACTION_SHORT,      // < 1s
    BUTTON_ACTION_POWER_OFF,  // 2-4s
    BUTTON_ACTION_ARM_DISARM, // 4-8s
    BUTTON_ACTION_WIFI_TOGGLE // >= 8s
} button_action_t;

esp_err_t button_init(void);
void button_update(void);                 // вызывать часто
button_action_t button_get_action(void);  // получить и сбросить
bool button_is_pressed(void);
