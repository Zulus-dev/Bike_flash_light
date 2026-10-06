#include "motion_sensor.h"
#include "config.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <string.h>

static const char *TAG = "MPU";

#define I2C_PORT              I2C_NUM_0
#define MPU6050_ADDR          0x68
#define MPU6050_WHO_AM_I      0x75
#define MPU6050_PWR_MGMT_1    0x6B
#define MPU6050_ACCEL_XOUT_H  0x3B
#define MPU6050_CONFIG        0x1A
#define MPU6050_GYRO_CONFIG   0x1B
#define MPU6050_ACCEL_CONFIG  0x1C

static motion_data_t motion = {0};
static float filtered_roll = 0;
static float filtered_pitch = 0;
static int64_t last_update_us = 0;
static float ref_ax, ref_ay, ref_az;
static float ref_roll, ref_pitch;
static bool ref_saved;

/* LPF ускорений + ВЧ-оценка «кочки» */
static float filtered_ax = 0;
static float filtered_ay = 0;
static float ax_slow = 0;       /* очень медленная база для high-pass */
static float ay_slow = 0;
static float filtered_total = 1.0f;
static float bump_energy = 0;
static bool  filt_ax_init = false;

static esp_err_t mpu_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_write_to_device(I2C_PORT, MPU6050_ADDR, buf, 2, pdMS_TO_TICKS(100));
}

static esp_err_t mpu_read_regs(uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_write_read_device(I2C_PORT, MPU6050_ADDR, &reg, 1, data, len, pdMS_TO_TICKS(100));
}

esp_err_t motion_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 400000,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_PORT, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_PORT, conf.mode, 0, 0, 0));

    // Проверка WHO_AM_I
    uint8_t who = 0;
    if (mpu_read_regs(MPU6050_WHO_AM_I, &who, 1) != ESP_OK ||
        (who != 0x68 && who != 0x70 && who != 0x98 && who != 0x71)) {
        ESP_LOGE(TAG, "MPU6050 not found (WHO_AM_I=0x%02X)", who);
        motion.valid = false;
        return ESP_FAIL;
    }

    // Пробуждение
    mpu_write_reg(MPU6050_PWR_MGMT_1, 0x00);
    vTaskDelay(pdMS_TO_TICKS(100));

    // DLPF ~21 Hz
    mpu_write_reg(MPU6050_CONFIG, 0x04);
    // Gyro ±500 dps
    mpu_write_reg(MPU6050_GYRO_CONFIG, 0x08);
    // Accel ±4g
    mpu_write_reg(MPU6050_ACCEL_CONFIG, 0x08);

    motion.valid = true;
    last_update_us = esp_timer_get_time();
    ESP_LOGI(TAG, "MPU6050 init OK (WHO=0x%02X)", who);
    return ESP_OK;
}

esp_err_t motion_update(void)
{
    if (!motion.valid) return ESP_FAIL;

    uint8_t raw[14];
    if (mpu_read_regs(MPU6050_ACCEL_XOUT_H, raw, 14) != ESP_OK) {
        return ESP_FAIL;
    }

    int16_t ax = (int16_t)((raw[0] << 8) | raw[1]);
    int16_t ay = (int16_t)((raw[2] << 8) | raw[3]);
    int16_t az = (int16_t)((raw[4] << 8) | raw[5]);
    int16_t gx = (int16_t)((raw[8] << 8) | raw[9]);
    int16_t gy = (int16_t)((raw[10] << 8) | raw[11]);
    int16_t gz = (int16_t)((raw[12] << 8) | raw[13]);

    // ±4g → 8192 LSB/g, ±500 dps → 65.5 LSB/(dps)
    motion.accel_x = (ax / 8192.0f) - g_config.accel_offset_x;
    motion.accel_y = (ay / 8192.0f) - g_config.accel_offset_y;
    motion.accel_z = (az / 8192.0f) - g_config.accel_offset_z;

    motion.gyro_x = (gx / 65.5f) - g_config.gyro_offset_x;
    motion.gyro_y = (gy / 65.5f) - g_config.gyro_offset_y;
    motion.gyro_z = (gz / 65.5f) - g_config.gyro_offset_z;

    motion.total_accel = sqrtf(motion.accel_x * motion.accel_x +
                               motion.accel_y * motion.accel_y +
                               motion.accel_z * motion.accel_z);

    float accel_roll  = atan2f(motion.accel_y, motion.accel_z) * 57.2957795f;
    float accel_pitch = atan2f(-motion.accel_x,
                               sqrtf(motion.accel_y * motion.accel_y +
                                     motion.accel_z * motion.accel_z)) * 57.2957795f;

    int64_t now = esp_timer_get_time();
    float dt = (now - last_update_us) / 1000000.0f;
    if (dt > 0.1f) dt = 0.01f;
    if (dt < 0.001f) dt = 0.001f;
    last_update_us = now;

    const float alpha = 0.96f;
    filtered_roll  = alpha * (filtered_roll  + motion.gyro_x * dt) + (1.0f - alpha) * accel_roll;
    filtered_pitch = alpha * (filtered_pitch + motion.gyro_y * dt) + (1.0f - alpha) * accel_pitch;

    motion.roll  = filtered_roll;
    motion.pitch = filtered_pitch;

    /* Умеренный LPF по осям + медленная база (high-pass для тормоза) */
    uint8_t br = g_config.brake_bump_reject;
    if (br < 1) br = 1;
    if (br > 10) br = 10;
    /* alpha: 0.40 … 0.75 — не глушим реальное торможение */
    float alpha_ax = 0.40f + (br - 1) * (0.35f / 9.0f);
    if (!filt_ax_init) {
        filtered_ax = motion.accel_x;
        filtered_ay = motion.accel_y;
        ax_slow = motion.accel_x;
        ay_slow = motion.accel_y;
        filtered_total = motion.total_accel;
        filt_ax_init = true;
    } else {
        filtered_ax = alpha_ax * filtered_ax + (1.0f - alpha_ax) * motion.accel_x;
        filtered_ay = alpha_ax * filtered_ay + (1.0f - alpha_ax) * motion.accel_y;
        /* Медленная база ~1–2 с — гравитационный «наклон» уходит в базу */
        ax_slow = 0.98f * ax_slow + 0.02f * motion.accel_x;
        ay_slow = 0.98f * ay_slow + 0.02f * motion.accel_y;
        filtered_total = 0.92f * filtered_total + 0.08f * motion.total_accel;
    }
    float high_freq = fabsf(motion.total_accel - filtered_total);
    bump_energy = 0.75f * bump_energy + 0.25f * high_freq;

    return ESP_OK;
}

const motion_data_t* motion_get_data(void)
{
    return &motion;
}

bool motion_is_braking(float threshold)
{
    if (threshold < 0.05f) threshold = 0.05f;

    /*
     * Динамическое ускорение = текущее − медленная база.
     * Наклон рамы (гравитация) уходит в базу и НЕ даёт ложный стоп.
     * Резкое торможение — всплеск на X или Y (ориентация модуля на раме разная).
     * Берём максимум |Δ| по горизонтальным осям.
     */
    float dx = filtered_ax - ax_slow;
    float dy = filtered_ay - ay_slow;
    float mag = fabsf(dx);
    if (fabsf(dy) > mag) mag = fabsf(dy);

    return (mag >= threshold);
}

bool motion_is_nose_down(float pitch_deg)
{
    if (pitch_deg <= 0.1f) return false;
    /* Срабатывает при наклоне вперёд ИЛИ назад (модуль может стоять зеркально) */
    return (fabsf(motion.pitch) >= pitch_deg);
}

bool motion_is_bump(float strength_g)
{
    if (strength_g < 0.05f) strength_g = 0.05f;
    return (bump_energy >= strength_g);
}

float motion_get_filtered_ax(void)
{
    return filtered_ax;
}

bool motion_is_tilted_left(float min_deg, float max_deg)
{
    float abs_roll = fabsf(motion.roll);
    if (abs_roll < min_deg) return false;
    /* max_deg <= min → без верхней границы (можно наклонять сильно) */
    if (max_deg > min_deg && abs_roll > max_deg) return false;
    bool left = (motion.roll > 0);
    if (g_config.invert_turn_signals) left = !left;
    return left;
}

bool motion_is_tilted_right(float min_deg, float max_deg)
{
    float abs_roll = fabsf(motion.roll);
    if (abs_roll < min_deg) return false;
    if (max_deg > min_deg && abs_roll > max_deg) return false;
    bool right = (motion.roll < 0);
    if (g_config.invert_turn_signals) right = !right;
    return right;
}

bool motion_is_stationary(float accel_threshold)
{
    return motion_is_stationary_ex(accel_threshold, 15.0f);
}

bool motion_is_stationary_ex(float accel_threshold, float gyro_threshold)
{
    float dev = fabsf(motion.total_accel - 1.0f);
    float gyro_mag = sqrtf(motion.gyro_x * motion.gyro_x +
                           motion.gyro_y * motion.gyro_y +
                           motion.gyro_z * motion.gyro_z);
    return (dev < accel_threshold && gyro_mag < gyro_threshold);
}

float motion_get_movement_magnitude(void)
{
    float dev = fabsf(motion.total_accel - 1.0f);
    float gyro_mag = sqrtf(motion.gyro_x * motion.gyro_x +
                           motion.gyro_y * motion.gyro_y +
                           motion.gyro_z * motion.gyro_z) / 100.0f;
    return dev + gyro_mag;
}

void motion_calibrate(uint16_t samples)
{
    ESP_LOGI(TAG, "Calibrating... keep still");
    float ax = 0, ay = 0, az = 0, gx = 0, gy = 0, gz = 0;

    for (uint16_t i = 0; i < samples; i++) {
        motion_update();
        ax += motion.accel_x + g_config.accel_offset_x;
        ay += motion.accel_y + g_config.accel_offset_y;
        az += motion.accel_z + g_config.accel_offset_z;
        gx += motion.gyro_x + g_config.gyro_offset_x;
        gy += motion.gyro_y + g_config.gyro_offset_y;
        gz += motion.gyro_z + g_config.gyro_offset_z;
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    g_config.accel_offset_x = ax / samples;
    g_config.accel_offset_y = ay / samples;
    g_config.accel_offset_z = az / samples - 1.0f;
    g_config.gyro_offset_x  = gx / samples;
    g_config.gyro_offset_y  = gy / samples;
    g_config.gyro_offset_z  = gz / samples;

    config_save();
    ESP_LOGI(TAG, "Calibration saved");
}

void motion_save_reference(void)
{
    motion_update();
    ref_ax = motion.accel_x;
    ref_ay = motion.accel_y;
    ref_az = motion.accel_z;
    ref_roll = motion.roll;
    ref_pitch = motion.pitch;
    ref_saved = true;
}

bool motion_detect_alarm(float threshold)
{
    if (!ref_saved) return false;
    float dx = motion.accel_x - ref_ax;
    float dy = motion.accel_y - ref_ay;
    float dz = motion.accel_z - ref_az;
    float d_acc = sqrtf(dx * dx + dy * dy + dz * dz);
    float d_ang = fabsf(motion.roll - ref_roll) + fabsf(motion.pitch - ref_pitch);
    /* threshold в g; угол: ~40° на 0.1g чувствительности */
    return (d_acc > threshold) || (d_ang > threshold * 40.0f);
}
