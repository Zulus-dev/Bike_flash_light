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

/** Сырое условие по продольному ускорению (уже с LPF). */
bool motion_is_braking(float threshold);
/** Наклон вперёд (pitch ≤ −deg) — стоп на спуске. deg≤0 → всегда false. */
bool motion_is_nose_down(float pitch_deg);
/** Высокочастотная «кочка»: сильное кратковременное отклонение total_accel. */
bool motion_is_bump(float strength_g);
float motion_get_filtered_ax(void);
bool motion_is_tilted_left(float min_deg, float max_deg);
bool motion_is_tilted_right(float min_deg, float max_deg);
bool motion_is_stationary(float accel_threshold);
bool motion_is_stationary_ex(float accel_threshold, float gyro_threshold);
float motion_get_movement_magnitude(void);

void motion_calibrate(uint16_t samples);
void motion_save_reference(void);
bool motion_detect_alarm(float threshold);
