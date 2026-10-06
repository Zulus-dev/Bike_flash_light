#pragma once

#include "esp_err.h"
#include <stdbool.h>

typedef struct {
    float accel_x, accel_y, accel_z;  // g
    float gyro_x, gyro_y, gyro_z;     // deg/s
    float roll, pitch;                // degrees
    float total_accel;
    bool  valid;
} motion_data_t;

esp_err_t motion_init(void);
esp_err_t motion_update(void);
const motion_data_t* motion_get_data(void);

bool motion_is_braking(float threshold);
bool motion_is_tilted_left(float min_deg, float max_deg);
bool motion_is_tilted_right(float min_deg, float max_deg);
bool motion_is_stationary(float accel_threshold);
bool motion_is_stationary_ex(float accel_threshold, float gyro_threshold);
float motion_get_movement_magnitude(void);

void motion_calibrate(uint16_t samples);
void motion_save_reference(void);
bool motion_detect_alarm(float threshold);
