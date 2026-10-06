#pragma once

#include "esp_err.h"

typedef enum {
    LED_GROUP_LEFT = 0,
    LED_GROUP_CENTER,
    LED_GROUP_RIGHT,
    LED_GROUP_STRIP,
    LED_GROUP_LASER
} led_group_t;

esp_err_t led_control_init(void);
void led_set_brightness(led_group_t group, float percent); // 0.0 - 100.0
void led_set_all(float left, float center, float right);
void led_all_off(void);
void strip_set_brightness(float percent);
void strip_off(void);
void laser_set_brightness(float percent);
void laser_off(void);
